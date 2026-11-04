#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""实际用户选字体、重复回退和重启提示：不误计预览，保存失败不重启。"""
from pathlib import Path
import ast, json, subprocess, tempfile
from home_cover_cache_host_test import function
from metalio_font_job_host_support import FIXTURE,completion_wrapper
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'main/apps/app_book.c'
view=ROOT/'.kiiko-source-view.json'
# 设备视图已经裁剪平台分支；只测试实际平台，共享源码仍覆盖两机。
# Board views have already selected platform branches; test that board, or both in shared sources.
boards=(int(json.loads(view.read_text())['board']=='metalio47'),) if view.exists() else (0,1)
has_observer='static void reader_font_fallback_observe(' in SRC.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TTF_FONT_PATH_MAX 160
#define APP_REDRAW_NONE 0
#define APP_REDRAW_PAGE 1
#define APP_REDRAW_DONE 2
#define UI_GESTURE_TAP 1
#define READING 1
#define SHELF 0
#define READER_PANEL_FONT_PICKER 5
#define READER_PANEL_FONT_RECOVERY 20
typedef int app_redraw_t;typedef int esp_err_t;
typedef struct {bool consumed;} app_ctx_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int type;uint16_t x0,y0,x,y;} ui_gesture_event_t;
typedef struct {char path[160];} ttf_font_item_t;
static unsigned s_font_fallbacks;
static bool s_font_runtime_fallback_seen,recovery_mode;
static bool s_font_choice_pending,s_font_recovery_pending,s_font_restart_pending,s_font_recovery_save_failed;
static bool builtin=true,ready=true,load_fails,layout_fails,save_fails,invalidated,locked,s_save_failed;
static char active[160]="builtin",saved[160]="old.ttf",s_font_path[160],s_reader_notice[64],s_message[128];
static int s_view=READING,s_reader_panel=READER_PANEL_FONT_PICKER,s_reader_slider,s_pressed_control;
static struct {bool pending;}s_reader_tap;
static const char *s_text="text";static size_t s_text_len=4,s_page=7,s_block_count;
static int s_blocks,s_px=40;static int64_t s_reader_notice_until;
static unsigned opens,restarts,checkpoints,retries,progress_saves;
static bool ttf_font_path_is_builtin(const char *p){return !p||!*p||!strcmp(p,"builtin");}
static bool ttf_font_is_builtin(void){return builtin;}
static bool ttf_font_ready(void){return ready;}
static const char *ttf_font_path(void){return active;}
static void copy_text(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void invalidate_prep(void){invalidated=true;}
static int ttf_font_open(const char *p){++opens;
 if(load_fails&&strcmp(p,"builtin")){strcpy(active,"builtin");builtin=true;return -1;}
 copy_text(active,sizeof(active),p);builtin=ttf_font_path_is_builtin(p);return ESP_OK;}
static size_t book_layout_page_start_offset(size_t page){(void)page;return 1234;}
static int body_rect(void){return 0;}
static bool book_layout_build_blocks(const char *t,size_t n,int b,size_t c,int rect,int px){
 (void)t;(void)n;(void)b;(void)c;(void)rect;(void)px;return !layout_fails;}
static size_t book_layout_page_for_offset(size_t off){assert(off==1234);return 7;}
static void app_settings_set_font_path(const char *p){copy_text(saved,sizeof(saved),p);}
static const char *app_settings_font_path(void){return saved;}
static void save_progress(void){++progress_saves;}
static void free_book(void){s_text=NULL;}
static bool pico_boot_recovery(void){return recovery_mode;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void before_lock(app_ctx_t *ctx){(void)ctx;++checkpoints;invalidate_prep();s_save_failed=save_fails;}
static void retry_progress(void){++retries;s_save_failed=save_fails;}
static void app_restart_host(void){assert(checkpoints&&retries&&!s_save_failed);++restarts;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
'''
unit+='#if KIIKO_BOARD_METALIO47\n#define FONT_STAGE_PUBLISHED() (builtin=ttf_font_path_is_builtin(active))\n'+FIXTURE+'\n#endif\n'
for name in ('reader_font_choice_begin','reader_font_choice_finish','reader_font_recovery_reset',
 'reader_font_recovery_button','reader_font_recovery_gesture','reader_font_recovery_tick'):
    unit+=(('#if KIIKO_BOARD_METALIO47\n'+function('reader_font_fallback_observe',SRC)+'\n#endif\n') if name=='reader_font_recovery_tick' and has_observer else '')+function(name,SRC)+'\n'
unit+='\n/* INSERT_SELECTION */\n'
unit+=r'''
static void reset(void){reader_font_recovery_reset();recovery_mode=false;builtin=ready=true;load_fails=layout_fails=save_fails=s_save_failed=false;
 s_view=READING;s_reader_panel=READER_PANEL_FONT_PICKER;s_text="text";s_page=7;locked=invalidated=false;
 opens=restarts=checkpoints=retries=progress_saves=0;strcpy(active,"builtin");strcpy(saved,"old.ttf");}
static ui_gesture_event_t tap(int index){EpdRect b=reader_font_recovery_button(index);return (ui_gesture_event_t){UI_GESTURE_TAP,b.x+20,b.y+20,b.x+20,b.y+20};}
int main(void){app_ctx_t ctx={0};ttf_font_item_t item={"new.ttf"};
 reset();assert(select_reading_font_item(&ctx,NULL)==APP_REDRAW_NONE&&!s_font_choice_pending);
 // 真实选择连续失败，3次才提示；同一次选择被绘图/轮询反复观察也只记一次。
 // Real consecutive failures require three choices; repeated render/poll observations count once.
 load_fails=true;
 for(unsigned i=1;i<=3;++i){assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
  assert(s_font_fallbacks==i&&!locked&&builtin);
  for(int j=0;j<100;++j)reader_font_choice_finish();assert(s_font_fallbacks==i);
  assert(s_font_recovery_pending==(i==3));}
 assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_FONT_RECOVERY);
 assert(!s_font_recovery_pending&&!restarts);
 for(int i=0;i<50;++i)assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE&&!restarts);
 // 滑动、取消及跨按钮抬起不能重启；只有一次完整点按排队到正常tick。
 // Swipes/cancels/cross-button releases never restart; a complete tap queues the regular tick.
 ui_gesture_event_t ev=tap(1);ev.type=2;assert(reader_font_recovery_gesture(&ctx,&ev)==APP_REDRAW_NONE);
 ev=tap(1);ev.x=20;assert(reader_font_recovery_gesture(&ctx,&ev)==APP_REDRAW_NONE&&!s_font_restart_pending);
 ev=tap(0);assert(reader_font_recovery_gesture(&ctx,&ev)==APP_REDRAW_PAGE);
 assert(!s_font_fallbacks&&s_reader_panel==READER_PANEL_FONT_PICKER&&!restarts);
#if KIIKO_BOARD_METALIO47
 for(int poll=0;poll<100;++poll)assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE&&!s_font_fallbacks);
#endif
 // 用户主动选内建、一次真正成功，均清零；没有主动选择的预览失败不计数。
 // Explicit built-in and genuine success reset; preview failures without a choice do not count.
 reset();s_font_fallbacks=2;ttf_font_item_t built={"builtin"};
 (void)select_reading_font_item(&ctx,&built);assert(!s_font_fallbacks);
 s_font_fallbacks=2;strcpy(active,"old.ttf");builtin=false;
 assert(select_reading_font_item(&ctx,&built)==APP_REDRAW_PAGE);reader_font_choice_finish();assert(!s_font_fallbacks&&!s_font_choice_pending&&!s_font_recovery_pending);
 reset();s_font_fallbacks=2;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(s_font_choice_pending);reader_font_choice_finish();assert(!s_font_recovery_pending&&!builtin);
#if KIIKO_BOARD_METALIO47
 assert(s_font_fallbacks==2);
#else
 assert(!s_font_fallbacks);
#endif
 unsigned count_after_success=s_font_fallbacks;
 builtin=true;for(int i=0;i<50;++i)reader_font_choice_finish();assert(s_font_fallbacks==count_after_success);
 // 选择加载成功，但名称预览归还时掉回内建，同样准确统计。
 // A successful load followed by built-in restoration from preview is still an actual fallback.
 reset();for(int i=0;i<3;++i){strcpy(active,"builtin");builtin=true;
  assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);strcpy(active,"builtin");builtin=true;reader_font_choice_finish();}
 assert(s_font_recovery_pending);assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_PAGE);
 ev=tap(1);assert(reader_font_recovery_gesture(&ctx,&ev)==APP_REDRAW_PAGE&&!restarts);
 ctx.consumed=true;assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE&&!checkpoints);
 ctx.consumed=false;save_fails=true;assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_PAGE);
 assert(s_font_recovery_save_failed&&!restarts&&checkpoints==1&&retries==1&&!s_font_restart_pending);
 save_fails=false;assert(reader_font_recovery_gesture(&ctx,&ev)==APP_REDRAW_PAGE);
 assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_DONE&&restarts==1&&checkpoints==2&&retries==2);
 // 离开书籍/关书重置；普通轮询、系统字体与后台恢复不累计。
 // Closing/leaving the book resets; ordinary polling, system faces and retries never accumulate.
 reader_font_recovery_reset();assert(!s_font_recovery_pending&&!s_font_fallbacks&&!s_font_restart_pending);
 s_view=SHELF;s_font_recovery_pending=true;assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE);
