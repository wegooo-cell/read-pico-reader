/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：文件首页和目录的固定布局，绘制与命中共用；内容不进入底栏保护带。
 * English: Shared file-hub/directory paint and hit geometry, outside the footer guard.
 */
#pragma once
#include "epdiy.h"
#define UI_FILES_LIST_TOP 310
#define UI_FILES_ROW_HEIGHT 94
#define UI_FILES_ROWS 7
#define UI_FILES_LIST_END (UI_FILES_LIST_TOP + UI_FILES_ROW_HEIGHT * UI_FILES_ROWS)
#define UI_FILES_FOOTER_TOP 1096
static inline EpdRect ui_files_storage_rect(void) { return (EpdRect){36,175,612,200}; }
static inline EpdRect ui_files_category_rect(int i) { return (EpdRect){36+i*210,399,192,170}; }
static inline EpdRect ui_files_transfer_rect(int i) { return (EpdRect){36,662+i*102,612,102}; }
static inline EpdRect ui_files_batch_rect(int i) {
    int x=i*684/4, end=(i+1)*684/4;
    return (EpdRect){x,UI_FILES_FOOTER_TOP,end-x,120};
}
_Static_assert(UI_FILES_LIST_END < 1006, "directory ends before its pager");
_Static_assert(1006+60 <= UI_FILES_FOOTER_TOP-8, "pager leaves footer guard");

_Static_assert(662+4*102 <= UI_FILES_FOOTER_TOP-8, "Four transfer entries leave footer guard");
