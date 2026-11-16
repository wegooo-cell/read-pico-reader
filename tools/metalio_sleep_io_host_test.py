"""Run actual sleep I/O hooks against host files; retain face/index state and reject changed media."""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
from board_source_view import select_source
root=Path(__file__).resolve().parents[1]
unit=r'''
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define TTF_FONT_SLOTS 4
typedef struct {int f_fd;bool f_io_suspended,f_io_ready,f_ready;off_t f_io_position;struct stat f_io_stat;char f_path[512];unsigned cached_glyphs;void *image;} ttf_slot_t;
static ttf_slot_t s_slots[TTF_FONT_SLOTS];
static int s_cur_index;
static void ttf_font_slots_init(void){}
typedef struct {FILE *file;bool io_suspended;struct stat io_stat;unsigned count;} zip_reader_t;
static void zip_prefetch_clear(zip_reader_t *z){(void)z;}
typedef struct {FILE *file;unsigned count;} book_txt_t;
static book_txt_t s_book;
static void *s_epub;
static char s_io_path[512];
static bool s_io_suspended;
static struct stat s_io_stat;
static int book_epub_suspend_io(void *b){(void)b;return ESP_OK;}
static int book_epub_resume_io(void *b){(void)b;return ESP_OK;}
'''
with tempfile.TemporaryDirectory() as excerpts:
    for rel, names in {
        'font/ttf_font.c': ('ttf_font_suspend_io', 'ttf_font_resume_io'),
        'book/zip_reader.c': ('zip_suspend_io', 'zip_resume_io'),
        'book/book_source.c': ('book_suspend_io', 'book_resume_io'),
    }.items():
        source = Path(excerpts) / Path(rel).name
        source.write_text(select_source((root / 'main' / rel).read_text(), 'metalio47'))
        for name in names:
            unit += function(name, source) + '\n'
unit+=r'''
static void fill(const char *path){FILE *f=fopen(path,"wb");assert(f);assert(fwrite("abcdefgh",1,8,f)==8);assert(!fclose(f));}
int main(void){
 char first[]="/tmp/s31-font-XXXXXX",second[]="/tmp/s31-book-XXXXXX";
 int fd=mkstemp(first);assert(fd>=0);close(fd);fd=mkstemp(second);assert(fd>=0);close(fd);
 fill(first);fill(second);
 for(int i=0;i<4;i++)s_slots[i].f_fd=-1;
 for(int i=0;i<2;i++){
  ttf_slot_t *f=&s_slots[i];strcpy(f->f_path,first);f->f_fd=open(first,O_RDONLY);assert(f->f_fd>=0);
  assert(lseek(f->f_fd,i+1,SEEK_SET)==i+1);f->f_ready=true;f->cached_glyphs=250+i;f->image=(void*)(uintptr_t)(10+i);
 }
 for(int loop=0;loop<100;loop++){
  assert(ttf_font_suspend_io()==ESP_OK&&ttf_font_suspend_io()==ESP_OK);
  assert(s_slots[0].f_fd<0&&s_slots[1].f_fd<0);
  assert(ttf_font_resume_io()==ESP_OK&&ttf_font_resume_io()==ESP_OK);
  for(int i=0;i<2;i++){
   assert(s_slots[i].f_ready&&s_slots[i].cached_glyphs==(unsigned)(250+i)&&s_slots[i].image==(void*)(uintptr_t)(10+i));
   assert(lseek(s_slots[i].f_fd,0,SEEK_CUR)==i+1);
  }
 }
 assert(ttf_font_suspend_io()==ESP_OK);assert(!unlink(first));
 assert(ttf_font_resume_io()!=ESP_OK&&!s_slots[0].f_ready&&!s_slots[1].f_ready);
 fill(first);assert(ttf_font_resume_io()==ESP_OK&&s_slots[0].f_ready&&s_slots[1].f_ready);
 // An unavailable idle face must not block a valid active one; keep its saved path suspended.
 assert(ttf_font_suspend_io()==ESP_OK);strcpy(s_slots[1].f_path,"/missing-idle-font.ttf");
 assert(ttf_font_resume_io()==ESP_OK&&s_slots[0].f_ready&&!s_slots[1].f_ready&&s_slots[1].f_io_suspended);
 strcpy(s_slots[1].f_path,first);s_cur_index=1;
 assert(ttf_font_resume_io()==ESP_OK&&s_slots[1].f_ready);
 for(int i=0;i<2;i++)close(s_slots[i].f_fd);
 zip_reader_t z={.file=fopen(second,"rb"),.count=5000};assert(z.file);
 assert(zip_suspend_io(&z)==ESP_OK&&!z.file&&z.count==5000);
 assert(zip_resume_io(&z,second)==ESP_OK&&z.file&&fgetc(z.file)=='a'&&z.count==5000);
 assert(zip_suspend_io(&z)==ESP_OK);FILE *f=fopen(second,"ab");assert(f);fputc('x',f);fclose(f);
 assert(zip_resume_io(&z,second)!=ESP_OK&&!z.file&&z.count==5000);
 fill(second);strcpy(s_io_path,second);s_book.file=fopen(second,"rb");s_book.count=99;
 assert(book_suspend_io()==ESP_OK&&!s_book.file&&s_book.count==99);
 assert(book_resume_io()==ESP_OK&&s_book.file&&s_book.count==99&&fgetc(s_book.file)=='a');
 assert(book_suspend_io()==ESP_OK);assert(!unlink(second));
 assert(book_resume_io()!=ESP_OK&&!s_book.file&&s_book.count==99);
 fill(second);assert(book_resume_io()==ESP_OK&&s_book.file);fclose(s_book.file);
 unlink(first);unlink(second);
 puts("PASS: real font/ZIP/TXT IO suspension x100; glyph caches/face identity/file positions/5000-entry index retained; missing/changed media rejected; no settings writes");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
