"""中文：运行真实整页缓存、异步IO和有界RLE的宿主回归。
English: Exercise real composed-page caching, asynchronous IO and bounded RLE.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]

STUBS = {
'epdiy.h': '#pragma once\ntypedef struct {int x,y,width,height;} EpdRect;\n',
'read_pico_sd.h': '#pragma once\n#include <stdbool.h>\nbool read_pico_sd_io_ready(void);\n',
'esp_app_desc.h': '#pragma once\n#include <stdint.h>\ntypedef struct {uint8_t app_elf_sha256[32];} esp_app_desc_t;\nconst esp_app_desc_t *esp_app_get_description(void);\n',
'esp_heap_caps.h': '#pragma once\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define heap_caps_malloc test_malloc\n#define heap_caps_get_free_size test_free_size\n',
'freertos/FreeRTOS.h': '#pragma once\n#include <pthread.h>\n#define portMUX_TYPE pthread_mutex_t\n#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER\n#define portENTER_CRITICAL(m) pthread_mutex_lock(m)\n#define portEXIT_CRITICAL(m) pthread_mutex_unlock(m)\n#define pdPASS 1\n',
'freertos/task.h': '#pragma once\nint xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,unsigned,void*);\nvoid vTaskDelay(unsigned);\nvoid vTaskDelete(void*);\n',
}
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "book_shelf_page_cache.h"
#include "esp_app_desc.h"
static atomic_bool card_ready=true,slow_io=false;
static bool fail_alloc,fail_task;
static size_t heap_budget=8*1024*1024,held;
static struct {void *ptr;size_t bytes;} allocations[4];
static unsigned opens,reads,writes;
static pthread_t threads[512];static unsigned thread_count;
static esp_app_desc_t app;
bool read_pico_sd_io_ready(void){if(atomic_load(&slow_io))usleep(500);return atomic_load(&card_ready);}
const esp_app_desc_t *esp_app_get_description(void){return &app;}
static size_t test_free_size(unsigned caps){assert(caps==3);return heap_budget>held?heap_budget-held:0;}
static void *test_malloc(size_t n,unsigned caps){assert(caps==3);if(fail_alloc)return NULL;void*p=malloc(n);if(p){for(unsigned i=0;i<4;++i)if(!allocations[i].ptr){allocations[i].ptr=p;allocations[i].bytes=n;held+=n;return p;}assert(false);}return p;}
static void test_free(void *p){if(!p)return;for(unsigned i=0;i<4;++i)if(allocations[i].ptr==p){held-=allocations[i].bytes;allocations[i].ptr=NULL;free(p);return;}assert(false);}
static FILE *test_open(const char *p,const char *mode){++opens;if(mode[0]=='r')++reads;else ++writes;return fopen(p,mode);}
struct launch {void(*fn)(void*);void *arg;};
static void *launch(void *p){struct launch job=*(struct launch*)p;free(p);job.fn(job.arg);return NULL;}
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
    assert(name&&stack<=12288&&priority==1&&!handle);if(fail_task)return 0;
    struct launch *p=malloc(sizeof(*p));assert(p);*p=(struct launch){fn,arg};
    assert(thread_count<512&&pthread_create(&threads[thread_count++],NULL,launch,p)==0);return 1;
}
void vTaskDelay(unsigned n){usleep(n*1000);}
void vTaskDelete(void*p){assert(!p);pthread_exit(NULL);}
#define free test_free
#define fopen test_open
#include "book_shelf_page_cache.c"
#undef fopen
#undef free
static void join_threads(void){for(unsigned i=0;i<thread_count;++i)assert(!pthread_join(threads[i],NULL));thread_count=0;}
static book_shelf_page_state_t wait_page(void){book_shelf_page_state_t state;for(unsigned i=0;i<20000;++i){state=book_shelf_page_poll();if(state!=BOOK_SHELF_PAGE_WAIT){join_threads();return state;}usleep(100);}assert(false);return BOOK_SHELF_PAGE_MISS;}
static void wait_write(void){for(unsigned i=0;i<20000&&running();++i)usleep(100);assert(!running());join_threads();}
static unsigned pixel(const uint8_t *fb,int x,int y,int w){return (fb[(y*w+x)/2]>>((x&1)*4))&15;}
static void put(uint8_t *fb,int x,int y,int w,unsigned value){uint8_t *p=&fb[(y*w+x)/2];unsigned shift=(x&1)*4;*p=(*p&~(15u<<shift))|((value&15)<<shift);}
static bool inside(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static void verify_area(const uint8_t *got,const uint8_t *expected,EpdRect r,int w,int h,unsigned outside){
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)assert(pixel(got,x,y,w)==(inside(r,x,y)?pixel(expected,x,y,w):outside));
}
static book_shelf_page_key_t key(unsigned n){book_shelf_page_key_t k;book_shelf_page_key_init(&k);book_shelf_page_key_add(&k,&n,sizeof(n));return k;}
static uint8_t *read_all(const char *path,size_t *n){FILE*f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&!fseek(f,0,SEEK_SET));uint8_t*p=malloc(size+16);assert(p&&fread(p,1,size,f)==(size_t)size&&!fclose(f));*n=size;return p;}
static void write_all(const char *path,const uint8_t *p,size_t n){FILE*f=fopen(path,"wb");assert(f&&fwrite(p,1,n,f)==n&&!fclose(f));}
int main(void){
    enum{W=128,H=112,N=W*H/2};uint8_t before[N],final[N],out[N];
    for(unsigned i=0;i<N;++i)before[i]=(uint8_t)(i*73+(i/7)*19);
    memcpy(final,before,N);
    book_shelf_page_layout_t layout={.area={1,12,120,96},.covers={{13,17,20,40},{54,19,22,40}},.width=W,.height=H,.count=2};
    for(int y=47;y<59;++y)for(int x=0;x<W;++x)put(final,x,y,W,7);
    assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);
    assert(wait_page()==BOOK_SHELF_PAGE_MISS);unsigned no_io=opens;
    assert(book_shelf_page_stage_covers(before));assert(book_shelf_page_capture(final,2,100));assert(opens==no_io);
    memset(out,0xee,N);assert(book_shelf_page_restore(out));verify_area(out,final,layout.area,W,H,14);assert(book_shelf_page_favorite_edges()==2);
    for(unsigned row=0;row<2;++row){
        memset(out,0xee,N);EpdRect src=layout.covers[row],dst=src;dst.y-=8;
        assert(book_shelf_page_copy_cover(out,row,dst));
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)assert(pixel(out,x,y,W)==(inside(dst,x,y)?pixel(before,x,y+8,W):14));
        dst.x++;assert(!book_shelf_page_copy_cover(out,row,dst));
    }
    assert(opens==no_io);assert(!book_shelf_page_copy_cover(out,2,layout.covers[0]));
    book_shelf_page_idle(899);assert(opens==no_io);book_shelf_page_idle(900);wait_write();assert(writes==1);
    char path[256];path_for(path,sizeof(path),s_wanted.key,"bin");size_t saved_size;uint8_t *saved=read_all(path,&saved_size);
    assert(saved_size<sizeof(page_header_t)+s_wanted.bytes+s_wanted.bytes/100+32);
    assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_HIT);assert(opens==no_io+1);
    book_shelf_page_release();assert(!held);
    assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_HIT);
    memset(out,0xee,N);assert(book_shelf_page_restore(out));verify_area(out,final,layout.area,W,H,14);
    // 不同样式、模式及页内容均分开；双槽复用，实时信息不在像素层。
    // Distinct styles/modes/content use distinct keys; the two slots reuse artwork only.
    assert(book_shelf_page_request(key(2),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);
    assert(book_shelf_page_stage_covers(before)&&book_shelf_page_capture(final,1,1000));
    unsigned old_opens=opens;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_HIT);assert(opens==old_opens);
    for(unsigned i=3;i<16;++i){assert(book_shelf_page_request(key(i),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);assert(book_shelf_page_stage_covers(before)&&book_shelf_page_capture(final,i&3,0));assert(held<=PAGE_BUDGET);}
    book_shelf_page_release();
    // 损坏、截断、越界RLE和尾部垃圾一律失败，不显示半张旧图。
    // Corruption, truncation, overrun and trailing bytes never publish partial/stale artwork.
    for(unsigned corruption=0;corruption<12;++corruption){
        uint8_t *bad=malloc(saved_size+1);memcpy(bad,saved,saved_size);size_t length=saved_size;
        page_header_t *h=(page_header_t*)bad;
        switch(corruption){case 0:h->magic^=1;break;case 1:h->version++;break;case 2:h->header_bytes--;break;
        case 3:h->bytes=UINT32_MAX;break;case 4:h->key.v[1]^=1;break;case 5:h->layout.area.width++;break;
        case 6:h->edges=1u<<12;break;case 7:h->checksum^=1;break;case 8:length--;break;
        case 9:bad[length++]=0;break;case 10:length=sizeof(*h)+1;bad[sizeof(*h)]=127;break;
        default:bad[saved_size-1]^=64;break;}
        write_all(path,bad,length);free(bad);
        assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);assert(!book_shelf_page_restore(out));
        book_shelf_page_release();
    }
    write_all(path,saved,saved_size);
    app.app_elf_sha256[0]=1;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_HIT);book_shelf_page_release();app.app_elf_sha256[0]=0;
    layout.covers[0].x++;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);book_shelf_page_release();layout.covers[0].x--;
    atomic_store(&card_ready,false);old_opens=opens;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS);assert(opens==old_opens);book_shelf_page_release();atomic_store(&card_ready,true);
    fail_alloc=true;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS);assert(!book_shelf_page_stage_covers(before));fail_alloc=false;
    heap_budget=PAGE_RESERVE;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS);assert(!book_shelf_page_stage_covers(before));heap_budget=8*1024*1024;
    fail_task=true;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS);fail_task=false;book_shelf_page_release();
    assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_HIT);
    atomic_store(&slow_io,true);book_shelf_page_request(key(200),layout);book_shelf_page_release();join_threads();atomic_store(&slow_io,false);assert(!held&&!running());
    // 编解码覆盖白底、随机灰阶与128/130字节边界。
    // Round-trip white backgrounds, random tones and literal/run boundaries.
    for(unsigned pattern=0;pattern<6;++pattern)for(unsigned n=1;n<2048;n+=7){
        uint8_t *input=malloc(n),*result=malloc(n);for(unsigned i=0;i<n;++i)input[i]=pattern==0?255:pattern==1?(i/129)%5:pattern==2?(i%130==0?3:4):pattern==3?i:pattern==4?(i*31)^(i/7):(((((i*37)^(i/11))&1)?15:0)|((((i*37)^(i/11))&2)?240:0));
        FILE *f=tmpfile();assert(f&&encode(f,input,n)&&!fflush(f)&&!fseek(f,0,SEEK_SET)&&decode(f,result,n)&&!memcmp(input,result,n));fclose(f);free(input);free(result);
    }
    // 私有缓存有文件数/容量上限；不清理用户文件。
    // Bound private cache count/bytes without deleting unrelated user files.
    for(unsigned i=0;i<40;++i){char p[256];book_shelf_page_key_t k=key(500+i);path_for(p,sizeof(p),k,"bin");FILE*f=fopen(p,"wb");assert(f);if(i==0)assert(!ftruncate(fileno(f),9*1024*1024));else assert(fputc(1,f)!=EOF);assert(!fclose(f));}
    char unrelated[256];snprintf(unrelated,sizeof(unrelated),BOOK_SHELF_PAGE_ROOT "/books.epub");write_all(unrelated,(const uint8_t*)"book",4);
    assert(book_shelf_page_request(key(300),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);assert(book_shelf_page_stage_covers(before)&&book_shelf_page_capture(final,2,0));book_shelf_page_idle(800);wait_write();
    DIR*d=opendir(BOOK_SHELF_PAGE_ROOT);assert(d);struct dirent *e;unsigned count=0;size_t bytes=0;
    while((e=readdir(d)))if(own_file(e->d_name)){char p[256];snprintf(p,sizeof(p),BOOK_SHELF_PAGE_ROOT "/%s",e->d_name);struct stat st;assert(!stat(p,&st));bytes+=st.st_size;++count;}closedir(d);assert(count<=DISK_FILES&&bytes<=DISK_BUDGET&&access(unrelated,F_OK)==0);
    // 无效几何不分配，也不读取；释放等待worker关闭句柄。
    // Invalid geometry allocates/reads nothing; release joins file ownership.
    book_shelf_page_release();layout.count=10;old_opens=opens;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS&&opens==old_opens&&!held);
    layout.count=2;layout.area.x=-1;assert(book_shelf_page_request(key(1),layout)==BOOK_SHELF_PAGE_MISS&&!held);
    book_shelf_page_release();join_threads();assert(!held);free(saved);
    puts("PASS: composed page RAM/SD hits, exact pixels/cover lifting, RLE, invalidation, corruption, cancel/OOM and bounds");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='kiiko-shelf-pages-') as tmp:
        t=Path(tmp)
        for name,text in STUBS.items():
            p=t/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        (t/'test.c').write_text(UNIT)
        flags=['cc','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-format-truncation','-I'+str(t),'-I'+str(ROOT/'main/book'),'-DBOOK_SHELF_PAGE_ROOT="'+str(t/'cache')+'"']
        subprocess.run(flags+[str(t/'test.c'),'-lpthread','-o',str(t/'test')],check=True)
        subprocess.run([str(t/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')))

if __name__=='__main__':main()
