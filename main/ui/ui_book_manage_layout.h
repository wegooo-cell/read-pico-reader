/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：按用户确认预览统一管理页列表/图标、搜索、排序、选择和底栏的绘制/命中。
 * English: Shared management list/grid, search, order, selection and footer paint/hit geometry approved by the user.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"
static inline int ui_book_manage_rows(bool grid){return grid?6:5;}
static inline EpdRect ui_book_manage_row(int i){return (EpdRect){36,310+i*130,612,130};}
static inline EpdRect ui_book_manage_entry(bool grid,int i){
    return grid?(EpdRect){36+(i%3)*210,322+(i/3)*330,192,314}:ui_book_manage_row(i);
}
static inline EpdRect ui_book_manage_cover(bool grid,int i){
    EpdRect r=ui_book_manage_entry(grid,i);
    return grid?(EpdRect){r.x+26,r.y+8,140,192}:(EpdRect){r.x+12,r.y+12,80,108};
}
static inline EpdRect ui_book_manage_search(void){return (EpdRect){36,160,612,64};}
static inline EpdRect ui_book_manage_layout(void){return (EpdRect){584,79,64,66};}
static inline EpdRect ui_book_manage_sort(void){return (EpdRect){252,244,296,46};}
static inline EpdRect ui_book_manage_select(void){return (EpdRect){560,244,88,46};}
static inline EpdRect ui_book_manage_action(int i){
    int action=i-3,x=action*684/4,end=(action+1)*684/4;
    return action>=0&&action<4?(EpdRect){x,1096,end-x,120}:(EpdRect){0};
}
_Static_assert(310+5*130<1006,"List must finish before pager");
_Static_assert(322+330+314<1006,"Grid must finish before pager");
_Static_assert(1006+50<1096-8,"Pager leaves footer guard");
_Static_assert(1096+120==1216,"Four actions share native navigation geometry");
