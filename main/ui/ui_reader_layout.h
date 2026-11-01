/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：标准和非沉浸全屏共用完整状态栏及章节进度，标准底部保留工具栏；沉浸全屏保留铺屏细线。
 * English: Standard and nonimmersive fullscreen share the complete status row and chapter progress; standard retains tools and immersive fullscreen retains full-page text with a thin line.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"
enum { UI_READER_STANDARD_TOP=80, UI_READER_STANDARD_BOTTOM=1048,
       UI_READER_METADATA_TOP=UI_READER_STANDARD_TOP, UI_READER_METADATA_BOTTOM=1168,
       UI_READER_FULLSCREEN_TOP=0, UI_READER_FULLSCREEN_BOTTOM=1204,
       UI_READER_FOOTER_TEXT_Y=1180, UI_READER_FOOTER_TEXT_PX=21,
       UI_READER_HEADER_TEXT_PX=25, UI_READER_TOOLS_TOP=1096 };
static inline EpdRect ui_reader_area(bool fullscreen,bool immersive) {
    int top=!fullscreen?UI_READER_STANDARD_TOP:immersive?UI_READER_FULLSCREEN_TOP:UI_READER_METADATA_TOP;
    int bottom=!fullscreen?UI_READER_STANDARD_BOTTOM:immersive?UI_READER_FULLSCREEN_BOTTOM:UI_READER_METADATA_BOTTOM;
    return (EpdRect){0,top,684,bottom-top};
}
static inline EpdRect ui_reader_body(bool fullscreen,bool immersive,int margin,bool vertical) {
    EpdRect r=ui_reader_area(fullscreen,immersive);
    // 正式版正文上留24像素、下留2像素；横竖排继续使用用户边距。
    // Retain the release's24px top and2px bottom text inset, with configured horizontal/vertical margins.
    if(fullscreen&&immersive){r.y+=24;r.height-=26;}
    if(vertical)return (EpdRect){24,r.y+margin,636,r.height-2*margin};
    return (EpdRect){margin,r.y,684-2*margin,r.height};
}
static inline EpdRect ui_reader_header(void){return (EpdRect){0,0,684,UI_READER_METADATA_TOP};}
static inline EpdRect ui_reader_footer(void){return (EpdRect){0,1168,684,48};}
static inline EpdRect ui_reader_progress(void){return (EpdRect){18,1194,648,6};}
static inline EpdRect ui_reader_fullscreen_footer(void){return (EpdRect){0,1204,684,12};}
static inline EpdRect ui_reader_fullscreen_progress(void){return (EpdRect){36,1207,612,3};}
static inline EpdRect ui_reader_footer_for(bool fullscreen,bool immersive){return !fullscreen?(EpdRect){0,UI_READER_STANDARD_BOTTOM,684,48}:immersive?ui_reader_fullscreen_footer():ui_reader_footer();}
_Static_assert(UI_READER_METADATA_BOTTOM+48==1216,"Reader metadata stays on screen");
_Static_assert(UI_READER_FULLSCREEN_BOTTOM+12==1216,"Fullscreen keeps only the release progress strip");

_Static_assert(UI_READER_STANDARD_BOTTOM+48==UI_READER_TOOLS_TOP,"Standard metadata fits above the tools");
