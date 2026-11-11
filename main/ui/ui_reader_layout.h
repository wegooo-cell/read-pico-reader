/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：标准显示状态栏、返回/书名/收藏及底部工具栏；非沉浸全屏只保留状态及章节进度，沉浸全屏保留铺屏细线。
 * English: Standard shows status, Back/title/Favorite/Fullscreen and bottom tools; nonimmersive fullscreen retains status/chapter progress and immersive fullscreen retains a thin line.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"
enum { UI_READER_STANDARD_TOP=176, UI_READER_STANDARD_HEADER_HEIGHT=152, UI_READER_STANDARD_BOTTOM=1048,
       UI_READER_METADATA_TOP=80, UI_READER_METADATA_BOTTOM=1168,
       UI_READER_FULLSCREEN_TOP=0, UI_READER_FULLSCREEN_BOTTOM=1204,
       UI_READER_FOOTER_TEXT_Y=1180, UI_READER_FOOTER_TEXT_PX=21,
       UI_READER_HEADER_TEXT_PX=25, UI_READER_TOOLS_TOP=1096 };
static inline EpdRect ui_reader_area(bool fullscreen,bool immersive) {
    int top=!fullscreen?UI_READER_STANDARD_TOP:immersive?UI_READER_FULLSCREEN_TOP:UI_READER_METADATA_TOP;
    int bottom=!fullscreen?UI_READER_STANDARD_BOTTOM:immersive?UI_READER_FULLSCREEN_BOTTOM:UI_READER_METADATA_BOTTOM;
    return (EpdRect){0,top,684,bottom-top};
}
static inline EpdRect ui_reader_body(bool fullscreen,bool immersive,int margin,bool vertical) {
    // 工具栏及题头悬浮在正文上；显隐只改变可见/刷新范围，不改变排版。
    // Header/tools overlay the body; visibility changes the paint crop, never pagination.
    (void)fullscreen;
    EpdRect r=ui_reader_area(true,immersive);
    // 带状态栏时顶部只留4px，底部16px；沉浸仍上下各16px，续页余量保持均分。
    // Keep4px below status chrome and16px above the footer; immersive insets stay16px with balanced continuation slack.
    const int top_inset=immersive?16:4;
    r.y+=top_inset;r.height-=top_inset+16;
    if(vertical)return (EpdRect){24,r.y+margin,636,r.height-2*margin};
    return (EpdRect){margin,r.y,684-2*margin,r.height};
}
static inline EpdRect ui_reader_header(void){return (EpdRect){0,0,684,UI_READER_METADATA_TOP};}
static inline EpdRect ui_reader_back_control(void){return (EpdRect){32,80,64,64};}
static inline EpdRect ui_reader_favorite_control(void){return (EpdRect){520,80,64,70};}
static inline EpdRect ui_reader_fullscreen_control(void){return (EpdRect){588,80,64,70};}
static inline EpdRect ui_reader_footer(void){return (EpdRect){0,1168,684,48};}
static inline EpdRect ui_reader_progress(void){return (EpdRect){18,1194,648,6};}
static inline EpdRect ui_reader_fullscreen_footer(void){return (EpdRect){0,1204,684,12};}
static inline EpdRect ui_reader_fullscreen_progress(void){return (EpdRect){36,1207,612,3};}
static inline EpdRect ui_reader_footer_for(bool fullscreen,bool immersive){return !fullscreen?(EpdRect){0,UI_READER_STANDARD_BOTTOM,684,48}:immersive?ui_reader_fullscreen_footer():ui_reader_footer();}
_Static_assert(UI_READER_METADATA_BOTTOM+48==1216,"Reader metadata stays on screen");
_Static_assert(UI_READER_FULLSCREEN_BOTTOM+12==1216,"Fullscreen keeps only the release progress strip");

_Static_assert(UI_READER_STANDARD_BOTTOM+48==UI_READER_TOOLS_TOP,"Standard metadata fits above the tools");

_Static_assert(UI_READER_STANDARD_HEADER_HEIGHT<UI_READER_STANDARD_TOP,"Chrome fill ends at the divider without moving the reader crop");