#if KIIKO_BOARD_METALIO47
 // 自动/唤醒回退同次开书累计；重复tick不多计，暂时恢复不丢失计数。
 // Count runtime/wake episodes per open book, deduplicating ticks and retaining counts across temporary recovery.
 reset();
 for(unsigned episode=1;episode<=3;++episode){
  builtin=false;strcpy(active,"old.ttf");assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE);
  builtin=true;strcpy(active,"builtin");
  assert(reader_font_recovery_tick(&ctx)==(episode==3?APP_REDRAW_PAGE:APP_REDRAW_NONE));
  for(int poll=0;poll<100;++poll)assert(reader_font_recovery_tick(&ctx)==APP_REDRAW_NONE);
  assert(s_font_fallbacks==episode&&!restarts);
 }
 assert(s_reader_panel==READER_PANEL_FONT_RECOVERY);
 // 异常启动保护和主动内建都不计入，缺字库尚未就绪也不误报。
 // Exclude abnormal-start protection, explicit built-in and incomplete font setup.
 reset();recovery_mode=true;for(int i=0;i<100;++i)reader_font_recovery_tick(&ctx);assert(!s_font_fallbacks);
 reset();strcpy(saved,"builtin");for(int i=0;i<100;++i)reader_font_recovery_tick(&ctx);assert(!s_font_fallbacks);
 reset();ready=false;reader_font_recovery_tick(&ctx);assert(!s_font_fallbacks);
 reset();load_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);reader_font_recovery_tick(&ctx);assert(s_font_fallbacks==1);
