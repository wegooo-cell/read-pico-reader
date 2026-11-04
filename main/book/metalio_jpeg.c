/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：有界JPEG硬解与完整渐进式软解；最终灰阶与现有缓存/波形保持独立。
 * English: Bounded hardware JPEG and full progressive software decoding; preserve grayscale/cache/waveform policy.
 */
#include "metalio_jpeg.h"
#include "book_cover.h"
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "jpeglib.h"
#include "jerror.h"
#ifdef ESP_PLATFORM
#include "driver/jpeg_decode.h"
#include "read_pico_sd.h"
#include <fcntl.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "metalio_jpeg_host.h"
#endif

#define JPEG_RESERVE (2u*1024u*1024u)
#define JPEG_WORK_MAX (12u*1024u*1024u)
static const char *TAG="s31_jpeg";
static atomic_uint jpeg_cancel_generation;
static _Thread_local bool jpeg_last_cancelled, jpeg_progressive_claimed;
static _Thread_local unsigned jpeg_operation_generation;

// 强缩小时用面积平均保留细线；放大保留双线性，不加锐化或改灰阶。
// Area averaging retains fine lines on reduction; enlargement remains bilinear with no sharpening/quantization.
static bool jpeg_scale(const uint8_t *src,unsigned sw,unsigned sh,unsigned stride,
                       unsigned dw,unsigned dh,uint8_t *out) {
    if(!src||!out||!sw||!sh||sw>8192||sh>8192||stride<sw||!dw||!dh||dw>1216||dh>1216)return false;
    book_crop_t c=book_cover_crop(sw,sh,dw,dh);
    if(!c.width||!c.height||c.x+c.width>sw||c.y+c.height>sh)return false;
    for(unsigned y=0;y<dh;++y) {
        if(c.width>=dw && c.height>=dh) {
            unsigned top=(uint64_t)y*c.height*256/dh,bottom=(uint64_t)(y+1)*c.height*256/dh;
            for(unsigned x=0;x<dw;++x) {
                unsigned left=(uint64_t)x*c.width*256/dw,right=(uint64_t)(x+1)*c.width*256/dw;
                uint64_t sum=0,total=(uint64_t)(right-left)*(bottom-top);
                for(unsigned sy=top/256;sy<(bottom+255)/256;++sy) {
                    unsigned a=top>sy*256?top:sy*256,b=bottom<(sy+1)*256?bottom:(sy+1)*256;
                    const uint8_t *row=src+(size_t)(c.y+sy)*stride+c.x;
                    for(unsigned sx=left/256;sx<(right+255)/256;++sx) {
                        unsigned l=left>sx*256?left:sx*256,r=right<(sx+1)*256?right:(sx+1)*256;
                        sum+=(uint64_t)row[sx]*(r-l)*(b-a);
                    }
                }
                out[(size_t)y*dw+x]=(uint8_t)((sum+total/2)/total);
            }
        } else {
            int py=(int)(((uint64_t)(2*y+1)*c.height*128)/dh)-128;
            if(py<0)py=0;
            unsigned sy=(unsigned)py/256,fy=(unsigned)py%256,sy1=sy+1<c.height?sy+1:sy;
            for(unsigned x=0;x<dw;++x) {
                int px=(int)(((uint64_t)(2*x+1)*c.width*128)/dw)-128;
                if(px<0)px=0;
                unsigned sx=(unsigned)px/256,fx=(unsigned)px%256,sx1=sx+1<c.width?sx+1:sx;
                const uint8_t *a=src+(size_t)(c.y+sy)*stride+c.x,*b=src+(size_t)(c.y+sy1)*stride+c.x;
                unsigned n=(a[sx]*(256-fx)+a[sx1]*fx)*(256-fy)+(b[sx]*(256-fx)+b[sx1]*fx)*fy;
                out[(size_t)y*dw+x]=(uint8_t)((n+32768)/65536);
            }
        }
        if((y&63u)==0)vTaskDelay(1);
    }
    return true;
}

