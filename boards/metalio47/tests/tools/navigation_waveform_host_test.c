/* SPDX-License-Identifier: Apache-2.0
 * 中文：检查实际短表每对灰阶只向目标驱动，不能把它当作实屏灰阶或残影验证。
 * English: Check that every actual short-table gray pair drives only toward the target; this does not validate optical grays or ghosting.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "e0470_epaper_waveform.h"
#include "display_main_waveform.h"
#include "ui/ui_image_dither.h"

int main(void) {
    e0470_waveform_init();
    // 旧条带引擎读取的相位必须与DU20完全同一指针，适配器不能生成新表。
    // The old band engine must read the exact DU20 phase object; the adapter cannot generate a table.
    assert(e0470_waveform_phases(display_main_bw_turn_waveform(),MODE_GL16)==
           e0470_waveform_phases(display_main_bw_waveform(),MODE_DU));

    const EpdWaveformPhases* short_table = e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM, 1);
    const EpdWaveformPhases* gray_table = e0470_waveform_phases(&E0470_WAVEFORM, 5);
    assert(short_table && gray_table && short_table->phases == 8);
    const EpdWaveformPhases* original_bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
    uint8_t saved_bw[E0470_FULL_DU_FRAMES * 16 * 4], saved_gray[E0470_GL16_FRAMES * 16 * 4];
    assert(original_bw && original_bw->phases == E0470_FULL_DU_FRAMES);
    memcpy(saved_bw, original_bw->luts, sizeof(saved_bw));
    memcpy(saved_gray, gray_table->luts, sizeof(saved_gray));
    int gray_reverse_dark = 0;
    for (int from = 0; from < 16; ++from) {
        for (int to = 0; to < 16; ++to) {
            int pushes = 0;
            for (int f = 0; f < short_table->phases; ++f) {
                const int action = e0470_phase_action(short_table, f, to, from);
                if (from == to) assert(action == 0);
                else if (action) {
                    assert(action == (to > from ? 2 : 1));
                    ++pushes;
                }
            }
            if (from != to) assert(pushes > 0 && pushes <= 8);
            if (from == 0 && to == 15) assert(pushes == 8);
            if (from == 15 && to == 0) assert(pushes == 7);
            // GL16 中部分变浅迁移含反向压黑；短表所有对应迁移均已检查无反向动作。
            // Some GL16 brightening pairs include reverse darkening; every corresponding short-table pair above has no reverse action.
            if (to > from) {
                for (int f = 0; f < gray_table->phases; ++f)
                    gray_reverse_dark += e0470_phase_action(gray_table, f, to, from) == 1;
            }
        }
    }
    assert(gray_reverse_dark > 0);
    // 底栏轻量表只组合现有完整序列；白→浅灰没有黑复位，旧黑字先擦白、不会再整块压黑。
    // Quiet navigation only composes existing complete sequences: white-to-light-gray has no black reset; old ink whitens first.
    const EpdWaveformPhases *nav=e0470_waveform_phases(display_main_navigation_waveform(),MODE_GL16);
    const int nav_frames=E0470_FULL_DU_FRAMES+E0470_FOLLOW_FRAMES;
    assert(nav&&nav->phases==nav_frames&&!nav->phase_times);
    for(int f=0;f<nav_frames;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
        int expected=0;
        if(to!=from){
            if(to==0||to==15)expected=f<20?e0470_phase_action(original_bw,f,to,from):0;
            else if(from==15)expected=f<8?e0470_phase_action(short_table,f,to,15):0;
            else expected=f<20?e0470_phase_action(original_bw,f,15,from):e0470_phase_action(short_table,f-20,to,15);
        }
        int action=e0470_phase_action(nav,f,to,from);assert(action==expected);
        if(to>0&&to<15&&from!=15&&to!=from){
            if(f<20)assert(action==0||action==2);else assert(action==0||action==1);
        }
    }
    int background_black=0,old_background_black=0;
    for(int f=0;f<nav_frames;++f)background_black+=e0470_phase_action(nav,f,14,15)==1;
    for(int f=0;f<gray_table->phases;++f)old_background_black+=e0470_phase_action(gray_table,f,14,15)==1;
    assert(background_black==1&&old_background_black>background_black);
    assert(!memcmp(saved_bw,original_bw->luts,sizeof(saved_bw))&&!memcmp(saved_gray,gray_table->luts,sizeof(saved_gray)));
    puts("PASS: navigation gray14 from white uses one existing FOLLOW pulse, original full DU20 endpoints and white-first nonwhite cleanup, no black reset, original vendor LUTs unchanged");


    // 局部灰阶不裁短任何变色迁移，只有等灰动作清零；原始校准表不得被原地改写。
    // Local grays do not truncate changed transitions: only equal-gray actions are cleared; the original calibration table must not be modified in place.
    const EpdWaveform* local = display_main_gray_waveform();
    const EpdWaveformPhases* bands = e0470_waveform_phases(local, MODE_GL16);
    const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
    assert(local != &E0470_WAVEFORM && local->temp_intervals == E0470_WAVEFORM.temp_intervals);
    assert(bands && bands->phases == gray_table->phases && bands->phase_times == gray_table->phase_times);
    assert(bw && bw->phases == 20);
    for (int f = 0; f < bw->phases; ++f) {
        assert(e0470_phase_action(bw, f, 0, 0) == 0);
        assert(e0470_phase_action(bw, f, 15, 15) == 0 || e0470_phase_action(bw,f,15,15)==2);
    }
    int original_diagonal = 0;
    for (int f = 0; f < bands->phases; ++f)
        for (int from = 0; from < 16; ++from)
            for (int to = 0; to < 16; ++to) {
                int action = e0470_phase_action(bands, f, to, from);
                int original = e0470_phase_action(gray_table, f, to, from);
                if (from == to) {assert(action == 0); original_diagonal += original != 0;}
                else assert(action == original);
            }
    assert(original_diagonal > 0 && display_main_gray_waveform() == local);
    const EpdWaveformPhases *exit_table=e0470_waveform_phases(display_main_shelf_exit_waveform(),MODE_DU);
    assert(exit_table&&exit_table->phases==20);
    for(int f=0;f<20;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
        assert(e0470_phase_action(exit_table,f,to,from)==(from==to?0:e0470_phase_action(bw,f,to,from)));
    const EpdWaveform *profiles[]={display_main_gray_waveform(),display_main_water_waveform(),display_main_normal_waveform()};
    for(unsigned i=0;i<3;++i){
        const EpdWaveformPhases *table=e0470_waveform_phases(profiles[i],MODE_GL16);
        assert(table&&table->phases==E0470_GL16_FRAMES&&!table->phase_times);
        for(int f=0;f<E0470_GL16_FRAMES;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
            assert(e0470_phase_action(table,f,to,from)==(from==to?0:
                i==2&&(to==0||to==15)?(f<20?e0470_phase_action(bw,f,to,from):0):e0470_phase_action(gray_table,f,to,from)));
    }
    const EpdWaveformPhases* ordinary=e0470_waveform_phases(display_main_normal_waveform(),MODE_GL16);
    for(int f=0;f<ordinary->phases;++f)for(int from=0;from<16;++from){
        int white=e0470_phase_action(ordinary,f,15,from);
        assert(white==0||white==2);
    }
    assert(!memcmp(saved_bw,bw->luts,sizeof(saved_bw))&&!memcmp(saved_gray,gray_table->luts,sizeof(saved_gray)));
    const EpdWaveform* soft=display_main_shelf_soft_waveform();
    const EpdWaveformPhases* chrome=e0470_waveform_phases(soft,MODE_GL16);
    assert(soft!=&E0470_WAVEFORM&&chrome&&chrome->phases==E0470_GL16_FRAMES);
    for(int f=0;f<E0470_GL16_FRAMES;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
        int expected=to==from?0:(to==0||to==15)?(f<20?e0470_phase_action(bw,f,to,from):0):e0470_phase_action(gray_table,f,to,from);
        assert(e0470_phase_action(chrome,f,to,from)==expected);
    }
    for(int mode=MODE_GC16;mode<=MODE_GL16;mode+=MODE_GL16-MODE_GC16){
        const EpdWaveform* shelf=display_main_shelf_waveform(mode);
        const EpdWaveformPhases* table=e0470_waveform_phases(shelf,mode);
        const EpdWaveformPhases* original=e0470_waveform_phases(&E0470_FULL_WAVEFORM,mode);
        assert(table&&original&&shelf==display_main_shelf_waveform(mode));
        // PR64默认短表不能容纳完整48相图层时，沿用作者既有的默认表回退。
        // PR64's shorter chrome cannot contain 48-phase art; retain the author's existing default-table fallback.
        if(original->phases>chrome->phases){
            assert(shelf==&E0470_WAVEFORM&&table==e0470_waveform_phases(&E0470_WAVEFORM,mode));
            continue;
        }
        assert(table->phases==original->phases);
        for(int f=0;f<table->phases;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
            int action=e0470_phase_action(table,f,to,from);
            assert(action==e0470_phase_action(to==15?(from==15?e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16):chrome):original,f,to,from));
            if(to==15)assert(action==0||action==2);
        }
    }
    assert(!memcmp(saved_bw,bw->luts,sizeof(saved_bw))&&!memcmp(saved_gray,gray_table->luts,sizeof(saved_gray)));
    puts("PASS: shelf native art or approved default fallback, DU20 endpoints without extra pushes, equal-held chrome, white-only erase and original LUTs");
    const EpdWaveformPhases* page=e0470_waveform_phases(display_main_page_gray_waveform(),MODE_GL16);
    const EpdWaveformPhases* binary=e0470_waveform_phases(display_main_page_bw_waveform(),MODE_DU);
    int white_pushes=0,binary_white=0;
    for(int f=0;f<page->phases;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
        int action=e0470_phase_action(page,f,to,from);
        assert(action==(to==from&&to!=15?0:e0470_phase_action(gray_table,f,to,from)));
        if(to==15&&from==15){assert(action==0||action==2);white_pushes+=action==2;}
    }
    for(int f=0;f<20;++f){int action=e0470_phase_action(binary,f,15,15);assert(action==0||action==2);binary_white+=action==2;}
    assert(white_pushes>=3&&binary_white>0);
    puts("PASS: page white maintenance stays separate from fixed-navigation/slider equal holds");
    // 点阵亮度随输入单调增加且稳定，不引入中灰；阅读插图原16灰输出仍保留。
    // Dot brightness increases monotonically and remains stable with no intermediate grays; existing 16-gray reader output is retained.
    int previous_white = 0, previous_cover_white = 0;
    for (int gray = 0; gray <= 255; ++gray) {
        int white = 0, cover_white = 0;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            int v = ui_image_dither_bw((uint8_t)gray, x, y);
            assert(v == 0 || v == 255);
            assert(v == ui_image_dither_bw((uint8_t)gray, x + 4, y + 4));
            assert(v == ui_image_dither_bw((uint8_t)gray, x - 4, y - 4));
            white += v == 255;
            int cover=ui_image_dither_cover_bw((uint8_t)gray,x,y);
            assert(cover==0 || cover==255);
            assert(cover==ui_image_dither_cover_bw((uint8_t)gray,x+4,y+4));
            assert(cover==ui_image_dither_cover_bw((uint8_t)gray,x-4,y-4));
            assert(cover>=v);
            cover_white+=cover==255;
        }
        assert(white >= previous_white); previous_white = white;
        assert(cover_white>=previous_cover_white); previous_cover_white=cover_white;
        if (gray == 0) assert(white == 0 && cover_white==1);
        if (gray == 128) assert(white == 8 && cover_white==8);
    }
    assert(previous_white == 16 && previous_cover_white==16 && ui_image_dither_gray(119, 0, 0) == 119);
    puts("navigation waveform: calibrated GL16, unchanged grays, shelf whitespace erase, original water midgrays, vendor tables and lighter covers passed; optical behavior requires hardware");
    return 0;
}
