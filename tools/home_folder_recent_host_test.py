"""中文：最近阅读书夹排除续读卡，并保持第2–4本的封面/缓存选择一致。
English: Exclude the resume card and keep ranks2–4 consistent for folder covers and keys.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
UNIT=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
typedef struct{uint32_t last_open_s;uint8_t pct;}book_progress_t;
typedef struct{char path[288];int64_t modified;bool has_progress;book_progress_t progress;}home_book_t;
static home_book_t s_current,s_recent[6];
static bool readable[7],fast;
static unsigned contrast=100,progress_loads,stats;
static int identity(const char *path){int id=-1;assert(sscanf(path,"/books/%d.epub",&id)==1&&id>=0&&id<7);return id;}
static bool book_progress_load(const char *path,uint32_t bytes,book_progress_t *progress){
    assert(bytes==100);++progress_loads;int id=identity(path);if(!readable[id])return false;
    *progress=(book_progress_t){.last_open_s=1000-id,.pct=(uint8_t)(10+id)};return true;
}
static int test_stat(const char*path,struct stat*st){++stats;int id=identity(path);memset(st,0,sizeof(*st));st->st_size=100;st->st_mtime=200+id;return 0;}
#define stat(path,out) test_stat(path,out)
static bool app_settings_main_fast_refresh(void){return fast;}
static unsigned app_settings_system_contrast(void){return contrast;}
static void reset(void){memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));memset(readable,1,sizeof(readable));}
static void add(unsigned id){char path[288];snprintf(path,sizeof(path),"/books/%u.epub",id);struct stat st={.st_size=100,.st_mtime=200+id};consider_book(path,&st);}
static void current(unsigned id){snprintf(s_current.path,sizeof(s_current.path),"/books/%u.epub",id);s_current.has_progress=true;}
static void expect(const int *ids,unsigned count){
    home_book_t *sentinel=(home_book_t*)(uintptr_t)0x1234,*out[5]={sentinel,sentinel,sentinel,sentinel,sentinel};
    home_book_t before_current=s_current,before_recent[6];memcpy(before_recent,s_recent,sizeof(before_recent));
    unsigned io=progress_loads+stats;
    assert(folder_books(out+1)==count&&out[0]==sentinel&&out[4]==sentinel);
    for(unsigned i=0;i<count;++i){assert(identity(out[i+1]->path)==ids[i]);assert(strcmp(out[i+1]->path,featured()->path));assert(out[i+1]->has_progress);}
    for(unsigned i=count;i<3;++i)assert(out[i+1]==sentinel);
    assert(io==progress_loads+stats&&!memcmp(&before_current,&s_current,sizeof(s_current))&&!memcmp(before_recent,s_recent,sizeof(s_recent)));
}
int main(void){
    // 扫描器原本就排除了当前书，不能再盲跳过s_recent[0]。
    // Scanning already excludes current: never blindly skip the first recent entry.
    for(unsigned total=0;total<=7;++total){
        reset();if(total)current(0);for(int id=(int)total-1;id>=0;--id)add((unsigned)id);
        const int ids[]={1,2,3};expect(ids,total>4?3:total?total-1:0);
    }
    reset();current(0);const unsigned shuffled[]={6,2,0,5,3,1,4};
    for(unsigned i=0;i<7;++i)add(shuffled[i]);add(2);
    const int second_to_fourth[]={1,2,3};expect(second_to_fourth,3);
    for(unsigned i=0;i<6;++i)assert(identity(s_recent[i].path)==(int)i+1);
    uint32_t first_key=folder_key();s_current.progress.pct=97;s_recent[0].progress.pct=72;
    assert(folder_key()==first_key);fast=true;assert(folder_key()!=first_key);fast=false;
    ++contrast;assert(folder_key()!=first_key);--contrast;
    // 当前书变更但旧最近缓存尚在时，仍不能重复展示该路径。
    // Also handle a changed featured book while a previous recent list remains cached.
    current(3);const int changed[]={1,2,4};expect(changed,3);assert(folder_key()!=first_key);
    s_current.has_progress=false;expect(changed,3);
    // last-path缺失时，续读会回退第一本最近书，书夹从其后开始。
    // Without last-path, Continue falls back to the first recent book: start after it.
    reset();for(int id=6;id>=0;--id)add((unsigned)id);
    assert(identity(featured()->path)==0);expect(second_to_fourth,3);
    s_recent[1].has_progress=false;s_recent[2].path[0]=0;
    const int gaps[]={3,4,5};expect(gaps,3);
    for(int i=1;i<6;++i)s_recent[i].has_progress=false;expect(NULL,0);
    // 进度无效的图书不补进最近阅读，历史不足保持真实数量。
    // Never fill recent-reading slots with unread/invalid-progress entries.
    reset();current(0);readable[2]=false;for(int i=3;i>=0;--i)add((unsigned)i);
    const int short_history[]={1,3};expect(short_history,2);
    reset();expect(NULL,0);
    puts("PASS: actual scan + folder selection shows ranks2-4, excludes resume/fallback/stale duplicate, preserves short history/order and pure rendering; cover key follows the same books");
}
'''

def main():
    for board,relative in [('pico','boards/pico/src/main/apps/app_dashboard.c'),('metalio47','main/apps/app_dashboard.c')]:
        source=ROOT/relative
        unit=UNIT[:UNIT.index('static void reset(')]
        for name in ['consider_book','featured','folder_books','folder_key']:
            unit+=function(name,source)+'\n'
        unit+=UNIT[UNIT.index('static void reset('):]
        with tempfile.TemporaryDirectory(prefix='kiiko-home-folder-')as tmp:
            c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(unit)
            subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
            print(board,flush=True);subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
