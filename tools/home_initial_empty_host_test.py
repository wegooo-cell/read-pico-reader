#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实首页扫描尊重首次入架边界，旧历史不删除，状态变化使缓存失效。
English: Exercise initial-enrollment gating, preserved legacy history and Home cache invalidation.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "book_file_path.h"
#define BOOK_STORE_PATH_MAX 288
typedef int esp_err_t;
enum {ESP_OK, ESP_FAIL, ESP_ERR_NOT_FINISHED};
typedef struct {uint32_t file_size,last_open_s;uint8_t pct;} book_progress_t;
typedef struct {char path[288],title[128],author[128];time_t modified;bool has_progress;book_progress_t progress;} home_book_t;
typedef struct {bool present,mounted;} read_pico_sd_info_t;
static home_book_t s_current,s_recent[6];
static bool pending,s_books_cache_valid,s_cached_library_pending,s_cached_sd_present,s_cached_sd_mounted;
static unsigned s_cached_store_revision,store_revision=1,history_reads,metadata_reads;
static char s_cached_books_dir[128],s_cached_last_path[288],root[128],old_path[288];
static bool app_setup_library_pending(void){return pending;}
static const char *app_settings_books_dir(void){return root;}
static unsigned book_store_revision(void){return store_revision;}
static bool book_store_flash_ready(void){return false;}
static void read_pico_sd_get_info(read_pico_sd_info_t *sd){*sd=(read_pico_sd_info_t){true,true};}
static bool book_progress_last_path(char *out,size_t cap){++history_reads;snprintf(out,cap,"%s",old_path);return *out;}
static bool book_progress_load(const char *path,uint32_t size,book_progress_t *out){
    ++history_reads;if(strcmp(path,old_path))return false;
    *out=(book_progress_t){size,99,42};return true;
}
static int book_progress_list(bool (*visit)(const char*,const book_progress_t*,void*),void *ctx){
    ++history_reads;book_progress_t progress={1,99,42};visit(old_path,&progress,ctx);return ESP_OK;
}
static void title_for(home_book_t *book){++metadata_reads;snprintf(book->title,sizeof(book->title),"old title");}
static bool book_author_clean_display(char *s,size_t cap){(void)s;(void)cap;return false;}
static bool pico_boot_asset_allowed(const char *path){(void)path;return true;}
static int book_epub_metadata_cached(const char *path,char *title,size_t tcap,char *author,size_t acap){
    (void)path;(void)tcap;(void)acap;++metadata_reads;*title=*author=0;return ESP_FAIL;
}
static int book_epub_metadata(const char *path,char *title,size_t tcap,char *author,size_t acap){return book_epub_metadata_cached(path,title,tcap,author,acap);}
static home_book_t *book_at(int index){return index?&s_recent[index-1]:&s_current;}
'''

CASES = r'''
static void seed_stale(void){
    snprintf(s_current.path,sizeof(s_current.path),"%s",old_path);
    s_current.has_progress=true;
    for(int i=0;i<6;++i)s_recent[i]=s_current;
}
static void expect_empty(void){for(int i=0;i<7;++i)assert(!book_at(i)->path[0]&&!book_at(i)->has_progress);}
int main(int argc,char **argv){
    assert(argc==2);snprintf(root,sizeof(root),"%s/books",argv[1]);
    snprintf(old_path,sizeof(old_path),"%s/old.epub",root);
    read_pico_sd_info_t sd={true,true};
    // 首次引导后、插卡重扫及重新进入首页，旧历史均不能填充续读。
    // Repeated fresh entry and media scans must never expose previous-card history.
    pending=true;
    for(int i=0;i<3;++i){seed_stale();scan_books();expect_empty();assert(!history_reads&&!metadata_reads);}
    remember_scan_state(&sd);assert(s_cached_library_pending&&!s_cached_last_path[0]&&!history_reads);
    assert(scan_state_matches(&sd,ESP_OK));assert(!scan_state_matches(&sd,ESP_ERR_NOT_FINISHED));
    ++store_revision;assert(!scan_state_matches(&sd,ESP_OK));--store_revision;
    sd.mounted=false;assert(!scan_state_matches(&sd,ESP_OK));sd.mounted=true;
    // 明确导入完成时，即使卡/目录/存储版本全相同也须重扫；老用户记录原样继续使用。
    // Enrollment completion invalidates an otherwise identical cache and keeps legacy resume data.
    pending=false;assert(!scan_state_matches(&sd,ESP_OK));scan_books();
    assert(!strcmp(s_current.path,old_path)&&s_current.has_progress&&s_current.progress.pct==42);
    assert(history_reads&&metadata_reads);remember_scan_state(&sd);
    assert(scan_state_matches(&sd,ESP_OK)&&!strcmp(s_cached_last_path,old_path));
    pending=true;assert(!scan_state_matches(&sd,ESP_OK));
    unsigned reads=history_reads,metadata=metadata_reads;scan_books();expect_empty();
    remember_scan_state(&sd);assert(history_reads==reads&&metadata_reads==metadata);
    struct stat st;assert(!stat(old_path,&st)&&st.st_size==1);
    puts("PASS: fresh Home rejects stale history/fallback and cached books; enrollment invalidates cache; legacy resume/progress and source files retained");
}
'''


def main():
    for board, relative in [('pico', 'boards/pico/src/main/apps/app_dashboard.c'),
                            ('metalio47', 'main/apps/app_dashboard.c')]:
        source = ROOT / relative
        unit = UNIT
        for name in ['is_book', 'copy_title', 'consider_book', 'scan_root', 'recent_visit',
                     'scan_books', 'scan_state_matches', 'remember_scan_state']:
            unit += function(name, source) + '\n'
        unit += CASES
        assert '现在到书架正式开始读一本书吧～' in source.read_text()
        assert 'scan_state_matches(&sd,error)' in function('on_enter', source)
        with tempfile.TemporaryDirectory(prefix='kiiko-home-initial-') as directory:
            temp = Path(directory)
            (temp / 'books').mkdir()
            (temp / 'books/old.epub').write_bytes(b'x')
            (temp / 'books/unread.txt').write_bytes(b'y')
            c, executable = temp / 'test.c', temp / 'test'
            c.write_text(unit)
            subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                            '-fsanitize=address,undefined', '-I'+str(ROOT/'main/book'),
                            str(c), '-o', str(executable)], check=True)
            print(board, flush=True)
            subprocess.run([str(executable), str(temp)], check=True)
            assert (temp / 'books/old.epub').read_bytes() == b'x'
            assert (temp / 'books/unread.txt').read_bytes() == b'y'


if __name__ == '__main__':
    main()
