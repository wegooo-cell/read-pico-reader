#include "anti_fade.h"
#include "nvs.h"
/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "boot_state.h"
uint8_t app_settings_system_contrast(void) {return 100;}
void pico_boot_ready(void) {}
void app_book_cover_mode_changed(void) {}
void app_home_cover_mode_changed(void) {}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) { (void)resume; return ESP_OK; }
void pico_boot_clear_resume(void) {}
bool app_book_resume_context(char *p,size_t c,bool *f) { (void)p;(void)c;(void)f;return false; }
#include "read_pico_transfer.h"
#include "ble_page_turner.h"
#include "ui_nav_layout.h"
#include "ui_ai_menu.h"
#include "metalio_hw.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

const app_desc_t app_book = {.title="book"};
bool app_book_volume_key(app_ctx_t *ctx,int direction,app_redraw_t *redraw){(void)ctx;(void)direction;(void)redraw;return false;}

const app_desc_t app_transfer = {.title="transfer"};
static int power_open_draws, power_controls_draws, power_final_draws;
static int power_trigger_step = -1, power_tap_step=-1, shutdowns, restarts;
static bool staged_shutdown;
EpdRect ui_power_dialog_rect(void) {return (EpdRect){0,0,684,1216};}
void ui_power_dialog_draw(uint8_t* fb,const ui_power_dialog_t *dialog) {(void)fb;(void)dialog;++power_open_draws;}
void ui_power_dialog_draw_controls(uint8_t* fb,const ui_power_dialog_t *dialog,EpdRect dirty) {(void)fb;(void)dialog;(void)dirty;++power_controls_draws;}
void ui_power_dialog_end(void){}
void ui_power_final_draw(uint8_t* fb,bool restarting) {(void)fb;(void)restarting;++power_final_draws;}

static jmp_buf done;
static uint8_t frame[128],panel_marker;
static bool check_clean_action;
static unsigned clean_action_fulls;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[16];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int long_keys;
static uint8_t idle_minutes;
static bool download_session;
bool app_download_active(void) { return download_session; }
static int idle_locks, before_locks, lock_feedback_calls, lock_badges, wake_clean_requests;
static bool feedback_enabled;
static bool online_busy;
static bool ble_enabled, saved_wifi, upload_busy, click_enabled, click_active;
static read_pico_transfer_status_t transfer_status;
static int slider_draws;
static int power_change_step=-1,power_header_draws;
static bool power_reported;
static int slider_partial_draws,slider_forced_draws;
static int button_draws,button_partial_draws;
static int network_ready_step=-1,wifi_stop_step=-1;
static int status_partial_draws;
static int quick_draws, quick_opens, quick_du, wifi_starts, wifi_stops, ble_resets, click_presses, click_restores;

static int persistent_ripple_height;
static int edge_returns,back_ripples,monochrome_calls,last_ripple_dir;
static bool binary_scope,navigation_scope;
unsigned display_main_quality_set(unsigned v){(void)v;return 0;}
enum EpdDrawError update_display_main_turn(EpdiyHighlevelState*h,EpdRect r,int d){return update_display_water_turn(h,r,d);}
bool display_main_fast_background_set(bool v){(void)v;return false;}
bool display_main_switch_erase_set(bool v){(void)v;return false;}
bool display_main_shelf_home_erase_set(bool v){(void)v;return false;}
bool app_sleep_media_ready(void){return true;}
bool app_sleep_media_failed(void){return false;}
#include "online_font.h"
void online_font_status(online_font_status_t *s){s->busy=false;}
void app_sleep_discard_media_io(void){}
void book_close(void){}
void ttf_font_close_embedded(void){}
void ttf_font_unload(void){}
esp_err_t read_pico_sd_invalidate_io(void){return ESP_OK;}
bool app_font_recover_media(void){return false;}
bool app_sd_recovery_busy(void){return false;}
void read_pico_pmu_drain_events(void){}
bool display_system_navigation_set(bool enabled){bool previous=navigation_scope;navigation_scope=enabled;return previous;}
int epd_rotated_display_height(void){return 1216;}
int epd_rotated_display_width(void){return 684;}
static int binary_outputs;
static int fake_root_index=-1,fake_target_index=-1;
static int volume_step=-1,volume_value,volume_tab=-1;
bool display_system_binary_set(bool enabled){bool previous=binary_scope;binary_scope=enabled;return previous;}
const void* display_main_bw_waveform(void){static int wave;return &wave;}

