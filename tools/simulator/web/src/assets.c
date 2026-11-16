#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
const uint8_t *builtin_ttf_start,*builtin_ttf_end;
const uint8_t *ui_hanzi_start,*ui_hanzi_end;
const uint8_t *lock_4bpp_bin_start,*lock_4bpp_bin_end;
const uint8_t *loading_4bpp_bin_start,*loading_4bpp_bin_end;
const uint8_t *display_test_png_start,*display_test_png_end;
const uint8_t *reading_md_start,*reading_md_end;
void sim_assets_init(void){
{FILE *f=fopen("/assets/builtin.ttf","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);builtin_ttf_start=p;builtin_ttf_end=p+n;}
{FILE *f=fopen("/assets/ui-hanzi.bin","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);ui_hanzi_start=p;ui_hanzi_end=p+n;}
{FILE *f=fopen("/assets/lock_4bpp.bin","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);lock_4bpp_bin_start=p;lock_4bpp_bin_end=p+n;}
{FILE *f=fopen("/assets/loading_4bpp.bin","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);loading_4bpp_bin_start=p;loading_4bpp_bin_end=p+n;}
{FILE *f=fopen("/assets/display-test.png","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);display_test_png_start=p;display_test_png_end=p+n;}
{FILE *f=fopen("/assets/reading.md","rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)abort();fclose(f);reading_md_start=p;reading_md_end=p+n;}
}

#include "book_progress.h"
#include "book_ticket.h"
#include "settings.h"
#include "book_excerpt.h"
#include <string.h>
#include <sys/stat.h>
void sim_seed(void){app_settings_init();char last[288];if(book_progress_last_path(last,sizeof(last))&&last[0])return;
 const char*paths[]={"/sdcard/books/山间来信.epub","/sdcard/books/慢慢读.epub","/sdcard/books/海边的信.txt"};
 for(unsigned i=0;i<3;i++){struct stat st;if(!sim_stat(paths[i],&st)){book_progress_t p={.file_size=st.st_size,.px=48,.pct=5+i*7};book_progress_save(paths[i],&p);book_progress_set_last_path(paths[i]);}}
 const char*quote="阅读不必匆忙，留一点时间给自己。";html_text_t text={.utf8=(char*)quote,.len=strlen(quote)};book_excerpt_offer(paths[2],0,&text);
 book_ticket_record((uint32_t)(book_ticket_local_time_ms()/UINT64_C(86400000)),480000,3);
}
