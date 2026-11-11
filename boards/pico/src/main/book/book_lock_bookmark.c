/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：透明PNG书封直接叠加在当前画面上；保留原画布比例，不依赖阅读记录或额外整帧缓存。
 * English: Composite a transparent PNG cover over the current frame, preserving canvas ratio without reader history or an extra full-frame cache.
 * 用户2026-10-21修订：有界PSRAM行环合并阴影/图案读取，保留逐像素结果；低内存沿用原双遍，缓存完整校验先于底图修改。
 * User revision2026-10-21: a bounded PSRAM row ring fuses shadow/art reads with identical pixels; low memory retains two passes and cache validation precedes background edits.
 * 用户2026-10-24修订：缓存灰度/alpha字节对无损行压缩，FAT安全替换，继续完整校验后再叠加。
 * User revision2026-10-24: losslessly pack cached gray/alpha pairs and replace safely on FAT, retaining full validation before composition.
 * 用户2026-11-04：内存充足时暂存校验中的解压层，完整校验后按原算法合成并释放；低内存沿用行式路径。
 * User2026-11-04: retain decoded pixels during validation when memory permits, compose identically then free; low memory retains streaming.
 */
#include "book_lock_bookmark.h"
#include "book_cover.h"
#include "ui_image_dither.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#ifndef BOOK_SEAL_CACHE_PARENT
#if KIIKO_BOARD_METALIO47
#define BOOK_SEAL_CACHE_PARENT "/sdcard/.metalio_s31"
#else
#define BOOK_SEAL_CACHE_PARENT "/sdcard/.readpico"
#endif
#endif
#define SEAL_CACHE BOOK_SEAL_CACHE_PARENT "/locks/seal-layer.bin"
#define SEAL_TEMP SEAL_CACHE ".tmp"
#define SEAL_PACK SEAL_CACHE ".packed"

typedef struct {
    uint8_t *visited,*pending;
    uint16_t *rows;
    unsigned height,head,tail,count;
} white_flood_t;
static void white_flood_mark(white_flood_t *f,size_t at,unsigned y) {
    uint8_t bit=(uint8_t)(1u<<(at&7));
    if(f->visited[at>>3]&bit)return;
    f->visited[at>>3]|=bit;
    if(f->pending[y])return;
    f->pending[y]=1;f->rows[f->tail]=(uint16_t)y;
    f->tail=(f->tail+1)%f->height;++f->count;
}
// 不透明白底PNG才自动去白：四邻接连通到画布边缘，封闭白色图案保留。
// Remove only near-white regions connected to an opaque PNG's canvas edges by four-neighbor paths; preserve enclosed whites.
// 原始alpha优先，避免透明素材的白色主体被误删；工作区仅一位/像素和有界行队列。
// Original alpha takes priority, protecting white artwork in transparent assets; workspace is one bit/pixel plus a bounded row queue.
static bool remove_white_background(uint8_t *gray,uint8_t *alpha,unsigned w,unsigned h) {
    size_t count=(size_t)w*h;
    if(!gray||!alpha||!w||!h||w>684||h>1216)return false;
    for(size_t i=0;i<count;++i)if(alpha[i]!=255)return true;
    bool border=false;
    for(unsigned x=0;x<w;++x)if(gray[x]>=250||gray[(h-1)*w+x]>=250){border=true;break;}
    if(!border)for(unsigned y=0;y<h;++y)if(gray[(size_t)y*w]>=250||gray[(size_t)y*w+w-1]>=250){border=true;break;}
    if(!border)return true;
    size_t bits=(count+7)/8,queue_offset=(bits+h+1)&~(size_t)1;
    uint8_t *work=heap_caps_malloc(queue_offset+2*h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!work)return false;
    memset(work,0,queue_offset);
    white_flood_t f={.visited=work,.pending=work+bits,.rows=(uint16_t *)(work+queue_offset),.height=h};
    for(unsigned x=0;x<w;++x){
        if(gray[x]>=250)white_flood_mark(&f,x,0);
        size_t at=(size_t)(h-1)*w+x;if(gray[at]>=250)white_flood_mark(&f,at,h-1);
    }
    for(unsigned y=1;y+1<h;++y){
        size_t at=(size_t)y*w;if(gray[at]>=250)white_flood_mark(&f,at,y);
        if(gray[at+w-1]>=250)white_flood_mark(&f,at+w-1,y);
    }
    while(f.count){
        unsigned y=f.rows[f.head];f.head=(f.head+1)%h;--f.count;f.pending[y]=0;
        size_t row=(size_t)y*w;
        for(unsigned x=0;x<w;){
            if(gray[row+x]<250){++x;continue;}
            unsigned left=x;bool reached=false;
            while(x<w&&gray[row+x]>=250){size_t at=row+x++;if(f.visited[at>>3]&(1u<<(at&7)))reached=true;}
            if(!reached)continue;
            for(unsigned xx=left;xx<x;++xx){
                size_t at=row+xx;f.visited[at>>3]|=(uint8_t)(1u<<(at&7));
                if(y&&gray[at-w]>=250)white_flood_mark(&f,at-w,y-1);
                if(y+1<h&&gray[at+w]>=250)white_flood_mark(&f,at+w,y+1);
            }
        }
    }
    for(size_t i=0;i<count;++i)if(f.visited[i>>3]&(1u<<(i&7)))gray[i]=alpha[i]=0;
    free(work);return true;
}
static uint8_t pixel(const uint8_t *fb,int x,int y) {
    int px=x,py=y;
    switch(epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        default:break;
    }
    return (epd_get_pixel(px,py,epd_width(),epd_height(),fb)>>4)*17u;
}

