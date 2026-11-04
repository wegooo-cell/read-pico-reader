"""Exercise actual paired reader sheets and native text-box fitting at all UI scales.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import json,re,subprocess,tempfile
def function(name,path):
    text=path.read_text()
    scan=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text)
    match=re.search(r'^(?:static )?(?:void|int|bool|EpdRect) '+name+r'\([^;{}]*\)\s*\{',scan,re.M)
    assert match,name
    at,depth=match.end(),1
    while depth:
        depth+=(scan[at]=='{')-(scan[at]=='}');at+=1
    return text[match.start():at]
ROOT=Path(__file__).resolve().parents[1]
book=ROOT/'main/apps/app_book.c'
kit=ROOT/'main/ui/ui_kit.c'
s=book.read_text()
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;}EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_INK_WHITE 15
#define UI_INK_BLACK 0
#define BOOK_PX_MIN 24
#define BOOK_PX_MAX 60
enum {UI_ICON_CHEVRON_RIGHT,UI_ICON_ROTATE_CW};
#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_SLOT_SYSTEM 0
static unsigned scale=100,face;
static int selected_slot=2;
static bool antialias;
static bool ttf_get_antialias(void){return antialias;}
static void ttf_set_antialias(bool on){antialias=on;}
static int ttf_font_select(int slot){int keep=selected_slot;selected_slot=slot;return keep;}
static int ttf_text_width_px(int px,const char *text);

static bool scaling=true,s_system_ttf;
static unsigned glyphs;
static EpdRect last_box, image_text_boxes[2];
static bool rule_probe;static int rule_style_label_bottom,rule_offset_label_bottom,rule_hint_bottom;
static bool image_probe;static unsigned image_text_count,image_switches;
static int s_reader_slider=-1,s_reader_preview_px=32,s_px=32;
static unsigned app_settings_system_font_size(void){return scale;}
static int ui_text_effective_px(int px){return scaling?(px+2)*(int)scale/100:px+2;}
static bool ttf_font_is_builtin(void){return face==0;}
static bool ui_font_has_text(const char *text){(void)text;return true;}
static int chars(const char *s){int n=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80)++n;return n;}
static int ui_text_fixed_width_px(int px,const char *text){return chars(text)*px*(face==2?12:9)/10;}
static void ttf_measure_line_px(int px,const char *text,int *above,int *below){(void)text;*above=px*3/4;*below=px*(face==2?5:2)/10;}
static void ui_font_measure_line_px(int px,const char *text,int *above,int *below){ttf_measure_line_px(px,text,above,below);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int cy,int px,const char *text,enum EpdFontFlags align,bool inv){
 (void)fb;(void)inv;int above,below;ttf_measure_line_px(px,text,&above,&below);
 int w=ui_text_fixed_width_px(px,text),baseline=cy+(above-below)/2;
 int left=align==EPD_DRAW_ALIGN_LEFT?x:align==EPD_DRAW_ALIGN_RIGHT?x-w:x-w/2;
 assert(left>=last_box.x&&left+w<=last_box.x+last_box.width);
 assert(baseline-above>=last_box.y&&baseline+below<=last_box.y+last_box.height);
 assert(!strchr(text,'\n'));++glyphs;
}
static int ttf_text_width_px(int px,const char *text){return ui_text_fixed_width_px(px,text);}
static void ttf_draw_text_px(uint8_t *fb,int x,int baseline,int px,const char *text,enum EpdFontFlags a,int fg,int bg){
 (void)fb;(void)a;assert(antialias&&selected_slot==0&&fg==0&&bg==15);
 int above,below;ttf_measure_line_px(px,text,&above,&below);
 assert(x>=last_box.x&&x+ttf_text_width_px(px,text)<=last_box.x+last_box.width);
 assert(baseline-above>=last_box.y&&baseline+below<=last_box.y+last_box.height);++glyphs;
}
static void epd_fill_rect(EpdRect r,int color,uint8_t *fb){(void)r;(void)color;(void)fb;}
static void ui_fill_rect_fast(EpdRect r,int color,uint8_t *fb){epd_fill_rect(r,color,fb);}
static void epd_fill_circle(int x,int y,int radius,int color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
static void epd_draw_circle(int x,int y,int radius,int color,uint8_t *fb){epd_fill_circle(x,y,radius,color,fb);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)radius;(void)color;assert(r.x>=0&&r.x+r.width<=684&&r.y>=0);
 if(image_probe&&r.width==69&&r.height==39){assert(image_text_count==2);for(unsigned i=0;i<2;++i)assert(image_text_boxes[i].y+image_text_boxes[i].height<=r.y || image_text_boxes[i].x+image_text_boxes[i].width+12<=r.x);++image_switches;}
}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int color){ui_fill_round_rect(fb,r,radius,color);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int color){(void)fb;(void)x;(void)y;(void)px;(void)icon;(void)color;}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static int app_settings_book_weight_fine(void){return 2;}
#define APP_BOOK_WEIGHT_FINE_MAX 32
static bool app_settings_book_vertical(void){return true;}
static bool app_settings_book_fonts(void){return true;}
static bool app_settings_book_antialias(void){return true;}
static unsigned rule_style=2;
static unsigned app_settings_book_reading_line(void){return rule_style;}
static bool rule_full;
static bool app_settings_book_reading_line_full(void){return rule_full;}
static int app_settings_book_reading_line_offset(void){return 0;}
static void draw_sheet_back(uint8_t *fb,int top){(void)fb;(void)top;}
static const char *ttf_font_display_name(void){return "测试字体的名称";}
static unsigned app_settings_reader_full_pages(void){return 15;}
static unsigned app_settings_reader_turn_effect(void){return 1;}
static bool app_settings_reader_vertical_turn(void){return true;}
static bool app_settings_reader_power_turn(void){return true;}
static bool app_settings_reader_default_fullscreen(void){return true;}
static bool app_settings_reader_immersive(void){return false;}
static bool app_settings_reader_hide_images(void){return false;}
'''
unit+=function('ui_text_box',kit).replace('void ui_text_box(', 'static void actual_text_box(')+'\n'
unit+=r'''
static void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inv){
 assert(box.x>=0&&box.y>=0&&box.x+box.width<=684&&box.y+box.height<=1216);
 if(image_probe){assert(image_text_count<2);image_text_boxes[image_text_count++]=box;}
 if(rule_probe){
  if(!strcmp(text,"选择样式"))rule_style_label_bottom=box.y+box.height;
  if(!strncmp(text,"上下位置",strlen("上下位置")))rule_offset_label_bottom=box.y+box.height;
  if(!strcmp(text,"选择虚线或点线后可调整位置"))rule_hint_bottom=box.y+box.height;
 }
 assert(box.width>0&&box.height>0);last_box=box;actual_text_box(fb,box,px,text,align,inv);
}
'''
unit+=function('draw_reading_font_name',book).replace('static void draw_reading_font_name(', 'static void actual_reading_font_name(')+'\n'
unit+=r'''
static void draw_reading_font_name(uint8_t *fb,EpdRect box,const char *text){
 last_box=box;int old_slot=selected_slot;bool old_aa=antialias;
 actual_reading_font_name(fb,box,text);assert(selected_slot==old_slot&&antialias==old_aa);
}
'''
unit+=function('draw_sheet',book)+'\n'
for name in ('reader_ui_extra','reader_font_top','reader_font_card','reader_slider_rect',
             'draw_pill_slider','draw_font_pair','reading_setting_rect','reading_settings_top',
             'reading_toggle_rect','draw_reading_switch','draw_reading_card','draw_reading_toggle'):
    unit+=function(name,book)+'\n'
for name in ['s_font_card','s_weight_card','s_bookfont_card','s_layout_card','s_rule_card','s_antialias_card']:
    unit+=re.search(r'^#define '+name+r' .*',s,re.M)[0]+'\n'
unit+=function('reader_panel_text_vc',book)+'\n'+function('reader_panel_text',book)+'\n'
for name in ['s_rule_offset_up','s_rule_offset_reset','s_rule_offset_down']:
    unit+=re.search(r'^static const EpdRect '+name+r' = .*',s,re.M)[0]+'\n'
for name in ['rule_style_rect','rule_span_rect','draw_rule_settings']:
    unit+=function(name,book)+'\n'
unit+=function('reader_weight_value',book)+'\n'
unit+=function('draw_font_settings',book)+'\n'+function('draw_reading_settings',book)+'\n'
unit+=function('reader_font_picker_top',book)+'\n'+function('reader_font_pick_rect',book)+'\n'
unit+='static bool s_bookmark_edit;\n'+function('bookmark_row_step',book)+'\n'+function('bookmark_rows',book)+'\n'+function('bookmark_row_rect',book)+'\n'
unit+=r'''
static bool overlap(EpdRect a,EpdRect b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
int main(void){uint8_t fb[1]={0};
 for(face=0;face<3;++face)for(scale=100;scale<=200;scale+=10){
   image_probe=KIIKO_BOARD_METALIO47;image_text_count=0;draw_reading_toggle(fb,2,"关闭书内图片","跳过正文插图，保留原书文件",true);image_probe=false;
   draw_font_settings(fb);assert(!antialias&&selected_slot==2);draw_reading_settings(fb);
   for(int style=0;style<3;++style)for(int full=0;full<2;++full){
    rule_style=style;rule_full=full;rule_probe=true;draw_rule_settings(fb);rule_probe=false;
    assert(rule_style_label_bottom<=rule_style_rect(0).y);
    assert(rule_offset_label_bottom<=s_rule_offset_up.y);
    if(!style)assert(rule_hint_bottom<1113);
   }
   EpdRect rule_controls[]={rule_style_rect(0),rule_style_rect(1),rule_style_rect(2),rule_span_rect(0),rule_span_rect(1),s_rule_offset_up,s_rule_offset_reset,s_rule_offset_down,{36,1113,612,72}};
   for(unsigned i=0;i<sizeof(rule_controls)/sizeof(rule_controls[0]);++i)for(unsigned j=0;j<i;++j)assert(!overlap(rule_controls[i],rule_controls[j]));
   for(int i=0;i<6;++i){EpdRect a=reader_font_card(i);assert(a.y>=reader_font_top()+100&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reader_font_card(j)));}
   EpdRect slider=reader_slider_rect(0);assert(slider.y>reader_font_top()+100&&slider.y+slider.height<reader_font_card(0).y);
   for(int i=0;i<9;++i){EpdRect a=reading_setting_rect(i);assert(a.y>=reading_settings_top()+100&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reading_setting_rect(j)));}
   for(int i=0;i<9;++i){EpdRect a=reader_font_pick_rect(i);assert(a.y>reader_font_picker_top()+120&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reader_font_pick_rect(j)));}
   for(int edit=0;edit<2;++edit){s_bookmark_edit=edit;assert(bookmark_rows()>=3);for(int row=0;row<bookmark_rows();++row){EpdRect a=bookmark_row_rect(row);assert(a.y+a.height<=560+(edit?520:600));if(row)assert(!overlap(a,bookmark_row_rect(row-1)));}}
 }
 assert(glyphs>500&&image_switches==(KIIKO_BOARD_METALIO47?33u:0u));
 scaling=false;scale=200;assert(ui_text_effective_px(30)==32);
 puts("PASS: 100–200% native glyph bounds for 3 font metrics, paired sheet/sliders/switches and shared hits, expanded font grid and bookmark rows; body scaling isolated");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,b=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    view=ROOT/".kiiko-source-view.json"
    boards=(int(json.loads(view.read_text())["board"]=="metalio47"),) if view.exists() else (0,1)
    for board in boards:
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}',str(c),'-o',str(b)],check=True)
        subprocess.run([str(b)],check=True)
