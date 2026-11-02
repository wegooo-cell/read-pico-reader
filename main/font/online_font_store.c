/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：固定字体的SHA256、流式解压、SFNT检查及同目录提交。
 * English: Pinned hashes, streaming decompression, SFNT validation and same-directory commit.
 * 冻结：不解析下载包里的路径；只解压已校验归档内固定偏移的单个字体。
 * Frozen: never extract archive paths; decode one fixed-offset payload from a verified archive.
 */
#include "online_font_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "psa/crypto.h"
#include "zlib.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#if KIIKO_BOARD_METALIO47
#include "read_pico_sd_access.h"
#endif
#endif

#define FONT_BLOCK 8192u
#define FONT_MAX (32u * 1024u * 1024u)

static uint16_t be16(const uint8_t *p) { return (uint16_t)p[0]<<8|p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static bool read_at(FILE *f,uint32_t off,void *data,size_t n) {
    return !fseek(f,(long)off,SEEK_SET)&&fread(data,1,n,f)==n;
}
esp_err_t online_font_store_validate(const char *path,uint32_t size) {
    if(!path||size<1024||size>FONT_MAX)return ESP_ERR_INVALID_SIZE;
    FILE *f=fopen(path,"rb");if(!f)return ESP_FAIL;
    uint8_t h[54];uint32_t offsets[7]={0},lengths[7]={0};
    const char tags[7][5]={"head","maxp","hhea","hmtx","cmap","loca","glyf"};
    bool ok=read_at(f,0,h,12)&&be32(h)==0x10000;
    unsigned count=ok?be16(h+4):0;
    ok=ok&&count>=7&&count<=128&&12u+count*16u<=size;
    for(unsigned i=0;ok&&i<count;++i){
        uint8_t t[16];ok=read_at(f,12+i*16,t,sizeof(t));if(!ok)break;
        uint32_t off=be32(t+8),len=be32(t+12);
        ok=off<=size&&len<=size-off;
        for(unsigned k=0;k<7&&ok;++k)if(!memcmp(t,tags[k],4)){
            if(offsets[k]){ok=false;break;}offsets[k]=off;lengths[k]=len;
        }
    }
    const unsigned minimum[7]={54,6,36,4,4,4,1};
    for(unsigned k=0;k<7;++k)ok=ok&&offsets[k]&&lengths[k]>=minimum[k];
    unsigned glyphs=0,format=0,metrics=0;
    if(ok){ok=read_at(f,offsets[0],h,54)&&be32(h+12)==0x5f0f3cf5;
        unsigned em=be16(h+18);format=be16(h+50);ok=ok&&em>=16&&em<=16384&&format<=1;}
    if(ok){ok=read_at(f,offsets[1],h,6);glyphs=be16(h+4);ok=ok&&glyphs>0;}
    if(ok){ok=read_at(f,offsets[2]+34,h,2);metrics=be16(h);ok=ok&&metrics>0&&metrics<=glyphs;}
    ok=ok&&lengths[3]>=metrics*4+(glyphs-metrics)*2&&lengths[5]>=(glyphs+1)*(format?4u:2u);
    uint32_t previous=0;uint8_t entries[512];unsigned stride=format?4u:2u;
    for(unsigned first=0;ok&&first<=glyphs;){
        unsigned n=glyphs+1-first;if(n>sizeof(entries)/stride)n=sizeof(entries)/stride;
        ok=read_at(f,offsets[5]+first*stride,entries,n*stride);
        for(unsigned i=0;ok&&i<n;++i){const uint8_t *p=entries+i*stride;
            uint32_t value=format?be32(p):2u*be16(p);
            ok=value>=previous&&value<=lengths[6];previous=value;
        }
        first+=n;
    }
    if(fclose(f))ok=false;
    return ok?ESP_OK:ESP_ERR_INVALID_RESPONSE;
}
static void *font_alloc(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}
static voidpf inflate_alloc(voidpf opaque,uInt n,uInt size) {
    (void)opaque;if(size&&n>SIZE_MAX/size)return NULL;
    void *p=font_alloc((size_t)n*size);if(p)memset(p,0,(size_t)n*size);return p;
}
static void inflate_free(voidpf opaque,voidpf p) { (void)opaque;free(p); }
static bool stopped(online_font_cancel_t cancel,void *ctx){return cancel&&cancel(ctx);}
static esp_err_t digest_file(const char *path,uint32_t expected,const uint8_t wanted[32],
                             uint8_t *buffer,online_font_cancel_t cancel,void *ctx) {
    FILE *f=fopen(path,"rb");if(!f)return ESP_FAIL;
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    esp_err_t result=ESP_FAIL;uint32_t bytes=0;uint8_t sum[32];
    if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_setup(&hash,PSA_ALG_SHA_256)!=PSA_SUCCESS)goto done;
    while(!stopped(cancel,ctx)){
        size_t n=fread(buffer,1,FONT_BLOCK,f);
        if(n){if(n>expected-bytes||psa_hash_update(&hash,buffer,n)!=PSA_SUCCESS)goto done;bytes+=(uint32_t)n;}
        if(n<FONT_BLOCK){if(ferror(f)||bytes!=expected)goto done;
            size_t length=0;
            if(psa_hash_finish(&hash,sum,sizeof(sum),&length)==PSA_SUCCESS&&length==32&&!memcmp(sum,wanted,32))result=ESP_OK;
            break;}
    }
    if(stopped(cancel,ctx))result=ESP_ERR_INVALID_STATE;
done:
    psa_hash_abort(&hash);if(fclose(f))result=ESP_FAIL;return result;
}
static esp_err_t inflate_font(const online_font_item_t *item,const char *source,const char *target,
                              uint8_t *buffer,online_font_cancel_t cancel,void *ctx){
    if(item->deflate_offset>item->source_size||item->deflate_size>item->source_size-item->deflate_offset)return ESP_ERR_INVALID_SIZE;
    FILE *in=fopen(source,"rb"),*out=NULL;esp_err_t result=ESP_FAIL;
    z_stream z={.zalloc=inflate_alloc,.zfree=inflate_free};bool initialized=false;
    if(!in||fseek(in,item->deflate_offset,SEEK_SET))goto done;
    out=fopen(target,"wb");if(!out)goto done;
    if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)goto done;
    initialized=true;
    uint32_t left=item->deflate_size,written=0;
    while(!stopped(cancel,ctx)){
        if(!z.avail_in&&left){unsigned n=left>FONT_BLOCK?FONT_BLOCK:left;
            if(fread(buffer,1,n,in)!=n)goto done;
            left-=n;z.next_in=buffer;z.avail_in=n;}
        z.next_out=buffer+FONT_BLOCK;z.avail_out=FONT_BLOCK;
        int code=inflate(&z,Z_NO_FLUSH);size_t n=FONT_BLOCK-z.avail_out;
        if(n>item->size-written||fwrite(buffer+FONT_BLOCK,1,n,out)!=n)goto done;
        written+=(uint32_t)n;
        if(code==Z_STREAM_END){if(written==item->size&&!left&&!z.avail_in)result=ESP_OK;break;}
        if(code!=Z_OK||(!left&&!z.avail_in&&!n))break;
    }
    if(stopped(cancel,ctx))result=ESP_ERR_INVALID_STATE;
