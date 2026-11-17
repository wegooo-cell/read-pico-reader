#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Exercise actual ticket layout/cache and bounded reader-idle preparation under ASan/UBSan."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
ticket=ROOT/'main/book/book_ticket.c'
pico_ticket=ROOT/'boards/pico/src/main/book/book_ticket.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "book_cover_jobs.h"
#include "ui_image_dither.h"
typedef struct {int x,y,width,height;} EpdRect;
enum {UI_GRAY_BLACK=0,UI_GRAY_WHITE=255,UI_GRAY_LIGHT=192,EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool oom, downloaded;static unsigned allocations,reads,list_reads,grid_reads,source_case,download_reads;
static unsigned system_scale=100,advance_factor=1;
static void *heap_caps_malloc(size_t n,int caps){assert(n==176*240&&caps==3);if(oom)return NULL;void *p=malloc(n);if(p)++allocations;return p;}
static bool book_cover_load_gray(const char*p,const char*t,const char*a,uint8_t*out,bool decode,bool*pending){
 (void)p;(void)t;assert(!decode&&!pending);++reads;++grid_reads;
 if(source_case==2&&!strcmp(a,"")){memset(out,77,176*240);return true;}return false;
}
static bool book_cover_load_list_gray(const char*p,const char*t,const char*a,uint8_t*out,bool decode,bool*pending,unsigned*w,unsigned*h){
 (void)p;(void)t;assert(!decode&&!pending);++reads;++list_reads;
 if((source_case==1&&!strcmp(a,""))||source_case==3){*w=source_case==3?1:166;*h=240;for(unsigned y=0;y<*h;++y)for(unsigned x=0;x<*w;++x)out[y**w+x]=(x*13+y*7)&255;return true;}return false;
}
static bool book_weread_cover_is_txt(const char *path) {
 const char *dot=strrchr(path,'.');return dot&&!strcasecmp(dot,".txt");
}
static bool book_weread_cover_read(const char *path,uint8_t *pixels,unsigned w,unsigned h){
 assert(book_weread_cover_is_txt(path)&&w==176&&h==240);++download_reads;
 if(!downloaded)return false;
 memset(pixels,51,w*h);pixels[11*w+9]=255;return true;
}
static uint8_t ui_contrast_gray(uint8_t g){return g;}
static int ui_text_effective_px(int px){return px*system_scale/100;}
static unsigned cp_count(const char *s){unsigned n=0;while(*s){if(((unsigned char)*s&0xc0)!=0x80)++n;++s;}return n;}
static int ui_text_title_width_px(int px,const char*t,const char*sample){assert(sample);return cp_count(t)*px*advance_factor;}
static int ui_text_title_fit(char*t,int px,int width,const char*sample){assert(!t&&width==520&&sample);return px;}
static int ui_text_fixed_width_px(int px,const char*t){return cp_count(t)*px;}
static char shown[3][260];static int shown_px[3],shown_y[3];static unsigned lines;
static void ui_text_title_vc(uint8_t*fb,int x,int y,int px,const char*t,const char*sample,int align){
 (void)fb;assert(x==82&&align==EPD_DRAW_ALIGN_LEFT&&lines<3);
 assert(ui_text_title_width_px(px,t,sample)<=520&&y>=202&&y<370);
 snprintf(shown[lines],sizeof(shown[lines]),"%s",t);shown_px[lines]=px;shown_y[lines++]=y;
}
static uint8_t pixels[684*1216];
static void epd_draw_pixel(int x,int y,uint8_t c,uint8_t*fb){assert(x>=0&&x<684&&y>=0&&y<1216);fb[y*684+x]=c;}
static void ui_fill_rect_fast(EpdRect r,uint8_t c,uint8_t*fb){for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)epd_draw_pixel(x,y,c,fb);}
static void ui_draw_round_rect(uint8_t*fb,EpdRect r,int radius,uint8_t c){(void)fb;(void)radius;(void)c;assert(r.x>=216&&r.y>=378&&r.x+r.width<=468&&r.y+r.height<=722);}
static void ui_text_vc(uint8_t*fb,int x,int y,int px,const char*t,int a,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)t;(void)a;(void)inv;}
'''
unit+=function('book_cover_crop',ROOT/'main/book/book_cover.c')+'\n'
for name in ('text_fit','cover_load','cover_background_draw','cover_draw','heading_prefix','ticket_heading'):
 unit+=function(name,ticket)+'\n'
unit+=function('cover_load',pico_ticket).replace('cover_load(', 'pico_cover_load(',1)+'\n'
unit+=r'''
enum {READING,SHELF};
static int s_view=READING,lock_style=0;static char *s_text="body";static char s_path[288]="/sdcard/books/current.epub",s_book_title[256]="title";
static bool s_toolbar,s_clear_confirm,media_ready=true,allowed=true,s_ticket_cover_started,s_ticket_cover_done;
static unsigned s_cover_generation,generation,cancels,submits,takes;static int take_result;
static int64_t s_stats_activity_ms;
typedef struct {bool touched;} touch_t;
typedef struct {int64_t now_ms;bool consumed,request_app,request_return,request_menu;touch_t *touch;} app_ctx_t;
static bool read_pico_sd_io_ready(void){return media_ready;}
static bool pico_boot_asset_allowed(const char*p){(void)p;return allowed;}
static int app_settings_lock_style(void){return lock_style;}
static void copy_text(char*d,size_t n,const char*s){snprintf(d,n,"%s",s);}
static int book_epub_metadata_cached(const char*p,char*t,size_t n,char*a,size_t m){(void)p;snprintf(t,n,"metadata");snprintf(a,m,"author");return 0;}
static unsigned book_cover_jobs_begin(void){return ++generation;}
static book_cover_job_t submitted;
static bool book_cover_jobs_submit(unsigned g,unsigned slot,const book_cover_job_t*q){assert(g==generation&&!slot);assert(q->list&&q->artwork_only&&!q->fast);submitted=*q;++submits;return true;}
static int book_cover_jobs_take(unsigned g,unsigned slot,book_cover_result_t*r){assert(g==generation&&!slot);++takes;if(take_result==1){r->pixels=malloc(176*240);assert(r->pixels);}return take_result;}
static void book_cover_jobs_cancel(unsigned g){assert(g==generation);++cancels;}
'''
# Header declarations are external; definitions here have external linkage too.
for name in ('book_cover_load_gray','book_cover_load_list_gray','book_cover_jobs_begin','book_cover_jobs_submit','book_cover_jobs_take','book_cover_jobs_cancel'):
 unit=unit.replace('static bool '+name+'(', 'bool '+name+'(').replace('static unsigned '+name+'(', 'unsigned '+name+'(').replace('static int '+name+'(', 'int '+name+'(').replace('static void '+name+'(', 'void '+name+'(')
unit+=function('warm_reader_ticket_cover',ROOT/'main/apps/app_book.c')+'\n'
unit+=r'''
int main(void){
 const char *title="十八岁出门远行";uint8_t dummy=0;
 for(system_scale=100;system_scale<=200;system_scale+=25)for(advance_factor=1;advance_factor<=3;++advance_factor){
  lines=0;char name[256];strcpy(name,title);
  int count=ticket_heading(&dummy,202,112,50,name);
  char whole[520];snprintf(whole,sizeof(whole),"%s%s",shown[0],count==2?shown[1]:"");
  assert(count==(int)lines&&!strcmp(whole,title)&&!strcmp(name,title));
 }
 advance_factor=1;system_scale=100;
 const char *long_title="这是一本比较长的书名需要保留全部字符并且在两行里完整显示";
 lines=0;assert(ticket_heading(&dummy,202,112,50,long_title)==2&&lines==2);
 char joined[520];snprintf(joined,sizeof(joined),"%s%s",shown[0],shown[1]);assert(!strcmp(joined,long_title));
 lines=0;ticket_heading(&dummy,326,44,28,"作者名字很长但不能挡住封面");assert(lines==1&&shown_y[0]+shown_px[0]/2<378);
 char huge[256];for(int i=0;i<84;++i)memcpy(huge+i*3,"书",3);huge[252]=0;
 lines=0;ticket_heading(&dummy,202,112,50,huge);assert(lines==2&&strstr(shown[1],"…")&&strlen(huge)==252);
 for(source_case=0;source_case<=3;++source_case){
  reads=list_reads=grid_reads=0;unsigned w=0,h=0;
  uint8_t *gray=cover_load("book.epub",title,"余华",&w,&h);
  if(!source_case){assert(!gray&&reads==4);continue;}
  assert(gray&&h==240&&w==(source_case==1?166:source_case==2?176:1));
  if(source_case==1)assert(list_reads==2&&!grid_reads);
  cover_background_draw(pixels,gray,w,h);memset(pixels,255,sizeof(pixels));cover_draw(pixels,gray,w,h,title);
  if(source_case==1){
   // The original portrait ratio survives. No stretched 252px-wide image.
   assert(pixels[(378+172)*684+216]==255);
   assert(pixels[(378+172)*684+342]==gray[120*w+83]);
  }
  free(gray);
 }
 reads=0;oom=true;unsigned w=0,h=0;assert(!cover_load("book.epub",title,"余华",&w,&h)&&!reads);oom=false;
 source_case=0;reads=0;
 assert(!cover_load("book.txt","自定义书名","作者",&w,&h));
 assert(!pico_cover_load("book.txt","自定义书名","作者")&&download_reads==2&&!reads);
 downloaded=true;
 uint8_t *txt=cover_load("book.TXT","自定义书名","作者",&w,&h);
 assert(txt&&w==176&&h==240&&txt[0]==51&&txt[11*w+9]==255&&download_reads==3&&!reads);
 free(txt);
 txt=pico_cover_load("book.TXT","自定义书名","作者");
 assert(txt&&txt[0]==51&&download_reads==4&&!reads);free(txt);
 assert(!pico_cover_load("book.epub",title,"余华")&&download_reads==4);
 cover_background_draw(pixels,NULL,0,0);cover_draw(pixels,NULL,0,0,title);
 app_ctx_t ctx={.now_ms=799};warm_reader_ticket_cover(&ctx);assert(!submits);
 ctx.now_ms=800;media_ready=false;warm_reader_ticket_cover(&ctx);assert(!submits);media_ready=true;
 s_toolbar=true;warm_reader_ticket_cover(&ctx);assert(!submits);s_toolbar=false;
 ctx.request_menu=true;warm_reader_ticket_cover(&ctx);assert(!submits);ctx.request_menu=false;
 touch_t touch={true};ctx.touch=&touch;warm_reader_ticket_cover(&ctx);assert(!submits);touch.touched=false;
 warm_reader_ticket_cover(&ctx);assert(submits==1&&s_ticket_cover_started&&!s_ticket_cover_done&&!strcmp(submitted.author,"author"));
 assert(!strcmp(submitted.path,s_path)&&!strcmp(submitted.title,s_book_title));
 strcpy(s_path,"changed.epub");assert(strcmp(submitted.path,s_path));strcpy(s_path,"/sdcard/books/current.epub");
 warm_reader_ticket_cover(&ctx);assert(takes==1&&!cancels);
 take_result=1;warm_reader_ticket_cover(&ctx);assert(takes==2&&cancels==1&&s_ticket_cover_done&&!s_ticket_cover_started&&!s_cover_generation);
 warm_reader_ticket_cover(&ctx);assert(submits==1);
 s_ticket_cover_done=false;lock_style=2;warm_reader_ticket_cover(&ctx);assert(submits==1);lock_style=0;
 allowed=false;warm_reader_ticket_cover(&ctx);assert(submits==1&&s_ticket_cover_done);allowed=true;
 s_ticket_cover_done=false;warm_reader_ticket_cover(&ctx);take_result=-1;warm_reader_ticket_cover(&ctx);assert(cancels==2&&s_ticket_cover_done);
 puts("PASS: intact photo title at100–200%, shrink/wrap/ellipsis, author boundary, natural+legacy+grid cache-only paths, OOM, narrow artwork stride, bounded idle queue, input/media/style guards, snapshot ownership and result release");
}
'''
with tempfile.TemporaryDirectory(prefix='ticket-host-') as tmp:
 tmp=Path(tmp);(tmp/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/ui'),str(tmp/'test.c'),str(ROOT/'main/ui/ui_image_dither.c'),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],check=True)