static size_t jpeg_budget(void) {
    size_t n=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return n>JPEG_RESERVE?n-JPEG_RESERVE:0;
}
static bool jpeg_budget_ok(size_t bytes) {
    return bytes<=JPEG_WORK_MAX && bytes<=jpeg_budget() &&
        bytes<=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

#define JPEG_PROGRESSIVE_WORK_MAX (3u*1024u*1024u)
#define JPEG_SOFT_DEADLINE_US INT64_C(30000000)
void metalio_jpeg_cancel(void) { atomic_fetch_add(&jpeg_cancel_generation, 1); }
bool metalio_jpeg_cancelled(void) { return jpeg_last_cancelled; }
bool metalio_jpeg_fallback_allowed(void) { return !jpeg_last_cancelled && !jpeg_progressive_claimed; }

typedef struct {
    JBLOCKARRAY rows;
    JBLOCK *blocks;
    uint8_t *present;
    FILE *backing;
    char path[128];
    JDIMENSION width, height, capacity, start, count;
    bool loaded, dirty;
} jpeg_band_t;
typedef struct {
    struct jpeg_error_mgr error;
    struct jpeg_progress_mgr progress;
    struct jpeg_decompress_struct jpeg;
    jmp_buf escape;
    uint8_t *gray, *scan_rows;
    uint64_t *sums;
    jpeg_band_t bands[3];
    unsigned band_count, generation;
    size_t work_bytes;
    void (*realize)(j_common_ptr);
    boolean (*fill_input)(j_decompress_ptr);
    void (*skip_input)(j_decompress_ptr,long);
    int64_t start, yield_at;
} jpeg_soft_t;
static void jpeg_soft_error(j_common_ptr p) {
#ifdef METALIO_JPEG_HOST_DEBUG
    char error[JMSG_LENGTH_MAX];p->err->format_message(p,error);fprintf(stderr,"JPEG: %s\n",error);
#endif
    longjmp(((jpeg_soft_t *)p->client_data)->escape,1);
}
static void jpeg_soft_check(jpeg_soft_t *s) {
    jpeg_last_cancelled = s->generation != atomic_load(&jpeg_cancel_generation);
    if (jpeg_last_cancelled || esp_timer_get_time()-s->start > JPEG_SOFT_DEADLINE_US)
        ERREXIT(&s->jpeg, JERR_BAD_STATE);
    if (esp_timer_get_time()-s->yield_at >= 20000) { s->yield_at=esp_timer_get_time(); vTaskDelay(1); }
}
static void jpeg_soft_progress(j_common_ptr p) { jpeg_soft_check(p->client_data); }
static boolean jpeg_soft_fill(j_decompress_ptr jpeg) {
    jpeg_soft_t *s=jpeg->client_data;
    jpeg_soft_check(s);
    boolean result=s->fill_input(jpeg);
    jpeg_soft_check(s);
    return result;
}
static void jpeg_soft_skip(j_decompress_ptr jpeg,long count) {
    jpeg_soft_t *s=jpeg->client_data;
    jpeg_soft_check(s);
    s->skip_input(jpeg,count);
    jpeg_soft_check(s);
}

static void jpeg_soft_quiet(j_common_ptr p) { (void)p; }
static void *jpeg_soft_alloc(jpeg_soft_t *s, size_t n) {
    if (n > JPEG_PROGRESSIVE_WORK_MAX-s->work_bytes || n+512u*1024u>jpeg_budget() || !jpeg_budget_ok(n)) ERREXIT(&s->jpeg,JERR_OUT_OF_MEMORY);
    void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!p) ERREXIT(&s->jpeg,JERR_OUT_OF_MEMORY);
    s->work_bytes += n;
    return p;
}
static jvirt_barray_ptr jpeg_band_request(j_common_ptr p,int pool,boolean zero,JDIMENSION width,
                                         JDIMENSION height,JDIMENSION access) {
    (void)pool; (void)zero;
#ifdef METALIO_JPEG_HOST_DEBUG
    fprintf(stderr,"request %u %u access %u\n",width,height,access);
#endif
    jpeg_soft_t *s=p->client_data;
    if (s->band_count==3 || !width || !height || !access) ERREXIT(p,JERR_BAD_STATE);
    jpeg_band_t *b=&s->bands[s->band_count++];
    b->width=width; b->height=height; b->capacity=access<height?access:height;
    return (jvirt_barray_ptr)b;
}
static bool jpeg_band_open(jpeg_band_t *b,unsigned index) {
#ifdef ESP_PLATFORM
    if (!read_pico_sd_io_ready()) return false;
    static atomic_uint sequence;
    unsigned id=atomic_fetch_add(&sequence,1);
    snprintf(b->path,sizeof(b->path),"/sdcard/.kiikoread-jpeg-%llx-%u-%u.tmp",
        (unsigned long long)esp_timer_get_time(),id,index);
    int fd=open(b->path,O_CREAT|O_EXCL|O_RDWR,0600);
    if (fd<0) { b->path[0]=0; return false; }
    b->backing=fdopen(fd,"w+b");
    if (!b->backing) { close(fd); unlink(b->path); b->path[0]=0; return false; }
#else
    (void)index;
    b->backing=tmpfile();
#endif
    return b->backing!=NULL;
}
static void jpeg_band_realize(j_common_ptr p) {
    jpeg_soft_t *s=p->client_data;
    s->realize(p);
    uint64_t total=0,largest=0;
    for(unsigned i=0;i<s->band_count;++i) {
        uint64_t n=(uint64_t)s->bands[i].width*s->bands[i].height*sizeof(JBLOCK);
        total+=n; if(n>largest)largest=n;
    }
    bool resident=largest+65536u<=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) && total+65536u<=JPEG_PROGRESSIVE_WORK_MAX-s->work_bytes && total+65536u<=jpeg_budget();
    for(unsigned i=0;i<s->band_count;++i) {
        jpeg_soft_check(s);
        jpeg_band_t *b=&s->bands[i];
        if (resident) b->capacity=b->height;
        b->rows=jpeg_soft_alloc(s,(size_t)b->capacity*sizeof(*b->rows));
        b->blocks=jpeg_soft_alloc(s,(size_t)b->capacity*b->width*sizeof(JBLOCK));
        memset(b->blocks,0,(size_t)b->capacity*b->width*sizeof(JBLOCK));
        for(JDIMENSION row=0;row<b->capacity;++row) b->rows[row]=b->blocks+(size_t)row*b->width;
        if (!resident) {
            b->present=jpeg_soft_alloc(s,(b->height+7u)/8u);
            memset(b->present,0,(b->height+7u)/8u);
            if (!jpeg_band_open(b,i)) ERREXIT(p,JERR_FILE_WRITE);
        }
    }
}
static void jpeg_band_flush(jpeg_soft_t *s,jpeg_band_t *b) {
    if (!b->loaded || !b->dirty || !b->backing) return;
    jpeg_soft_check(s);
    size_t row_bytes=(size_t)b->width*sizeof(JBLOCK);
    if (fseek(b->backing,(long)((size_t)b->start*row_bytes),SEEK_SET) ||
        fwrite(b->blocks,row_bytes,b->count,b->backing)!=b->count) ERREXIT(&s->jpeg,JERR_FILE_WRITE);
    for(JDIMENSION row=b->start;row<b->start+b->count;++row) b->present[row/8]|=1u<<(row%8);
    b->dirty=false;
}
static JBLOCKARRAY jpeg_band_access(j_common_ptr p,jvirt_barray_ptr array,JDIMENSION start,
                                    JDIMENSION count,boolean writable) {
    jpeg_soft_t *s=p->client_data;
    jpeg_band_t *b=(jpeg_band_t *)array;
    jpeg_soft_check(s);
#ifdef METALIO_JPEG_HOST_DEBUG
    fprintf(stderr,"access %u +%u cap%u height%u\n",start,count,b->capacity,b->height);
#endif
    if (!count || count>b->capacity || start>b->height || count>b->height-start) ERREXIT(p,JERR_BAD_STATE);
    if (!b->backing) return b->rows+start;
    if (!b->loaded || start<b->start || start+count>b->start+b->count) {
        jpeg_band_flush(s,b);
        b->start=start;
        b->count=b->height-start < b->capacity ? b->height-start : b->capacity;
        size_t row_bytes=(size_t)b->width*sizeof(JBLOCK);
        for(JDIMENSION i=0;i<b->count;++i) {
            JDIMENSION row=start+i;
            jpeg_soft_check(s);
            if (b->present[row/8] & (1u<<(row%8))) {
                if (fseek(b->backing,(long)((size_t)row*row_bytes),SEEK_SET) ||
                    fread(b->rows[i],1,row_bytes,b->backing)!=row_bytes) ERREXIT(p,JERR_FILE_READ);
            } else memset(b->rows[i],0,row_bytes);
        }
        b->loaded=true;
    }
    b->dirty |= writable!=0;
    return b->rows+(start-b->start);
}
static uint8_t *jpeg_soft_row(jpeg_soft_t *s,unsigned row) {
    while(s->jpeg.output_scanline<=row) {
        jpeg_soft_check(s);
        JSAMPROW target=s->scan_rows+(s->jpeg.output_scanline%2)*s->jpeg.output_width;
        if(jpeg_read_scanlines(&s->jpeg,&target,1)!=1) ERREXIT(&s->jpeg,JERR_BAD_STATE);
    }
    return s->scan_rows+(row%2)*s->jpeg.output_width;
}
// 两行灰度窗口直接缩放到目标，保留原面积/双线性规则，不保存整幅源灰度图。
// Resample from two gray rows with the existing area/bilinear rules, without a full source plane.
static void jpeg_soft_scale(jpeg_soft_t *s,unsigned dw,unsigned dh) {
    unsigned sw=s->jpeg.output_width,sh=s->jpeg.output_height;
    book_crop_t c=book_cover_crop(sw,sh,dw,dh);
    if(!c.width||!c.height) ERREXIT(&s->jpeg,JERR_BAD_STATE);
    for(unsigned y=0;y<dh;++y) {
        jpeg_soft_check(s);
        if(c.width>=dw && c.height>=dh) {
            unsigned top=(uint64_t)y*c.height*256/dh,bottom=(uint64_t)(y+1)*c.height*256/dh;
            memset(s->sums,0,(size_t)dw*sizeof(*s->sums));
            for(unsigned sy=top/256;sy<(bottom+255)/256;++sy) {
                unsigned a=top>sy*256?top:sy*256,b=bottom<(sy+1)*256?bottom:(sy+1)*256;
                const uint8_t *row=jpeg_soft_row(s,c.y+sy)+c.x;
                for(unsigned x=0;x<dw;++x) {
                    unsigned left=(uint64_t)x*c.width*256/dw,right=(uint64_t)(x+1)*c.width*256/dw;
                    for(unsigned sx=left/256;sx<(right+255)/256;++sx) {
                        unsigned l=left>sx*256?left:sx*256,r=right<(sx+1)*256?right:(sx+1)*256;
                        s->sums[x]+=(uint64_t)row[sx]*(r-l)*(b-a);
                    }
                }
            }
            for(unsigned x=0;x<dw;++x) {
                unsigned left=(uint64_t)x*c.width*256/dw,right=(uint64_t)(x+1)*c.width*256/dw;
                uint64_t total=(uint64_t)(right-left)*(bottom-top);
                s->gray[(size_t)y*dw+x]=(uint8_t)((s->sums[x]+total/2)/total);
            }
        } else {
            int py=(int)(((uint64_t)(2*y+1)*c.height*128)/dh)-128;
            if(py<0)py=0;
            unsigned sy=(unsigned)py/256,fy=(unsigned)py%256,sy1=sy+1<c.height?sy+1:sy;
            const uint8_t *a=jpeg_soft_row(s,c.y+sy)+c.x,*b=jpeg_soft_row(s,c.y+sy1)+c.x;
            for(unsigned x=0;x<dw;++x) {
                int px=(int)(((uint64_t)(2*x+1)*c.width*128)/dw)-128;
                if(px<0)px=0;
                unsigned sx=(unsigned)px/256,fx=(unsigned)px%256,sx1=sx+1<c.width?sx+1:sx;
                unsigned n=(a[sx]*(256-fx)+a[sx1]*fx)*(256-fy)+(b[sx]*(256-fx)+b[sx1]*fx)*fy;
                s->gray[(size_t)y*dw+x]=(uint8_t)((n+32768)/65536);
            }
        }
    }
    if(s->jpeg.output_scanline<sh) (void)jpeg_soft_row(s,sh-1);
}
static bool jpeg_progressive(const uint8_t *data,size_t size,FILE *file,unsigned dw,unsigned dh,uint8_t *out) {
    jpeg_soft_t *s=calloc(1,sizeof(*s));if(!s)return false;
    volatile bool ok=false;
    s->generation=jpeg_operation_generation;
    s->start=esp_timer_get_time();s->yield_at=s->start;
    s->jpeg.err=jpeg_std_error(&s->error);s->error.error_exit=jpeg_soft_error;s->error.output_message=jpeg_soft_quiet;
    s->jpeg.client_data=s;
    if(setjmp(s->escape))goto done;
    jpeg_create_decompress(&s->jpeg);s->jpeg.client_data=s;
    if(file)jpeg_stdio_src(&s->jpeg,file);else jpeg_mem_src(&s->jpeg,data,(unsigned long)size);
    // 标记扫描和源读取同样响应取消，不等到第一条图像扫描线。
    // Header scans and source reads also cooperate, before the first output scanline.
    s->fill_input=s->jpeg.src->fill_input_buffer;s->skip_input=s->jpeg.src->skip_input_data;
    s->jpeg.src->fill_input_buffer=jpeg_soft_fill;s->jpeg.src->skip_input_data=jpeg_soft_skip;
    if(jpeg_read_header(&s->jpeg,TRUE)!=JPEG_HEADER_OK||!s->jpeg.progressive_mode)goto done;
    jpeg_progressive_claimed=true;
    unsigned w=s->jpeg.image_width,h=s->jpeg.image_height;
    if(!w||!h||w>8192||h>8192||s->jpeg.num_components<1||s->jpeg.num_components>3)goto done;
    unsigned scale=1;
    for(unsigned n=2;n<=8;n*=2) {
        book_crop_t c=book_cover_crop((w+n-1)/n,(h+n-1)/n,dw,dh);
        if(c.width<dw||c.height<dh)break;
        scale=n;
    }
    s->jpeg.scale_num=1;s->jpeg.scale_denom=scale;s->jpeg.out_color_space=JCS_GRAYSCALE;
    jpeg_calc_output_dimensions(&s->jpeg);
    // 512KiB预留给库内部控制/行缓冲；另保留系统2MiB，不挤占字体与显示。
    // Reserve 512KiB for codec control/rows and a separate system 2MiB for fonts/display.
    if(jpeg_budget()<(size_t)dw*dh+(size_t)s->jpeg.output_width*2+dw*sizeof(uint64_t)+512u*1024u)goto done;
    s->gray=jpeg_soft_alloc(s,(size_t)dw*dh);
    s->scan_rows=jpeg_soft_alloc(s,(size_t)s->jpeg.output_width*2);
    s->sums=jpeg_soft_alloc(s,(size_t)dw*sizeof(*s->sums));
    s->realize=s->jpeg.mem->realize_virt_arrays;
    s->jpeg.mem->request_virt_barray=jpeg_band_request;
    s->jpeg.mem->realize_virt_arrays=jpeg_band_realize;
    s->jpeg.mem->access_virt_barray=jpeg_band_access;
    s->jpeg.mem->max_memory_to_use=512u*1024u;
    s->progress.progress_monitor=jpeg_soft_progress;s->jpeg.progress=&s->progress;
    if(!jpeg_start_decompress(&s->jpeg)||s->jpeg.output_components!=1)goto done;
    jpeg_soft_scale(s,dw,dh);
    if(!jpeg_finish_decompress(&s->jpeg))goto done;
    jpeg_soft_check(s);
    memcpy(out,s->gray,(size_t)dw*dh);ok=true;
    ESP_LOGI(TAG,"progressive bands %ux%u scale=1/%u work=%u ms=%lld",w,h,scale,(unsigned)s->work_bytes,
        (long long)((esp_timer_get_time()-s->start)/1000));
done:
    if(s->jpeg.mem)jpeg_destroy_decompress(&s->jpeg);
    for(unsigned i=0;i<s->band_count;++i) {
        jpeg_band_t *b=&s->bands[i];
        if(b->backing)fclose(b->backing);
#ifdef ESP_PLATFORM
        if(b->path[0])unlink(b->path);
#endif
        free(b->rows);free(b->blocks);free(b->present);
    }
    free(s->gray);free(s->scan_rows);free(s->sums);free(s);return ok;
}

