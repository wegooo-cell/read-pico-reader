/* SPDX-License-Identifier: Apache-2.0
 * 中文：按索引读取压缩的内建黑体汉字，各消费者仅缓存一个32字块，避免整份字体驻留。
 * English: Indexed compressed built-in Han glyphs; independent32-glyph block caches avoid retaining a whole font.
 */
#include "ui_hanzi.h"
#include "esp_heap_caps.h"
#include "miniz.h"
#include <stddef.h>
#include <string.h>
extern const uint8_t ui_hanzi_start[] asm("_binary_ui_hanzi_bin_start");
extern const uint8_t ui_hanzi_end[] asm("_binary_ui_hanzi_bin_end");
#define BLOCK 32u
static ui_hanzi_cache_t s_ui_cache;
static uint32_t word(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static size_t record_size(void) {return !memcmp(ui_hanzi_start,"PIF4",4)?UI_HANZI_AA_RECORD:UI_HANZI_RECORD;}
static int index_for(uint32_t cp) {
    size_t bytes = ui_hanzi_end - ui_hanzi_start;
    if (cp > 0xffff || bytes < 16 || (memcmp(ui_hanzi_start, "PIF1", 4)&&memcmp(ui_hanzi_start,"PIF4",4))) return -1;
    size_t count = word(ui_hanzi_start + 4), blocks = (count + BLOCK - 1) / BLOCK;
    if (count > 6763 || word(ui_hanzi_start + 8) != UI_HANZI_BASE_PX || word(ui_hanzi_start + 12) != BLOCK ||
        16 + count * 2 + (blocks + 1) * 4 > bytes) return -1;
    size_t low = 0, high = count;
    const uint8_t *chars = ui_hanzi_start + 16;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint32_t candidate = chars[mid * 2] | (uint32_t)chars[mid * 2 + 1] << 8;
        if (candidate < cp) low = mid + 1; else high = mid;
    }
    return low < count && (chars[low * 2] | (uint32_t)chars[low * 2 + 1] << 8) == cp ? (int)low : -1;
}
bool ui_hanzi_has(uint32_t cp) { return index_for(cp) >= 0; }
static const uint8_t *decode_record(ui_hanzi_cache_t *cache,uint32_t cp) {
    if (!cache) return false;
    int index = index_for(cp); if (index < 0) return NULL;
    size_t block = (unsigned)index / BLOCK;
    size_t bytes=BLOCK*record_size();
    if (!cache->block) {cache->block=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(cache->block)cache->bytes=bytes;}
    if (!cache->inflate) cache->inflate = heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cache->block || !cache->inflate) return false;
    if (!cache->valid || cache->cached != block) {
        size_t count = word(ui_hanzi_start + 4), blocks = (count + BLOCK - 1) / BLOCK;
        const uint8_t *offsets = ui_hanzi_start + 16 + count * 2;
        const uint8_t *data = offsets + (blocks + 1) * 4;
        size_t start = word(offsets + block * 4), end = word(offsets + (block + 1) * 4);
        size_t entries = count - block * BLOCK; if (entries > BLOCK) entries = BLOCK;
        cache->valid = false;
        if (end < start || end > (size_t)(ui_hanzi_end - data)) return false;
        // 解压器也放 PSRAM；便利解压函数的大型栈变量不适合 UI 任务。
        // Keep the inflater in PSRAM too; the convenience decoder's large stack frame is unsuitable for UI tasks.
        tinfl_init((tinfl_decompressor *)cache->inflate);
        size_t input = end - start, output = bytes;
        tinfl_status status = tinfl_decompress(cache->inflate, data + start, &input, cache->block, cache->block, &output,
            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status != TINFL_STATUS_DONE || output != entries * record_size() || input != end - start) return false;
        cache->cached = block;
        cache->valid = true;
    }
    return cache->block+((unsigned)index%BLOCK)*record_size();
}
bool ui_hanzi_get_cached(ui_hanzi_cache_t *cache,uint32_t cp,uint8_t record[UI_HANZI_RECORD]) {
    if(!record)return false;
    const uint8_t *raw=decode_record(cache,cp);if(!raw)return false;
    if(record_size()==UI_HANZI_RECORD)memcpy(record,raw,UI_HANZI_RECORD);
    else {
        memcpy(record,raw,5);memset(record+5,0,72);
        for(unsigned i=0;i<576;++i)if(((raw[5+i/2]>>((i%2)*4))&15)>=6)record[5+i/8]|=1u<<(i%8);
    }
    return record[2]>0&&record[2]<=24&&record[3]>0&&record[3]<=24&&record[4]==UI_HANZI_ADVANCE;
}
bool ui_hanzi_get_aa_cached(ui_hanzi_cache_t *cache,uint32_t cp,uint8_t record[UI_HANZI_AA_RECORD]) {
    if(!record)return false;
    const uint8_t *raw=decode_record(cache,cp);if(!raw)return false;
    if(record_size()==UI_HANZI_AA_RECORD)memcpy(record,raw,UI_HANZI_AA_RECORD);
    else {
        memcpy(record,raw,5);memset(record+5,0,288);
        for(unsigned i=0;i<576;++i)if((raw[5+i/8]>>(i%8))&1)record[5+i/2]|=15u<<((i%2)*4);
    }
    return record[2]>0&&record[2]<=24&&record[3]>0&&record[3]<=24&&record[4]==UI_HANZI_ADVANCE;
}

bool ui_hanzi_get(uint32_t cp, uint8_t record[UI_HANZI_RECORD]) {
    return ui_hanzi_get_cached(&s_ui_cache,cp,record);
}
size_t ui_hanzi_cache_release(ui_hanzi_cache_t *cache) {
    if (!cache) return 0;
    size_t bytes=cache->bytes+(cache->inflate?sizeof(tinfl_decompressor):0);
    heap_caps_free(cache->block);heap_caps_free(cache->inflate);
    memset(cache,0,sizeof(*cache));
    return bytes;
}
