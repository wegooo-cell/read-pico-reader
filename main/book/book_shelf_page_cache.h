/* SPDX-License-Identifier: Apache-2.0
 * 整页书架静态像素缓存；状态、文字和收藏由调用方实时绘制。
 * Composed shelf artwork; callers draw live status, text and favorites.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"
#define BOOK_SHELF_PAGE_COVERS 9
// 改变封面/家具像素规则或内建字库时递增；普通重编译不作废成品。
// Bump when artwork/furniture rules or built-in glyph assets change, not for ordinary rebuilds.
// 用户2026-11-11：4 作废旧版把“默认封面”整页固化的缓存，之后只有全真封面页才写入。
// User2026-11-11: 4 discards pages that froze generated default covers; only all-real pages are
// captured from now on.
#define BOOK_SHELF_ARTWORK_VERSION 4u
typedef struct { uint64_t v[2]; } book_shelf_page_key_t;
typedef struct {
    EpdRect area, covers[BOOK_SHELF_PAGE_COVERS];
    unsigned width, height, count;
} book_shelf_page_layout_t;
typedef enum { BOOK_SHELF_PAGE_MISS, BOOK_SHELF_PAGE_WAIT, BOOK_SHELF_PAGE_HIT } book_shelf_page_state_t;
void book_shelf_page_key_init(book_shelf_page_key_t *key);
void book_shelf_page_key_add(book_shelf_page_key_t *key,const void *data,size_t size);
/* 准备/轮询/空闲保存可访问TF；render只能调用下方像素函数。
 * Request/poll/idle service may access SD; render uses only the pixel functions below. */
book_shelf_page_state_t book_shelf_page_request(book_shelf_page_key_t key,book_shelf_page_layout_t layout);
book_shelf_page_state_t book_shelf_page_poll(void);
void book_shelf_page_idle(uint64_t now_ms);
void book_shelf_page_release(void);
void book_shelf_page_pause(void);
/* 纯内存操作。先存未遮挡封面，画挡板后再提交整页；失败沿用原绘制。
 * Memory only. Stage unobscured covers, then capture after furniture; failures retain ordinary drawing. */
bool book_shelf_page_restore(uint8_t *fb);
bool book_shelf_page_stage_covers(const uint8_t *fb);
bool book_shelf_page_capture(const uint8_t *fb,unsigned favorite_edges,uint64_t now_ms);
bool book_shelf_page_copy_cover(uint8_t *fb,unsigned row,EpdRect destination);
unsigned book_shelf_page_favorite_edges(void);