// 限制为原生支持的基线/采样；按MCU尺寸计算实际步长，不按可见宽度读填充行。
// Restrict to native baseline/sampling and use MCU-padded stride, never visible width for padded rows.
static bool jpeg_hw_shape(const uint8_t *data,size_t size,unsigned *w,unsigned *h,unsigned *pw,unsigned *ph,bool *gray) {
    if(!data||size<4||data[0]!=255||data[1]!=216)return false;
    for(size_t at=2;at+4<=size;) {
        if(data[at++]!=255)return false;
        while(at<size&&data[at]==255)++at;
        if(at>=size)return false;
        unsigned marker=data[at++];
        if(marker==0xda||marker==0xd9)return false;
        if(marker==1||(marker>=0xd0&&marker<=0xd8))continue;
        if(at+2>size)return false;
        size_t n=(unsigned)data[at]*256+data[at+1];if(n<2||n>size-at)return false;
        if(marker==0xc0) {
            if(n<8||data[at+2]!=8)return false;
            *h=(unsigned)data[at+3]*256+data[at+4];*w=(unsigned)data[at+5]*256+data[at+6];
            unsigned nf=data[at+7];if((nf!=1&&nf!=3)||n!=8+3*nf)return false;
            if(nf==3&&(data[at+8]!=1||data[at+11]!=2||data[at+14]!=3))return false;
            unsigned sample=data[at+9],mx=sample>>4,my=sample&15;
            if(nf==3&&(sample!=0x11&&sample!=0x21&&sample!=0x22))return false;
            if(!mx||!my||mx>2||my>2)return false;
            if(nf==3&&(data[at+12]!=0x11||data[at+15]!=0x11))return false;
            if(!*w||!*h||*w>8192||*h>8192||((uint64_t)(*w)*(*h))%8)return false;
            *pw=(*w+mx*8-1)/(mx*8)*(mx*8);*ph=(*h+my*8-1)/(my*8)*(my*8);*gray=nf==1;return true;
        }
        if(marker>=0xc1&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc)return false;
        at+=n;
    }
    return false;
}
static bool jpeg_hw_file_shape(FILE *file,size_t size,unsigned *w,unsigned *h,unsigned *pw,unsigned *ph,bool *gray) {
    if(fgetc(file)!=255||fgetc(file)!=216)return false;
    while(ftell(file)>=0&&(size_t)ftell(file)+4<=size) {
        if(fgetc(file)!=255)return false;
        int marker;do{marker=fgetc(file);}while(marker==255);
        if(marker<0||marker==0xda||marker==0xd9)return false;
        if(marker==1||(marker>=0xd0&&marker<=0xd8))continue;
        int hi=fgetc(file),lo=fgetc(file);if(hi<0||lo<0)return false;
        size_t n=(unsigned)hi*256+(unsigned)lo;
        if(n<2||n-2>size-(size_t)ftell(file))return false;
        if(marker==0xc0) {
            uint8_t frame[21]={255,216,255,192,(uint8_t)hi,(uint8_t)lo};
            if(n>sizeof(frame)-4||fread(frame+6,1,n-2,file)!=n-2)return false;
            return jpeg_hw_shape(frame,n+4,w,h,pw,ph,gray);
        }
        if(marker>=0xc1&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc)return false;
        if(fseek(file,(long)n-2,SEEK_CUR))return false;
    }
    return false;
}
static bool jpeg_hardware(const uint8_t *data,size_t size,FILE *file,unsigned dw,unsigned dh,uint8_t *out) {
    unsigned w=0,h=0,pw=0,ph=0;bool gray=false;
    if(file){bool shape=jpeg_hw_file_shape(file,size,&w,&h,&pw,&ph,&gray);
        if(fseek(file,0,SEEK_SET)||!shape)return false;
    }else if(!jpeg_hw_shape(data,size,&w,&h,&pw,&ph,&gray))return false;
    size_t bytes=(size_t)pw*ph*(gray?1:3),input_bytes=0,output_bytes=0;
    if(size>4u*1024u*1024u||size>UINT32_MAX||!jpeg_budget_ok(size+bytes+512))return false;
    // 大图小封面优先沿用流式缩解，避免为缩略图分配整幅RGB。
    // Prefer existing streaming scaled decode for small thumbnails of large source images.
    if(dw<=BOOK_COVER_W&&dh<=BOOK_COVER_H&&(uint64_t)w*h>1024u*1024u)return false;
    uint8_t *input=NULL,*pixels=NULL;bool ok=false;jpeg_decoder_handle_t engine=NULL;
    jpeg_decode_memory_alloc_cfg_t mem={.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER};
    input=jpeg_alloc_decoder_mem(size,&mem,&input_bytes);if(!input)goto done;
    if(file){if(fread(input,1,size,file)!=size)goto done;}else memcpy(input,data,size);
    mem.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    pixels=jpeg_alloc_decoder_mem(bytes,&mem,&output_bytes);if(!pixels)goto done;
    jpeg_decode_engine_cfg_t config={.timeout_ms=200};
    if(jpeg_new_decoder_engine(&config,&engine)!=ESP_OK)goto done;
    jpeg_decode_cfg_t cfg={.output_format=gray?JPEG_DECODE_OUT_FORMAT_GRAY:JPEG_DECODE_OUT_FORMAT_RGB888,
        .rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_RGB,.conv_std=JPEG_YUV_RGB_CONV_STD_BT601};
    uint32_t used=0;int64_t start=esp_timer_get_time();
    if(jpeg_decoder_process(engine,&cfg,input,size,pixels,output_bytes,&used)!=ESP_OK||used<bytes)goto done;
    if(!gray) {
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const uint8_t *rgb=pixels+((size_t)y*pw+x)*3;
            // RGB888的实际字节顺序由rgb_order显式指定并由宿主/实机对照验证。
            // Explicit rgb_order controls RGB888 byte order, covered by host/device comparisons.
            pixels[(size_t)y*w+x]=(77u*rgb[0]+150u*rgb[1]+29u*rgb[2]+128u)>>8;
        }
    }
    ok=jpeg_scale(pixels,w,h,gray?pw:w,dw,dh,out);
    if(ok)ESP_LOGI(TAG,"hardware %ux%u padded=%ux%u ms=%lld",w,h,pw,ph,(long long)((esp_timer_get_time()-start)/1000));
done:
    if(engine)jpeg_del_decoder_engine(engine);
    free(input);free(pixels);return ok;
}

bool metalio_jpeg_gray(const uint8_t *data,size_t size,FILE *file,unsigned width,unsigned height,uint8_t *out) {
    jpeg_last_cancelled=false; jpeg_progressive_claimed=false;
    jpeg_operation_generation=atomic_load(&jpeg_cancel_generation);
    if((!data&&!file)||(data&&file)||!size||!out||!width||!height||width>1216||height>1216)return false;
    static atomic_flag owner=ATOMIC_FLAG_INIT;
    if(atomic_flag_test_and_set(&owner))return false;
    bool ok=jpeg_hardware(data,size,file,width,height,out);
    if(jpeg_operation_generation!=atomic_load(&jpeg_cancel_generation)) { jpeg_last_cancelled=true; ok=false; goto done; }
    if(!ok) {
        if(file&&(fseek(file,0,SEEK_SET)||ferror(file)))goto done;
        ok=jpeg_progressive(data,size,file,width,height,out);
    }
    if(!ok)ESP_LOGD(TAG,"software fallback input=%u",(unsigned)size);
done:
    atomic_flag_clear(&owner);
    return ok;
}
