/* SPDX-License-Identifier: Apache-2.0
 * 两页有界PSRAM与有界TF成品缓存；后台RLE，不使用PNG或修改刷新波形。
 * Two bounded PSRAM pages and bounded SD artwork; background RLE, no PNG or waveform changes.
 */
#include "book_shelf_page_cache.h"
#include "book_cover_auto.h"
#include "read_pico_sd.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BOOK_SHELF_PAGE_ROOT
#if KIIKO_BOARD_METALIO47
#define BOOK_SHELF_PAGE_PARENT "/sdcard/.metalio_s31"
#else
#define BOOK_SHELF_PAGE_PARENT "/sdcard/.readpico"
#endif
#define BOOK_SHELF_PAGE_ROOT BOOK_SHELF_PAGE_PARENT "/shelf-pages"
#endif
#ifndef BOOK_SHELF_PAGE_PARENT
#define BOOK_SHELF_PAGE_PARENT BOOK_SHELF_PAGE_ROOT
#endif
#define PAGE_BUDGET (1024u*1024u)
#define PAGE_RESERVE (1536u*1024u)
#define DISK_BUDGET (8u*1024u*1024u)
#define DISK_FILES 32u
#define PAGE_MAGIC 0x32475053u
#define PAGE_VERSION 2u
typedef struct {
    uint32_t magic,version,header_bytes,bytes,checksum,edges;
    book_shelf_page_key_t key;
    book_shelf_page_layout_t layout;
} page_header_t;
typedef struct {
    page_header_t header;
    uint8_t *pixels;
    uint32_t stamp;
    bool valid,dirty,staged;
    uint64_t due;
} page_slot_t;
static page_slot_t s_slots[2];
static page_header_t s_wanted;
static int s_active=-1,s_work=-1;
static uint32_t s_stamp;
static bool s_lookup_done;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_running,s_cancel,s_writing;
void book_shelf_page_key_init(book_shelf_page_key_t *key) {
    *key=(book_shelf_page_key_t){{UINT64_C(14695981039346656037),UINT64_C(7809847782465536322)}};
}
void book_shelf_page_key_add(book_shelf_page_key_t *key,const void *data,size_t size) {
    const uint8_t *p=data;
    for(size_t i=0;i<size;++i){key->v[0]=(key->v[0]^p[i])*UINT64_C(1099511628211);key->v[1]=(key->v[1]+p[i])*UINT64_C(14029467366897019727);}
}
static size_t span(EpdRect r){return ((unsigned)(r.x&1)+(unsigned)r.width+1)/2;}
static size_t plane_bytes(EpdRect r){return span(r)*(unsigned)r.height;}
static bool fits(EpdRect r,unsigned w,unsigned h) {
    return r.x>=0&&r.y>=0&&r.width>0&&r.height>0&&(unsigned)r.x<=w&&(unsigned)r.y<=h&&
        (unsigned)r.width<=w-(unsigned)r.x&&(unsigned)r.height<=h-(unsigned)r.y;
}
static bool running(void) {
    portENTER_CRITICAL(&s_lock);bool v=s_running;portEXIT_CRITICAL(&s_lock);return v;
}
static bool cancelled(void) {
    portENTER_CRITICAL(&s_lock);bool v=s_cancel;portEXIT_CRITICAL(&s_lock);
    return v||!read_pico_sd_io_ready();
}
static void join(void) {
    portENTER_CRITICAL(&s_lock);s_cancel=true;portEXIT_CRITICAL(&s_lock);
    while(running())vTaskDelay(1);
    portENTER_CRITICAL(&s_lock);s_cancel=false;portEXIT_CRITICAL(&s_lock);
}
static uint32_t checksum(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;
}
static void path_for(char *out,size_t size,book_shelf_page_key_t key,const char *ext) {
    snprintf(out,size,BOOK_SHELF_PAGE_ROOT "/p-%016llx%016llx.%s",
        (unsigned long long)key.v[0],(unsigned long long)key.v[1],ext);
}
/* 每次最多512字节文件IO，取消后先关闭句柄再交还挂载。
 * Bound each file IO to512 bytes; close handles before returning mount ownership. */
