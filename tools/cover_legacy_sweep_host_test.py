#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行旧平铺封面目录迁移：灰度与完整列表封面改名进分组目录，保留已有目标的旧副本及失败原件，
# TXT 在线封面记录保留；只剩这些记录时停止，目录清空后删除目录。
# English: Execute the old flat cover folder migration: gray and full list covers are renamed into their
# group folder, retaining legacy alternates and originals on errors; other caches are deleted, TXT online cover
# records stay; stop when only those remain and remove the folder once it is empty.
from pathlib import Path
import re, subprocess, tempfile
from home_cover_cache_host_test import function

R = Path(__file__).resolve().parents[1]
source = R / 'main/book/book_cover.c'
text = source.read_text()

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp) / 'k'
    unit = r'''
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool fail_rename,fail_stat;
static int checked_rename(const char*a,const char*b){if(fail_rename){errno=EIO;return -1;}return rename(a,b);}
static int checked_stat(const char*p,struct stat*s){if(fail_stat&&strstr(p,"2000000000000002-176x240.bin")){errno=EIO;return -1;}return stat(p,s);}
#define rename checked_rename
#define stat(p,s) checked_stat(p,s)
'''
    unit += re.search(r'#define COVER_DIR [^\n]*\n', text)[0]
    unit += re.search(r'#define COVER_LEGACY_DIR [^\n]*\n', text)[0]
    for name in ('cover_file_path', 'cover_dir_ready', 'cover_legacy_kept', 'cover_legacy_movable',
                 'book_cover_legacy_sweep_step'):
        unit += function(name, source) + '\n'
    unit = unit.replace('/sdcard/.kiikoread', str(root))
    unit += r'''
#undef rename
#undef stat
static void put(const char *path, const char *body) {
    FILE *f = fopen(path, "wb"); assert(f && fputs(body, f) >= 0 && !fclose(f));
}
static void expect(const char *path, const char *body) {
    char got[32] = {0}; FILE *f = fopen(path, "rb"); assert(f);
    assert(fread(got, 1, sizeof(got) - 1, f) == strlen(body) && !fclose(f) && !strcmp(got, body));
}
int main(void) {
    char p[256];
    assert(!book_cover_legacy_sweep_step()); // 没有旧目录。/ No old folder.
    assert(!mkdir(ROOT, 0777) && !mkdir(COVER_LEGACY_DIR, 0777));
    put(COVER_LEGACY_DIR "/fc00000000000001-176x240.bin", "gray");
    put(COVER_LEGACY_DIR "/0400000000000002-176x240-list.bin", "list");
    put(COVER_LEGACY_DIR "/0800000000000003-176x240.bin", "stale");
    put(COVER_LEGACY_DIR "/0800000000000003-176x240-fast.bin", "fast");
    put(COVER_LEGACY_DIR "/0c00000000000004-noart.bin", "none");
    put(COVER_LEGACY_DIR "/0c00000000000004-176x240-s51x70.bin", "small");
    put(COVER_LEGACY_DIR "/0c00000000000005-176x240.bin.tmp", "tmp");
    put(COVER_LEGACY_DIR "/wr-0000000000000006-10-20.bin", "txt");
    put(COVER_LEGACY_DIR "/wr-0000000000000006-10-20.bin.part", "part");
    assert(!mkdir(COVER_DIR, 0777) && !mkdir(COVER_DIR "/02", 0777));
    put(COVER_DIR "/02/0800000000000003-176x240.bin", "fresh");
    int steps = 0;
    while (book_cover_legacy_sweep_step()) assert(++steps <= 8);
    assert(steps == 7);
    expect(COVER_DIR "/3f/fc00000000000001-176x240.bin", "gray");
    expect(COVER_DIR "/01/0400000000000002-176x240-list.bin", "list");
    // 保留旧件以便目标损坏时回退。/ Retain the legacy alternate for a corrupt-target fallback.
    expect(COVER_DIR "/02/0800000000000003-176x240.bin", "fresh");
    assert(access(COVER_DIR "/03/0c00000000000004-noart.bin", F_OK));
    expect(COVER_LEGACY_DIR "/wr-0000000000000006-10-20.bin", "txt");
    DIR *dir = opendir(COVER_LEGACY_DIR); int left = 0;
    for (struct dirent *e; (e = readdir(dir));) if (e->d_name[0] != '.') ++left;
    closedir(dir); assert(left == 2);
    // 只剩 TXT 记录：目录保留；记录也没了以后删除目录。/ Only TXT records left keeps the folder; once gone, remove it.
    assert(!book_cover_legacy_sweep_step() && !access(COVER_LEGACY_DIR, F_OK));
    snprintf(p, sizeof(p), "%s", COVER_LEGACY_DIR "/wr-0000000000000006-10-20.bin"); assert(!unlink(p));
    assert(!book_cover_legacy_sweep_step() && !access(COVER_LEGACY_DIR, F_OK));
    assert(!unlink(COVER_LEGACY_DIR "/0800000000000003-176x240.bin"));
    assert(!book_cover_legacy_sweep_step() && access(COVER_LEGACY_DIR, F_OK));
    assert(!mkdir(COVER_LEGACY_DIR,0777));
    put(COVER_LEGACY_DIR "/1000000000000001-176x240.bin","only-copy");
    put(COVER_DIR "/04","blocks-group-directory");
    assert(!book_cover_legacy_sweep_step());
    expect(COVER_LEGACY_DIR "/1000000000000001-176x240.bin","only-copy");
    assert(!unlink(COVER_DIR "/04"));
    assert(book_cover_legacy_sweep_step());
    expect(COVER_DIR "/04/1000000000000001-176x240.bin","only-copy");
    put(COVER_LEGACY_DIR "/2000000000000002-176x240.bin","last-copy");
    fail_stat=true;assert(!book_cover_legacy_sweep_step());
    expect(COVER_LEGACY_DIR "/2000000000000002-176x240.bin","last-copy");
    fail_stat=false;fail_rename=true;assert(!book_cover_legacy_sweep_step());
    expect(COVER_LEGACY_DIR "/2000000000000002-176x240.bin","last-copy");
    fail_rename=false;assert(book_cover_legacy_sweep_step());
    expect(COVER_DIR "/08/2000000000000002-176x240.bin","last-copy");
    puts("PASS: cover migration retains existing alternates and sole sources on mkdir/stat/rename failures, then retries successfully");
    return 0;
}
'''
    unit = unit.replace('ROOT', '"' + str(root) + '"')
    c = Path(tmp) / 't.c'; exe = Path(tmp) / 't'
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
