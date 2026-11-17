"""本章跳转真实控件、输入、锚点与生命周期回归。/ Actual chapter controls, input, anchors and lifecycle.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
source = BOOK.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;bool user_activity;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
typedef enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_CANCEL,UI_GESTURE_TAP,UI_GESTURE_SWIPE_R,UI_GESTURE_LONG_PRESS} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;uint16_t x0,y0,x,y;} ui_gesture_event_t;
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_STATS,READER_PANEL_CHAPTER_JUMP,
      READER_PANEL_BOOKMARKS,READER_PANEL_STATS_RECENT,READER_PANEL_TOOLS};
enum EpdFontFlags {EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_RIGHT};
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
static size_t s_page=6,s_chapter=3,s_jump_page,s_jump_offset=SIZE_MAX;
static const char *s_text="body";
static char s_title[128]="第一章 山边小村";
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_reader_slider=2,s_bookmark_page;
static bool s_water_turn_pending,s_reader_turn_pending,s_reader_text_frame;
static bool s_reader_image_frame __attribute__((unused)),s_reader_image_exit_pending __attribute__((unused));
static bool s_reader_footer_pending,s_reader_image_refresh_pending,s_reader_cleanup;
static bool s_bookmark_edit,s_bookmark_delete_confirm,s_bookmark_delete_error,s_recent_days_valid;
static unsigned s_bookmark_selected,s_session_turns=12;
static int s_recent_days[30];static int64_t s_stats_activity_ms,s_session_read_ms=26*60000;
static struct {bool pending;} s_reader_tap;
static bool locked;static unsigned saves,invalidations,releases,images,flushes;
static size_t span=100,chapter_length=3200;
static size_t book_layout_page_count(void){return span?(chapter_length+span-1)/span:0;}
static size_t book_layout_page_start_offset(size_t page){assert(locked||page<book_layout_page_count());return page*span;}
static size_t book_layout_page_for_offset(size_t offset){return offset/span;}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void invalidate_prep(void){++invalidations;}
static void release_page_images(void){assert(!locked);++releases;}
static void prepare_inline_image(void){assert(!locked&&s_page<book_layout_page_count());++images;}
static void save_progress(void){assert(!locked);++saves;}
static void flush_ticket_stats(void){++flushes;}
static bool book_ticket_recent_days(int *days){(void)days;return true;}
static unsigned bookmark_count(void){return 3;}
static size_t s_part_base;
static bool s_part_more;
static unsigned percent(size_t page){return (unsigned)(page*100/(book_layout_page_count()?book_layout_page_count():1));}
static void copy_text(char *dst,size_t cap,const char *src){snprintf(dst,cap,"%s",src);}
static bool ui_rect_hit(EpdRect r,int x,int y){return r.width>0&&r.height>0&&x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static void bounds(EpdRect r){assert(r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0&&r.x+r.width<=684&&r.y+r.height<=1216);}
static void draw_sheet(uint8_t *fb,int top,const char *s){(void)fb;(void)s;assert(top>0&&top<1216);}
static void epd_draw_line(int x,int y,int x2,int y2,int gray,uint8_t *fb){(void)gray;(void)fb;assert(x>=0&&x2<684&&y>=0&&y2<1216);}
static unsigned scale=100,wide=10,glyphs;static bool s_system_ttf;static EpdRect text_box;
static bool ttf_font_is_builtin(void){return !s_system_ttf;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_text_effective_px(int px){return (px+2)*(int)scale/100;}
static int ui_text_fixed_width_px(int px,const char *s){int n=0;for(;*s;++s)if(((unsigned char)*s&0xc0)!=0x80)++n;return n*px*(int)wide/10;}
static void ttf_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*3/4;*below=px/3;}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){ttf_measure_line_px(px,s,above,below);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int cy,int px,const char *s,enum EpdFontFlags align,bool inv){
 (void)fb;(void)inv;int w=ui_text_fixed_width_px(px,s),a,b;ttf_measure_line_px(px,s,&a,&b);
 int left=align==EPD_DRAW_ALIGN_LEFT?x:align==EPD_DRAW_ALIGN_RIGHT?x-w:x-w/2;
 int base=cy+(a-b)/2;
 assert(left>=text_box.x&&left+w<=text_box.x+text_box.width&&base-a>=text_box.y&&base+b<=text_box.y+text_box.height);++glyphs;
}
static void actual_text_box(uint8_t *fb,EpdRect r,int px,const char *s,enum EpdFontFlags align,bool inverse);
static void ui_text_box(uint8_t *fb,EpdRect r,int px,const char *s,enum EpdFontFlags align,bool inverse){bounds(r);text_box=r;actual_text_box(fb,r,px,s,align,inverse);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int gray){(void)fb;(void)radius;(void)gray;bounds(r);}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int gray){ui_fill_round_rect(fb,r,radius,gray);}
static void ui_fill_rect_fast(EpdRect r,int gray,uint8_t *fb){ui_fill_round_rect(fb,r,0,gray);}
static void ui_hairline(uint8_t *fb,int y,int x,int w,int gray){ui_fill_rect_fast((EpdRect){x,y,w,1},gray,fb);}
'''
unit += source[source.index('static size_t s_chapter_jump_page;'):source.index('// 微调长按只更新预览')]
unit += function('ui_text_box', ROOT/'main/ui/ui_kit.c').replace('void ui_text_box(', 'static void actual_text_box(') + '\n'
unit += '\n#define READER_STATS_TOP 586\n#define READER_JUMP_TOP 518\n'
for name in ('reader_page_offset', 'reader_stats_control_rect', 'reader_chapter_key_rect',
             'reader_chapter_undo_rect', 'reader_chapter_undo_visible', 'reader_chapter_jump_hit',
             'draw_reader_stats', 'draw_reader_chapter_jump', 'reader_chapter_jump_begin',
             'reader_chapter_jump_parse', 'reader_chapter_jump_commit', 'reader_chapter_jump_action',
             'reader_chapter_jump_gesture'):
    unit += function(name, BOOK) + '\n'
unit += r'''
static void reset(void){
 s_text="body";s_view=READING;s_page=6;s_chapter=3;span=100;chapter_length=3200;
 s_jump_offset=SIZE_MAX;s_reader_panel=READER_PANEL_NONE;s_reader_slider=2;
 s_chapter_jump_undo.valid=false;s_chapter_jump_digits[0]=0;
 saves=invalidations=releases=images=flushes=0;s_reader_tap.pending=true;
 s_water_turn_pending=s_reader_turn_pending=s_reader_text_frame=true;
 s_reader_footer_pending=s_reader_image_refresh_pending=s_reader_cleanup=true;
}
static app_redraw_t tap(app_ctx_t *ctx,int id){
 EpdRect r=s_reader_panel==READER_PANEL_STATS?reader_stats_control_rect(id):reader_chapter_key_rect(id-20);
 return reader_chapter_jump_action(ctx,r.x+r.width/2,r.y+r.height/2);
}
int main(void){
 app_ctx_t ctx={.now_ms=1000};size_t page=123;uint8_t fb=0;
 reset();reader_chapter_jump_begin();assert(s_chapter_jump_page==6);
 for(scale=100;scale<=200;scale+=25)for(wide=10;wide<=12;wide+=2)for(int face=0;face<2;++face){
  s_system_ttf=face;draw_reader_stats(&fb);s_reader_panel=READER_PANEL_CHAPTER_JUMP;
  strcpy(s_chapter_jump_digits,"99999999999999999999999");s_chapter_jump_error=true;draw_reader_chapter_jump(&fb);
 }
 assert(glyphs>500);reader_chapter_jump_begin();
 draw_reader_stats(&fb);assert(tap(&ctx,4)==APP_REDRAW_PAGE&&s_chapter_jump_page==7&&s_page==6&&!saves);
 assert(tap(&ctx,2)==APP_REDRAW_PAGE&&s_chapter_jump_page==6);
 assert(tap(&ctx,3)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_CHAPTER_JUMP);
 assert(!strcmp(s_chapter_jump_digits,"7"));draw_reader_chapter_jump(&fb);
 assert(tap(&ctx,20)==APP_REDRAW_PAGE&&!strcmp(s_chapter_jump_digits,"1"));
 assert(tap(&ctx,27)==APP_REDRAW_PAGE&&!strcmp(s_chapter_jump_digits,"18"));
 assert(tap(&ctx,33)==APP_REDRAW_PAGE&&s_page==17&&saves==1&&releases==1&&images==1);
 assert(s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1&&!s_reader_tap.pending);
 assert(!s_water_turn_pending&&!s_reader_turn_pending&&!s_reader_text_frame&&!s_reader_footer_pending&&!s_reader_image_refresh_pending&&!s_reader_cleanup);
 assert(reader_chapter_undo_visible()&&s_chapter_jump_undo.offset==600&&ctx.user_activity);
 span=150; // 字号变化后，原600字节锚点应落到第5页。/ Reflow maps the saved anchor to page5.
 assert(reader_chapter_jump_commit(&ctx,0,true)==APP_REDRAW_PAGE&&s_page==4&&s_jump_offset==600);
 assert(!s_chapter_jump_undo.valid&&saves==2);
 reset();reader_chapter_jump_begin();tap(&ctx,3);
 const char *bad[]={"","0","33","-1","1x","18446744073709551616000","99999999999999999999999"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
  copy_text(s_chapter_jump_digits,sizeof(s_chapter_jump_digits),bad[i]);page=123;
  assert(!reader_chapter_jump_parse(&page)&&page==123);
  assert(tap(&ctx,33)==APP_REDRAW_PAGE&&s_chapter_jump_error&&s_page==6&&!saves);
 }
 copy_text(s_chapter_jump_digits,sizeof(s_chapter_jump_digits),"00032");assert(reader_chapter_jump_parse(&page)&&page==31);
 tap(&ctx,31);assert(!strcmp(s_chapter_jump_digits,"0003")&&!s_chapter_jump_error);
 tap(&ctx,29);assert(!s_chapter_jump_digits[0]);
 for(int i=0;i<50;++i)tap(&ctx,28);assert(strlen(s_chapter_jump_digits)==sizeof(s_chapter_jump_digits)-1);
 tap(&ctx,32);assert(s_reader_panel==READER_PANEL_STATS&&s_page==6&&!saves);
 s_chapter_jump_page=0;assert(tap(&ctx,2)==APP_REDRAW_NONE&&s_chapter_jump_page==0);
 s_chapter_jump_page=31;assert(tap(&ctx,4)==APP_REDRAW_NONE&&s_chapter_jump_page==31);
 assert(tap(&ctx,5)==APP_REDRAW_PAGE&&s_page==31);
 ctx.now_ms=8000;assert(reader_chapter_jump_commit(&ctx,0,true)==APP_REDRAW_NONE&&s_page==31);
 ctx.now_ms=1000;s_chapter++;assert(reader_chapter_jump_commit(&ctx,0,true)==APP_REDRAW_NONE);
 s_chapter--;s_text=NULL;assert(reader_chapter_jump_commit(&ctx,0,true)==APP_REDRAW_NONE);
 reset();span=0;s_page=0;reader_chapter_jump_begin();draw_reader_stats(&fb);assert(tap(&ctx,5)==APP_REDRAW_NONE&&!saves);
 reset();reader_chapter_jump_begin();s_chapter_jump_page=31;chapter_length=300;
 tap(&ctx,5);assert(s_page==2); // 边界以确认时的布局为准。/ Confirm against the live layout.
 reset();chapter_length=100;s_page=0;reader_chapter_jump_begin();draw_reader_stats(&fb);
 assert(tap(&ctx,2)==APP_REDRAW_NONE&&tap(&ctx,4)==APP_REDRAW_NONE);
 tap(&ctx,5);assert(s_page==0);reader_chapter_jump_begin();tap(&ctx,5);assert(!s_chapter_jump_undo.valid);
 reset();s_jump_page=6;s_jump_offset=650;reader_chapter_jump_begin();
 tap(&ctx,5);assert(saves==1&&s_chapter_jump_undo.valid&&s_chapter_jump_undo.offset==650);
 reader_chapter_jump_commit(&ctx,0,true);assert(s_jump_offset==650&&saves==2);
 reset();reader_chapter_jump_begin();tap(&ctx,6);assert(s_reader_panel==READER_PANEL_BOOKMARKS&&!saves);
 reader_chapter_jump_begin();tap(&ctx,7);assert(s_reader_panel==READER_PANEL_STATS_RECENT&&flushes==1);
 reset();reader_chapter_jump_begin();tap(&ctx,3);
 EpdRect r=reader_chapter_key_rect(0);ui_gesture_event_t ev={.x0=r.x+2,.y0=r.y+2,.x=r.x+2,.y=r.y+2};bool handled;
 for(int type=UI_GESTURE_PRESS;type<=UI_GESTURE_LONG_PRESS;++type){
  ev.type=type;if(type==UI_GESTURE_TAP)continue;
  assert(reader_chapter_jump_gesture(&ctx,&ev,&handled)==APP_REDRAW_NONE&&handled&&!strcmp(s_chapter_jump_digits,"7"));
 }
 ev.type=UI_GESTURE_TAP;ev.x+=196;
 assert(reader_chapter_jump_gesture(&ctx,&ev,&handled)==APP_REDRAW_NONE&&handled&&!strcmp(s_chapter_jump_digits,"7"));
 ev.x-=196;assert(reader_chapter_jump_gesture(&ctx,&ev,&handled)==APP_REDRAW_PAGE&&handled&&!strcmp(s_chapter_jump_digits,"1"));
 s_reader_panel=READER_PANEL_NONE;assert(reader_chapter_jump_gesture(&ctx,&ev,&handled)==APP_REDRAW_NONE&&!handled);
 for(int panel=READER_PANEL_STATS;panel<=READER_PANEL_CHAPTER_JUMP;++panel){
  s_reader_panel=panel;
  for(unsigned y=0;y<1216;y+=7)for(unsigned x=0;x<684;x+=7){int hit=reader_chapter_jump_hit(x,y);assert(hit>=-1&&hit<=33);}
 }
 puts("PASS: actual chapter UI bounds, keypad, overflow, draft/confirm, zero/one/many pages, reflow undo, stale media/chapter/time and gesture cancellation");
}
'''
# 清理与过期路径必须取消锚点并清掉完整提示区域。/ Cleanup and expiry must invalidate anchors and erase all toast pixels.
assert 's_chapter_jump_undo.valid = false;' in function('free_book', BOOK)
assert 's_chapter_jump_undo.valid = false;' in function('load_chapter_at', BOOK)
tick = function('on_tick', BOOK)
assert 'ctx->now_ms >= s_chapter_jump_undo.until_ms' in tick and '(EpdRect){96,152,492,142}' in tick
assert 'reader_chapter_jump_gesture(ctx,ev,&jump_handled)' in function('gesture_event', BOOK)
with tempfile.TemporaryDirectory(prefix='chapter-jump-') as d:
    code, binary = Path(d)/'test.c', Path(d)/'test'
    code.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer',str(code),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
