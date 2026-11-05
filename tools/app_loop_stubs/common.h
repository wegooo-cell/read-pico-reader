/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
const char* esp_err_to_name(int err);
typedef void* cst836u_handle_t;
typedef void* sc7a20h_handle_t;
#define CST836U_MAX_POINTS 2
typedef struct { bool active; uint16_t x,y; } cst836u_point_t;
typedef struct { bool touched; uint8_t count; uint16_t x,y; cst836u_point_t points[2]; uint8_t raw[15]; } cst836u_touch_t;
typedef struct { int unused; } cst836u_info_t;
int cst836u_read(void*, cst836u_touch_t*);
int cst836u_get_info(void*, cst836u_info_t*);
typedef struct { int unused; } EpdiyHighlevelState;
typedef struct { int x,y,width,height; } EpdRect;
enum EpdDrawError { EPD_DRAW_SUCCESS };
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
#define MODE_DU 1
#define MODE_GL16 2
#define MODE_GC16 3
extern int E0470_WAVEFORM;
extern int E0470_FOLLOW_WAVEFORM;
bool continuous_du_init(void);
int64_t esp_timer_get_time(void);
void vTaskDelay(int);
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
void guard_draw_result(EpdiyHighlevelState*, enum EpdDrawError);
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_full(EpdiyHighlevelState*);
enum EpdDrawError update_display_mode(EpdiyHighlevelState*, int);
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*, int);
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*);
enum {E0470_TURN_LTR,E0470_TURN_RTL};
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState*,EpdRect,int);
EpdRect epd_full_screen(void);
bool ui_nav_refresh_power(uint8_t *fb);
EpdRect ui_nav_status_area(void);
enum EpdDrawError update_display_white(EpdiyHighlevelState*);
bool display_take_white_exit(void);
void display_main_transition_arm(const void*, bool, bool);
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER};
int app_settings_main_refresh_mode(void);
void display_main_transition_disarm(void);
void display_main_transition_cancel(void);
void rails_idle_check(int64_t);
bool read_pico_pmu_ready(void);
typedef enum { READ_PICO_PMU_KEY_NONE, READ_PICO_PMU_KEY_SHORT, READ_PICO_PMU_KEY_LONG } read_pico_pmu_key_action_t;
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void);
void enter_lock_and_sleep(EpdiyHighlevelState*, int64_t*, void*, void*, bool);
void app_lock_feedback(EpdiyHighlevelState*);
void app_lock_wait_key_idle(int);
typedef enum { APP_SLEEP_SOFT, APP_SLEEP_OFF } app_sleep_mode_t;
esp_err_t app_enter_host_sleep(app_sleep_mode_t);
void app_restart_host(void);
void epd_poweroff(void);
const char* app_settings_font_path(void);
bool app_settings_reader_power_turn(void);
bool app_settings_staged_shutdown(void);
bool ttf_font_path_is_builtin(const char*);
bool ttf_font_ready(void);
bool ttf_font_is_builtin(void);
const char* ttf_font_path(void);
int ttf_font_open(const char*);
int ttf_font_open_builtin(void);
typedef struct { bool present, mounted; } read_pico_sd_info_t;
#define READ_PICO_TRANSFER_UPLOADING 3
int read_pico_sd_get_info(read_pico_sd_info_t*);
int read_pico_sd_start_probe(void);
void read_pico_sd_poll_health(void);
bool read_pico_sd_needs_recovery(void);
int read_pico_sd_start_recovery(void);

int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
void epd_fill_triangle(int,int,int,int,int,int,uint8_t,uint8_t*);

void read_pico_transfer_resume_after_sleep(void);
