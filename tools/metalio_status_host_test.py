"""Exercise production battery header visibility, unplug/replug and low-SOC bolt rendering."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#define UI_LOCK_WIDTH 684
#define UI_GRAY_WHITE 0xf0
#define UI_NAV_BACKGROUND_GRAY 0xe0
#define UI_GRAY_BLACK 0
#define UI_STATUS_TIME_PX 28
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX 26
#define UI_NAV_NETWORK_ICON_PX 28
#define UI_ICON_WIFI 1
#define UI_ICON_BLUETOOTH 2
#define UI_ICON_ZAP 3
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define EPD_DRAW_ALIGN_RIGHT 2
#define READ_PICO_TRANSFER_MODE_STA 1
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool time_synced;unsigned unix_sec;} pmu_snapshot_t;
typedef struct {bool network_ready;int mode;} read_pico_transfer_status_t;
static pmu_snapshot_t state;
static int soc=65;static bool charging,network;
static const uint8_t *s_status_framebuffer;
static int s_power_signature=-1000;
static unsigned header_clears,bolts,bolt_wells;
static uint8_t fb[684*1216/2],preview[sizeof(fb)];
static uint8_t ui_read_pixel(const uint8_t*p,int x,int y){assert(x==0&&y==0);return p[0]>>4;}
static int epd_width(void){return 1216;}static int epd_height(void){return 684;}
static const pmu_snapshot_t *read_pico_pmu_get(void){return &state;}
static int pmu_battery_percent(const pmu_snapshot_t *p){assert(p==&state);return soc;}
static bool pmu_battery_charging(const pmu_snapshot_t *p){assert(p==&state);return charging;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->network_ready=network;s->mode=1;}
static bool app_settings_ble_turner(void){return true;}
static const char *app_settings_status_signature(void){return "S31";}
static void ui_text_fixed_vc(uint8_t *p,int x,int y,int px,const char *s,int align,bool b){(void)p;(void)x;(void)px;(void)s;(void)align;(void)b;assert(y==40);}
static int ui_text_fixed_width_px(int px,const char *s){return px*(int)strlen(s)/2;}
static void ui_draw_icon(uint8_t *p,int x,int y,int size,int icon,uint8_t gray){(void)p;(void)x;(void)size;assert(y==40);if(icon==UI_ICON_ZAP){assert(gray==0xf0&&bolt_wells>0);++bolts;}}
static void ui_nav_wifi_icon(uint8_t *p,int x,int y,int size,uint8_t gray){ui_draw_icon(p,x,y,size,UI_ICON_WIFI,gray);}
static void ui_draw_round_rect(uint8_t *p,EpdRect r,int radius,uint8_t gray){(void)p;(void)radius;(void)gray;assert(r.y>=12&&r.y+r.height<=68);}
static void ui_fill_round_rect(uint8_t *p,EpdRect r,int radius,uint8_t gray){
 if(r.x==614&&r.width==26&&r.height==20){assert(gray==0);++bolt_wells;}
 ui_draw_round_rect(p,r,radius,gray);
}
static void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *p){assert(r.x==0&&r.y==12&&r.width==684&&r.height==56&&(gray==0xf0||gray==UI_NAV_BACKGROUND_GRAY));memset(p+12*342,gray|(gray>>4),56*342);++header_clears;}
void ui_nav_status(uint8_t *fb);
'''
nav=(root/'main/ui/ui_nav.c').read_text()
unit+=r'''
#define TTF_FONT_PATH_MAX 288
static const char *ttf_font_path(void){return "builtin";}
static const char *app_settings_system_font_path(void){return "builtin";}
static int ui_text_fixed_context_width_px(int px,const char *text,const char *sample){(void)sample;return ui_text_fixed_width_px(px,text);}
static bool ui_font_has_text(const char *text){(void)text;return true;}
static int ui_font_text_width_px(int px,const char *text){return ui_text_fixed_width_px(px,text);}
'''
unit+=nav[nav.index('static bool s_status_reading;'):nav.index('static const char *const labels[]')]
for name,path in [('ui_status_mark_visible' ,'main/ui/ui_kit.c'),('ui_status_visible','main/ui/ui_kit.c'),('ui_clear_page','main/ui/ui_kit.c'),('power_signature','main/ui/ui_nav.c'),('ui_nav_status_area','main/ui/ui_nav.c'),('ui_nav_refresh_power','main/ui/ui_nav.c'),('status_time','main/ui/ui_nav.c'),('clock_text','main/ui/ui_nav.c'),('ui_nav_status','main/ui/ui_nav.c')]:
 unit+=function(name,root/path)+'\n'
unit+=r'''
int main(void){
 memset(fb,0xa5,sizeof(fb));assert(!ui_nav_refresh_power(fb)&&!header_clears);
 ui_nav_status(fb);assert(ui_status_visible(fb)&&!ui_nav_refresh_power(fb));
 charging=true;network=true;assert(ui_nav_refresh_power(fb)&&header_clears==1&&bolts==1);
 for(unsigned i=68*342;i<sizeof(fb);i++)assert(fb[i]==0xa5); // body untouched
 assert(!ui_nav_refresh_power(fb)&&header_clears==1);
 charging=false;assert(ui_nav_refresh_power(fb)&&header_clears==2&&bolts==1);
 charging=true;soc=0;assert(ui_nav_refresh_power(fb)&&bolts==2); // readable bolt on an empty battery
 ui_clear_page(preview);assert(ui_status_visible(fb)); // offscreen collage cannot hide live header
 ui_clear_page(fb);soc=10;assert(!ui_nav_refresh_power(fb)&&!ui_status_visible(fb)); // immersive/image frame
 ui_nav_status(fb);assert(ui_status_visible(fb));soc=11;assert(ui_nav_refresh_power(fb));
 soc=-1;charging=false;assert(ui_nav_refresh_power(fb)&&!ui_nav_refresh_power(fb));
 fb[0]=0xee;soc=9;assert(ui_nav_refresh_power(fb)&&fb[12*342]==0xee);
 fb[0]=0xff;soc=8;assert(ui_nav_refresh_power(fb)&&fb[12*342]==0xff);
 puts("PASS: actual charge insertion/removal/SOC-change detection, zero repeated redraw, 684x56 header-only crop, low-SOC black well/white bolt outline, immersive/fullscreen suppression, offscreen preview isolation");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
