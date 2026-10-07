/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：微信读书阅读页点划线句子弹热门想法弹窗。命中基于排版偏移区间
 * （book_layout_set_marks/mark_at），弹窗状态与绘制都在本模块；
 * app_book.c 只在开书/关书/换章/手势/按键/渲染处挂小钩子。
 * English: Tap-a-highlight thought popup for the Weread reader. Hit-testing rides
 * layout source spans; popup state and painting live here, app_book.c wires small hooks.
 *
 * 数据来自 pico_weread 只读桥（weread_notes_highlight_at/review_at），缓存由拉取任务
 * 预先落 SD；离线可读，查询失败一律静默退回正文点按原行为。
 * Data rides the pico_weread read bridge over SD caches; offline-capable, and
 * every miss falls back silently to the original body-tap behavior.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 开书后按库内路径绑定（/sdcard/WeRead/x.epub → /WeRead/x.epub 反查书号）；
/// 非微信读书书籍静默失败，不影响普通阅读。/ Bind after open; non-Weread books fail silently.
bool book_notes_bind(const char* phys_path);
/// 关书/换书/媒体丢失时解绑并收起弹窗。/ Unbind and collapse the popup on close.
void book_notes_unbind(void);
/// 绑定是否成功（该书有划线想法缓存可查）。/ True when bound to a Weread book.
bool book_notes_bound(void);
/// 章切换时同步当前章（加载该章划线并在章节全文定位偏移区间）。/ Track the current chapter for span decoration.
void book_notes_set_chapter(uint32_t spine);
/// 作废装饰缓存，强制下一次 set_chapter 重算（字体切换重排后调用）。/ Poison the decoration cache so the next set_chapter reloads (after a font reflow).
void book_notes_invalidate(void);
/// 弹窗是否展开。/ Popup open?
bool book_notes_active(void);
/// 点正文：给定 book_layout_mark_at 命中的划线序号，加载想法并展开弹窗。
/// / Open the popup for a mark index resolved by book_layout_mark_at.
bool book_notes_tap_mark(uint32_t spine, int mark);
/// 弹窗展开时消费手势；返回 true 表示画面有变化（需按弹窗区域局部重绘）。
/// / Consume a gesture while open; true means the popup area changed.
bool book_notes_gesture(int type, int x, int y);
/// 弹窗展开时消费三键；返回 true 表示画面有变化。/ Consume a key while open; true means changed.
bool book_notes_key(int key);
/// 直接收起弹窗（中键长按等）。/ Collapse without loading (middle-key hold).
void book_notes_close(void);
/// 弹窗覆盖区域（APP_REDRAW_AREA 刷新范围，开合通用）。/ Popup rect for area redraws.
EpdRect book_notes_area(void);
/// render 尾叠加绘制；未展开时空操作。/ Overlay painting; no-op when closed.
void book_notes_render(uint8_t* fb);
/// 阶段1调试：输出当前页命中的划线屏幕矩形与原文（须在绘制锁内调用）。
/// / Stage-1 debug: log on-page mark rects with source text (call under the draw lock).
void book_notes_map_page(size_t page);
/// 阅读时长上报：累计已读毫秒并按 5 分钟周期上传 rt（非微信读书书零开销直返）。
/// / Reading-time report: accumulate read ms and upload rt every 5 minutes (no-op for non-Weread books).
void book_notes_report_tick(uint32_t elapsed_ms, uint16_t chapter, uint32_t byte_off, uint8_t pct);
/// 关书补尾巴：累计 ≥30 秒时用 tick 记下的最后坐标补报一次并清零。/ Close-out: report the tail (≥30 s) with the last tick coords, then reset.
void book_notes_report_close(void);

#ifdef __cplusplus
}
#endif