static bool edge_enabled,edge_blocked;
static bool navigation_blocked(app_ctx_t*ctx){(void)ctx;return false;}
static bool edge_allowed(app_ctx_t *ctx) {(void)ctx;return edge_enabled;}
static bool keyboard_back_area(app_ctx_t *ctx,const ui_gesture_event_t *event){(void)ctx;return event->y0<560;}
static app_redraw_t edge_back_key(app_ctx_t *ctx,int key) {
    (void)ctx;assert(key==UI_KEY_1);++edge_returns;
    return edge_blocked?APP_REDRAW_NONE:APP_REDRAW_PAGE;
}
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl,EpdRect area,int dir) {(void)hl;assert(area.x==0&&area.y==0&&area.width==684&&(area.height==1216||area.height==UI_NAV_MARKER_SCAN_END));assert(dir==E0470_TURN_LTR||dir==E0470_TURN_RTL);last_ripple_dir=dir;++back_ripples;persistent_ripple_height=area.height;return EPD_DRAW_SUCCESS;}
static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second;
int ui_nav_hit(uint16_t x,uint16_t y){return y>=UI_NAV_TOUCH_TOP&&y<1216&&x<684?x/171:-1;}
void ui_nav_request(app_ctx_t *ctx,int index){volume_tab=index;ctx->request_app=index==0?&first:&second;}
static bool full_tick, lock_due, font_due, shake_activity;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static bool main_fast, main_water;
int app_settings_main_refresh_mode(void) {return main_water?APP_MAIN_REFRESH_WATER:main_fast?APP_MAIN_REFRESH_FAST:APP_MAIN_REFRESH_NORMAL;}
static int shelf_exit_arms;
static int nav_arms, nav_draws, nav_full_arms, nav_area_draws;
static const void *nav_armed;
static bool main_source_visible, main_target_visible;
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32], present_steps[32];
static int media_lost, media_ready, builtin_opens, font_opens, probes, loss_step;
int E0470_WAVEFORM;
int E0470_FOLLOW_WAVEFORM;
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    if(binary_scope)++binary_outputs;
    if(a.x==0&&a.y==0&&a.width==684&&a.height<UI_QUICK_HEIGHT){assert(w==&E0470_WAVEFORM&&m==MODE_GL16);++status_partial_draws;}
    if(a.y==12) {assert(a.x==0&&a.width==684&&a.height==56&&w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU);++power_header_draws;}
    if (a.height==UI_QUICK_HEIGHT) {assert(!"glass must not use a short differential");}
    if (a.y==1120) assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.height==48&&a.width==50);
    if(a.x==UI_QUICK_SLIDER_X(false)+2||a.x==UI_QUICK_SLIDER_X(true)+2) {
        assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.width==UI_QUICK_SLIDER_WIDTH-4&&
               a.height>0&&a.height<=236&&a.y>=106&&a.y+a.height<=342);
        ++slider_partial_draws;
    }
    if(a.width==96) {
        assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.height==96);
        assert((a.x==52||a.x==196)&&(a.y==104||a.y==236));
        ++button_partial_draws;
    }
    return update_display_area_with(h,w,m,a);
}
enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area) {
 const void *owner=nav_armed;nav_armed=NULL;
 enum EpdDrawError result=update_display_area_diff_with(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
 nav_armed=owner;return result;
}
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    if(a.height==UI_QUICK_HEIGHT){assert(a.x==0&&a.y==0&&a.width==684&&w==&E0470_WAVEFORM&&m==MODE_GL16);++quick_opens;}else {assert(a.width==UI_QUICK_SLIDER_WIDTH+4);++slider_draws;}
    if(a.width==UI_QUICK_SLIDER_WIDTH+4)++slider_forced_draws;
    return update_display_area_with(h,w,m,a);
}
enum EpdDrawError update_display_gray_transition(EpdiyHighlevelState*h,EpdRect a) {
    if(a.height==UI_QUICK_HEIGHT)++quick_opens;
    return update_display_area_with(h,&E0470_WAVEFORM,MODE_GL16,a);
}
static unsigned light_brightness=30,light_warmth=50,last_on=30;
static unsigned light_brightness_position=72,light_warmth_position=120;
static bool light_dirty;
static bool light_enabled=true;
static int light_saves,light_previews,haptics;
unsigned metalio_brightness(void) {return light_brightness;}
unsigned metalio_warmth(void) {return light_warmth;}
bool metalio_frontlight_enabled(void) {return light_enabled;}
unsigned metalio_frontlight_position(bool warmth) {return warmth?light_warmth_position:light_brightness_position;}
void metalio_frontlight_preview(unsigned b,unsigned w) {assert(b<=100&&w<=100);light_brightness=b;light_warmth=w;light_dirty=true;++light_previews;if(b)last_on=b;}
void metalio_frontlight_preview_position(unsigned b,unsigned w) {assert(b<=240&&w<=240);metalio_frontlight_preview((b*100+120)/240,(w*100+120)/240);light_brightness_position=b;light_warmth_position=w;}
void metalio_frontlight_commit(void) {if(light_dirty){++light_saves;light_dirty=false;}}
void metalio_frontlight_toggle(void) {light_enabled=!light_enabled;if(light_enabled&&!light_brightness){light_brightness=last_on;light_brightness_position=last_on*240/100;}light_dirty=true;metalio_frontlight_commit();}
esp_err_t metalio_haptic_click(void) {++haptics;return ESP_OK;}
static int ai_second_step=-1,ai_second_event;
static int ai_step=-1,ai_event,ai_saved,ai_saved_error,ai_open_calls,ai_restore_calls,ai_action_toggles;
static bool ai_oom,ai_reader;
int metalio_take_ai(void){if(step==ai_second_step){ai_second_step=-1;return ai_second_event;}if(step!=ai_step)return 0;ai_step=-1;return ai_event;}
void metalio_ai_clear(void){if(ai_step>=0&&ai_step<=step)ai_step=-1;if(ai_second_step>=0&&ai_second_step<=step)ai_second_step=-1;}
app_ai_action_t app_settings_ai_short(void){return ai_saved;}
esp_err_t app_settings_set_ai_short(app_ai_action_t action){if(ai_saved_error)return ai_saved_error;ai_saved=action;return ESP_OK;}
bool app_book_ai_bookmark(void){assert(ai_reader);++ai_action_toggles;return true;}
bool ui_ai_menu_begin(ui_ai_menu_t*m,uint8_t*fb,app_ai_action_t selected,bool reader){
    (void)reader;if(ai_oom)return false;
    *m=(ui_ai_menu_t){.open=true,.frame=fb,.rect={18,96,648,252},.selected=selected};++ai_open_calls;return true;
}
bool ui_ai_menu_restore(ui_ai_menu_t*m,uint8_t*fb){assert(m->open&&m->frame==fb);++ai_restore_calls;return true;}
void ui_ai_menu_end(ui_ai_menu_t*m){memset(m,0,sizeof(*m));}
int ui_ai_menu_feed(ui_ai_menu_t*m,const app_ctx_t*c,bool valid){
    (void)m;if(!valid||c->touch->count>1)return UI_AI_NO_ACTION;
    if(c->released)return c->touch->x==600?APP_AI_FRONTLIGHT:UI_AI_CLOSE;
    return UI_AI_NO_ACTION;
}
EpdRect ui_ai_menu_save_error(ui_ai_menu_t*m,uint8_t*f){(void)f;return m->rect;}
const void *display_main_gray_waveform(void){return &E0470_WAVEFORM;}
int metalio_take_volume(void) {if(step!=volume_step)return 0;volume_step=-1;return volume_value;}
void ui_quick_menu_draw(uint8_t *fb,bool wifi,bool bluetooth,bool enabled,unsigned b,unsigned w,bool soften_text) {assert(!soften_text);(void)fb;(void)wifi;(void)bluetooth;assert(enabled==light_enabled&&b==light_brightness_position&&w==light_warmth_position);fb[0]=0xaa;++quick_draws;}
void ui_quick_menu_draw_slider(uint8_t *fb,bool warmth,unsigned value) {
 (void)fb;unsigned expected=warmth?metalio_frontlight_position(true):ui_quick_brightness_position(metalio_frontlight_position(false));
 int raw=UI_QUICK_SLIDER_Y1-samples[step].y;if(raw<0)raw=0;if(raw>UI_QUICK_SLIDER_RANGE)raw=UI_QUICK_SLIDER_RANGE;
 assert(value==expected || (samples[step].count==1&&value==(unsigned)raw));++slider_draws;
}
static int recent_requests,recent_cancels,status_draws;
void ble_pt_reconnect_recent(void){++recent_requests;}
void ble_pt_cancel_recent(void){++recent_cancels;}
void ui_quick_menu_draw_status(uint8_t *fb){(void)fb;++status_draws;}
void ui_quick_menu_end(void){}
bool ui_quick_menu_restore(uint8_t *fb){fb[0]=0x11;return true;}
bool ui_quick_menu_draw_button(uint8_t *fb,unsigned index,bool enabled,bool pressed,EpdRect *dirty) {(void)fb;(void)pressed;assert(index<4);assert(enabled==(index==0?transfer_status.mode==READ_PICO_TRANSFER_MODE_STA&&transfer_status.state!=READ_PICO_TRANSFER_STOPPED:index==1?ble_enabled:index==3?light_enabled:false));*dirty=(EpdRect){UI_QUICK_CENTER_X(index)-48,UI_QUICK_CENTER_Y(index)-48,96,96};++button_draws;return true;}

