"""Execute the actual S31 three-choice power render and touch blocks together."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_device_settings.c').read_text()
# The maintained source still includes the Pico alternative; generated views omit it.
start = source.index('    if (s_page == SETTINGS_POWER_SLEEP) {', source.index('省电选项见'))
end = source.find('\n#else', start)
if end < 0:
    end = source.index('    if (s_page == SETTINGS_BOOT)', start)
paint = source[start:end]
start = source.index('    if (s_page == SETTINGS_POWER_SLEEP) {', source.index('static app_redraw_t on_gesture'))
end = source.find('\n#else', start)
if end < 0:
    end = source.index('    if (s_page == SETTINGS_CONFIG)', start)
tap = source[start:end]
assert 'SETTINGS_LOCK_SLEEP' not in source
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int x0,y0;} Event;
enum {SETTINGS_POWER_SLEEP=1,APP_REDRAW_NONE=0,APP_REDRAW_PAGE=2};
#define UI_GRAY_WHITE 255
#define EPD_DRAW_ALIGN_LEFT 0
#define UI_GRAY_BLACK 0
static int s_page=SETTINGS_POWER_SLEEP,cards,dots;
typedef enum{APP_LOCK_SLEEP_TIMED,APP_LOCK_SLEEP_OFF,APP_LOCK_SLEEP_KEEP_LIGHT}app_lock_sleep_mode_t;
static int mode;
static EpdRect boxes[3];
static int titles;
static app_lock_sleep_mode_t app_settings_lock_sleep_mode(void){return (app_lock_sleep_mode_t)mode;}
static int app_settings_set_lock_sleep_mode(app_lock_sleep_mode_t v){mode=v;return 0;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void back_header(uint8_t*f,const char*t){(void)f;assert(!strcmp(t,"关机睡眠"));}
static void section(uint8_t*f,int y,const char*t){(void)f;(void)y;(void)t;}
static void settings_card(uint8_t*f,EpdRect r,int radius,int fill,int border){
 (void)f;(void)radius;(void)border;assert(cards<3);boxes[cards]=r;
 assert(fill==((mode==cards)?0xd0:255));++cards;
}
static void ui_text(uint8_t*f,int x,int y,int px,const char*t,int align,bool bold){
 (void)f;(void)x;(void)y;(void)px;(void)align;(void)bold;
 assert(!strstr(t,"锁屏省电方式")&&!strstr(t,"保留待机"));
 if(!strcmp(t,"浅睡10分钟后关机")||!strcmp(t,"彻底断电")||!strcmp(t,"保持浅睡"))++titles;
}
static void epd_fill_circle(int x,int y,int r,int c,uint8_t*f){(void)x;(void)y;(void)r;(void)c;(void)f;++dots;}
static void settings_navigation(uint8_t*f){(void)f;}
'''
unit += function('power_sleep_choice_rect',root/'main/apps/app_device_settings.c')+'\n'
unit += 'static void paint(void){uint8_t *fb=NULL;' + paint + '}\n'
unit += 'static int tap(int x,int y){Event e={x,y};const Event *ev=&e;' + tap + 'return APP_REDRAW_NONE;}\n'
unit += r'''
int main(void){
 for(int initial=0;initial<3;initial++){
  mode=initial;cards=dots=titles=0;paint();assert(cards==3&&dots==1&&titles==3);
  // Every drawn pixel shares the same hit rectangle; gaps and the notes do nothing.
  for(int y=0;y<1216;y+=3)for(int x=0;x<684;x+=3){
   mode=initial;int target=-1;
   for(int i=0;i<3;i++)if(ui_rect_hit(boxes[i],x,y))target=i;
   int result=tap(x,y);
   assert(result==(target<0?APP_REDRAW_NONE:APP_REDRAW_PAGE));
   assert(mode==(target<0?initial:target));
  }
  mode=initial;assert(tap(300,800)==APP_REDRAW_NONE&&mode==initial);
 }
 puts("PASS: actual S31 render/touch staged10min, power-off and keep-light; identical card/hit bounds, gap/notes isolation, one selection marker");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