static bool io(FILE *f,void *data,size_t bytes,bool write) {
    uint8_t *p=data;
    while(bytes){if(cancelled())return false;size_t n=bytes>512?512:bytes;
        if((write?fwrite(p,1,n,f):fread(p,1,n,f))!=n)return false;
        p+=n;bytes-=n;}
    return true;
}
typedef struct { FILE *file;uint8_t data[512];unsigned used; } page_writer_t;
static bool emit(page_writer_t *w,uint8_t b) {
    w->data[w->used++]=b;
    if(w->used<sizeof(w->data))return true;
    bool ok=io(w->file,w->data,w->used,true);w->used=0;return ok;
}
static bool binary_four(const uint8_t *p) {
    for(unsigned i=0;i<4;++i)if((p[i]&15)!=0&&(p[i]&15)!=15)return false;
    for(unsigned i=0;i<4;++i)if((p[i]>>4)!=0&&(p[i]>>4)!=15)return false;
    return true;
}
static bool encode(FILE *f,const uint8_t *p,size_t bytes) {
    page_writer_t w={.file=f};size_t i=0;
    while(i<bytes){
        if(cancelled())return false;
        size_t run=1;while(run<129&&i+run<bytes&&p[i+run]==p[i])++run;
        if(run>=3){if(!emit(&w,(uint8_t)(128+run-3))||!emit(&w,p[i]))return false;i+=run;continue;}
        // 黑白噪声/抖动块直接打包为1bpp，保留其余灰阶字节原值。
        // Pack BW noise/dither blocks at1bpp, preserving all other gray bytes exactly.
        if(i+4<=bytes&&binary_four(p+i)){
            size_t groups=1;while(groups<128&&i+4*(groups+1)<=bytes&&binary_four(p+i+4*groups))++groups;
            if(!emit(&w,255)||!emit(&w,(uint8_t)(groups-1)))return false;
            for(size_t g=0;g<groups;++g){unsigned bits=0;
                for(unsigned j=0;j<4;++j){uint8_t v=p[i++];if(v&15)bits|=1u<<(j*2);if(v>>4)bits|=2u<<(j*2);}
                if(!emit(&w,(uint8_t)bits))return false;
            }
            continue;
        }
        size_t start=i;i+=run;
        while(i<bytes&&i-start<128){
            if(i+2<bytes&&p[i]==p[i+1]&&p[i]==p[i+2])break;
            if(i+4<=bytes&&binary_four(p+i))break;
            ++i;
        }
        if(i-start>128)i=start+128;
        if(!emit(&w,(uint8_t)(i-start-1)))return false;
        for(size_t n=start;n<i;++n)if(!emit(&w,p[n]))return false;
    }
    return !w.used||io(f,w.data,w.used,true);
}
static bool decode(FILE *f,uint8_t *p,size_t bytes) {
    size_t i=0;
    while(i<bytes){
        if(cancelled())return false;
        int c=fgetc(f);if(c==EOF)return false;
        if(c==255){
            int groups=fgetc(f);if(groups<0||(size_t)(groups+1)*4>bytes-i)return false;
            for(int g=0;g<=groups;++g){int bits=fgetc(f);if(bits<0)return false;
                for(unsigned j=0;j<4;++j)p[i++]=((bits&(1u<<(j*2)))?15:0)|((bits&(2u<<(j*2)))?240:0);
            }
            continue;
        }
        size_t n=c<128?(size_t)c+1:(size_t)(c&127)+3;
        if(n>bytes-i)return false;
        if(c<128){if(!io(f,p+i,n,false))return false;}
        else{int v=fgetc(f);if(v==EOF)return false;memset(p+i,v,n);}
        i+=n;
    }
    return fgetc(f)==EOF&&!ferror(f)&&!cancelled();
}
static bool own_file(const char *name) {
    if(strlen(name)!=38||strncmp(name,"p-",2)||(strcmp(name+34,".bin")&&strcmp(name+34,".tmp")))return false;
    for(unsigned i=2;i<34;++i)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return false;
    return true;
}
static void trim(const char *keep) {
    for(unsigned pass=0;pass<DISK_FILES+2&&!cancelled();++pass){
        DIR *d=opendir(BOOK_SHELF_PAGE_ROOT);if(!d)return;
        size_t bytes=0;unsigned count=0;time_t oldest=0;char victim[256]={0};struct dirent *e;
        while(!cancelled()&&(e=readdir(d))){
            if(!own_file(e->d_name))continue;
            char path[256];snprintf(path,sizeof(path),BOOK_SHELF_PAGE_ROOT "/%.38s",e->d_name);
            if(!strcmp(e->d_name+34,".tmp")){(void)unlink(path);continue;}
            struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode))continue;
            bytes+=(size_t)st.st_size;++count;
            if(strcmp(path,keep)&&(!victim[0]||st.st_mtime<oldest)){oldest=st.st_mtime;strcpy(victim,path);}
        }
        closedir(d);
        if(cancelled()||(bytes<=DISK_BUDGET&&count<=DISK_FILES)||!victim[0])return;
        if(unlink(victim))return;
    }
}
static void worker(void *unused) {
    (void)unused;page_slot_t *slot=&s_slots[s_work];bool ok=false;
    char path[256],temp[256];path_for(path,sizeof(path),slot->header.key,"bin");path_for(temp,sizeof(temp),slot->header.key,"tmp");
    if(!s_writing){
        FILE *f=cancelled()?NULL:fopen(path,"rb");
        if(f){page_header_t h={0};
            ok=io(f,&h,sizeof(h),false)&&h.magic==PAGE_MAGIC&&h.version==PAGE_VERSION&&
                h.header_bytes==sizeof(h)&&h.bytes==slot->header.bytes&&
                !memcmp(&h.key,&slot->header.key,sizeof(h.key))&&!memcmp(&h.layout,&slot->header.layout,sizeof(h.layout))&&
                !(h.edges>>h.layout.count)&&decode(f,slot->pixels,h.bytes)&&checksum(slot->pixels,h.bytes)==h.checksum;
            if(fclose(f))ok=false;
            if(ok)slot->header=h;
        }
        if(cancelled())ok=false;
        slot->valid=ok;
    }else{
        if(!cancelled()){
            (void)mkdir(BOOK_SHELF_PAGE_PARENT,0755);(void)mkdir(BOOK_SHELF_PAGE_ROOT,0755);
            slot->header.checksum=checksum(slot->pixels,slot->header.bytes);
            FILE *f=cancelled()?NULL:fopen(temp,"wb");
            if(f){ok=io(f,&slot->header,sizeof(slot->header),true)&&encode(f,slot->pixels,slot->header.bytes);
                if(fclose(f))ok=false;
                if(cancelled())ok=false;
                if(ok){
                    int moved=rename(temp,path);
                    // FAT不一定支持覆盖rename；只回收本缓存的同名旧文件。
                    // Some FAT ports cannot overwrite on rename; retire only this cache's old target.
                    if(moved&&errno==EEXIST&&!cancelled()&&!unlink(path))moved=rename(temp,path);
                    ok=moved==0;
                }
                if(!ok)(void)unlink(temp);
            }
            if(ok)trim(path);
        }
        slot->dirty=false;
    }
    portENTER_CRITICAL(&s_lock);s_running=false;portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}
