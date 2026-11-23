/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行共享缓存，覆盖淘汰、版本、容量、内存失败与并发复制。
 * English: Exercise production cache eviction, versions, capacity, OOM and concurrent copies.
 */
#include "metalio_cover_cache.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t available=16u*1024u*1024u;
static bool oom;
void *heap_caps_malloc(size_t n,unsigned caps) {assert(caps==3);return oom?NULL:malloc(n);}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {assert(caps==3);return oom?NULL:calloc(n,size);}
size_t heap_caps_get_free_size(unsigned caps) {assert(caps==3);return available;}
size_t heap_caps_get_largest_free_block(unsigned caps) {assert(caps==3);return available;}
static metalio_cover_key_t key(unsigned n) {return (metalio_cover_key_t){{n+1,77,123,456,0,1,0,0}};}
static void *parallel(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;
    uint8_t input[64],out[64];memset(input,(int)id,sizeof(input));
    metalio_cover_key_t k=key(100+id);
    for(int i=0;i<1000;++i) {
        assert(metalio_cover_cache_put(&k,input,sizeof(input),(metalio_cover_meta_t){8,8,false}));
        assert(metalio_cover_cache_get(&k,out,sizeof(out),NULL));
        assert(!memcmp(input,out,sizeof(out)));
    }
    return NULL;
}
static uint8_t s_other_pixels[16];
static void *other_put(void *arg) {
    metalio_cover_key_t k=key(83);(void)arg;
    return metalio_cover_cache_put(&k,s_other_pixels,sizeof(s_other_pixels),(metalio_cover_meta_t){4,4,false})?arg:NULL;
}
int main(void) {
    enum {CHUNK=512*1024};
    uint8_t *pixels=malloc(CHUNK),*out=malloc(CHUNK);assert(pixels&&out);
    memset(pixels,0x5a,CHUNK);
    metalio_cover_meta_t meta={176,240,true},got;
    metalio_cover_key_t k=key(0);
    oom=true;assert(!metalio_cover_cache_put(&k,pixels,CHUNK,meta));oom=false;
    assert(!metalio_cover_cache_get(&k,out,CHUNK,&got));
    for(unsigned i=0;i<8;++i){k=key(i);assert(metalio_cover_cache_put(&k,pixels,CHUNK,meta));}
    assert(metalio_cover_cache_stats().bytes==4u*1024u*1024u);
    k=key(0);assert(metalio_cover_cache_get(&k,out,CHUNK,&got)&&got.width==176&&got.height==240&&got.white_edge);
    assert(!memcmp(pixels,out,CHUNK));out[0]=0;assert(metalio_cover_cache_get(&k,out,CHUNK,NULL)&&out[0]==0x5a);
    k=key(8);assert(metalio_cover_cache_put(&k,pixels,CHUNK,meta));
    k=key(1);assert(!metalio_cover_cache_get(&k,out,CHUNK,NULL));
    k=key(0);assert(metalio_cover_cache_get(&k,out,CHUNK,NULL));
    assert(!metalio_cover_cache_get(&k,out,CHUNK-1,NULL));
    metalio_cover_cache_clear();assert(!metalio_cover_cache_stats().bytes);
    k=key(6);assert(metalio_cover_cache_put(&k,pixels,64,meta));
    metalio_cover_key_t style=k;style.field[4]=2;style.field[6]=91;
    assert(metalio_cover_cache_put(&style,pixels,64,meta));
    metalio_cover_key_t revised=k;revised.field[2]++;
    assert(metalio_cover_cache_put(&revised,pixels,64,meta));
    assert(!metalio_cover_cache_get(&k,out,CHUNK,NULL)&&!metalio_cover_cache_get(&style,out,CHUNK,NULL));
    assert(metalio_cover_cache_stats().entries==1);
    metalio_cover_cache_clear();k=key(6);
    assert(metalio_cover_cache_put(&k,pixels,64,meta));
    style=k;style.field[4]=2;assert(metalio_cover_cache_put(&style,pixels,64,meta));
    metalio_cover_key_t finished=k;finished.field[4]=3;
    assert(metalio_cover_cache_put(&finished,pixels,16,meta));
    assert(!metalio_cover_cache_get(&k,out,CHUNK,NULL) && !metalio_cover_cache_get(&style,out,CHUNK,NULL));
    assert(metalio_cover_cache_get(&finished,out,CHUNK,NULL));
    assert(metalio_cover_cache_stats().entries==1 && metalio_cover_cache_stats().bytes==16);
    available=2560u*1024u+63;
    assert(!metalio_cover_cache_put(&k,pixels,64,meta));assert(!metalio_cover_cache_stats().bytes);
    available=16u*1024u*1024u;
    assert(metalio_cover_cache_put(&k,pixels,CHUNK,meta));
    metalio_cover_cache_trim(128*1024);assert(!metalio_cover_cache_stats().bytes);
    assert(metalio_cover_cache_put(&k,pixels,64,meta));oom=true;
    style=key(7);assert(!metalio_cover_cache_put(&style,pixels,64,meta));oom=false;
    assert(!metalio_cover_cache_stats().bytes);
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i)assert(!pthread_create(&threads[i],NULL,parallel,(void *)(uintptr_t)i));
    for(unsigned i=0;i<4;++i)assert(!pthread_join(threads[i],NULL));
    assert(metalio_cover_cache_stats().entries==4);
    metalio_cover_cache_clear();free(pixels);free(out);
    uint8_t source[7*19],target[7*19];for(unsigned i=0;i<sizeof(source);++i)source[i]=(uint8_t)i;
    k=key(80);assert(metalio_cover_cache_put_rows(&k,source,19,11,7));memset(target,0xa5,sizeof(target));
    assert(metalio_cover_cache_get_rows(&k,target,19,11,7));
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<19;++x)assert(target[y*19+x]==(x<11?source[y*19+x]:0xa5));
    assert(!metalio_cover_cache_get_rows(&k,target,19,12,7));
    assert(!metalio_cover_cache_get_rows(&k,target,10,11,7));
    assert(!metalio_cover_cache_put_rows(&k,source,19,SIZE_MAX,7));
    assert(!metalio_cover_cache_put_rows(&k,source,SIZE_MAX,1,7));
    metalio_cover_cache_trim(0);assert(!metalio_cover_cache_get_rows(&k,target,19,11,7));
    // 暂停收录只影响调用线程：写入被忽略且不淘汰已有项，读取仍命中，解除后恢复。
    // A hold affects the calling thread only: puts are ignored without evicting, reads still hit, release restores.
    metalio_cover_key_t kept=key(81),held=key(82);
    assert(metalio_cover_cache_put(&kept,source,sizeof(source),(metalio_cover_meta_t){7,19,false}));
    metalio_cover_cache_hold_admission(true);
    assert(!metalio_cover_cache_put(&held,source,sizeof(source),(metalio_cover_meta_t){7,19,false}));
    assert(!metalio_cover_cache_get(&held,target,sizeof(target),NULL));
    assert(metalio_cover_cache_get(&kept,target,sizeof(target),NULL)&&metalio_cover_cache_stats().entries==1);
    pthread_t other;void *admitted=NULL;assert(!pthread_create(&other,NULL,other_put,&other)&&!pthread_join(other,&admitted)&&admitted);
    assert(metalio_cover_cache_stats().entries==2);
    metalio_cover_cache_hold_admission(false);
    assert(metalio_cover_cache_put(&held,source,sizeof(source),(metalio_cover_meta_t){7,19,false})&&metalio_cover_cache_stats().entries==3);
    metalio_cover_cache_clear();
    puts("PASS: per-task admission hold ignores puts without evicting; dynamic 4MiB cap, LRU recency, copy ownership, raster metadata, file-version invalidation, 2.5MiB shared reserve, trim/OOM fallback and 4000 concurrent copy cycles");
    return 0;
}
