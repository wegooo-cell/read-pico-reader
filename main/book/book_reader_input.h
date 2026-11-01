/* SPDX-License-Identifier: Apache-2.0
 * 中文：正文两侧单击翻页、中间单击全屏；插图两侧保留双击缩放；页面负责取消离页/锁屏/滑动后的等待。
 * English: Side taps turn, centre taps enter fullscreen and image side double taps zoom; pages cancel pending taps on exit, lock or swipe.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

#define BOOK_READER_DOUBLE_MS 340
#define BOOK_READER_TAP_FULLSCREEN 2

typedef struct {
    bool pending;
    int direction;
    int64_t at_ms;
    int x, y;
} book_reader_tap_t;

static inline bool book_reader_center(EpdRect body, int x, int y) {
    return x >= body.x + body.width * 28 / 100 && x < body.x + body.width * 72 / 100 &&
           y >= body.y && y < body.y + body.height;
}
/// rtl 表示竖排从右到左：横向分区的「上一页」在右半边，左右翻转。
/// rtl marks right-to-left vertical text: the previous-page half is the right one.
static inline int book_reader_tap_direction(EpdRect body, int x, int y, bool vertical, bool rtl) {
    if (x < body.x || x >= body.x + body.width || y < body.y || y >= body.y + body.height) return 0;
    if (vertical) return y < body.y + body.height / 3 ? -1 : 1;
    bool previous_half = rtl ? x >= body.x + body.width / 2 : x < body.x + body.width / 2;
    return previous_half ? -1 : 1;
}
static inline int book_reader_tap_feed(book_reader_tap_t *tap, EpdRect body,
                                      int x, int y, bool vertical, bool rtl, int64_t now_ms) {
    (void)now_ms;
    tap->pending=false;
    if(book_reader_center(body,x,y))return BOOK_READER_TAP_FULLSCREEN;
    return book_reader_tap_direction(body,x,y,vertical,rtl);
}
static inline int book_reader_tap_tick(book_reader_tap_t *tap, int64_t now_ms, bool touching) {
    if (!tap->pending) return 0;
    if (now_ms < tap->at_ms) { tap->pending = false; return 0; }
    if (touching || now_ms - tap->at_ms <= BOOK_READER_DOUBLE_MS) return 0;
    tap->pending = false;
    return tap->direction;
}
/// 整页插图用：任意位置的双击都算双击，单击一律延后到双击窗口结束才翻页。
/// For a whole-page illustration a double tap counts anywhere, and a single tap is deferred until
/// the double-tap window expires before it turns the page.
static inline int book_reader_tap_feed_any(book_reader_tap_t *tap, EpdRect body,
                                          int x, int y, bool vertical, bool rtl, int64_t now_ms) {
    if(book_reader_center(body,x,y)){tap->pending=false;return BOOK_READER_TAP_FULLSCREEN;}
    const int direction = book_reader_tap_direction(body, x, y, vertical, rtl);
    if (!direction) { tap->pending = false; return 0; }
    const int dx = x - tap->x, dy = y - tap->y;
    if (tap->pending && now_ms >= tap->at_ms && now_ms - tap->at_ms <= BOOK_READER_DOUBLE_MS &&
        dx * dx + dy * dy <= 120 * 120) {
        tap->pending = false;
        return BOOK_READER_TAP_FULLSCREEN;
    }
    const int expired = tap->pending && now_ms >= tap->at_ms &&
        now_ms - tap->at_ms > BOOK_READER_DOUBLE_MS ? tap->direction : 0;
    *tap = (book_reader_tap_t){true, direction, now_ms, x, y};
    return expired;
}
