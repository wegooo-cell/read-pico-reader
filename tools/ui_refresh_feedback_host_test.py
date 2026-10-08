"""书架差分反馈与状态栏实际函数回归。/ Actual shelf feedback and status-bar regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (root / path).read_text()
    match = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert match, name
    start, at, depth, quote, escape = match.start(), match.end(), 1, None, False
    while depth:
        c = text[at]
        if quote:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        at += 1
    return text[start:at]


shelf = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;int64_t now_ms;unsigned leaf;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SHELF,MANAGE,READING,READER_PANEL_NONE,TOC,EDIT,SEARCH};
enum EpdDrawMode {MODE_GL16,MODE_DU,MODE_GC16};
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_ERROR};
#define APP_PAGE_REFRESH_MODE MODE_GL16
#define SHELF_BOOK_LIFT_PX 16
#define portMAX_DELAY 0
static const int E0470_WAVEFORM=0,E0470_FULL_WAVEFORM=1,E0470_FOLLOW_WAVEFORM=2,E0470_TEXTTURN_WAVEFORM=3;
static const char *TAG="test";
static int s_view,s_presented_view,s_reader_panel,s_pressed_control;
static bool s_shelf_feedback_pending,s_reader_cleanup,s_reader_image_refresh_pending,s_toolbar,s_clear_confirm;
static bool s_reader_fullscreen,s_water_turn_pending,s_reader_footer_pending,s_reader_turn_pending,s_reader_text_frame;
static enum EpdDrawMode s_mode;
static EpdRect s_area,s_du_area;
static unsigned s_du_count;
static int64_t s_du_ms;
static int s_water_turn_dir;
static void *s_prep_done;
static unsigned pushes;
static int route;
static enum EpdDrawMode pushed_mode;
static EpdRect pushed_area;
static int trace_route[256], trace_mode[256], trace_wave[256];
static EpdRect trace_area[256];
static unsigned trace_count;
enum {DIFF=1,AREA,FULL,WHOLE,FAST,WATER,READER,LOCAL_FULL};
static int shelf_rows(void){return 9;}
static EpdRect row_rect(int row){return (EpdRect){42+(row%3)*210,220+(row/3)*272,176,240};}
static const char *s_text;
static void prepare_inline_image(void){}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;}
static bool kick_prep(void){return false;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI test_log
static void xSemaphoreTake(void *done,int timeout){(void)done;(void)timeout;}
static void guard_draw_result(void *hl,int error){(void)hl;assert(error==EPD_DRAW_SUCCESS||error==EPD_DRAW_ERROR);}
static EpdRect reader_area(void){return (EpdRect){36,176,612,856};}
static EpdRect progress_rect(void){return (EpdRect){36,1040,612,42};}
static EpdRect reader_fullscreen_progress_area(void){return (EpdRect){0,1200,684,4};}
static unsigned percent(unsigned page){return page;}
static unsigned s_page,s_chapter;
static bool test_hide_images,test_load_failed,s_save_failed,s_input_settle;
static int test_types[2][2],test_push_error,test_effect;
static uint8_t test_full_pages;
static unsigned s_turns,s_unsaved,s_stats_pending_turns,s_session_turns;
static size_t s_jump_offset;
static int64_t s_last_turn_ms,s_stats_activity_ms;
static uint8_t *s_next_fb;
static int s_next_page=-1;
static struct {uint8_t *gray;} test_image={.gray=(uint8_t*)"gray"},*s_page_images=&test_image;
#define BOOK_TOC_ROWS 12
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define E0470_TURN_DEFAULT_TICK_US 21000
#define E0470_TURN_FAST_TICK_US 14000
static int test_tick_us,test_tick_sets;
static void e0470_page_turn_set_tick_us(int us){test_tick_us=us;++test_tick_sets;}
static int book_layout_page_image_count(unsigned page){return !test_hide_images&&test_types[s_chapter][page]?1:0;}
static int book_layout_page_image(unsigned page){return !test_hide_images&&test_types[s_chapter][page]==2?0:-1;}
static unsigned book_layout_page_count(void){return 2;}
static unsigned book_chapter_count(void){return 2;}
static bool app_settings_reader_hide_images(void){return test_hide_images;}
static uint8_t app_settings_reader_full_pages(void){return test_full_pages;}
static int app_settings_reader_turn_effect(void){return test_effect;}
static void save_progress(void){}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static bool load_chapter(app_ctx_t *ctx,unsigned chapter,int off,bool last){(void)ctx;(void)off;if(test_load_failed)return false;s_chapter=chapter;s_page=last?1:0;return true;}
static void set_reader_view(int view){s_view=view;}
static void lock_draw(void){}
static void unlock_draw(void){}
static size_t fb_bytes(void){return 1;}
static void draw_reader(uint8_t *fb,unsigned page){*fb=(uint8_t)page;}

static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static enum EpdDrawError record(int kind,enum EpdDrawMode mode,EpdRect area){assert(trace_count<256);trace_route[trace_count]=kind;trace_mode[trace_count]=mode;trace_area[trace_count++]=area;++pushes;route=kind;pushed_mode=mode;pushed_area=area;return (enum EpdDrawError)test_push_error;}
static enum EpdDrawError update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(DIFF,mode,area);}
static enum EpdDrawError update_display_area_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(AREA,mode,area);}
static enum EpdDrawError update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(LOCAL_FULL,mode,area);}
static enum EpdDrawError update_display_with(void *hl,const int *wave,enum EpdDrawMode mode){(void)hl;(void)wave;return record(WHOLE,mode,(EpdRect){0});}
static enum EpdDrawError update_display_full(void *hl){(void)hl;return record(FULL,MODE_GC16,(EpdRect){0});}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;return record(FAST,MODE_GL16,(EpdRect){0});}
static enum EpdDrawError update_display_mode_diff(void *hl,enum EpdDrawMode mode){(void)hl;return record(READER,mode,(EpdRect){0});}
static enum EpdDrawError update_display_water_turn(void *hl,EpdRect area,int dir){(void)hl;(void)dir;return record(WATER,MODE_GL16,area);}
'''
shelf += function("main/apps/app_book.c", "paint_control") + "\n"
for name in ("present", "paint_reading", "turn_page"):
    shelf += function("main/apps/app_book.c", name) + "\n"
shelf += r'''
int main(void){
 uint8_t fb=0;app_ctx_t ctx={.fb=&fb};s_view=s_presented_view=SHELF;
 for(int repeat=0;repeat<80;++repeat){
   int row=repeat%9;EpdRect original=row_rect(row);s_pressed_control=repeat%2?row:-1;
   s_reader_fullscreen=true;
   assert(paint_control(&ctx,original)==APP_REDRAW_AREA&&s_shelf_feedback_pending);
   assert(s_area.x==original.x&&s_area.y==original.y-16&&s_area.width==original.width&&s_area.height==original.height+16);
   unsigned before=pushes;assert(present(&ctx,APP_REDRAW_AREA));
   assert(pushes==before+1&&route==DIFF&&pushed_mode==MODE_GL16&&!s_shelf_feedback_pending);
   assert(pushed_area.y==original.y-16&&pushed_area.height==original.height+16);
 }
 s_reader_fullscreen=false;assert(paint_control(&ctx,(EpdRect){500,90,90,54})==APP_REDRAW_AREA&&!s_shelf_feedback_pending);
 assert(present(&ctx,APP_REDRAW_AREA)&&route==AREA&&pushed_mode==MODE_DU);
 assert(paint_control(&ctx,row_rect(0))==APP_REDRAW_AREA);s_view=READING;
 assert(present(&ctx,APP_REDRAW_AREA)&&route==READER&&!s_shelf_feedback_pending);
 s_view=s_presented_view=SHELF;paint_control(&ctx,row_rect(0));
 assert(present(&ctx,APP_REDRAW_FULL)&&route==FULL&&pushed_mode==MODE_GC16&&!s_shelf_feedback_pending);
 puts("PASS: 80 cover lift/cancel feedbacks use one grayscale differential, preserve the union and never force cleanup; buttons, view transitions and manual refresh retain their paths");
 // 阅读翻页只有一次正文灰阶推屏；页脚、清残影、图片、水波纹与设置面板保持原路由。
 // One grayscale body commit per text turn; footer, cleanup, images, water and settings keep their routes.
 s_view=s_presented_view=READING;s_text="text";s_reader_panel=READER_PANEL_NONE;
 s_area=reader_area();s_mode=MODE_GL16;s_reader_turn_pending=s_reader_footer_pending=true;
 trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
 assert(trace_count==2&&trace_route[0]==DIFF&&trace_mode[0]==MODE_GL16&&trace_wave[0]==E0470_TEXTTURN_WAVEFORM);
 assert(trace_area[0].y==reader_area().y&&trace_area[0].height==reader_area().height);
 assert(trace_route[1]==AREA&&trace_mode[1]==MODE_DU&&trace_wave[1]==E0470_FOLLOW_WAVEFORM);
 assert(trace_area[1].y==progress_rect().y&&!s_reader_turn_pending);
 for(int full=0;full<2;++full){
   s_reader_fullscreen=full;s_reader_turn_pending=true;s_reader_cleanup=true;
   s_reader_footer_pending=!full;trace_count=0;
   assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==FULL&&pushed_mode==MODE_GC16);
   s_reader_turn_pending=true;trace_count=0;
   assert(present(&ctx,APP_REDRAW_FULL)&&trace_count==1&&route==FULL);
 }
 s_reader_fullscreen=false;s_reader_turn_pending=true;s_reader_image_refresh_pending=true;
 trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
 assert(trace_count==1&&route==AREA&&pushed_mode==MODE_GC16&&trace_wave[0]==E0470_FULL_WAVEFORM);
 s_reader_turn_pending=s_water_turn_pending=true;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==WATER);
 // 翻页标志已消费，下一次设置绘制不能误走翻页差分。
 // The consumed turn flag must not affect the next settings repaint.
 s_reader_panel=99;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==AREA&&pushed_mode==MODE_GL16&&trace_wave[0]==E0470_WAVEFORM);
 s_reader_panel=READER_PANEL_NONE;s_reader_fullscreen=true;s_reader_turn_pending=true;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==2&&trace_route[0]==DIFF&&trace_route[1]==DIFF&&trace_wave[0]==E0470_TEXTTURN_WAVEFORM&&trace_wave[1]==E0470_WAVEFORM);
 puts("PASS: text turn/body/footer, periodic/manual cleanup, grayscale image, water effect, settings and full-screen routes");
 for(int input_view=EDIT;input_view<=SEARCH;input_view++)for(int repeat=0;repeat<80;repeat++){
   s_view=input_view;s_reader_fullscreen=true;s_reader_footer_pending=true;
   s_area=(EpdRect){36,243,612,82};s_mode=repeat==0?MODE_GL16:MODE_DU;s_du_count=10;
   trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
   assert(trace_count==1&&route==DIFF&&trace_wave[0]==(repeat==0?E0470_WAVEFORM:E0470_FOLLOW_WAVEFORM));
   assert(pushed_area.y==243&&pushed_area.height==82&&!s_du_count);
 }
 for(int input_view=EDIT;input_view<=SEARCH;input_view++){
   s_view=input_view;s_reader_footer_pending=true;s_input_settle=true;
   s_area=(EpdRect){24,612,636,114};s_mode=MODE_DU;s_du_count=10;
   trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
   assert(trace_count==1&&route==LOCAL_FULL&&pushed_mode==MODE_GL16&&trace_wave[0]==E0470_WAVEFORM);
   assert(pushed_area.x==24&&pushed_area.y==612&&pushed_area.width==636&&pushed_area.height==114);
   assert(!s_input_settle&&!s_du_count);
 }
 s_view=s_presented_view=READING;s_reader_fullscreen=false;s_reader_footer_pending=false;
 puts("PASS: 160 search/title updates keep a single local push; idle settles drive only the candidate strip with full-pixel GL16 and never refresh the footer or whole page");

 // 实际翻页、绘制和推屏函数共用页类型；混排与跨章不靠纯图标志判定。
 // Actual turn/paint/present functions share page types; mixed and cross-chapter guards do not rely on the pure-image flag.
 for(int full=0;full<2;++full)for(int hidden=0;hidden<2;++hidden)
 for(int from=0;from<3;++from)for(int to=0;to<3;++to){
   s_reader_fullscreen=full;test_hide_images=hidden;test_types[0][0]=from;test_types[0][1]=to;
   s_view=s_presented_view=READING;s_reader_panel=READER_PANEL_NONE;s_chapter=s_page=0;
   trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
   assert(s_reader_text_frame==(hidden||from==0));
   app_redraw_t redraw=turn_page(&ctx,1);
   bool eligible=hidden||(from==0&&to==0);
   assert(redraw==APP_REDRAW_AREA&&s_reader_turn_pending==eligible);
   trace_count=0;present(&ctx,redraw);
   if(eligible)assert(trace_route[0]==DIFF&&trace_wave[0]==E0470_TEXTTURN_WAVEFORM);
   else if(to==2)assert(trace_mode[0]==MODE_GC16&&trace_wave[0]==E0470_FULL_WAVEFORM);
   else assert(trace_wave[0]==E0470_WAVEFORM&&trace_route[0]==(full?DIFF:AREA));
   assert(!s_reader_turn_pending&&s_reader_text_frame==(hidden||to==0));
 }
 s_reader_fullscreen=false;test_hide_images=false;
 for(int from=0;from<3;++from)for(int to=0;to<3;++to)for(int dir=-1;dir<=1;dir+=2){
   int old=dir>0?0:1,new=1-old;
   test_types[old][dir>0?1:0]=from;test_types[new][dir>0?0:1]=to;
   s_chapter=old;s_page=dir>0?1:0;trace_count=0;
   present(&ctx,paint_reading(&ctx,MODE_GL16));
   app_redraw_t redraw=turn_page(&ctx,dir);
   assert(s_chapter==(unsigned)new&&s_reader_turn_pending==(from==0&&to==0));
   trace_count=0;present(&ctx,redraw);
   assert(trace_wave[0]!=(int)E0470_TEXTTURN_WAVEFORM||(from==0&&to==0));
 }
 // 失败推屏、面板及离开阅读会失效旧基准；下一次正常绘制后恢复。
 // Failed presents, panels and leaving reading invalidate history; a successful body restores it.
 memset(test_types,0,sizeof(test_types));s_chapter=s_page=0;
 test_push_error=EPD_DRAW_ERROR;trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
 assert(!s_reader_text_frame);test_push_error=0;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 trace_count=0;present(&ctx,APP_REDRAW_AREA);assert(s_reader_text_frame);
 s_reader_panel=99;trace_count=0;present(&ctx,APP_REDRAW_PAGE);assert(!s_reader_text_frame);
 s_reader_panel=READER_PANEL_NONE;s_page=0;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 trace_count=0;present(&ctx,APP_REDRAW_AREA);assert(s_reader_text_frame);
 s_view=SHELF;trace_count=0;present(&ctx,APP_REDRAW_PAGE);assert(!s_reader_text_frame);
 s_view=s_presented_view=READING;s_chapter=0;s_page=1;test_load_failed=true;
 assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_reader_turn_pending&&s_view==TOC);
 test_load_failed=false;s_view=s_presented_view=READING;s_page=0;
 for(int policy=0;policy<2;++policy){
   s_page=0;trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
   test_full_pages=policy?1:0;test_effect=policy?0:1;s_turns=0;
   app_redraw_t redraw=turn_page(&ctx,1);trace_count=0;present(&ctx,redraw);
   assert(trace_route[0]==(policy?FULL:WATER));
 }
 test_full_pages=0;test_effect=0;s_page=0;test_types[0][0]=1;test_image.gray=NULL;
 trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));assert(!s_reader_text_frame);
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 // 原速/快档切换只更改等待；全刷与插图保持优先级，不漏到普通翻页。
 // Switching speeds changes padding only; cleanup and images retain priority, without affecting ordinary turns.
 memset(test_types,0,sizeof(test_types));s_chapter=0;test_image.gray=(uint8_t*)"gray";
 s_view=s_presented_view=READING;s_reader_panel=READER_PANEL_NONE;s_clear_confirm=false;
 for(int full=0;full<2;++full) for(int step=0;step<6;++step){
   const int sequence[]={2,1,2,0,1,0};test_effect=sequence[step];test_full_pages=0;
   s_reader_fullscreen=full;s_page=0;s_turns=0;trace_count=0;
   present(&ctx,paint_reading(&ctx,MODE_GL16));
   int before=test_tick_sets;app_redraw_t redraw=turn_page(&ctx,1);
   trace_count=0;assert(present(&ctx,redraw));
   assert(trace_route[0]==(test_effect?WATER:DIFF));
   assert(test_tick_sets==before+(test_effect?1:0));
   if(test_effect)assert(test_tick_us==(test_effect==2?14000:21000));
 }
 test_effect=2;test_full_pages=1;s_turns=0;s_page=0;
 int before=test_tick_sets;app_redraw_t redraw=turn_page(&ctx,1);trace_count=0;
 assert(present(&ctx,redraw)&&trace_route[0]==FULL&&test_tick_sets==before);
 test_full_pages=0;s_page=0;test_types[0][1]=2;trace_count=0;
 redraw=turn_page(&ctx,1);trace_count=0;
 assert(present(&ctx,redraw)&&trace_route[0]!=WATER&&test_tick_sets==before);
 puts("PASS: original/fast/default switching in normal and full-screen reading; manual/periodic cleanup and grayscale-image priority unchanged");

 puts("PASS: 36 text/mixed/image/hidden/full-screen combinations, 18 bidirectional chapter transitions, failed image/display/load guards and actual cleanup/water turns");
}
'''

nav = (root / "main/ui/ui_nav.c").read_text()
defines = "\n".join(re.findall(r"^#define (?:UI_NAV_(?:NETWORK|BOLT)_ICON_PX|UI_STATUS_\w+_PX) .+$", nav, re.M))
status = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum {UI_ICON_WIFI,UI_ICON_BLUETOOTH,UI_ICON_ZAP};
#define UI_LOCK_WIDTH 684
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
#define READ_PICO_TRANSFER_MODE_STA 1
typedef struct {bool time_synced;time_t unix_sec;} pmu_snapshot_t;
typedef struct {bool network_ready;int mode;} read_pico_transfer_status_t;
static pmu_snapshot_t pmu;
static int pct;
static bool wifi,bluetooth,s_system_ttf;
static bool ttf_font_is_builtin(void){return false;}
static char signature[96];
static const pmu_snapshot_t *read_pico_pmu_get(void){return &pmu;}
// 与真实油量表一致，仅返回0..100或未知值-1。/ Match the real gauge's 0..100 or unknown -1 contract.
static int pmu_battery_percent(const pmu_snapshot_t *snapshot){(void)snapshot;return pct>=0&&pct<=100?pct:-1;}
static bool pmu_battery_charging(const pmu_snapshot_t *snapshot){(void)snapshot;return false;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *status){*status=(read_pico_transfer_status_t){wifi,READ_PICO_TRANSFER_MODE_STA};}
static bool app_settings_ble_turner(void){return bluetooth;}
static const char *app_settings_status_signature(void){return signature;}
static int letters(const char *s){int n=0;for(;*s;++s)if(((unsigned char)*s&0xc0)!=0x80)++n;return n;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_font_text_width_px(int px,const char *s){return letters(s)*px/2;}
static int ttf_text_width_px(int px,const char *s){return letters(s)*px*3/4;}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*3/4;*below=1;}
static void ttf_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*2/3;*below=px/5;}
typedef struct {int left,right,center,px,align;char text[96];} text_t;
static text_t texts[3];static int text_count,network_left;
static void draw_text(uint8_t *fb,int x,int baseline,int px,const char *s,enum EpdFontFlags align,int fg,int bg){
 (void)fb;(void)fg;(void)bg;assert(text_count<3);
 int above,below,width=s_system_ttf?ttf_text_width_px(px,s):ui_font_text_width_px(px,s);
 if(s_system_ttf)ttf_measure_line_px(px,s,&above,&below);else ui_font_measure_line_px(px,s,&above,&below);
 text_t *t=&texts[text_count++];t->left=x-(align==EPD_DRAW_ALIGN_RIGHT?width:align==EPD_DRAW_ALIGN_CENTER?width/2:0);
 t->right=t->left+width;t->center=baseline+(below-above)/2;t->px=px;t->align=align;snprintf(t->text,sizeof(t->text),"%s",s);
}
static void ui_font_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg,bool bw){(void)bw;draw_text(f,x,y,px,s,a,fg,bg);}
static void ttf_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg){draw_text(f,x,y,px,s,a,fg,bg);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;(void)gray;assert(y==40);if(icon==UI_ICON_ZAP)return;if(x-px/2<network_left)network_left=x-px/2;}
static void ui_nav_wifi_icon(uint8_t *fb,int x,int y,int px,int gray){ui_draw_icon(fb,x,y,px,UI_ICON_WIFI,gray);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){(void)fb;(void)radius;(void)gray;assert(box.y+box.height/2==40);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){(void)fb;(void)box;(void)radius;(void)gray;}
'''
status += defines + "\n"
for path, name in (("main/ui/ui_kit.c", "ui_text_fixed_context_width_px"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_width_px"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_context_vc"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_vc"), ("main/ui/ui_nav.c", "ui_nav_status")):
    status += function(path, name) + "\n"
status += r'''
int main(void){
 uint8_t fb=0;const int percentages[]={0,9,78,100,-1};
 for(int font=0;font<2;++font)for(int mask=0;mask<4;++mask)for(unsigned p=0;p<5;++p){
   s_system_ttf=font;wifi=mask&1;bluetooth=mask&2;pct=percentages[p];text_count=0;network_left=1000;
   snprintf(signature,sizeof(signature),"时间统一 · 正在阅读我的收藏书籍，保持热爱 ABCabc");
   ui_nav_status(&fb);assert(text_count==3&&texts[0].px==28&&texts[1].px==26&&texts[2].px==28);
   for(int i=0;i<3;++i)assert(texts[i].center>=39&&texts[i].center<=41);
   assert(texts[0].right+12<=texts[1].left);
   assert(texts[1].right+12<=(mask?network_left:texts[2].left));
   assert(texts[2].right==595&&texts[1].align==EPD_DRAW_ALIGN_CENTER);
   assert(texts[1].left+texts[1].right>=683&&texts[1].left+texts[1].right<=685);
   size_t length=strlen(texts[1].text);assert(length&&((unsigned char)texts[1].text[length-1]&0xc0)!=0xc0);
 }
 signature[0]=0;text_count=0;ui_nav_status(&fb);assert(text_count==2);
 puts("PASS: larger status text shares the icon centerline with builtin/custom metrics; simultaneous radios, 100% and long UTF-8 signatures do not overlap");
}
'''

with tempfile.TemporaryDirectory() as folder:
    for name, unit in (("shelf", shelf), ("status", status)):
        source, binary = Path(folder) / (name + ".c"), Path(folder) / name
        source.write_text(unit)
        # 让GCC看见内联油量表的值域；保留所有警告和检测器。/ Expose the inline gauge range to GCC; retain warnings and sanitizers.
        subprocess.run(["cc", "-std=gnu11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
