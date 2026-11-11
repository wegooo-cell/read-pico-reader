"""中文：真实书架绘图与整页缓存的像素等价和实时信息回归。
English: Actual shelf rendering/cache pixel equivalence and live metadata regressions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from home_cover_cache_host_test import function
from shelf_page_cache_host_test import STUBS, UNIT
ROOT=Path(__file__).resolve().parents[1]

PREFIX=r'''
#include <math.h>
#include "ui_image_dither.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_MARGIN 36
#define UI_GAP 20
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_STRIDE 21
static unsigned pixel_calls,cover_draws,furniture_draws;
#define display_rotation rotation
static enum EpdRotation rotation=EPD_ROT_PORTRAIT;
static struct{int width,height;} host_display;
static typeof(host_display)*display=&host_display;
int epd_width(void){return rotation==1||rotation==3?1216:684;}
int epd_height(void){return rotation==1||rotation==3?684:1216;}
int epd_rotated_display_width(void){return UI_LOCK_WIDTH;}
int epd_rotated_display_height(void){return UI_LOCK_HEIGHT;}
enum EpdRotation epd_get_rotation(void){return rotation;}
typedef struct {uint16_t x,y;} Coord_xy;
#define _swap_int(a,b) {int temporary=a;a=b;b=temporary;}
static int style=2,refresh_mode,contrast=100;
static int app_settings_shelf_style(void){return style;}
static bool badges=true;static bool app_settings_shelf_progress_badges(void){return badges;}
static int ttf_ascender_px(int px){return px;}
static bool ttf_text_mask_px(uint8_t*m,unsigned w,unsigned h,int x,int y,int px,const char*t){(void)x;(void)y;(void)px;(void)t;memset(m,0,w*h);for(unsigned j=2;j<h-2;++j)for(unsigned i=2;i<30;++i)m[j*w+i]=255;return true;}
static bool app_settings_main_fast_refresh(void){return refresh_mode==1;}
static int app_settings_system_contrast(void){return contrast;}
'''
UI=r'''
enum{SHELF,BULK};static int s_view=SHELF,s_pressed_control=-1,s_visible_count;
static bool s_bulk_grid,s_shelf_page_cached,s_shelf_page_loading,s_shelf_page_cache_allowed,s_scan_pending;
static unsigned s_cover_pending_mask;
typedef struct{int leaf;int64_t now_ms;uint8_t *fb;} app_ctx_t;
typedef struct{char name[256],author[128];bool removed,favorite,has_progress;unsigned pct;uint16_t chapter,chapters;}shelf_entry_t;
static shelf_entry_t s_shelf[9];
static struct{uint8_t *gray,*fast_bits;unsigned width,height;uint8_t fast_phase,fast_contrast;bool fast_white_edge;}s_covers[9];
static int shelf_rows(void){return style==5?8:9;}
static int ui_content_width(void){return 612;}
static void copy_text(char *d,size_t n,const char*s){snprintf(d,n,"%s",s);}
static int ttf_em_height_px(int px){return px;}
static int ui_text_fixed_width_px(int px,const char*s){return (int)strlen(s)*px/2;}
static void fit_text(char*s,int px,int w){while(*s&&ui_text_fixed_width_px(px,s)>w)s[strlen(s)-1]=0;}
static void text(uint8_t *fb,int x,int y,int px,const char*s,uint8_t ink){for(unsigned i=0;s[i]&&i<24;++i)ui_fill_rect_fast((EpdRect){x+(int)i*px/2,y-px/2,3+(s[i]&7),px/2},ink,fb);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char*s,int align,bool inverse){if(align==EPD_DRAW_ALIGN_RIGHT)x-=ui_text_fixed_width_px(px,s);text(fb,x,y,px,s,inverse?255:0);}
static void ui_text_fixed_ink_vc(uint8_t *fb,int x,int y,int px,const char*s,int align,uint8_t ink){if(align==EPD_DRAW_ALIGN_RIGHT)x-=ui_text_fixed_width_px(px,s);text(fb,x,y,px,s,ink);}
#define ui_text_vc ui_text_fixed_vc
// 独立的挡板测试验证真实玻璃；此处用确定的取样挡板检测预遮挡缓存。
// Actual glass is covered separately; sampled rails here expose pre-furniture cache errors.
static void draw_shelf_guard(uint8_t *fb,EpdRect r,int slot){
    ++furniture_draws;for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x){
        EpdRect p=ui_rotate_rect_to_fb((EpdRect){x,y,1,1});unsigned shade=pixel(fb,p.x,p.y,epd_width());
        uint8_t v=(uint8_t)((shade*9+slot*7+(style==2?35:70))&240);
        if(app_settings_main_fast_refresh())v=ui_image_dither_cover_bw(v,x,y);
        epd_draw_pixel(x,y,v,fb);
    }
}
#if KIIKO_BOARD_METALIO47
typedef struct{uint64_t words[8];} metalio_cover_key_t;
static bool display_pipeline_enabled(void){return false;}
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *p,size_t n){return seed^checksum(p,n);}
static bool metalio_cover_cache_get_rows(const metalio_cover_key_t*k,uint8_t*p,int stride,int span,int rows){(void)k;(void)p;(void)stride;(void)span;(void)rows;return false;}
static bool metalio_cover_cache_put_rows(const metalio_cover_key_t*k,const uint8_t*p,int stride,int span,int rows){(void)k;(void)p;(void)stride;(void)span;(void)rows;return false;}
#endif
'''
MAIN=r'''
static void seed(void){
 for(int row=0;row<9;++row){
  free(s_covers[row].gray);free(s_covers[row].fast_bits);s_covers[row].gray=malloc(176*240);assert(s_covers[row].gray);
  s_covers[row].fast_bits=calloc(214,21);assert(s_covers[row].fast_bits);
  s_covers[row].width=176;s_covers[row].height=240;s_covers[row].fast_contrast=contrast;
  EpdRect image=shelf_cover_image(row);s_covers[row].fast_phase=(image.x&3)|((image.y&3)<<2);
  for(unsigned y=0;y<240;++y)for(unsigned x=0;x<176;++x)s_covers[row].gray[y*176+x]=(uint8_t)(x*31+y*7+row*61);
  for(unsigned y=0;y<214;++y)for(unsigned x=0;x<164;++x)if(((x*17+y*31+row*61)%7)>2)s_covers[row].fast_bits[y*21+x/8]|=1u<<(x&7);
  s_covers[row].fast_white_edge=row&1;
 }
}
static void paint(app_ctx_t *ctx,uint8_t *fb){memset(fb,0xff,684*1216/2);ui_fill_rect_fast((EpdRect){0,0,684,1096},0xf0,fb);draw_shelf_page_artwork(ctx,fb);}
int main(void){
 const size_t bytes=684*1216/2;uint8_t *reference=malloc(bytes),*cached=malloc(bytes);assert(reference&&cached);
 app_ctx_t ctx={.now_ms=100,.fb=cached};unsigned cases=0;
 for(style=0;style<=6;++style)for(refresh_mode=0;refresh_mode<3;++refresh_mode)for(int rot=0;rot<4;++rot){
  rotation=rot;display->width=epd_width();display->height=epd_height();s_visible_count=shelf_rows();s_cover_pending_mask=0;s_pressed_control=-1;
  for(int row=0;row<9;++row){snprintf(s_shelf[row].name,256,"Book %u",row);strcpy(s_shelf[row].author,"Author");s_shelf[row].favorite=row%3==0;s_shelf[row].has_progress=true;s_shelf[row].pct=row==8?100:row*9;}
  seed();book_shelf_page_release();s_shelf_page_cached=s_shelf_page_loading=s_shelf_page_cache_allowed=false;
  paint(&ctx,reference);
  book_shelf_page_layout_t layout={.area=ui_rotate_rect_to_fb((EpdRect){0,160,684,832}),.width=epd_width(),.height=epd_height(),.count=shelf_rows()};
  for(unsigned row=0;row<layout.count;++row)layout.covers[row]=ui_rotate_rect_to_fb(shelf_cover_image(row));
  assert(book_shelf_page_request(key(++cases),layout)==BOOK_SHELF_PAGE_WAIT);assert(wait_page()==BOOK_SHELF_PAGE_MISS);
  s_shelf_page_cache_allowed=true;unsigned before_io=opens;paint(&ctx,cached);
  assert(s_shelf_page_cached&&!memcmp(reference,cached,bytes)&&opens==before_io);
  for(int row=0;row<9;++row){free(s_covers[row].gray);free(s_covers[row].fast_bits);s_covers[row].gray=s_covers[row].fast_bits=NULL;}
  cover_draws=furniture_draws=0;paint(&ctx,cached);assert(!memcmp(reference,cached,bytes)&&!cover_draws&&!furniture_draws&&opens==before_io);
  // 进度/收藏不冻结在成品里，冷命中也不依赖单本像素。
  // Progress/favorites remain live and cold hits need no per-book raster buffers.
  s_shelf[0].chapter=11;s_shelf[0].chapters=120;s_shelf[0].pct=97;s_shelf[0].favorite=false;s_shelf[shelf_rows()-1].favorite=true;paint(&ctx,cached);
  s_shelf_page_cached=false;s_shelf_page_cache_allowed=false;seed();paint(&ctx,reference);assert(!memcmp(reference,cached,bytes));
  if(style==1||style==2||style==6){badges=false;paint(&ctx,cached);assert(memcmp(reference,cached,bytes));badges=true;paint(&ctx,cached);assert(!memcmp(reference,cached,bytes));}
  for(int pressed=0;pressed<2;++pressed){
   s_pressed_control=pressed?shelf_rows()-1:0;paint(&ctx,reference);
   for(int row=0;row<9;++row){free(s_covers[row].gray);free(s_covers[row].fast_bits);s_covers[row].gray=s_covers[row].fast_bits=NULL;}
   s_shelf_page_cached=true;paint(&ctx,cached);assert(!memcmp(reference,cached,bytes));
   s_shelf_page_cached=false;seed();
  }
  s_pressed_control=-1;assert(opens==before_io);assert(held<=PAGE_BUDGET);
 }
 for(int row=0;row<9;++row){free(s_covers[row].gray);free(s_covers[row].fast_bits);}
 book_shelf_page_release();join_threads();assert(!held);free(reference);free(cached);
 printf("PASS: %u real shelf layer cases (7 styles x3 modes x4 rotations), exact cached/live pixels, unobscured lift, zero cover/furniture work and zero file IO on warm redraws\n",cases);
}
'''


KEY_UNIT=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "book_shelf_page_cache.h"
#define UI_LOCK_WIDTH 684
static int style=1,mode,contrast=100,rotation=1,font_size=100,order,rows=9;
static int s_visible_count=18,s_category_selected,s_request_count,stats;
static bool s_shelf_page_key_valid,s_shelf_page_loading,s_shelf_page_cached,s_shelf_page_cache_allowed,missing;
static book_shelf_page_key_t s_shelf_page_base,requested;
static int64_t modified=100,book_bytes=200;
static char s_query[65],font[128];
typedef struct{int leaf;}app_ctx_t;
typedef struct{char path[288],name[256],author[128];bool removed,favorite;unsigned pct;}shelf_entry_t;
static shelf_entry_t s_shelf[18];
static struct{int index;}s_covers[9];
static unsigned s_cover_pending_mask,s_cover_dirty_mask;
static int shelf_rows(void){return rows;}
static int app_settings_shelf_style(void){return style;}
static bool badges=true;static bool app_settings_shelf_progress_badges(void){return badges;}
static int ttf_ascender_px(int px){return px;}
static bool ttf_text_mask_px(uint8_t*m,unsigned w,unsigned h,int x,int y,int px,const char*t){(void)x;(void)y;(void)px;(void)t;memset(m,0,w*h);for(unsigned j=2;j<h-2;++j)for(unsigned i=2;i<30;++i)m[j*w+i]=255;return true;}
static int app_settings_main_refresh_mode(void){return mode;}
static int app_settings_system_contrast(void){return contrast;}
enum EpdRotation epd_get_rotation(void){return (enum EpdRotation)rotation;}
static int app_settings_system_font_size(void){return font_size;}
static int app_settings_shelf_order(void){return order;}
static const char *app_settings_system_font_path(void){return font;}
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
static EpdRect ui_rotate_rect_to_fb(EpdRect r){return (EpdRect){1216-r.y-r.height,r.x,r.height,r.width};}
static EpdRect shelf_cover_image(int row){return (EpdRect){36+(row%3)*210,192+(row/3)*282,164,214};}
static void invalidate_covers(void){s_shelf_page_key_valid=s_shelf_page_loading=s_shelf_page_cached=s_shelf_page_cache_allowed=false;}
static int test_stat(const char *p,struct stat*st){assert(p[0]);++stats;if(missing)return -1;memset(st,0,sizeof(*st));st->st_mode=S_IFREG;st->st_size=book_bytes;st->st_mtime=modified;return 0;}
#define stat(path,out) test_stat(path,out)
book_shelf_page_state_t book_shelf_page_request(book_shelf_page_key_t key,book_shelf_page_layout_t layout){assert(layout.count>0);requested=key;++s_request_count;return BOOK_SHELF_PAGE_MISS;}
"""
KEY_MAIN=r"""
static void changed(app_ctx_t *ctx){book_shelf_page_key_t old=requested;int count=s_request_count;assert(!prepare_shelf_page(ctx));assert(s_request_count==count+1&&memcmp(&old,&requested,sizeof(old)));}
int main(void){
 app_ctx_t ctx={0};for(int i=0;i<18;++i){snprintf(s_shelf[i].path,288,"/books/%d.epub",i);snprintf(s_shelf[i].name,256,"Book %d",i);strcpy(s_shelf[i].author,"Author");}
 assert(!prepare_shelf_page(&ctx)&&s_request_count==1&&s_shelf_page_cache_allowed);int st=stats;
 for(int i=0;i<20;++i){s_shelf[0].pct=i;s_shelf[0].favorite=i&1;assert(!prepare_shelf_page(&ctx));assert(s_request_count==1&&stats==st);}
 s_shelf_page_cached=true;assert(prepare_shelf_page(&ctx)&&s_request_count==1&&stats==st);s_shelf_page_cached=false;
 for(mode=1;mode<3;++mode)changed(&ctx);mode=0;changed(&ctx);
 for(style=2;style<=6;++style)changed(&ctx);
 ++contrast;changed(&ctx);++rotation;changed(&ctx);++font_size;changed(&ctx);++order;changed(&ctx);
 strcpy(font,"/fonts/System.ttf");changed(&ctx);strcpy(s_query,"query");changed(&ctx);++s_category_selected;changed(&ctx);
 ctx.leaf=1;changed(&ctx);ctx.leaf=0;changed(&ctx);
 strcpy(s_shelf[0].name,"Edited title");changed(&ctx);strcpy(s_shelf[0].author,"Edited author");changed(&ctx);
 shelf_entry_t temp=s_shelf[0];s_shelf[0]=s_shelf[1];s_shelf[1]=temp;changed(&ctx);
 ++modified;invalidate_covers();changed(&ctx);++book_bytes;invalidate_covers();changed(&ctx);
 rows=8;changed(&ctx);missing=true;invalidate_covers();int count=s_request_count;assert(!prepare_shelf_page(&ctx)&&!s_shelf_page_cache_allowed&&s_request_count==count);
 missing=false;invalidate_covers();assert(!prepare_shelf_page(&ctx)&&s_shelf_page_cache_allowed);
 s_visible_count=0;invalidate_covers();count=s_request_count;assert(!prepare_shelf_page(&ctx)&&!s_shelf_page_cache_allowed&&s_request_count==count);
 puts("PASS: actual page preparation keys modes/styles/order/page/rotation/contrast/font/category/query/content/file size+mtime; live progress/favorites skip TF revalidation; missing files never cache");
}
"""

