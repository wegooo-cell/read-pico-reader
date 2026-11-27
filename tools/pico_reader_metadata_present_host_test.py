#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际PICO推屏路由保持信息栏旧帧，页数双向变化与失败重试。
English: Exercise the real Pico presenter with independent body/metadata history and failure retries.
"""
from pathlib import Path
import ast
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'boards/pico/src/main/apps/app_book.c'
tree = ast.parse((ROOT/'tools/ui_refresh_feedback_host_test.py').read_text())
base = next(ast.literal_eval(node.value) for node in tree.body
            if isinstance(node, ast.Assign) and any(isinstance(x, ast.Name) and x.id == 'shelf' for x in node.targets))
base = base.replace('typedef struct {void *hl;', 'typedef struct {bool touched;} Touch;\ntypedef struct {bool consumed,request_return,request_menu;Touch *touch;void *hl;')
base = base.replace('static EpdRect reader_area(void){return (EpdRect){36,176,612,856};}',
                    'static bool immersive;static EpdRect reader_area(void){return (EpdRect){0,s_reader_fullscreen?(immersive?0:80):176,684,s_reader_fullscreen?(immersive?1204:1088):872};}')
base = base.replace('static EpdRect progress_rect(void){return (EpdRect){36,1040,612,42};}',
                    'static EpdRect progress_rect(void){return (EpdRect){0,s_reader_fullscreen?(immersive?1204:1168):1048,684,s_reader_fullscreen&&immersive?12:48};}')
base = base.replace('return record(WATER,MODE_GL16,area);', 'drive(area,true);return record(WATER,MODE_GL16,area);')
base = base.replace('return !test_hide_images&&test_types[s_chapter][page]?1:0;', '(void)page;return 0;')
unit = '#include <stdbool.h>\ntypedef struct {int x,y,width,height;} ModelRect;\nstatic void model_drive(int,int,int,int,bool);\n'
base = base.replace('drive(area,true);', 'model_drive(area.x,area.y,area.width,area.height,true);')
unit += base
unit += r'''
#define TT_STAMP(x) ((void)0)
static bool s_reader_dc_recover;
static enum EpdDrawError display_dc_recover_light(void *hl){(void)hl;return 0;}
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
typedef int EpdWaveform;
typedef struct {uint8_t *front_fb,*back_fb;} EpdiyHighlevelState;
enum {READER_PANEL_TOUCH_SETTINGS=80,READER_PANEL_TOUCH_MORE,BULK};
enum {EPD_ROT_PORTRAIT=1,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static bool s_rc_full_was_visible,s_shelf_key_paint_pending,s_txt_opening,s_chapter_display_pending;
#define READER_RAPID_SETTLE_MS 700
static bool s_rapid_turn_pending,s_rapid_settle_due,s_rapid_cleanup_due,s_rapid_settle_pending;
static int64_t s_turn_done_ms=-1000000;
#define reader_rapid_threshold_body(fb,area) ((void)(fb),(void)(area))
static int64_t now_ms=1000,s_reader_footer_due_ms;
static uint8_t front[684*1216/2],back[684*1216/2];
static int fail_region=-1,attempts;
static bool rc_custom_modal(void){return false;}
static bool reader_indent_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}
static void display_main_transition_cancel(void){}
static const EpdWaveform *display_main_gray_waveform(void){return &E0470_WAVEFORM;}
static const EpdWaveform *display_main_normal_waveform(void){return &E0470_WAVEFORM;}
static const EpdWaveform *display_reader_soft_turn_waveform(void){return &E0470_SOFT_TEXTTURN_WAVEFORM;}
static const EpdWaveform *display_reader_image_turn_waveform(void){return display_reader_soft_turn_waveform();}
static const EpdWaveform *display_reader_gray8_turn_waveform(void){static const int wave=8;return &wave;}
static void reader_prefetch_end(bool prep){(void)prep;}
// 低残影（fe154b3）默认关闭，本测试覆盖默认推屏路由。/ Low-ghost (fe154b3) defaults off; this test covers the default route.
static bool app_settings_low_ghost(void){return false;}
static const EpdWaveform *display_reader_white_push_waveform(void){static const int wave=9;return &wave;}
static void pico_boot_book_end(bool ok){(void)ok;}
static void ui_image_bw_rect(uint8_t *fb,EpdRect rect){(void)fb;(void)rect;}
static int epd_width(void){return 684;}
static int epd_height(void){return 1216;}
static int epd_get_rotation(void){return 0;}
static EpdRect ui_rotate_rect_to_fb(EpdRect r){return r;}
static unsigned pixel(const uint8_t *p,int x,int y){return(p[y*684/2+x/2]>>(4*(x&1)))&15;}
static void put(uint8_t *p,int x,int y,unsigned v){unsigned shift=4*(x&1);p[y*684/2+x/2]=(p[y*684/2+x/2]&~(15u<<shift))|(v<<shift);}
static void model_drive(int left,int top,int width,int height,bool full){
 EpdRect body=reader_area();
 // 水波纹与周期清理只擦正文；普通/快速翻页按fe154b3整屏一次擦白，信息栏白底同扫。局刷以物理back为起点。
 // Ripple and cleanup erase only the body; ordinary/fast turns erase the whole panel in one pass
 // (fe154b3) so the metadata whites sweep with it. Local updates start from physical back.
 const bool whole=!left&&!top&&width==UI_LOCK_WIDTH&&height==UI_LOCK_HEIGHT;
 if(full)assert((top>=body.y&&top+height<=body.y+body.height)||(whole&&s_reader_turn_pending&&!s_water_turn_pending&&!s_reader_cleanup));
 if(test_push_error)return;
 for(int y=top;y<top+height;++y)for(int x=left;x<left+width;++x)put(back,x,y,pixel(front,x,y));
}
static enum EpdDrawError update_display_reader_area(void *hl,const EpdWaveform *wave,enum EpdDrawMode mode,EpdRect a,bool full){
 assert(hl);trace_wave[trace_count]=*wave;
 int which=full?0:a.y>=progress_rect().y?2:1;
 ++attempts;if(which==fail_region)return EPD_DRAW_ERROR;
 model_drive(a.x,a.y,a.width,a.height,full);
 return record(90+which,mode,a);
}
static void draw_reader_footer(uint8_t *fb,size_t page){
 EpdRect r=progress_rect();
 for(int x=12;x<20;++x)put(fb,x,r.y+5,(page>>(x-12))&1?0:15);
}
'''
# Keep the production clock/retry deadlines while the presenter records all submitted regions.
unit = unit.replace('return 1000000;', 'return now_ms*1000;')
unit = unit.replace('static int64_t esp_timer_get_time(void)', 'static int64_t now_ms;\nstatic int64_t esp_timer_get_time(void)')
unit = unit.replace('static int64_t now_ms=1000,s_reader_footer_due_ms;', 'static int64_t s_reader_footer_due_ms;')
for name in ['reader_footer_dirty', 'present_reader_footer', 'present_reader_metadata', 'reader_metadata_tick', 'present']:
    unit += function(name, SOURCE) + '\n'
unit += r'''
static void target(unsigned page,unsigned minute){
 put(front,100,reader_area().y+20,(page&1)?0:7);
 put(front,35,reader_area().y+20,0);
 if(reader_area().y){put(front,8,8,(minute&1)?0:15);put(front,9,8,(minute&1)?15:0);put(front,20,8,0);}
 draw_reader_footer(front,page);
}
static void reset(unsigned effect,int style){
 s_reader_fullscreen=style!=0;immersive=style==2;
 memset(front,255,sizeof(front));memset(back,255,sizeof(back));
 target(42,1);memcpy(back,front,sizeof(back));s_page=43;target(43,2);
 s_view=s_presented_view=READING;s_text="text";s_reader_panel=READER_PANEL_NONE;
 s_reader_turn_pending=true;s_water_turn_pending=effect==1;s_reader_cleanup=false;s_reader_footer_pending=false;
 s_rapid_turn_pending=s_rapid_settle_due=s_rapid_cleanup_due=s_rapid_settle_pending=false;s_turn_done_ms=-1000000;
 s_mode=MODE_GL16;s_area=reader_area();test_effect=effect;test_push_error=0;fail_region=-1;
 trace_count=attempts=0;now_ms=1000;
}
int main(void){
 EpdiyHighlevelState hl={front,back};app_ctx_t ctx={.hl=&hl,.fb=front,.now_ms=1000};
 const unsigned effects[]={0,1,3};
 for(int style=0;style<3;++style)for(unsigned i=0;i<3;++i){
  reset(effects[i],style);assert(present(&ctx,APP_REDRAW_AREA));
  assert(!memcmp(front,back,sizeof(front))&&!s_reader_footer_pending);
  assert(trace_route[0]==(effects[i]==1?WATER:90));
  if(effects[i]!=1)assert(trace_wave[0]==(effects[i]==3?8:E0470_SOFT_TEXTTURN_WAVEFORM));
  for(unsigned j=1;j<trace_count;++j)assert(trace_mode[j]==MODE_DU&&trace_wave[j]==E0470_FOLLOW_WAVEFORM);
  // 页数与进度向前/向后都提交，不因白色驱动提前吞掉变化。
  // Both incrementing and decrementing page digits reach the panel.
  s_page=41;target(41,3);s_reader_turn_pending=true;s_water_turn_pending=effects[i]==1;
  trace_count=0;assert(present(&ctx,APP_REDRAW_AREA)&&!memcmp(front,back,sizeof(front)));
  reset(effects[i],style);s_reader_cleanup=true;trace_count=0;
  assert(present(&ctx,APP_REDRAW_AREA)&&trace_route[0]==90&&trace_wave[0]==E0470_FULL_WAVEFORM&&!memcmp(front,back,sizeof(front)));
  reset(effects[i],style);trace_count=0;
  assert(present(&ctx,APP_REDRAW_FULL)&&trace_count==1&&trace_route[0]==FULL);
  reset(effects[i],style);s_reader_image_refresh_pending=true;trace_count=0;
  assert(present(&ctx,APP_REDRAW_AREA)&&trace_route[0]==90&&!memcmp(front,back,sizeof(front)));
  // 正文失败不提交信息栏；顶部或底部失败保持独立重试。普通翻页已整屏同扫，信息栏独立推送只剩水波纹。
  // Body failures leave metadata pending; header/footer failures retry independently. Ordinary turns
  // now sweep the whole panel, so ripple is the route that still pushes metadata separately.
  for(int region=0;region<3;++region){
   if(region==1&&!reader_area().y)continue;
   reset(region?1:0,style);fail_region=region;assert(present(&ctx,APP_REDRAW_AREA));
   if(!region){assert(pixel(back,12,progress_rect().y+5)!=pixel(front,12,progress_rect().y+5));continue;}
   assert(s_reader_footer_pending&&s_reader_footer_due_ms==1150);
   ctx.now_ms=1149;assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE);
   fail_region=-1;ctx.now_ms=now_ms=1150;
   assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&!s_reader_footer_pending&&!memcmp(front,back,sizeof(front)));
  }
 }
 puts("PASS: actual Pico presenter ordinary/fast whole-panel and ripple body-only output, changed header/footer digits both ways, three fullscreen styles and independent failed-metadata retries");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-pico-meta-') as directory:
    temp = Path(directory)
    (temp/'test.c').write_text(unit)
    executable = temp/'test'
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
                    '-fsanitize=address,undefined',str(temp/'test.c'),'-o',str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