// 缓存的是PNG层，不含阅读底图；逐行叠加到每次锁屏的真实当前页面。
// Cache only the PNG layer, then stream it over the current page without retaining fonts/images twice.
typedef struct { uint32_t magic,width,height,checksum; uint64_t key; } seal_header_t;
typedef struct { FILE *file; uint8_t *white; bool has_alpha; } seal_sink_t;
static uint32_t seal_checksum(uint32_t hash,const uint8_t *p,size_t n) {
    while(n--)hash=(hash^*p++)*16777619u;
    return hash;
}
// 每行按灰度/alpha原始字节对无损压缩；高噪声行保持原样，不降低透明度或灰阶。
// Losslessly pack exact gray/alpha pairs per row; noisy rows remain raw without reducing alpha or gray precision.
static bool seal_write_row(FILE *file,const uint8_t *row,unsigned w){
    uint8_t packed[684*2+6],size[2];unsigned x=0,n=0;
    while(x<w){
        unsigned run=1;
        while(run<128&&x+run<w&&!memcmp(row+x*2,row+(x+run)*2,2))++run;
        if(run>=3){packed[n++]=(uint8_t)(128+run-1);packed[n++]=row[x*2];packed[n++]=row[x*2+1];x+=run;}
        else{
            unsigned start=x;x+=run;
            while(x<w&&x-start<128){
                run=1;while(run<3&&x+run<w&&!memcmp(row+x*2,row+(x+run)*2,2))++run;
                if(run>=3)break;
                if(run>128-(x-start))run=128-(x-start);
                x+=run;
            }
            unsigned count=x-start;packed[n++]=(uint8_t)(count-1);
            memcpy(packed+n,row+start*2,count*2);n+=count*2;
        }
    }
    unsigned length=n<w*2?n:0;size[0]=(uint8_t)length;size[1]=(uint8_t)(length>>8);
    return fwrite(size,1,2,file)==2&&fwrite(length?packed:row,1,length?length:w*2,file)==(length?length:w*2);
}
static bool seal_read_row(FILE *file,uint8_t *row,unsigned w){
    uint8_t size[2],packed[684*2+6];
    if(fread(size,1,2,file)!=2)return false;
    unsigned length=(unsigned)size[0]+((unsigned)size[1]<<8);
    if(!length)return fread(row,2,w,file)==w;
    if(length>w*2+(w+127)/128||length>sizeof(packed)||fread(packed,1,length,file)!=length)return false;
    unsigned at=0,x=0;
    while(at<length){
        unsigned flag=packed[at++],count=(flag&127)+1;
        if(count>w-x)return false;
        if(flag&128){
            if(length-at<2)return false;
            for(unsigned i=0;i<count;++i){row[(x+i)*2]=packed[at];row[(x+i)*2+1]=packed[at+1];}at+=2;
        }else{
            if(length-at<count*2)return false;
            memcpy(row+x*2,packed+at,count*2);at+=count*2;
        }
        x+=count;
    }
    return x==w;
}
static bool seal_publish(void){
    if(!rename(SEAL_PACK,SEAL_CACHE))return true;
    // FAT不能覆盖rename；保留旧缓存直到新文件可发布，失败恢复，不反复走PNG解码。
    // FAT cannot replace by rename; preserve the old cache until publication and roll back failures instead of repeatedly decoding PNG.
    (void)remove(SEAL_CACHE ".bak");
    if(!rename(SEAL_CACHE,SEAL_CACHE ".bak")){
        if(!rename(SEAL_PACK,SEAL_CACHE)){(void)remove(SEAL_CACHE ".bak");return true;}
        (void)rename(SEAL_CACHE ".bak",SEAL_CACHE);
    }
    return false;
}
static bool seal_build_memory(const char *path,seal_header_t *header,bool *attempted){
    const size_t count=(size_t)header->width*header->height;
    *attempted=false;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<2*count+1024u*1024u)return false;
    uint8_t *layers=heap_caps_malloc(2*count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!layers)return false;
    bool ok=book_png_file_layer(path,header->width,header->height,layers,layers+count)&&
        remove_white_background(layers,layers+count,header->width,header->height);
    if(!ok){free(layers);return false;}
    // 首次生成借用空闲PSRAM，避免把未压缩层反复写回TF；低内存继续走有界行流程。
    // Borrow spare PSRAM for the first build, avoiding raw SD intermediates; low memory retains bounded row processing.
    *attempted=true;
    FILE *file=fopen(SEAL_PACK,"wb");
    if(!file){free(layers);return false;}
    header->checksum=2166136261u;uint8_t row[684*2];
    ok=fwrite(header,1,sizeof(*header),file)==sizeof(*header);
    for(unsigned y=0;y<header->height&&ok;++y){
        for(unsigned x=0;x<header->width;++x){
            size_t at=(size_t)y*header->width+x;row[x*2]=layers[at];row[x*2+1]=layers[count+at];
        }
        header->checksum=seal_checksum(header->checksum,row,header->width*2);
        ok=seal_write_row(file,row,header->width);
    }
    free(layers);
    if(ok)ok=!fseek(file,0,SEEK_SET)&&fwrite(header,1,sizeof(*header),file)==sizeof(*header)&&!fflush(file);
    if(fclose(file))ok=false;
    if(ok&&seal_publish())return true;
    (void)remove(SEAL_PACK);return false;
}
static bool seal_row(void *context,unsigned y,unsigned w,const uint8_t *gray,const uint8_t *alpha) {
    seal_sink_t *s=context; uint8_t row[684*2];
    for(unsigned x=0;x<w;++x) {
        row[x*2]=gray[x];row[x*2+1]=alpha[x];
        if(alpha[x]!=255)s->has_alpha=true;
        size_t at=(size_t)y*w+x;
        if(gray[x]>=250)s->white[at>>3]|=(uint8_t)(1u<<(at&7));
    }
    return fwrite(row,2,w,s->file)==w;
}
static bool seal_white(const uint8_t *bits,size_t at) {return (bits[at>>3]>>(at&7))&1u;}
static bool seal_remove_white(FILE *file,const uint8_t *white,unsigned w,unsigned h) {
    size_t bits=((size_t)w*h+7)/8,offset=(bits+h+1)&~(size_t)1;
    uint8_t *work=heap_caps_malloc(offset+2*h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!work)return false;
    memset(work,0,offset);
    white_flood_t f={.visited=work,.pending=work+bits,.rows=(uint16_t*)(work+offset),.height=h};
    for(unsigned x=0;x<w;++x) {
        if(seal_white(white,x))white_flood_mark(&f,x,0);
        size_t at=(size_t)(h-1)*w+x;if(seal_white(white,at))white_flood_mark(&f,at,h-1);
    }
    for(unsigned y=1;y+1<h;++y) {
        size_t at=(size_t)y*w;
        if(seal_white(white,at))white_flood_mark(&f,at,y);
        if(seal_white(white,at+w-1))white_flood_mark(&f,at+w-1,y);
    }
    while(f.count) {
        unsigned y=f.rows[f.head];f.head=(f.head+1)%h;--f.count;f.pending[y]=0;
        size_t row=(size_t)y*w;
        for(unsigned x=0;x<w;) {
            if(!seal_white(white,row+x)){++x;continue;}
            unsigned left=x;bool reached=false;
            while(x<w&&seal_white(white,row+x)){size_t at=row+x++;reached|=seal_white(f.visited,at);}
            if(!reached)continue;
            for(unsigned xx=left;xx<x;++xx) {
                size_t at=row+xx;f.visited[at>>3]|=(uint8_t)(1u<<(at&7));
                if(y&&seal_white(white,at-w))white_flood_mark(&f,at-w,y-1);
                if(y+1<h&&seal_white(white,at+w))white_flood_mark(&f,at+w,y+1);
            }
        }
    }
    bool ok=true;uint8_t row[684*2];
    for(unsigned y=0;y<h&&ok;++y) {
        long at=(long)(sizeof(seal_header_t)+(size_t)y*w*2);
        ok=!fseek(file,at,SEEK_SET)&&fread(row,2,w,file)==w;
        for(unsigned x=0;x<w&&ok;++x)if(seal_white(f.visited,(size_t)y*w+x))row[x*2]=row[x*2+1]=0;
        if(ok)ok=!fseek(file,at,SEEK_SET)&&fwrite(row,2,w,file)==w;
    }
    free(work);return ok;
}
static bool seal_build(const char *path,seal_header_t *header) {
    if((mkdir(BOOK_SEAL_CACHE_PARENT,0775)&&errno!=EEXIST)||
       (mkdir(BOOK_SEAL_CACHE_PARENT "/locks",0775)&&errno!=EEXIST))return false;
    bool attempted=false,ready=seal_build_memory(path,header,&attempted);
    if(attempted)return ready;
    size_t bits=((size_t)header->width*header->height+7)/8;
    uint8_t *white=heap_caps_malloc(bits,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!white)return false;
    memset(white,0,bits);
    FILE *f=fopen(SEAL_TEMP,"wb+");
    if(!f){free(white);return false;}
    seal_sink_t sink={.file=f,.white=white};
    bool ok=fwrite(header,1,sizeof(*header),f)==sizeof(*header)&&
        book_png_file_layer_rows(path,header->width,header->height,seal_row,&sink);
    if(ok&&!sink.has_alpha)ok=seal_remove_white(f,white,header->width,header->height);
    free(white);
    uint8_t row[684*2];header->checksum=2166136261u;
    if(ok)ok=!fflush(f)&&!fseek(f,sizeof(*header),SEEK_SET);
    FILE *packed=ok?fopen(SEAL_PACK,"wb"):NULL;
    if(ok)ok=packed&&fwrite(header,1,sizeof(*header),packed)==sizeof(*header);
    for(unsigned y=0;y<header->height&&ok;++y) {
        ok=fread(row,2,header->width,f)==header->width;
        if(ok){header->checksum=seal_checksum(header->checksum,row,header->width*2);ok=seal_write_row(packed,row,header->width);}
    }
    if(ok)ok=!fseek(packed,0,SEEK_SET)&&fwrite(header,1,sizeof(*header),packed)==sizeof(*header)&&!fflush(packed);
    if(packed&&fclose(packed))ok=false;
    if(fclose(f))ok=false;
    (void)remove(SEAL_TEMP);
    if(ok&&seal_publish())return true;
    (void)remove(SEAL_PACK);return false;
}
static FILE *seal_open(const seal_header_t *wanted,uint8_t **decoded) {
    if(decoded)*decoded=NULL;
    FILE *f=fopen(SEAL_CACHE,"rb");if(!f)return NULL;
    seal_header_t h;uint8_t row[684*2];uint32_t sum=2166136261u;
    bool ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&h.magic==wanted->magic&&h.width==wanted->width&&
        h.height==wanted->height&&h.key==wanted->key;
    uint8_t *layer=NULL;
    // 完整校验时保留解压层，避免随后再次读卡/解压；保留1MiB余量，失败沿用行式路径。
    // Retain decoded pixels during validation to avoid a second read/decode. Keep1MiB free; allocation failure retains streaming.
    const size_t bytes=(size_t)wanted->width*wanted->height*2;
    if(ok&&decoded&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>=bytes+1024u*1024u)
        layer=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    for(unsigned y=0;y<wanted->height&&ok;++y) {
        uint8_t *target=layer?layer+(size_t)y*wanted->width*2:row;
        ok=seal_read_row(f,target,wanted->width);
        if(ok)sum=seal_checksum(sum,target,wanted->width*2);
    }
    if(ok)ok=sum==h.checksum&&fgetc(f)==EOF&&!ferror(f);
    if(ok){if(decoded)*decoded=layer;return f;}
    free(layer);fclose(f);return NULL;
}
// 同一合成循环读取已校验内存层或低内存行环；两者保留相同阴影/图案混合顺序。
// One compositor reads a validated memory layer or a bounded row ring, preserving the same shadow/art blending order.
static bool seal_draw_rows(FILE *f,uint8_t *fb,unsigned w,unsigned h,uint8_t *work,uint8_t *decoded,bool *started) {
    const size_t row_bytes=(size_t)w*2;
    uint8_t *block=work?work+7*row_bytes:NULL;
    unsigned buffered=0,block_row=0;
    const int ox=(684-(int)w)/2,oy=(1216-(int)h)/2;
    int dx=1,dy=epd_width(),origin=0;
    switch(epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:dx=epd_width();dy=-1;origin=epd_width()-1;break;
        case EPD_ROT_INVERTED_PORTRAIT:dx=-epd_width();dy=1;origin=(epd_height()-1)*epd_width();break;
        case EPD_ROT_INVERTED_LANDSCAPE:dx=-1;dy=-epd_width();origin=epd_width()*epd_height()-1;break;
        default:break;
    }
    if(!decoded&&fseek(f,sizeof(seal_header_t),SEEK_SET))return false;
    for(unsigned y=0;y<h+6&&oy+(int)y<1216;++y) {
        uint8_t *art=NULL;
        if(decoded){if(y<h)art=decoded+(size_t)y*row_bytes;}
        else if(y<h) {
            if(block_row==buffered) {
                buffered=h-y<16?h-y:16;block_row=0;
                for(unsigned i=0;i<buffered;++i)if(!seal_read_row(f,block+i*row_bytes,w))return false;
            }
            art=work+(y%7)*row_bytes;
            memcpy(art,block+block_row++*row_bytes,row_bytes);
        }
        const uint8_t *shadow=y>=6?(decoded?decoded+(size_t)(y-6)*row_bytes:work+((y-6)%7)*row_bytes):NULL;
        int at=origin+(oy+(int)y)*dy+ox*dx;
        for(unsigned x=0;x<w+4&&ox+(int)x<684;++x,at+=dx) {
            unsigned a=art&&x<w?art[x*2+1]:0;
            unsigned sa=shadow&&x>=4&&x-4<w?shadow[(x-4)*2+1]:0;
            if(!a&&!sa)continue;
            const int xx=ox+(int)x,yy=oy+(int)y,shift=(at&1)*4;
            uint8_t *target=fb+(size_t)at/2;
            unsigned value=((*target>>shift)&15)*17u;
            if(sa)value=ui_image_dither_gray((value*(255-sa/5)+127)/255,xx,yy);
            if(a) {
                value=art[x*2]+(value*(255-a)+127)/255;
                if(value>255)value=255;
                value=ui_image_dither_gray(value,xx,yy);
            }
            *started=true;
            *target=(uint8_t)((*target&~(15u<<shift))|((value>>4)<<shift));
        }
    }
    return decoded||(fgetc(f)==EOF&&!ferror(f));
}
static bool seal_draw(uint8_t *fb,const char *path,unsigned w,unsigned h,bool *started) {
    struct stat st;if(stat(path,&st))return false;
    uint64_t key=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char*)path;*p;++p)key=(key^*p)*UINT64_C(1099511628211);
    key=(key^(uint64_t)st.st_size)*UINT64_C(1099511628211);
    key=(key^(uint64_t)st.st_mtime)*UINT64_C(1099511628211);
    seal_header_t wanted={.magic=UINT32_C(0x53454132),.width=w,.height=h,.key=key};
    uint8_t *decoded=NULL;
    FILE *f=seal_open(&wanted,&decoded);
    if(!f&&seal_build(path,&wanted))f=seal_open(&wanted,&decoded);
    if(!f)return false;
    if(decoded) {
        fclose(f);
        bool ok=seal_draw_rows(NULL,fb,w,h,NULL,decoded,started);
        free(decoded);return ok;
    }
    uint8_t *work=heap_caps_malloc((size_t)w*2*23,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(work) {
        bool ok=seal_draw_rows(f,fb,w,h,work,NULL,started);
        free(work);fclose(f);return ok;
    }
    uint8_t row[684*2];bool ok=true;int ox=(684-(int)w)/2,oy=(1216-(int)h)/2;
    for(unsigned pass=0;pass<2&&ok;++pass) {
        ok=!fseek(f,sizeof(wanted),SEEK_SET);
        for(unsigned y=0;y<h&&ok;++y) {
            ok=seal_read_row(f,row,w);
            for(unsigned x=0;x<w&&ok;++x) {
                unsigned a=row[x*2+1];if(!a)continue;
                int xx=ox+x+(pass?0:4),yy=oy+y+(pass?0:6);
                if(xx>=684||yy>=1216)continue;
                unsigned bg=pixel(fb,xx,yy);
                unsigned value=pass?row[x*2]+(bg*(255-a)+127)/255:(bg*(255-a/5)+127)/255;
                if(value>255)value=255;
                *started=true;
                epd_draw_pixel(xx,yy,ui_image_dither_gray(value,xx,yy),fb);
            }
        }
    }
    fclose(f);return ok;
}
bool book_lock_bookmark_draw(uint8_t *fb,const char *path) {
    unsigned sw,sh;
    const char *ext=path?strrchr(path,'.'):NULL;
    if(!fb||!ext||strcasecmp(ext,".png")||!book_image_file_dimensions(path,true,&sw,&sh)||!sw||!sh)return false;
    // 以整个PNG画布居中等比缩放，684x1216时逐像素对齐，不识别或裁剪主体。
    // Center-fit the entire PNG canvas; 684x1216 maps pixel-for-pixel, without subject detection or cropping.
    unsigned w=684,h=(uint64_t)sh*w/sw;
    if(h>1216){h=1216;w=(uint64_t)sw*h/sh;}
    if(!w||!h)return false;
    bool started=false;
    if(seal_draw(fb,path,w,h,&started))return true;
    // 卡在输出途中失败时底图已不完整，交给锁屏入口画白底锁；禁止在半成品上再叠一次PNG。
    // A mid-output media failure leaves an incomplete frame: let the lock entry draw its white badge, never overlay the PNG twice.
    if(started)return false;
    size_t count=(size_t)w*h;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<2*count+512u*1024u)return false;
    uint8_t *layers=heap_caps_malloc(2*count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!layers)return false;
    uint8_t *alpha=layers+count;
    bool ok=book_png_file_layer(path,w,h,layers,alpha) && remove_white_background(layers,alpha,w,h);
    int ox=(684-(int)w)/2,oy=(1216-(int)h)/2;
    if(ok) {
        // 小幅偏移的柔影只作用在非透明轮廓附近。/ A softly offset shadow affects only the nontransparent silhouette.
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            unsigned a=alpha[(size_t)y*w+x];if(!a||x+ox+4>=684||y+oy+6>=1216)continue;
            int xx=ox+x+4,yy=oy+y+6;
            unsigned bg=pixel(fb,xx,yy);
            epd_draw_pixel(xx,yy,ui_image_dither_gray((bg*(255-a/5)+127)/255,xx,yy),fb);
        }
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            size_t at=(size_t)y*w+x;unsigned a=alpha[at];if(!a)continue;
            int xx=ox+x,yy=oy+y;
            unsigned value=layers[at]+(pixel(fb,xx,yy)*(255-a)+127)/255;
            if(value>255)value=255;
            epd_draw_pixel(xx,yy,ui_image_dither_gray(value,xx,yy),fb);
        }
    }
    free(layers);return ok;
}
void book_lock_bookmark_preview_paint(uint8_t *fb,const uint8_t *canvas,EpdRect area) {
    if(!fb||!canvas||area.width<1||area.height<1)return;
    for(int y=0;y<area.height;++y)for(int x=0;x<area.width;++x)
        epd_draw_pixel(area.x+x,area.y+y,pixel(canvas,x*684/area.width,y*1216/area.height)&0xf0,fb);
}
