#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Verify the real reader against every GB2312 Han, including cold metrics and missing imported glyphs."""
from pathlib import Path
import json,os,struct,subprocess,tempfile,sys,zlib,hashlib
from font_hanzi_host_support import font_hanzi_host_sources,subset_without_xing
ROOT=Path(__file__).resolve().parents[1]
AA_MODE=int('#include "ui_hanzi_aa_scale.h"' in (ROOT/'main/font/ttf_font.c').read_text())
chars=[]
for hi in range(0xb0,0xf8):
 for lo in range(0xa1,0xff):
  try:chars.append(ord(bytes([hi,lo]).decode('gb2312')))
  except UnicodeDecodeError:pass
assert len(set(chars))==6763
extras=[]
if (ROOT/'main/font/reader_extra_hanzi.txt').exists():
 for file in ['reader_extra_hanzi.txt','reader_extra_outlines.txt']:
  extra=ROOT/'main/font'/file
  if extra.exists():extras += [int(line.split('#',1)[0].strip(),16) for line in extra.read_text().splitlines() if line.split('#',1)[0].strip()]
extras=sorted(set(extras))
font=(ROOT/'main/assets/builtin.ttf').read_bytes()
quality=json.loads((ROOT/'tools/fixtures/pico-reader-ink.json').read_text()) if AA_MODE else None
with tempfile.TemporaryDirectory(prefix='reader-hanzi-') as temp:
 w=Path(temp)
 (w/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_SIZE 0x104\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
 (w/'esp_log.h').write_text('#pragma once\nstatic inline void test_log(const char*t,const char*f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) test_log(__VA_ARGS__)\n#define ESP_LOGW(...) test_log(__VA_ARGS__)\n#define ESP_LOGE(...) test_log(__VA_ARGS__)\n')
 (w/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
 (w/'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t n,unsigned caps);
void *heap_caps_calloc(size_t n,size_t z,unsigned caps);
void heap_caps_free(void *p);
void *heap_caps_realloc(void *p,size_t n,unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
''')
 assets='#include <stdint.h>\nconst uint8_t builtin[] asm("_binary_builtin_ttf_start")={'+','.join(map(str,font))+'};\n'+f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(font)}");\n'
 (w/'assets.c').write_text(assets)
 imported=subset_without_xing(font)
 # Make native imported advances distinguishable from the embedded fallback face.
 u16=lambda at:struct.unpack_from('>H',imported,at)[0]
 for i in range(u16(4)):
  at=12+i*16
  if imported[at:at+4]==b'hmtx':
   offset,length=struct.unpack_from('>II',imported,at+8)
   for p in range(offset,offset+length-3,4):
    advance=u16(p);struct.pack_into('>H',imported,p,advance+advance//4)
 (w/'imported.ttf').write_bytes(imported)
 unit=r'''
#define TEST_AA AA_MODE
#include "ttf_font.c"
#include "ui_hanzi.h"
#include "book_layout.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <zlib.h>
typedef union {max_align_t align;size_t bytes;} allocation_t;
static size_t live,allocations,peak;
static int fail_after=-1;
void *heap_caps_malloc(size_t n,unsigned caps){
 (void)caps;if(fail_after==0)return NULL;if(fail_after>0)--fail_after;
 allocation_t*p=malloc(sizeof(*p)+n);if(!p)return NULL;p->bytes=n;live+=n;++allocations;if(live>peak)peak=live;return p+1;
}
void *heap_caps_calloc(size_t n,size_t z,unsigned caps){void*p=heap_caps_malloc(n*z,caps);if(p)memset(p,0,n*z);return p;}
void heap_caps_free(void*p){if(!p)return;allocation_t*a=(allocation_t*)p-1;assert(live>=a->bytes);live-=a->bytes;free(a);}
void *heap_caps_realloc(void *p,size_t n,unsigned caps){if(!n){heap_caps_free(p);return NULL;}void*q=heap_caps_malloc(n,caps);if(!q)return NULL;if(p){allocation_t*a=(allocation_t*)p-1;memcpy(q,p,a->bytes<n?a->bytes:n);heap_caps_free(p);}return q;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 16u*1024u*1024u;}
size_t heap_caps_get_largest_free_block(unsigned caps){return heap_caps_get_free_size(caps);}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
static uint8_t screen[684*1216];
static unsigned painted;
void epd_draw_pixel(int x,int y,uint8_t ink,uint8_t*fb){assert(x>=0&&x<684&&y>=0&&y<1216);fb[y*684+x]=ink>>4;++painted;}
static void utf8(uint32_t cp,char out[5]){
 if(cp<0x80){out[0]=cp;out[1]=0;}
 else if(cp<0x800){out[0]=0xc0|(cp>>6);out[1]=0x80|(cp&63);out[2]=0;}
 else if(cp<0x10000){out[0]=0xe0|(cp>>12);out[1]=0x80|((cp>>6)&63);out[2]=0x80|(cp&63);out[3]=0;}
 else{out[0]=0xf0|(cp>>18);out[1]=0x80|((cp>>12)&63);out[2]=0x80|((cp>>6)&63);out[3]=0x80|(cp&63);out[4]=0;}
}
static const uint16_t corpus[]={CORPUS};
static unsigned matched,supplemental;
static void native_pixels(uint32_t cp,int px){
 const glyph_entry_t*g=get_glyph(cp,px);assert(g);
 int gid=stbtt_FindGlyphIndex(&font_info,cp),x0,y0,x1,y1;assert(gid);
 float scale=stbtt_ScaleForPixelHeight(&font_info,px);
 stbtt_GetGlyphBitmapBox(&font_info,gid,scale,scale,&x0,&y0,&x1,&y1);
 assert(g->left==x0&&g->top==-y0&&g->width==x1-x0&&g->height==y1-y0);
 size_t n=(size_t)g->width*g->height;uint8_t*expected=malloc(n?n:1);assert(expected);
 if(n){stbtt_MakeGlyphBitmap(&font_info,expected,g->width,g->height,g->width,scale,scale,gid);assert(!memcmp(expected,g->bitmap,n));}
 free(expected);
}
int main(int argc,char**argv){
 assert((argc==2||argc==3)&&ttf_font_open_builtin()==ESP_OK&&ttf_font_is_builtin());
 int native_wen=ttf_text_width_px(48,"文"),native_xing=ttf_text_width_px(48,"行");
 ui_hanzi_cache_t reference={0};uint32_t sample=0;
 for(unsigned i=0;i<sizeof(corpus)/sizeof(corpus[0]);++i){
  uint32_t cp=corpus[i];char text[5];utf8(cp,text);
  size_t before=allocations;int cold=ttf_text_width_px(24,text);assert(cold>0&&allocations==before);
  bool native=stbtt_FindGlyphIndex(&font_info,cp)!=0;
  const glyph_entry_t*g=get_glyph(cp,24);assert(g&&g->advance_x==cold&&g->bitmap&&g->width>0&&g->height>0);
  assert(ttf_text_width_px(24,text)==cold);
  if(native){++matched;native_pixels(cp,24);}
  else{
   assert(ui_hanzi_has(cp)&&!ttf_font_has_text(text));++supplemental;sample=cp;
#if !TEST_AA
   uint8_t r[77];assert(ui_hanzi_get_cached(&reference,cp,r));
   assert(g->width==r[2]&&g->height==r[3]&&g->left==(int8_t)r[0]&&g->top==-(int8_t)r[1]&&g->advance_x==r[4]);
   for(int y=0;y<g->height;++y)for(int x=0;x<g->width;++x){unsigned bit=y*24+x;assert(g->bitmap[y*g->width+x]==(((r[5+bit/8]>>(bit&7))&1)?255:0));}
#endif
  }
#if TEST_AA
  if(!native){uint8_t r[UI_HANZI_AA_RECORD];assert(ui_hanzi_get_aa_cached(&s_reader_hanzi,cp,r));
   for(int y=0;y<g->height;++y)for(int x=0;x<g->width;++x){unsigned at=y*24+x;assert(g->bitmap[y*g->width+x]==((r[5+at/2]>>((at%2)*4))&15)*17);}}
#endif
 }
 assert(matched==1129&&supplemental==5634&&sample);ui_hanzi_cache_release(&reference);
#if TEST_AA
 static const uint32_t additional[]={EXTRA_CORPUS};
 for(unsigned i=0;i<sizeof(additional)/sizeof(additional[0]);++i){
  uint32_t cp=additional[i];char text[5];utf8(cp,text);size_t before=allocations;
  int cold=ttf_text_width_px(43,text);assert(cold>=0&&allocations==before);
  const glyph_entry_t*g=get_glyph(cp,43);assert(g&&g->advance_x==cold&&ttf_text_width_px(43,text)==cold);
  if(g->width&&g->height){memset(screen,15,sizeof(screen));painted=0;ttf_draw_text_px(screen,40,240,43,text,EPD_DRAW_ALIGN_LEFT,0,15);assert(painted);}
 }
#endif
 // 独立参考来自未缩放的完整Medium轮廓，包含用户反馈的丰富/替/雪/艳/鬓/簪。
 // Independent references use full Medium outlines, including the reported dense-stroke glyphs.
#if TEST_AA
 static const int reference_sizes[]={QUALITY_SIZES};
 static const struct {uint32_t cp;uint32_t ink[9];} reference_ink[]={QUALITY_ROWS};
 double relative_error=0;unsigned comparisons=0;
 for(unsigned i=0;i<sizeof(reference_ink)/sizeof(reference_ink[0]);++i){
  for(unsigned j=0;j<sizeof(reference_sizes)/sizeof(reference_sizes[0]);++j){
   const glyph_entry_t*q=get_glyph(reference_ink[i].cp,reference_sizes[j]);assert(q);
   uint32_t ink=0;for(size_t p=0;p<q->bitmap_bytes;++p)ink+=s_cover[q->bitmap[p]];
   double ratio=(double)ink/reference_ink[i].ink[j];
   if(ratio<.89||ratio>1.08)fprintf(stderr,"ink U+%04X px=%d ratio=%.4f\n",reference_ink[i].cp,reference_sizes[j],ratio);
   assert(ratio>=.89&&ratio<=1.08);relative_error+=fabs(ratio-1);++comparisons;
  }
 }
 assert(comparisons>800&&relative_error/comparisons<.025);
 printf("Reader ink: %u independent full-outline comparisons, mean absolute error %.3f%%\n",comparisons,100*relative_error/comparisons);
#endif
 // Allocation failure never inserts a .notdef into the requested character's cache.
 char text[5];utf8(sample,text);ttf_font_cache_clear_all();fail_after=0;
 int cold=ttf_text_width_px(48,text);assert(cold>0&&!get_glyph(sample,48)&&!cache_lookup(sample,48));
 fail_after=-1;const glyph_entry_t*g=get_glyph(sample,48);assert(g&&g->advance_x==cold);
 // 黑白实验的字重/缩放在下方阅读上下文单独验证，UI保持原单色接口。
 // The mono experiment tests strokes/scaling in its reader context below; UI retains its monochrome interface.
 int sizes[]={12,16,24,28,32,36,40,43,48,60,72,96,120,160};
 for(unsigned size=0;size<sizeof(sizes)/sizeof(sizes[0]);++size)for(int stroke=-16;stroke<=16;stroke+=16){
  int px=sizes[size];
  // UI尺寸验证正常笔画；正文24px起覆盖微调。/ Small UI sizes use normal strokes; body sizes cover adjustments.
  if(px<24&&stroke!=0)continue;
  ttf_set_stroke_adjust_fine(stroke);ttf_font_cache_clear_all();
  int before=ttf_text_width_px(px,text),left=ttf_text_left_bearing_px(px,text);
  g=get_glyph(sample,px);assert(g&&g->advance_x==before&&g->left==left&&ttf_text_width_px(px,text)==before);
  for(int aa=0;aa<2;++aa){
   ttf_set_antialias(aa);memset(screen,15,sizeof(screen));painted=0;
   ttf_draw_text_px(screen,40,240,px,text,EPD_DRAW_ALIGN_LEFT,0,15);if(!painted)fprintf(stderr,"empty px=%d stroke=%d aa=%d\n",px,stroke,aa);assert(painted);
   unsigned grays=0;for(size_t at=0;at<sizeof(screen);++at)grays+=screen[at]>0&&screen[at]<15;
   if(TEST_AA)assert(aa?grays>0:grays==0);
   memset(screen,15,sizeof(screen));painted=0;
   assert(ttf_draw_text_px_vertical(screen,480,100,px,text,0,0,15)>0&&painted);
  }
 }
 ttf_set_stroke_adjust_fine(0);ttf_set_antialias(true);
 // Actual pagination stays byte-identical before/after warming the supplemental glyphs.
 char paragraph[4096];paragraph[0]=0;
 for(int i=0;i<30;++i){strcat(paragraph,"海风吹过古城，阅读文字与远处山林。 ");strcat(paragraph,text);strcat(paragraph,"\n");
#if TEST_AA
  strcat(paragraph,"《夜行》‘玥与喆’① 𠳐𥻗𬉼\n");
#endif
 }
 for(int vertical=0;vertical<2;++vertical){
  book_layout_set_vertical(vertical);ttf_font_cache_clear_all();
  assert(book_layout_build(paragraph,strlen(paragraph),(EpdRect){20,40,210,180},32));
  size_t count=book_layout_page_count(),offsets[512];assert(count>1&&count<512);
  for(size_t i=0;i<count;++i)offsets[i]=book_layout_page_start_offset(i);
  const char*warm=paragraph;while(*warm){uint32_t cp=decode_utf8(&warm);if(cp!='\n'&&cp!='\r')assert(get_glyph(cp,32));}
  assert(book_layout_build(paragraph,strlen(paragraph),(EpdRect){20,40,210,180},32)&&book_layout_page_count()==count);
  for(size_t i=0;i<count;++i)assert(book_layout_page_start_offset(i)==offsets[i]);
  book_layout_free();
 }


 if(argc==3){
  const char*lines[]={"丰富、替、雪、艳、鬓、簪。","内建字体深浅与抗锯齿。","春江潮水连海平，明月生。"};
  for(int aa=0;aa<2;++aa){ttf_set_antialias(aa);memset(screen,15,sizeof(screen));
   int pxs[]={24,32,40,43,48,60};int y=70;
   for(unsigned n=0;n<sizeof(pxs)/sizeof(pxs[0]);++n){
    ttf_draw_text_px(screen,30,y,pxs[n],lines[n%3],EPD_DRAW_ALIGN_LEFT,0,15);y+=100;}
   char path[1024];snprintf(path,sizeof(path),"%s-aa%d.pgm",argv[2],aa);FILE*f=fopen(path,"wb");assert(f);
   fprintf(f,"P5\n684 1216\n255\n");for(size_t i=0;i<sizeof(screen);++i){uint8_t v=screen[i]*17;assert(fwrite(&v,1,1,f)==1);}assert(!fclose(f));}
 }
 assert(ttf_font_open(argv[1])==ESP_OK&&!ttf_font_is_builtin());
 assert(!ttf_font_has_text("行")&&ttf_text_width_px(48,"文")>native_wen&&ttf_text_width_px(48,"行")==native_xing);
 native_pixels(0x6587,48);const char *saved=ttf_font_path();char path[288];snprintf(path,sizeof(path),"%s",saved);
 g=get_glyph(0x884c,48);assert(g&&g->advance_x==native_xing&&!strcmp(path,ttf_font_path())&&ttf_font_selected()==0);
 // A borrowed book face keeps its slot and coverage query; fallback changes glyphs only.
 FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*mem=heap_caps_malloc(n,3);assert(mem&&fread(mem,1,n,f)==(size_t)n);fclose(f);
 int slot=ttf_font_open_mem(mem,n,"embedded");assert(slot>0);ttf_font_select(slot);
 assert(!ttf_font_slot_has_text(slot,"行"));g=get_glyph(0x884c,48);assert(g&&ttf_font_selected()==slot);
 ttf_font_close_embedded();ttf_font_select(0);ttf_font_cache_clear_all();assert(!s_reader_hanzi.block&&!s_reader_hanzi.inflate);
 ttf_font_unload();printf("PASS:6763 real GB2312 reader glyphs (%u native/%u supplemental), native/supplement coverage, AA on/off gray pixels, cold/warm advances,12–160px/strokes/AA/vertical, OOM retry, imported/embedded face/slot retention and released workspace; peak=%zu\n",matched,supplemental,peak);
}
'''.replace('AA_MODE',str(AA_MODE)).replace('EXTRA_CORPUS',','.join(map(str,extras)) or '0').replace('CORPUS',','.join(map(str,chars)))
 unit=unit.replace('QUALITY_SIZES',','.join(map(str,quality['sizes'])) if quality else '0')
 unit=unit.replace('QUALITY_ROWS',','.join('{'+str(row['cp'])+',{'+','.join(map(str,row['ink']))+'}}' for row in quality['glyphs']) if quality else '{0,{0}}')
 (w/'test.c').write_text(unit)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I'+str(w),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'tools/ui_gesture_stubs')]
 gc='-Wl,-dead_strip' if os.uname().sysname=='Darwin' else '-Wl,--gc-sections'
 # ESP-IDF permits free(heap_caps_malloc()); the tracking host allocator models that in these objects.
 objects=[]
 for name in ('book_layout','html_text'):
  obj=w/(name+'.o');subprocess.run(flags+['-Dfree=heap_caps_free','-c',str(ROOT/'main/book'/(name+'.c')),'-o',str(obj)],check=True);objects.append(str(obj))
 subprocess.run(flags+font_hanzi_host_sources(ROOT,w)+[str(w/'test.c'),str(w/'assets.c'),*objects,'-lm',gc,'-o',str(w/'test')],check=True)
 subprocess.run([str(w/'test'),str(w/'imported.ttf')]+([os.environ['KIIKO_FONT_PREVIEW']] if os.environ.get('KIIKO_FONT_PREVIEW') else []),check=True)