done:
    if(initialized)inflateEnd(&z);
    if(in&&fclose(in))result=ESP_FAIL;
    if(out){if(fflush(out)||fsync(fileno(out)))result=ESP_FAIL;if(fclose(out))result=ESP_FAIL;}
    return result;
}
static bool paths(const online_font_item_t *item,const char *dir,char font[256],char notice[256]){
    if(!item||!dir||!item->filename||strchr(item->filename,'/')||strstr(item->filename,".."))return false;
    int n=snprintf(font,256,"%s/%s",dir,item->filename);
    return n>0&&n<240&&snprintf(notice,256,"%s.OFL.txt",font)<256;
}
bool online_font_store_installed(const online_font_item_t *item,const char *dir){
    char font[256],notice[256];struct stat st,lic;
    return paths(item,dir,font,notice)&&!stat(font,&st)&&S_ISREG(st.st_mode)&&
        st.st_size==item->size&&!stat(notice,&lic)&&S_ISREG(lic.st_mode)&&lic.st_size==(off_t)strlen(item->license);
}
esp_err_t online_font_store_commit(const online_font_item_t *item,const char *dir,const char *source,
                                  online_font_cancel_t cancel,void *ctx){
#if KIIKO_BOARD_METALIO47
#ifdef ESP_PLATFORM
    read_pico_sd_access_scope_t access_scope __attribute__((cleanup(read_pico_sd_access_scope_end)))=read_pico_sd_access_scope(dir);
    if(!access_scope.allowed)return ESP_ERR_INVALID_STATE;
#endif
#endif
    char target[256],notice[256],temp[256],license_temp[256];struct stat st;
    if(!paths(item,dir,target,notice)||!source||item->size>FONT_MAX||item->source_size>FONT_MAX)return ESP_ERR_INVALID_ARG;
    if(!stat(target,&st))return ESP_ERR_INVALID_STATE;
    if(snprintf(temp,sizeof(temp),"%s/.kiiko-font.unpack",dir)>=(int)sizeof(temp)||
       snprintf(license_temp,sizeof(license_temp),"%s/.kiiko-font.notice",dir)>=(int)sizeof(license_temp))return ESP_ERR_INVALID_SIZE;
    uint8_t *buffer=font_alloc(FONT_BLOCK*2);if(!buffer)return ESP_ERR_NO_MEM;
    esp_err_t result=digest_file(source,item->source_size,item->source_sha256,buffer,cancel,ctx);
    const char *payload=source;
    if(result==ESP_OK&&item->deflate_size){
        result=inflate_font(item,source,temp,buffer,cancel,ctx);payload=temp;
        if(result==ESP_OK)result=digest_file(temp,item->size,item->sha256,buffer,cancel,ctx);
    }else if(result==ESP_OK&&(item->size!=item->source_size||memcmp(item->sha256,item->source_sha256,32)))result=ESP_ERR_INVALID_RESPONSE;
    if(result==ESP_OK&&!stopped(cancel,ctx))result=online_font_store_validate(payload,item->size);
    if(result==ESP_OK&&!stopped(cancel,ctx)){
        FILE *f=fopen(license_temp,"wb");
        if(!f)result=ESP_FAIL;
        else{size_t n=strlen(item->license);bool ok=fwrite(item->license,1,n,f)==n;
            if(fflush(f)||fsync(fileno(f)))ok=false;
            if(fclose(f))ok=false;
            if(!ok)result=ESP_FAIL;}
    }
    if(stopped(cancel,ctx))result=ESP_ERR_INVALID_STATE;
    if(result==ESP_OK){
        // 字体最后改名；失败不碰已选字体，最多留下独立的许可证文件。
        // Rename the face last; failure never touches selected fonts, at most leaving an independent notice.
        if(!stat(target,&st)||rename(license_temp,notice)||rename(payload,target))result=ESP_FAIL;
    }
    unlink(temp);unlink(license_temp);free(buffer);return result;
}
