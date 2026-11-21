/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：按用户确认预览统一管理页列表/图标、搜索、排序、选择和底栏的绘制/命中。
 * English: Shared management list/grid, search, order, selection and footer paint/hit geometry approved by the user.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"
static inline int ui_book_manage_rows(bool grid){return grid?12:13;}
static inline EpdRect ui_book_manage_row(int i){return (EpdRect){36,310+i*53,612,53};}
static inline EpdRect ui_book_manage_entry(bool grid,int i){
    return grid?(EpdRect){36+(i%4)*156,316+(i/4)*226,144,222}:ui_book_manage_row(i);
}
static inline EpdRect ui_book_manage_cover(bool grid,int i){
    EpdRect r=ui_book_manage_entry(grid,i);
    return grid?(EpdRect){r.x+9,r.y,126,170}:(EpdRect){0};
}
static inline EpdRect ui_book_import_entry(bool grid,int i){
    return grid?(EpdRect){36+(i%4)*156,244+(i/4)*258,144,250}:(EpdRect){36,244+i*59,612,59};
}
static inline EpdRect ui_book_import_cover(bool grid,int i){
    EpdRect r=ui_book_import_entry(grid,i);return grid?(EpdRect){r.x+9,r.y,126,170}:(EpdRect){0};
}
static inline EpdRect ui_book_import_search(void){return (EpdRect){584,79,64,66};}
static inline EpdRect ui_book_import_layout(void){return (EpdRect){520,79,64,66};}
static inline EpdRect ui_book_import_parent(void){return (EpdRect){36,148,612,34};}
static inline EpdRect ui_book_import_select(void){return (EpdRect){400,194,106,40};}
static inline EpdRect ui_book_import_cancel(void){return (EpdRect){520,194,128,40};}
static inline EpdRect ui_book_manage_tab(int i){return (EpdRect){36+i*314,166,298,62};}
static inline EpdRect ui_book_manage_search(void){return (EpdRect){520,79,64,66};}
static inline EpdRect ui_book_manage_style(void){return (EpdRect){0};}
static inline EpdRect ui_book_manage_layout(void){return (EpdRect){584,79,64,66};}
static inline EpdRect ui_book_manage_parent(void){return (EpdRect){36,246,128,48};}
enum { UI_BOOK_MANAGE_COVER_CONTROL = 117 };
static inline EpdRect ui_book_manage_get_cover(void){return (EpdRect){372,86,136,58};}
static inline EpdRect ui_book_manage_sort(void){return (EpdRect){170,246,246,48};}
static inline EpdRect ui_book_manage_import(void){return (EpdRect){0};}
static inline EpdRect ui_book_manage_select(void){return (EpdRect){436,246,98,48};}
static inline EpdRect ui_book_manage_cancel(void){return (EpdRect){548,246,100,48};}
static inline EpdRect ui_book_manage_get_books(void){return (EpdRect){0};}
static inline EpdRect ui_book_manage_rescan(void){return (EpdRect){0};}
static inline EpdRect ui_book_manage_action(int i){
    int action=i-3,x=action*684/4,end=(action+1)*684/4;
    return action>=0&&action<4?(EpdRect){x,1096,end-x,120}:(EpdRect){0};
}
_Static_assert(310+13*53<=1006,"List must finish before pager");
_Static_assert(316+2*226+222<=1006,"Grid must finish before pager");
_Static_assert(244+13*59<=1028,"Import rows finish before their pager");
_Static_assert(1028+50<1096-8,"Pager leaves footer guard");
_Static_assert(1096+120==1216,"Actions share native navigation geometry");

_Static_assert(244+2*258+250<=1028,"Import grid finishes before pager");