def main():
    board=os.environ.get('KIIKO_TEST_BOARD','pico')
    book=ROOT/'main/apps/app_book.c'
    driver=ROOT/'components/epdiy/src/epdiy.c'
    unit=UNIT[:UNIT.index('int main(void)')]+PREFIX
    if 'draw_axis_line(' in driver.read_text():unit+=function('draw_axis_line',driver)+'\n'
    for name in ['_rotate','epd_draw_pixel','epd_get_pixel','epd_draw_hline','epd_draw_vline','epd_fill_rect','epd_draw_circle','epd_fill_circle_helper','epd_fill_circle','epd_write_line','epd_draw_line']:
        body=function(name,driver)
        if name=='epd_draw_pixel':body=body.replace('void epd_draw_pixel(', 'void epd_draw_pixel(').replace('{','{++pixel_calls;',1)
        unit+=body+'\n'
    for name in ['ui_contrast_gray','ui_rotate_rect_to_fb','ui_fill_rect_fast','ui_blit_mono_bitmap','clamp_radius','draw_arc','ui_draw_round_rect','ui_fill_round_rect','ui_inset_rect','ui_draw_control_frame','ui_draw_separator','ui_read_pixel','ui_acrylic_bw_rect','ui_hairline']:
        unit+=function(name,ROOT/'main/ui/ui_kit.c')+'\n'
    unit+=function('ui_root_shadow',ROOT/'main/ui/ui_root_style.h')+'\n'
    unit+='typedef struct{unsigned x,y,width,height;}book_crop_t;\n'+function('book_cover_crop',ROOT/'main/book/book_cover.c')+'\n'
    unit+='\n'.join(line for line in (ROOT/'main/ui/ui_book_manage_layout.h').read_text().splitlines() if not line.startswith(('#include','#pragma')))+'\n'+UI
    unit+=(ROOT/'main/apps/book_shelf_badges.inc').read_text()+'\n'
    for name in ['row_rect','shelf_cover_image','shelf_cover_frame_ready','fast_cover_matches','draw_favorite_icon','draw_shelf_favorite_icon','cover_favorite_needs_white_edge','shelf_cover_white_edge','draw_shelf_cover_favorite','draw_shelf_cover','draw_list_title','draw_list_row','draw_shelf_furniture','draw_shelf_page_artwork']:
        body=function(name,book)
        if name=='draw_shelf_cover':body=body.replace('{','{++cover_draws;',1)
        unit+=body+'\n'
    unit+=MAIN
    with tempfile.TemporaryDirectory(prefix='kiiko-shelf-render-') as folder:
        t=Path(folder)
        stubs=dict(STUBS);stubs['epdiy.h']=(ROOT/'tools/ui_gesture_stubs/epdiy.h').read_text()
        for name,text in stubs.items():
            p=t/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        (t/'test.c').write_text(unit)
        flags=['cc','-Dshelf_display_style=app_settings_shelf_style','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-format-truncation','-DKIIKO_BOARD_METALIO47='+str(int(board=='metalio47')),'-I'+str(t),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main'),'-DBOOK_SHELF_PAGE_ROOT="'+str(t/'cache')+'"']
        subprocess.run(flags+[str(t/'test.c'),str(ROOT/'main/ui/ui_image_dither.c'),'-lpthread','-lm','-o',str(t/'test')],check=True)
        subprocess.run([str(t/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')))
        keys=KEY_UNIT
        for name in ['book_shelf_page_key_init','book_shelf_page_key_add']:keys+=function(name,ROOT/'main/book/book_shelf_page_cache.c')+'\n'
        for name in ['shelf_page_mask','prepare_shelf_page']:keys+=function(name,book)+'\n'
        (t/'keys.c').write_text(keys+KEY_MAIN)
        subprocess.run(flags+[str(t/'keys.c'),'-o',str(t/'keys')],check=True)
        subprocess.run([str(t/'keys')],check=True)


if __name__=='__main__':main()