EpdRect ui_quick_button_rect(unsigned index) {return (EpdRect){UI_QUICK_CENTER_X(index)-46,UI_QUICK_CENTER_Y(index)-46,93,93};}
EpdRect ui_quick_slider_change_rect(bool warmth,unsigned before,unsigned after) {int a=240-(int)before,b=240-(int)after,t=a<b?a:b,z=a>b?a:b;if(t<2)t=2;if(t>238)t=238;if(z<2)z=2;if(z>238)z=238;return (EpdRect){UI_QUICK_SLIDER_X(warmth)+2,104+t,112,z>t?z-t:0};}
EpdRect ui_quick_slider_rect(bool warmth) {return (EpdRect){UI_QUICK_SLIDER_X(warmth)-2,UI_QUICK_SLIDER_Y0-2,UI_QUICK_SLIDER_WIDTH+4,UI_QUICK_SLIDER_Y1-UI_QUICK_SLIDER_Y0+4};}
bool app_settings_ble_turner(void) {return ble_enabled;}
void app_settings_set_ble_turner(bool enabled) {ble_enabled=enabled;}
void ble_pt_reset_failure(void) {++ble_resets;}
bool ble_pt_pop_key(ble_pt_event_t *e) {(void)e;return false;}
bool ble_pt_pop_raw(ble_pt_raw_t *e) {(void)e;return false;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *s) {if(network_ready_step>=0&&step>=network_ready_step)transfer_status.network_ready=true;if(wifi_stop_step>=0&&step>=wifi_stop_step)transfer_status.state=READ_PICO_TRANSFER_STOPPED;*s=transfer_status;}
int read_pico_transfer_get_saved_wifi(char *ssid,bool *configured) {strcpy(ssid,"saved");*configured=saved_wifi;return ESP_OK;}
int read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) {assert(cfg->mode==READ_PICO_TRANSFER_MODE_STA&&cfg->network_only);++wifi_starts;transfer_status.mode=cfg->mode;transfer_status.state=1;return ESP_OK;}
bool read_pico_transfer_try_stop_if_idle(void) {if(upload_busy)return false;++wifi_stops;transfer_status.state=0;return true;}
static int pulse_tab=-1,pulse_requests,pulse_draws;
void ui_click_feedback_reset(void) {click_active=false;pulse_tab=-1;}
void ui_click_feedback_begin(uint8_t *fb) {(void)fb;click_active=false;}
void ui_click_feedback_nav_request(int tab) {pulse_tab=tab;++pulse_requests;}
bool ui_click_feedback_nav_finish(uint8_t *fb,EpdRect *r) {(void)fb;(void)r;pulse_tab=-1;return false;}
bool ui_click_feedback_nav_pending(void) {return pulse_tab>=0;}
bool ui_click_feedback_nav_tick(uint8_t *fb,int64_t now_ms,EpdRect *r) {
 (void)fb;(void)now_ms;
 if(pulse_tab<0)return false;
 // 首姿态只要求当前画面存在，不依赖目标页面进入。/ A first pose requires the current frame, never destination entry.
 ++pulse_draws;pulse_tab=-1;
 *r=(EpdRect){60,1120,50,48};return true;
}
bool ui_click_feedback_active(void) {return click_active;}
bool ui_click_feedback_press(uint8_t *fb,int x,int y,EpdRect *r) {(void)fb;if(!click_enabled||y!=1141)return false;(void)x;*r=(EpdRect){60,1120,50,48};click_active=true;++click_presses;return true;}
bool ui_click_feedback_release(uint8_t *fb,EpdRect *r) {(void)fb;if(!click_active)return false;click_active=false;++click_restores;*r=(EpdRect){60,1120,50,48};return true;}
bool ui_click_feedback_cancel_at(int x,int y) {return x>120||y!=1141;}
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
bool continuous_du_init(void) {return true;}
void display_request_full_refresh(void) {++wake_clean_requests;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;(void)a;panel_marker=frame[0];nav_area_draws+=nav_armed!=NULL;nav_armed=NULL;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;if(check_clean_action&&fulls){assert(frame[0]==0x11);++clean_action_fulls;}panel_marker=frame[0];fulls++;nav_full_arms+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*h) {(void)h;mode++;nav_draws+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
void display_main_transition_cancel(void) {nav_armed=NULL;}
void display_main_transition_arm(const void* owner, bool shelf_exit, bool changing_page) {(void)changing_page;display_main_transition_cancel();nav_armed=owner;++nav_arms;shelf_exit_arms+=shelf_exit;}
void display_main_transition_disarm(void) {nav_armed=NULL;}
void display_main_transition_monochrome(void) {++monochrome_calls;}
void display_monochrome_content(uint8_t* fb) {(void)fb;}
void rails_idle_check(int64_t n) {(void)n;}
bool read_pico_pmu_ready(void) {return lock_due||step==power_trigger_step||step==power_tap_step;}
esp_err_t read_pico_pmu_poll(void) {return ESP_OK;}
bool ui_nav_refresh_power(uint8_t *fb) {
    (void)fb;
    if(power_change_step<0||step<power_change_step||power_reported)return false;
    power_reported=true;return true;
}
EpdRect ui_nav_status_area(void) {return (EpdRect){0,12,684,56};}
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {return step==power_trigger_step?READ_PICO_PMU_KEY_LONG:READ_PICO_PMU_KEY_SHORT;}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a,void*tp,bool reader) {(void)tp;(void)h;(void)t;(void)a;(void)reader;assert(!nav_armed);if(check_clean_action)assert(frame[0]==0x11&&panel_marker==0x11);++idle_locks;lock_due=false;time_offset+=1000000;}
void app_lock_set_resume_ready(bool ready) {(void)ready;}
void app_lock_feedback(EpdiyHighlevelState*h) {(void)h;++lock_feedback_calls;if(feedback_enabled){assert(!before_locks&&!idle_locks);++lock_badges;}}
void app_lock_wait_key_idle(int ms) {(void)ms;}
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {assert(mode==APP_SLEEP_OFF);++shutdowns;longjmp(done,1);}
void app_restart_host(void) {++restarts;longjmp(done,1);}
void epd_poweroff(void) {}
const char* app_settings_font_path(void) {return "builtin";}
bool app_settings_reader_power_turn(void) {return false;}
bool app_settings_staged_shutdown(void) {return staged_shutdown;}
bool ttf_font_path_is_builtin(const char*p) {(void)p;return !font_due&&!saved_sd_font;}
bool ttf_font_ready(void) {return true;}
bool ttf_font_is_builtin(void) {return !sd_font;}
const char* ttf_font_path(void) {return "other";}
int ttf_font_open(const char*p) {(void)p;font_opens++;sd_font=true;return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
int read_pico_sd_get_info(read_pico_sd_info_t*i) {i->present=media_test?present_steps[step]:font_due;i->mounted=media_test?mounted_steps[step]:font_due;return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
int read_pico_sd_start_probe(void) {probes++;return ESP_ERR_NOT_FINISHED;}
void read_pico_sd_poll_health(void) {}
bool read_pico_sd_needs_recovery(void) {return false;}
int read_pico_sd_start_recovery(void) {return ESP_ERR_INVALID_STATE;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
static void ready(app_ctx_t*c) {(void)c;media_ready++;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:NULL;}
int app_index_of(const app_desc_t* app) {return app==&first?(fake_root_index>=0?fake_root_index:0):app==&second?(fake_target_index>=0?fake_target_index:1):-1;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static bool gray_list_ui(app_ctx_t*c){(void)c;return true;}
static int gray_list_outputs;
static bool gray_list_present(app_ctx_t*c,app_redraw_t redraw){
    (void)c;assert(!binary_scope);
    if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL){++gray_list_outputs;return true;}
    return false;
}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) {(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const cst836u_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t binary_tap(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    (void)ctx;return ev->type==UI_GESTURE_TAP?APP_REDRAW_PAGE:APP_REDRAW_NONE;
}
static app_redraw_t tap_return_to_root(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    main_source_visible=true;return binary_tap(ctx,ev);
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if (shake_activity && step == 0) c->user_activity = true;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static app_redraw_t consume_power(app_ctx_t*c) {(void)c;lock_due=false;return APP_REDRAW_DONE;}
static void reset(void) {
    volume_step=volume_tab=-1;volume_value=0;
    ai_second_step=-1;ai_second_event=0;ai_step=-1;ai_event=ai_saved=ai_saved_error=ai_open_calls=ai_restore_calls=ai_action_toggles=0;ai_oom=ai_reader=false;
    binary_scope=navigation_scope=false;binary_outputs=0;
    edge_returns=back_ripples=monochrome_calls=0;fake_root_index=fake_target_index=-1;edge_enabled=edge_blocked=false;
    memset(frame,0,sizeof(frame));panel_marker=0;check_clean_action=false;clean_action_fulls=0;
    anti_fade_set_minutes(0);
    download_session=false;
    light_brightness=last_on=30;light_warmth=50;light_dirty=false;light_enabled=true;light_saves=light_previews=haptics=slider_draws=0;
    slider_partial_draws=slider_forced_draws=0;
    power_change_step=-1;power_header_draws=0;power_reported=false;
    button_draws=button_partial_draws=0;network_ready_step=wifi_stop_step=-1;
    light_brightness_position=72;light_warmth_position=120;
    pulse_tab=-1;pulse_requests=pulse_draws=0;
    power_open_draws=power_controls_draws=power_final_draws=shutdowns=restarts=0;power_trigger_step=power_tap_step=-1;staged_shutdown=false;
    ble_enabled=saved_wifi=upload_busy=click_enabled=click_active=false;
    transfer_status=(read_pico_transfer_status_t){0};
    recent_cancels=status_draws=status_partial_draws=0;
    recent_requests=quick_draws=quick_opens=quick_du=wifi_starts=wifi_stops=ble_resets=click_presses=click_restores=0;
    main_fast=main_water=false;shelf_exit_arms=0;idle_minutes=0;idle_locks=before_locks=lock_feedback_calls=lock_badges=wake_clean_requests=0;feedback_enabled=false;online_busy=false;
    nav_arms=nav_draws=nav_full_arms=nav_area_draws=0;nav_armed=NULL;
    main_source_visible=main_target_visible=true;
    media_test=sd_font=saved_sd_font=false;media_lost=media_ready=builtin_opens=font_opens=probes=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));memset(present_steps,0,sizeof(present_steps));
    long_keys=0;
    home_on_touch=home_on_tick=home_on_key=start_second=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=shake_activity=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.fb=frame,.first_app=start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static bool product_menu_disabled(app_ctx_t *ctx) {(void)ctx;return false;}
static void before_lock(app_ctx_t *ctx) {(void)ctx;if(check_clean_action)assert(frame[0]==0x11&&panel_marker==0x11);++before_locks;}
static bool main_first(app_ctx_t* ctx) {(void)ctx;return main_source_visible;}
static bool main_second(app_ctx_t* ctx) {(void)ctx;return main_target_visible;}
static void enter_subview(app_ctx_t* ctx) {enter(ctx);main_target_visible=false;}
static int root_key_taps;
static bool root_key_tap(app_ctx_t *ctx,int key,app_redraw_t *r){(void)ctx;assert(key==UI_KEY_2);++root_key_taps;*r=APP_REDRAW_NONE;return true;}
static app_redraw_t root_disappears(app_ctx_t *ctx){(void)ctx;if(step==2)main_source_visible=false;return APP_REDRAW_NONE;}
static void enable_main_tabs(void) {main_fast=true;first.main_page_visible=main_first;second.main_page_visible=main_second;}
static app_redraw_t nav_return_tick(app_ctx_t* ctx) {
    if(step==0)ctx->request_app=&second;
    else if(step==1)ctx->request_return=true;
    return APP_REDRAW_NONE;
}
static void enter_with_pending_tab(app_ctx_t* ctx){enter(ctx);ctx->request_app=&second;}
static app_redraw_t main_late_tick(app_ctx_t* ctx) {(void)ctx;return step == 1 ? response : APP_REDRAW_NONE;}
// 文件目录结构切换不新增GC；显式全刷仍由原入口执行。/ File structural transitions add no GC; explicit full refresh keeps its original path.
static bool child_view;
static bool child_root(app_ctx_t *ctx){(void)ctx;return !child_view;}
static app_redraw_t child_cycle(app_ctx_t *ctx){(void)ctx;child_view=!child_view;return APP_REDRAW_PAGE;}
int main(void) {
    for(int white=0;white<2;++white)for(int binary=0;binary<2;++binary){
        reset();child_view=false;first.enter_full=true;first.persistent_navigation=true;
        first.white_child_transition=white;first.binary_ui=binary;first.main_page_visible=child_root;first.on_tick=child_cycle;
        for(int i=0;i<4;++i)add(0,0,0,0);
        run();assert(fulls==(white?1:5));
    }
    puts("PASS: actual root/child dispatch uses white page updates without extra GC for opted-in file pages; other pages retain full cleanup");

    // 第一帧报点立即切页，尚未抬手也能响应；同次停留和释放不重复。
    // The first contact switches before lift; its subsequent hold and release never repeat navigation.
    for(int policy=0;policy<3;++policy) {
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);run();assert(nav_draws==1&&enters==2&&!touch_calls&&pulse_requests==1&&pulse_draws==1);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(232,1092,1,0);add(232,1092,1,0);add(0,0,0,0);run();
        assert(nav_draws==1&&enters==2&&!touch_calls&&pulse_requests==1&&pulse_draws==1);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(0,0,0,0);add(85,1080,1,0);add(0,0,0,0);run();
        assert(nav_draws==2&&enters==3&&!touch_calls&&pulse_requests==2&&pulse_draws==2);
    }
    reset();enable_main_tabs();add(85,1080,1,0);add(0,0,0,0);run();assert(!nav_draws&&enters==1&&pulse_requests==1&&pulse_draws==1);
    reset();enable_main_tabs();first.on_gesture=gesture;add(200,1080,2,0);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();add(200,1080,1,-1);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();main_source_visible=false;first.on_gesture=gesture;
    add(200,1080,1,0);run();assert(!nav_draws&&enters==1);
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(85,1080,1,0);add(0,0,0,0);add(85,1080,1,0);add(0,0,0,0);run();
        assert(!nav_draws&&enters==1&&pulse_requests==2&&pulse_draws==2);
    }
    // 待切另一页时点击当前页也必须反馈，并取消旧待切请求。
    // Tapping the active page while another tab is pending must give feedback and replace that pending request.
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;first.on_enter=enter_with_pending_tab;
        add(85,1080,1,0);run();assert(enters==1&&!nav_draws&&pulse_requests==1&&pulse_draws==1);
    }
    puts("PASS: root tabs act on first contact in all modes, enlarged top edge, one-shot holds, rapid taps, active-tab no-op, subview/multitouch/read-error guards");
    reset();add(10,1141,1,0);
    app_ctx_t loading={.tp=(void*)1};
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1};
    assert(app_poll_main_navigation(&loading)&&loading.request_app==&first&&loading.user_activity);
    loading.request_app=NULL;samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)); // current shelf, not a cancellation
    samples[0]=(sample_t){.x=10,.y=1141,.count=2};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1,.error=-1};assert(!app_poll_main_navigation(&loading));

    // 冷封面加载的轮询中，点当前书架也能反馈，保持不取消；同一接触不重复。
    // During cold-cover polling even the current shelf responds without cancelling; one contact never repeats.
    uint8_t loading_fb=0;EpdiyHighlevelState loading_hl={0};
    loading.fb=&loading_fb;loading.hl=&loading_hl;loading.request_app=NULL;
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    int loading_pulses=pulse_draws;
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+1);
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+1);
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+2);

    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    // 开机首帧前已加载保存字体，因此首个手势无需再被延迟字体切换取消。
    // The saved font is ready before the first frame, so the first gesture is no longer cancelled.
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&font_opens==1);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[2]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1&&haptics==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 首次无卡时后插卡应触发探测，挂载完成只通知页面一次。
    // First insertion after an empty boot probes, then notifies the active page once.
    reset();media_test=true;time_step=600000;first.on_media_ready=ready;
    present_steps[1]=present_steps[2]=present_steps[3]=true;
    mounted_steps[2]=mounted_steps[3]=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(probes>=1&&media_ready==1&&!media_lost);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&!font_opens);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==1);
    reset();idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&wake_clean_requests==1); // Real scheduler flushes before automatic lock.
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&wake_clean_requests==1); /* Activity is consumed once, not latched. */
    reset();idle_minutes=0;time_step=600000000;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;online_busy=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;download_session=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    puts("PASS: real scheduler keeps host-download waiting session awake without a USB connection");
    reset();idle_minutes=1;time_step=600000000;first.holds_pmu=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=30000000;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();first.owns_keys=true;first.on_gesture=gesture;first.menu_handle_enabled=product_menu_disabled;
    add(636,1150,1,0);add(0,0,0,0);run();
    assert(!menus&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    // 二级页常驻底栏在按下即切换；阻塞任务拒绝切页，根按键语义不受影响。
    // Persistent child tabs navigate on contact, reject noninterruptible jobs and retain root-key semantics.
    reset();enable_main_tabs();main_source_visible=false;first.persistent_navigation=true;
    add(0,0,0,0);add(200,1141,1,0);add(0,0,0,0);run();assert(exits==1);
    reset();enable_main_tabs();main_source_visible=false;first.persistent_navigation=true;
    add(0,0,0,0);add(60,1141,1,0);add(0,0,0,0);run();assert(exits==1);
    reset();enable_main_tabs();main_source_visible=false;first.persistent_navigation=true;first.navigation_enabled=navigation_blocked;
    add(0,0,0,0);add(200,1141,1,0);add(0,0,0,0);run();assert(!exits);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;first.persistent_navigation=true;
        first.on_gesture=gesture;first.on_key=edge_back_key;first.edge_back_enabled=edge_allowed;edge_enabled=true;
        add(12,400,1,0);add(150,410,1,0);add(0,0,0,0);run();
        assert(back_ripples==1&&persistent_ripple_height==UI_NAV_MARKER_SCAN_END&&fulls==1);
    }

    // 首帧保持全刷；切页一次完成，按住、松开、空闲都不安排第二次刷新。
    // Retain the initial full update; finish navigation once without another draw on hold, release or idle.
    reset();enable_main_tabs();add(0,0,0,0);run();assert(!nav_arms&&!nav_draws&&!nav_full_arms);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;add(0,0,0,0);add(0,0,0,0);second.on_tick=return_tick;add(0,0,0,0);run();
    assert(nav_arms==2&&nav_draws==2&&!nav_armed&&shelf_exit_arms==1);
    // 目录延后完成重绘和同页局部更新也获得固定底栏许可，没有额外 idle 推屏。
    // Deferred directory completion and local repaint receive frozen-navigation permission without an idle redraw.
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_PAGE;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==2&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    // 子视图、非主页面保留普通出口，显式全刷优先。
    // Subviews and non-main pages retain ordinary output; explicit full refresh has priority.
    reset();enable_main_tabs();main_source_visible=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.on_enter=enter_subview;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.main_page_visible=NULL;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.enter_full=true;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(nav_arms==1&&nav_full_arms==1&&!nav_draws);
    reset();enable_main_tabs();main_fast=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!shelf_exit_arms&&!nav_full_arms);
    reset();enable_main_tabs();main_fast=false;first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_full_arms);
    reset();enable_main_tabs();lock_due=true;time_step=3000000;add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&!nav_arms&&!nav_draws);
    puts("main-tab scheduler: single immediate draw, no hold/release/idle redraw, rapid return, root/subview boundaries and full-output priority passed");
    // 跑真实边缘识别和主循环，验证不会漏给底层页面或按住重复动作。
    // Run the real edge recognizer and loop to reject underlay leakage and repeated hold actions.
    reset();first.on_gesture=gesture;
    add(300,20,1,0);add(300,110,1,0);add(300,120,1,0);add(0,0,0,0);
    add(300,500,1,0);add(0,0,0,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(quick_draws==1&&quick_opens==1&&!quick_du&&fulls==2);
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    reset();main_fast=true;saved_wifi=true;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(100,152,1,0);add(100,152,1,0);add(0,0,0,0);
    add(100,152,1,0);add(0,0,0,0);run();
    assert(wifi_starts==1&&wifi_stops==1&&quick_opens==1&&quick_draws==1&&button_draws==4&&button_partial_draws==4&&!quick_du&&!touch_calls);
    reset();upload_busy=true;transfer_status=(read_pico_transfer_status_t){.state=1,.mode=READ_PICO_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(100,152,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&transfer_status.state==1);
    assert(quick_opens==1&&button_partial_draws==2&&quick_draws==1);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(100,152,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&!touch_calls);
    assert(quick_opens==1&&button_partial_draws==2&&quick_draws==1);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(244,152,1,0);add(0,0,0,0);add(244,152,1,0);add(0,0,0,0);run();
    assert(!ble_enabled&&!ble_resets&&recent_requests==1&&recent_cancels==1&&!touch_calls);
    assert(quick_opens==1&&quick_draws==1&&button_partial_draws==4&&status_partial_draws==2&&status_draws==2&&button_draws==4);
    reset();first.on_before_lock=before_lock;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(244,284,1,0);add(0,0,0,0);add(100,284,1,0);add(0,0,0,0);run();
    assert(!idle_locks&&!before_locks&&fulls>=2&&!touch_calls&&!light_enabled&&light_brightness==30&&light_saves==1&&haptics==2);
    assert(quick_opens==1&&button_partial_draws==3&&button_draws==3);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;check_clean_action=true;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
        add(100,284,1,0);add(0,0,0,0);add(100,500,1,0);add(0,0,0,0);run();
        assert(clean_action_fulls==1&&quick_draws==1&&touch_calls==1);
    }
    // 网络ready只改变内部状态，不重画菜单；异步WiFi停止只刷WiFi圆钮。
    // Network readiness is nonvisual; an asynchronous stop refreshes only its WiFi control.
    reset();network_ready_step=4;transfer_status=(read_pico_transfer_status_t){.state=1,.mode=READ_PICO_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(quick_opens==1&&quick_draws==1&&!button_draws&&!button_partial_draws&&status_draws==1&&status_partial_draws==1);
    reset();wifi_stop_step=4;transfer_status=(read_pico_transfer_status_t){.state=1,.mode=READ_PICO_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(quick_opens==1&&quick_draws==1&&button_draws==1&&button_partial_draws==1);
    reset();light_enabled=false;light_brightness=light_brightness_position=0;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(244,284,1,0);add(0,0,0,0);run();
    assert(light_enabled&&light_brightness_position==72&&button_draws==2&&slider_draws==1&&quick_opens==1&&!slider_forced_draws);
    reset();time_step=100000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(426,296,1,0);add(426,224,1,0);add(0,0,1,0);add(0,0,0,0);run();
    assert(light_brightness==100&&light_warmth==50&&light_previews==3&&light_saves==1&&slider_draws>=2&&!touch_calls&&quick_opens==1);
    assert(slider_partial_draws==slider_draws&&!slider_forced_draws);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(572,104,1,0);add(0,0,0,9);add(0,0,0,0);run();
    assert(light_warmth==100&&light_saves==1&&!touch_calls);
    reset();light_enabled=false;time_step=100000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(426,104,1,0);add(0,0,0,0);add(572,104,1,0);add(0,0,0,0);run();
    assert(!light_enabled&&light_brightness==100&&light_warmth==100&&light_saves==2&&!touch_calls);
    assert(slider_partial_draws==2&&!slider_forced_draws);
    reset();time_step=100000;light_enabled=false;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(426,342,1,0);add(426,341,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!light_enabled&&light_brightness_position==1&&light_previews==1&&light_saves==1);
    assert(slider_draws==2&&slider_partial_draws==2&&!slider_forced_draws);
    reset();time_step=100000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(426,224,1,0);add(0,0,0,0);run();assert(light_brightness==20&&light_brightness_position==48&&light_saves==1);
    reset();time_step=100000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    // 当前50%色温不变：按下/抬起不重复绘制，更不产生GL定稿黑框。
    // Tapping unchanged warmth must not redraw or force a GL capsule settle.
    add(572,224,1,0);add(0,0,0,0);run();
    assert(!slider_partial_draws&&!slider_forced_draws&&!slider_draws&&!light_saves);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;first.on_gesture=gesture;click_enabled=true;
        add(85,1141,1,0);add(85,1141,1,0);add(0,0,0,0);run();
        assert(click_presses==1&&click_restores==1&&!click_active&&ticks==1&&du_areas==2&&!gl_areas);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;click_enabled=true;request_on_touch=true;
        add(200,1141,1,0);add(0,0,0,0);run();
        assert(!click_presses&&!click_restores&&!click_active&&du_areas==1&&!gl_areas&&nav_draws==1&&pulse_requests==1&&pulse_draws==1);
    }
    reset();click_enabled=true;add(85,1141,1,0);add(85,1141,2,0);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    reset();click_enabled=true;add(85,1141,1,0);add(0,0,0,1);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    reset();power_change_step=1;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_header_draws==1&&du_areas==1&&fulls==1&&!mode);
    reset();power_change_step=0;first.holds_pmu=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(!power_header_draws&&!power_reported);
    reset();power_change_step=0;add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(power_header_draws==1&&fulls==1);
    reset();power_change_step=2;add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!power_header_draws&&!power_reported&&quick_draws==1);
    reset();power_change_step=1;menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!power_header_draws&&!power_reported&&menus);
    puts("charging scheduler: one header-only update without full-page refresh; held touch defers; quick/menu/PMU-owner overlays suppress power redraw");
    puts("quick layer/click scheduler: modal ownership, local BW/gray, one saved-network connect/disconnect, upload/no-credential guards, BLE toggle, full/frontlight, slider save-on-release/error and release/multitouch/read-error restoration passed");
    puts("app_loop: scheduler scenarios passed, including changed-band-only sliders, no forced release settle, unchanged-value skip, automatic lock, save callback, input reset and busy guards");
    puts("quick layer/click scheduler: modal ownership, local BW/gray, one saved-network connect/disconnect, upload/no-credential guards, BLE toggle, full/lock and release/multitouch/read-error restoration passed");
    // 反馈必须早于页面保存/后台收尾；下一页与自动休眠不显示主动锁屏徽标。
    // Acknowledgement precedes saves/joins; consumed page turns and idle sleep omit it.
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;feedback_enabled=true;
        first.on_before_lock=before_lock;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();
        assert(lock_badges==1&&lock_feedback_calls==1&&before_locks==1&&idle_locks==1);
        reset();feedback_enabled=true;first.on_power_short=consume_power;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();assert(!lock_badges&&!lock_feedback_calls&&!before_locks&&!idle_locks);
        reset();feedback_enabled=true;idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
        add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&before_locks==1&&!lock_feedback_calls&&!lock_badges);
    }
    puts("PASS: manual lock acknowledgement precedes page save/reader joins in all three modes; consumed power turns and auto sleep omit the badge");
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(240,374,1,0);add(556,374,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&power_controls_draws==2&&shutdowns==1&&!restarts&&power_final_draws==1&&!touch_calls&&ticks==1&&du_areas==2&&gl_areas==1);
    reset();power_trigger_step=1;time_step=3000000;staged_shutdown=true;
    add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!idle_locks&&!wake_clean_requests&&shutdowns==1&&!restarts&&power_final_draws==1);
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_RESTART_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1);
    reset();enable_main_tabs();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_CANCEL_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!shutdowns&&!restarts&&!power_final_draws&&nav_arms==0&&mode==1);
    for(int fault=0;fault<2;++fault){
        reset();power_trigger_step=1;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(556,374,fault?2:0,fault?0:1);add(0,0,0,0);run();
        assert(!shutdowns&&!restarts&&power_controls_draws==2&&!touch_calls);
    }
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&!power_controls_draws&&!shutdowns&&!restarts);
    puts("PASS: real loop and power recognizer: DU-only contact, release commits off/staged sleep/restart once, cancel restores full navigation, touch/multitouch errors and entry-held suppression");
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==1&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=300000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&fulls==2&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;first.on_tick=root_disappears;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&!keys[1]);
    reset();time_step=30000000;for(int i=0;i<6;++i)add(0,0,0,0);run();
    int idle_baseline=wake_clean_requests;
    reset();anti_fade_set_minutes(1);time_step=30000000;for(int i=0;i<6;++i)add(0,0,0,0);run();
    assert(wake_clean_requests==idle_baseline+2&&!idle_locks&&!quick_opens);
    reset();anti_fade_set_minutes(1);download_session=true;time_step=30000000;for(int i=0;i<6;++i)add(0,0,0,0);run();
    assert(wake_clean_requests==idle_baseline&&!idle_locks);
    reset();anti_fade_set_minutes(1);first.holds_pmu=true;time_step=30000000;for(int i=0;i<6;++i)add(0,0,0,0);run();
    assert(wake_clean_requests==idle_baseline&&!idle_locks);
    // 真实主循环：内容右滑返回只推一次，多指/模态拒绝不播放。/ Actual loop: content right-swipe returns present once; multitouch/refusal never animates.
    for(int trial=0;trial<7;++trial) {
        reset();first.on_gesture=gesture;first.on_key=edge_back_key;first.edge_back_enabled=edge_allowed;
        edge_enabled=trial!=3;edge_blocked=trial==4;
        add(trial==1?80:12,trial==5?50:400,1,0);
        if(trial==2)add(40,400,2,0);
        add(trial==1?210:trial==6?60:150,trial==5?50:410,1,0);add(0,0,0,0);run();
        assert(edge_returns==(trial==0||trial==1||trial==4?1:0));
        assert(back_ripples==(trial==0||trial==1?1:0));
    }
    // 全局模式不能把点按进入子页变成水波纹；只有侧滑意图可播放。/ Global mode cannot animate child taps; only edge-return intent can start water.
    for(int policy=0;policy<3;++policy)for(int x=80;x<=560;x+=120)for(int trial=0;trial<7;++trial){
        reset();main_fast=policy==1;main_water=policy==2;first.persistent_navigation=true;
        first.on_gesture=gesture;first.on_key=edge_back_key;first.edge_back_enabled=edge_allowed;edge_enabled=true;
        int dx=trial==0?119:trial==4?(x<120?-x:-120):120,dy=trial==1?119:trial==2?-119:trial==3?120:0;
        if(trial==6)first.back_swipe_area=keyboard_back_area;
        int y=trial>=5?650:400;
        add(x,y,1,0);add(x+dx,y+dy,1,0);add(0,0,0,0);run();
        bool back=trial==1||trial==2||trial==5;
        assert(edge_returns==back&&back_ripples==back&&fulls==1);
        assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_MOVE]>=1);
        if(trial==6)assert(events[UI_GESTURE_SWIPE_R]==1);
    }
    for(int water=0;water<2;++water) {
        reset();main_water=water;first.edge_back_enabled=edge_allowed;first.on_gesture=gesture;
        edge_enabled=true;response=APP_REDRAW_PAGE;add(200,400,1,0);add(0,0,0,0);run();
        assert(back_ripples==0);
    }
    for(int tab=2;tab<=3;++tab)for(int policy=0;policy<3;++policy) {
        reset();fake_root_index=tab;first.main_page_visible=main_first;
        main_fast=policy==1;main_water=policy==2;add(0,0,0,0);run();
        assert((monochrome_calls>0)==(policy!=2));
    }
    reset();first.on_tick=origin_tick;request_on_tick=true;second.on_gesture=gesture;second.on_key=key;
    second.edge_back_enabled=edge_allowed;edge_enabled=true;home_on_key=true;
    add(0,0,0,0);add(12,400,1,0);add(150,410,1,0);add(0,0,0,0);run();assert(back_ripples==1);
    for(int policy=0;policy<3;++policy)for(int tap_back=0;tap_back<2;++tap_back){
        reset();first.binary_ui=true;main_fast=policy==1;main_water=policy==2;
        first.on_gesture=binary_tap;
        add(tap_back?60:200,tap_back?110:400,1,0);add(0,0,0,0);run();
        assert(!binary_scope&&fulls==1&&binary_outputs==1);
        assert(back_ripples==0);
    }
    reset();first.binary_ui=true;first.on_gesture=gesture;first.on_key=edge_back_key;
    first.edge_back_enabled=edge_allowed;edge_enabled=true;
    add(12,400,1,0);add(150,410,1,0);add(0,0,0,0);run();
    assert(back_ripples==1&&last_ripple_dir==E0470_TURN_LTR&&!binary_outputs&&!binary_scope);
    for(int tab=2;tab<=3;++tab)for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();second.binary_ui=true;fake_target_index=tab;
        main_fast=policy==1;main_water=policy==2;
        add(340,1160,1,0);add(0,0,0,0);run();
        assert(back_ripples==(policy==2?1:0));
        if(policy==2)assert(last_ripple_dir==E0470_TURN_RTL);
        assert(binary_outputs==(policy==2?0:1));
        assert(!binary_scope&&!navigation_scope);
    }
    for(int policy=0;policy<3;++policy){
        reset();first.binary_ui=true;fake_root_index=3;first.main_page_visible=main_first;
        main_source_visible=false;first.on_gesture=tap_return_to_root;
        main_fast=policy==1;main_water=policy==2;
        add(60,110,1,0);add(0,0,0,0);run();
        assert(!back_ripples&&binary_outputs==1&&!binary_scope&&!navigation_scope);
    }
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();second.binary_ui=true;second.grayscale_ui=gray_list_ui;
        second.present=gray_list_present;fake_target_index=3;gray_list_outputs=0;
        main_fast=policy==1;main_water=policy==2;
        add(340,1160,1,0);add(0,0,0,0);run();
        assert(gray_list_outputs==1&&!binary_outputs&&!binary_scope&&!navigation_scope);
    }
    puts("PASS: actual S31 scheduler admits gray settings presenter in all root modes without binary normalization or duplicate output");
    puts("PASS: system roots honor Water, child taps/top-left stay ordinary, edge returns use logical portrait LTR, scoped navigation restored");
    puts("PASS: S31 edge/central right-swipe return admission,one output,short/status/multitouch/refused boundaries");
    puts("PASS: real loop with real anti-fade: idle refresh, no transfer/modal-owner refresh or wake; legacy quick/PIN/sleep gates retained");
    // 实际音量事件经过主键派发：四页循环、子页映射和覆盖层/升级保护。
    // Actual volume events share root keys while retaining child mappings and overlay/update guards.
    for(int tab=0;tab<4;++tab)for(int dir=-1;dir<=1;dir+=2){
        reset();enable_main_tabs();fake_root_index=tab;volume_step=0;volume_value=dir;
        add(0,0,0,0);add(0,0,0,0);run();
        assert(volume_tab==(tab+(dir<0?3:1))%4&&!keys[0]&&!keys[2]);
    }
    for(int dir=-1;dir<=1;dir+=2){
        reset();first.owns_keys=true;volume_step=0;volume_value=dir;
        add(0,0,0,0);run();assert(keys[dir<0?0:2]==1&&volume_tab==-1);
    }
    reset();enable_main_tabs();online_busy=true;volume_step=0;volume_value=1;
    add(0,0,0,0);run();assert(volume_tab==-1&&!keys[2]);
    reset();enable_main_tabs();power_trigger_step=1;time_step=3000000;volume_step=2;volume_value=1;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();assert(power_open_draws==1&&volume_tab==-1&&!keys[2]);
    reset();enable_main_tabs();menu_on_tick=true;volume_step=1;volume_value=1;
    add(0,0,0,0);add(0,0,0,0);run();assert(volume_tab==-1&&!keys[2]);
    // 每次展开/收起仅一次区域推屏；选择只保存不执行，不穿透底页。
    // One bounded presentation per open/close; selection only persists and never leaks to the page.
    reset();ai_step=1;ai_event=METALIO_AI_LONG;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(ai_open_calls==1&&!ai_restore_calls&&gl_areas==1&&fulls==1&&ticks==1);
    reset();ai_step=1;ai_event=METALIO_AI_LONG;
    add(0,0,0,0);add(0,0,0,0);add(600,500,1,0);add(0,0,0,0);run();
    assert(ai_open_calls==1&&ai_restore_calls==1&&gl_areas==1&&ai_saved==APP_AI_FRONTLIGHT&&!light_saves&&!touch_calls&&!keys[0]&&fulls==2);
    reset();ai_step=1;ai_event=METALIO_AI_LONG;ai_saved_error=1;
    add(0,0,0,0);add(0,0,0,0);add(600,500,1,0);add(0,0,0,0);run();
    assert(ai_open_calls==1&&!ai_restore_calls&&gl_areas==2&&ai_saved==APP_AI_REFRESH&&!touch_calls);
    reset();ai_step=1;ai_event=METALIO_AI_LONG;ai_oom=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!ai_open_calls&&!gl_areas&&fulls==1);
    reset();ai_step=1;ai_event=METALIO_AI_SHORT;
    add(0,0,0,0);add(0,0,0,0);run();assert(!ai_open_calls&&fulls==2);
    reset();ai_step=1;ai_event=METALIO_AI_SHORT;ai_saved=APP_AI_FRONTLIGHT;
    add(0,0,0,0);add(0,0,0,0);run();assert(!ai_open_calls&&light_saves==1&&fulls==1&&!light_enabled);
    reset();ai_step=1;ai_event=METALIO_AI_SHORT;ai_saved=APP_AI_NONE;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks&&!before_locks&&!ai_open_calls&&!haptics&&fulls==1);
    for(int button=0;button<3;++button){
        reset();first.owns_keys=true;first.on_key=key;first.on_key_long=button==1?long_key:NULL;
        add(0,0,0,0);add(80+160*button,1500,1,0);add(80+160*button,1500,1,0);
        add(80+160*button,1500,1,0);add(0,0,0,0);run();assert(haptics==1);
    }
    for(int tab=0;tab<2;++tab){
        reset();enable_main_tabs();add(0,0,0,0);add(85+171*tab,1160,1,0);
        add(85+171*tab,1160,1,0);add(0,0,0,0);run();assert(haptics==1);
    }
    for(int trial=0;trial<4;++trial){
        reset();ai_step=1;ai_event=METALIO_AI_LONG;
        if(trial==0)online_busy=true;
        if(trial==1)download_session=true;
        if(trial==2)first.holds_pmu=true;
        if(trial==3)transfer_status.state=READ_PICO_TRANSFER_UPLOADING;
        add(0,0,0,0);add(0,0,0,0);run();assert(!ai_open_calls&&!gl_areas&&fulls==1);
    }
    for(int held=0;held<2;++held){
        reset();ai_step=1;ai_event=METALIO_AI_LONG;ai_second_step=3;ai_second_event=held?METALIO_AI_LONG:METALIO_AI_SHORT;
        for(int i=0;i<5;++i)add(0,0,0,0);run();
        assert(ai_open_calls==1&&ai_restore_calls==1&&gl_areas==1&&fulls==2);
    }
    reset();time_step=3000000;power_trigger_step=3;
    for(int i=0;i<5;++i)add(0,0,0,0);run();int power_only_updates=gl_areas;
    reset();ai_step=1;ai_event=METALIO_AI_LONG;time_step=3000000;power_trigger_step=3;
    for(int i=0;i<5;++i)add(0,0,0,0);run();
    assert(ai_open_calls==1&&ai_restore_calls==1&&gl_areas==power_only_updates+1&&power_open_draws==1);
    reset();ai_step=1;ai_event=METALIO_AI_LONG;time_step=3000000;power_tap_step=3;
    for(int i=0;i<5;++i)add(0,0,0,0);run();
    assert(ai_open_calls==1&&ai_restore_calls==1&&gl_areas==1&&fulls==2&&idle_locks==1&&!power_open_draws);
    reset();ai_step=1;ai_event=METALIO_AI_LONG;time_step=30000000;idle_minutes=1;
    for(int i=0;i<5;++i)add(0,0,0,0);run();
    assert(ai_open_calls==1&&ai_restore_calls==1&&gl_areas==1&&fulls==2&&idle_locks==1);
    puts("PASS: actual AI scheduler one open/close scan,underlay frozen,selection persists without executing,save/OOM fallback,short full/light actions and upload/update/PMU/online guards");
    puts("PASS: actual S31 volume events cycle all four roots, retain page-owned reader/child keys and reject online/power/menu input");
    puts("app_loop: 50 scheduler scenarios passed, including automatic lock, save callback, input reset and busy guards");
    return 0;
}