static bool start(int slot,bool write) {
    s_work=slot;s_writing=write;
    portENTER_CRITICAL(&s_lock);s_cancel=false;s_running=true;portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(worker,"shelf-page",6144,NULL,1,NULL)==pdPASS)return true;
    portENTER_CRITICAL(&s_lock);s_running=false;portEXIT_CRITICAL(&s_lock);return false;
}
static int reserve(void) {
    int slot=s_work>=0&&!s_slots[s_work].valid?s_work:(!s_slots[0].valid?0:!s_slots[1].valid?1:s_slots[0].stamp<=s_slots[1].stamp?0:1);
    page_slot_t *p=&s_slots[slot];
    size_t need=s_wanted.bytes;
    if(p->pixels&&p->header.bytes!=need){free(p->pixels);p->pixels=NULL;}
    p->valid=p->dirty=p->staged=false;
    if(!p->pixels){
        int other=1-slot;
        size_t held=s_slots[other].pixels?s_slots[other].header.bytes:0;
        if(held+need>PAGE_BUDGET||heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+PAGE_RESERVE){
            free(s_slots[other].pixels);memset(&s_slots[other],0,sizeof(s_slots[other]));
        }
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+PAGE_RESERVE)return -1;
        p->pixels=heap_caps_malloc(need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!p->pixels)return -1;
    }
    p->header=s_wanted;p->stamp=++s_stamp;return slot;
}
book_shelf_page_state_t book_shelf_page_request(book_shelf_page_key_t key,book_shelf_page_layout_t layout) {
    join();s_active=-1;s_work=-1;s_lookup_done=true;memset(&s_wanted,0,sizeof(s_wanted));
    if(!layout.width||!layout.height||layout.width>2048||layout.height>2048||(layout.width&1)||
       !layout.count||layout.count>BOOK_SHELF_PAGE_COVERS||!fits(layout.area,layout.width,layout.height))return BOOK_SHELF_PAGE_MISS;
    size_t bytes=plane_bytes(layout.area);
    for(unsigned i=0;i<layout.count;++i){if(!fits(layout.covers[i],layout.width,layout.height))return BOOK_SHELF_PAGE_MISS;bytes+=plane_bytes(layout.covers[i]);}
    if(bytes>PAGE_BUDGET)return BOOK_SHELF_PAGE_MISS;
    const uint32_t artwork_version[]={BOOK_SHELF_ARTWORK_VERSION,BOOK_AUTO_COVER_VERSION};
    book_shelf_page_key_add(&key,artwork_version,sizeof(artwork_version));
    book_shelf_page_key_add(&key,&layout,sizeof(layout));
    s_wanted=(page_header_t){.magic=PAGE_MAGIC,.version=PAGE_VERSION,.header_bytes=sizeof(page_header_t),.bytes=(uint32_t)bytes,.key=key,.layout=layout};
    for(int i=0;i<2;++i)if(s_slots[i].valid&&!memcmp(&s_slots[i].header.key,&key,sizeof(key))){s_active=i;s_slots[i].stamp=++s_stamp;return BOOK_SHELF_PAGE_HIT;}
    if(!read_pico_sd_io_ready())return BOOK_SHELF_PAGE_MISS;
    int slot=reserve();if(slot<0)return BOOK_SHELF_PAGE_MISS;
    s_lookup_done=!start(slot,false);return s_lookup_done?BOOK_SHELF_PAGE_MISS:BOOK_SHELF_PAGE_WAIT;
}
book_shelf_page_state_t book_shelf_page_poll(void) {
    if(s_active>=0)return BOOK_SHELF_PAGE_HIT;
    if(s_lookup_done)return BOOK_SHELF_PAGE_MISS;
    if(running())return BOOK_SHELF_PAGE_WAIT;
    s_lookup_done=true;
    if(s_work>=0&&s_slots[s_work].valid){s_active=s_work;return BOOK_SHELF_PAGE_HIT;}
    return BOOK_SHELF_PAGE_MISS;
}
/* 原物理像素逐行复制；奇数x仅覆盖边界半字节，避免破坏阴影。
 * Copy physical rows; odd-x edges preserve neighboring shadow nibbles. */
