/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"

void display_request_full_refresh(void);
bool display_system_fast_set(bool enabled);
enum EpdDrawError update_display_main_turn(EpdiyHighlevelState* hl,EpdRect area,int dir);
static inline bool display_page_prepare_begin(void){return true;}
static inline void display_page_prepare_end(bool held){(void)held;}

enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area);
bool display_main_fast_background_set(bool);
#if KIIKO_BOARD_METALIO47
bool display_main_switch_erase_set(bool);
bool display_main_shelf_home_erase_set(bool);
enum EpdDrawError update_display_gray_transition(EpdiyHighlevelState* hl,EpdRect area);
#endif

unsigned display_main_quality_set(unsigned);