#endif
 puts("PASS: real user font choices require 3 actual built-in fallbacks; successful/built-in/reset paths, preview deduplication, release-confirmed cancel/restart, deferred checkpoint and failed-save retry");}
'''
# Integration points matter: finishing before previews restore would miscount a later fallback.
s=SRC.read_text()
picker=s[s.index('static void draw_font_picker('):s.index('static EpdRect reader_font_recovery_button(')]
assert picker.index('reader_font_choice_finish();')>picker.index('ttf_font_draw_preview' if 'ttf_font_draw_preview' in picker else 'if (active_builtin)')
assert 'reader_font_recovery_reset();' in function('free_book',SRC)
assert function('gesture_event',SRC).index('reader_font_recovery_gesture')<function('gesture_event',SRC).index('image_zoom_gesture')
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)
 selection=s[s.index('static app_redraw_t select_reading_font_item('):s.index('static int slider_index(')]
 if 'static void reader_font_worker(' in s:
  selection='#if KIIKO_BOARD_METALIO47\n'+function('reader_font_worker',SRC)+'\n'+function('reader_font_reload_cancel',SRC)+'\n#endif\n'+selection
 wrapper=completion_wrapper().replace('assert(!locked);run_worker();', 'assert(!locked);unsigned before=s_font_fallbacks;reader_font_choice_finish();assert(s_font_fallbacks==before);run_worker();')
 selection+='\n#if KIIKO_BOARD_METALIO47\n'+wrapper+'\n#else\nstatic app_redraw_t select_and_complete(app_ctx_t*c,const ttf_font_item_t*i){return select_reading_font_item(c,i);}\n#endif\n'
 unit=unit.replace('assert(select_reading_font_item(', 'assert(select_and_complete(').replace('(void)select_reading_font_item(', '(void)select_and_complete(')
 for board in boards:
  selected=subprocess.run(['cc','-E','-P','-x','c',f'-DKIIKO_BOARD_METALIO47={board}','-'],input=selection,text=True,capture_output=True,check=True).stdout
  (p/'test.c').write_text(unit.replace('/* INSERT_SELECTION */',selected))
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

# Reuse the established text-box boundary probe with the actual modal and 100–200% UI sizes.
chapter_test=ast.parse((ROOT/'tools/reader_chapter_jump_host_test.py').read_text())
base=next(ast.literal_eval(n.value) for n in chapter_test.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='unit' for t in n.targets))
geometry=base+r"""
static bool s_font_recovery_save_failed;
static void ui_text_set_system_scale(bool enabled){(void)enabled;}
static EpdRect ui_inset_rect(EpdRect r,int n){r.x+=n;r.y+=n;r.width-=2*n;r.height-=2*n;return r;}
"""
geometry+=function('ui_text_box',ROOT/'main/ui/ui_kit.c').replace('void ui_text_box(', 'static void actual_text_box(')+'\n'
geometry+=function('reader_font_recovery_button',SRC)+'\n'+function('draw_font_recovery',SRC)+'\n'
geometry+=r"""
int main(void){uint8_t fb=0;for(scale=100;scale<=200;scale+=25)for(wide=10;wide<=13;++wide)for(int face=0;face<2;++face)for(int failed=0;failed<2;++failed){s_system_ttf=face;s_font_recovery_save_failed=failed;draw_font_recovery(&fb);}assert(glyphs>400);puts("PASS: actual restart dialog labels/buttons fit at 100-200% system size, both font paths and failed-save state");}
"""
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(geometry)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