/* 中文：硬件 BLE 与联网任务替身。/ English: Hardware BLE and online worker shims. */
void ble_pt_sync(bool enabled) { (void)enabled; }
void ble_pt_poll(void) {}
bool pico_online_busy(void) { return online_busy; }

uint8_t app_settings_auto_lock_minutes(void){return idle_minutes;}
uint32_t ble_pt_input_serial(void){return 0;}


void crash_note_log(void){}

bool app_time_service_online_sync_pending(void) { return false; }
void app_time_service_poll(bool idle) { (void)idle; }

void ui_avatar_invalidate(void) {}
bool ui_avatar_quiesce(unsigned timeout){(void)timeout;return true;}
bool book_cover_jobs_quiesce(unsigned timeout){(void)timeout;return true;}
void book_lock_collage_invalidate(void) {}

bool ble_pt_connected(void) { return false; }

static uint8_t af_store=5;
int nvs_open(const char* space,int mode,nvs_handle_t* h){(void)space;(void)mode;*h=1;return 0;}
int nvs_get_u8(nvs_handle_t h,const char* key,uint8_t* v){(void)h;(void)key;*v=af_store;return 0;}
int nvs_set_u8(nvs_handle_t h,const char* key,uint8_t v){(void)h;(void)key;af_store=v;return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}

// 引导出口状态由独立setup测试覆盖；此组保持正常页面。/ Setup state is exercised separately; these fixtures use normal pages.
bool app_setup_pending(void){return false;}
