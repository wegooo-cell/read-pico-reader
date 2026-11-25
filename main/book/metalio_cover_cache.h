/* SPDX-License-Identifier: Apache-2.0
 * 中文：S31 共用封面缓存，键包含文件版本和绘制参数，不向调用方借出指针。
 * English: Shared S31 cover cache uses versioned raster keys and copy-out ownership.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { uint64_t field[8]; } metalio_cover_key_t;
typedef struct { unsigned width,height; bool white_edge; } metalio_cover_meta_t;
typedef struct { size_t bytes,entries; uint64_t hits,misses,evictions; } metalio_cover_stats_t;
/// out 可为 NULL 以只检查命中并读元数据。/ Pass NULL for out to check residency and read metadata only.
bool metalio_cover_cache_get(const metalio_cover_key_t *key,void *out,size_t capacity,metalio_cover_meta_t *meta);
bool metalio_cover_cache_put(const metalio_cover_key_t *key,const void *pixels,size_t bytes,metalio_cover_meta_t meta);
/// 在同一LRU预算内复制已处理的物理像素行，不借出缓存指针。/ Copy finished physical raster rows within the same LRU budget, without lending pointers.
bool metalio_cover_cache_get_rows(const metalio_cover_key_t*,void*,size_t stride,size_t span,size_t rows);
bool metalio_cover_cache_put_rows(const metalio_cover_key_t*,const void*,size_t stride,size_t span,size_t rows);
/// 仅对调用任务生效：true 时该任务的写入被忽略，读取不受影响；用于只写卡的后台预热。
/// Applies to the calling task only: while held its puts are ignored and reads still hit; used by card-only warming.
void metalio_cover_cache_hold_admission(bool hold);
void metalio_cover_cache_trim(size_t budget);
/// 按最久未用让出至少 bytes 的缓存给解码；调用任务暂停收录时不让出。返回实际释放字节。
/// Evict least-recently-used entries until at least bytes are freed for decoding; a task holding admission frees nothing. Returns the bytes freed.
size_t metalio_cover_cache_yield(size_t bytes);
void metalio_cover_cache_clear(void);
metalio_cover_stats_t metalio_cover_cache_stats(void);