static void rows(uint8_t *fb,uint8_t *pixels,EpdRect r,unsigned width,bool out) {
    size_t n=span(r),stride=width/2;
    for(int y=0;y<r.height;++y){
        uint8_t *f=fb+(size_t)(r.y+y)*stride+r.x/2,*p=pixels+(size_t)y*n;
        if(!out){memcpy(p,f,n);continue;}
        uint8_t first=f[0],last=f[n-1];memcpy(f,p,n);
        if(r.x&1)f[0]=(f[0]&0xf0)|(first&0x0f);
        if((r.x+r.width)&1)f[n-1]=(f[n-1]&0x0f)|(last&0xf0);
    }
}
bool book_shelf_page_restore(uint8_t *fb) {
    if(!fb||s_active<0||!s_slots[s_active].valid)return false;
    page_slot_t *s=&s_slots[s_active];rows(fb,s->pixels,s->header.layout.area,s->header.layout.width,true);return true;
}
bool book_shelf_page_stage_covers(const uint8_t *fb) {
    if(!fb||s_active>=0||running()||!s_lookup_done||!s_wanted.bytes)return false;
    int slot=reserve();if(slot<0)return false;s_work=slot;page_slot_t *s=&s_slots[slot];
    size_t offset=plane_bytes(s->header.layout.area);
    for(unsigned i=0;i<s->header.layout.count;++i){EpdRect r=s->header.layout.covers[i];rows((uint8_t*)fb,s->pixels+offset,r,s->header.layout.width,false);offset+=plane_bytes(r);}
    s->staged=true;return true;
}
bool book_shelf_page_capture(const uint8_t *fb,unsigned favorite_edges,uint64_t now_ms) {
    if(!fb||s_active>=0||s_work<0||running()||!s_slots[s_work].staged)return false;
    page_slot_t *s=&s_slots[s_work];rows((uint8_t*)fb,s->pixels,s->header.layout.area,s->header.layout.width,false);
    s->header.edges=favorite_edges&((1u<<s->header.layout.count)-1);s->valid=s->dirty=true;s->staged=false;s->due=now_ms+800;s_active=s_work;return true;
}
bool book_shelf_page_copy_cover(uint8_t *fb,unsigned row,EpdRect destination) {
    if(!fb||s_active<0)return false;
    page_slot_t *s=&s_slots[s_active];
    if(row>=s->header.layout.count)return false;
    EpdRect source=s->header.layout.covers[row];
    if(destination.width!=source.width||destination.height!=source.height||(destination.x&1)!=(source.x&1)||
        !fits(destination,s->header.layout.width,s->header.layout.height))return false;
    size_t offset=plane_bytes(s->header.layout.area);
    for(unsigned i=0;i<row;++i)offset+=plane_bytes(s->header.layout.covers[i]);
    rows(fb,s->pixels+offset,destination,s->header.layout.width,true);return true;
}
unsigned book_shelf_page_favorite_edges(void){return s_active<0?0:s_slots[s_active].header.edges;}
void book_shelf_page_idle(uint64_t now_ms) {
    if(running()||s_active<0||!read_pico_sd_io_ready())return;
    page_slot_t *s=&s_slots[s_active];if(s->dirty&&now_ms>=s->due&&!start(s_active,true))s->dirty=false;
}
void book_shelf_page_pause(void){join();}
void book_shelf_page_release(void) {
    join();for(int i=0;i<2;++i){free(s_slots[i].pixels);memset(&s_slots[i],0,sizeof(s_slots[i]));}
    memset(&s_wanted,0,sizeof(s_wanted));s_active=s_work=-1;s_lookup_done=true;
}
