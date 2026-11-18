#!/usr/bin/env python3
"""Check actual reader clock, saved chapter identity, and old NVS reading data."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#define KIIKO_BOARD_METALIO47 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NVS_NOT_FOUND 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
typedef int esp_err_t;
typedef struct {uint32_t file_size, byte_off; uint16_t chapter; uint8_t pct;} book_progress_t;
typedef struct {time_t unix_sec; bool time_synced;} pmu_snapshot_t;
static pmu_snapshot_t pmu;
static const pmu_snapshot_t *read_pico_pmu_get(void) {return &pmu;}
static uint64_t wall_ms;
static int fake_time(struct timeval *tv, void *tz) {
    (void)tz; tv->tv_sec = wall_ms / 1000; tv->tv_usec = (wall_ms % 1000) * 1000; return 0;
}
#define gettimeofday fake_time
static struct {char key[24]; size_t size; uint8_t data[512];} blobs[5];
static int nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name,"mt_ticket")); (void)mode; *handle = 1; return ESP_OK;
}
static int nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *len) {
    assert(h == 1);
    for (size_t i = 0; i < 5; ++i) if (!strcmp(key, blobs[i].key)) {
        assert(*len >= blobs[i].size);
        *len = blobs[i].size; memcpy(data, blobs[i].data, *len); return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len) {
    assert(h == 1 && len <= sizeof(blobs[0].data));
    for (size_t i = 0; i < 5; ++i) if (!blobs[i].key[0] || !strcmp(key, blobs[i].key)) {
        strcpy(blobs[i].key, key); blobs[i].size = len; memcpy(blobs[i].data, data, len); return ESP_OK;
    }
    assert(false); return ESP_ERR_INVALID_STATE;
}
static int nvs_commit(nvs_handle_t h) {assert(h == 1); return ESP_OK;}
static void nvs_close(nvs_handle_t h) {assert(h == 1);}
#include "main/book/book_ticket_data.inc"
static int64_t s_stats_activity_ms;
static uint32_t s_session_read_ms, s_session_turns;
static bool app_book_reader_body_visible(void) {return true;}
#include "main/apps/book_ticket_reader.inc"

typedef struct {size_t offset, len; bool chapter_start,heading_subtitle;} blk_t;
static blk_t blocks[] = {{.offset=0,.len=7,.chapter_start=true}, {.offset=7,.len=4}, {.offset=11,.len=8,.chapter_start=true}};
static const blk_t *s_blocks = blocks;
static const char *s_text = "ChapterbodyChapter2";
static size_t s_page, s_chapter = 2, s_block_count = 3;
static size_t s_selected_toc = SIZE_MAX, s_selected_toc_offset;
static bool txt;
#define BOOK_KIND_TXT 1
static int book_kind(void) {return txt;}
static size_t reader_page_offset(size_t page) {return page;}
static size_t book_navigation_position(size_t chapter, size_t off) {
    assert(chapter == 2);
    return off > 0 ? 1 : SIZE_MAX;
}
static size_t book_navigation_count(void) {return 2;}
static size_t book_navigation_chapter(size_t toc) {return txt ? toc : toc + 1;}
static int book_navigation_title(size_t toc, char *title, size_t cap) {
    snprintf(title, cap, "%s", toc ? "第一章 漫长的章节" : "前言"); return ESP_OK;
}
static int book_chapter_title(size_t chapter, char *title, size_t cap) {
    assert(chapter == 2); snprintf(title, cap, "Chapter1"); return ESP_OK;
}
#include "main/apps/book_reader_chapter.inc"
'''

source += r'''
int main(void) {
    const uint32_t day = 20500;
    wall_ms = day * TICKET_DAY_MS - 8 * 3600 * UINT64_C(1000) - 500;
    legacy_ticket_stats_t legacy = {.magic=0x52505431, .day=day-1, .total_seconds=136,
        .today_seconds=100, .total_turns=15, .today_turns=10};
    struct {uint32_t magic, day[30], seconds[30];} old = {.magic=0x52504831};
    old.day[(day-2)%30]=day-2;
    old.seconds[(day-2)%30]=36;
    nvs_set_blob(1,"stats",&legacy,sizeof(legacy));
    nvs_set_blob(1,"heatmap",&old,sizeof(old));
    app_book_ticket_sample(1000,true);
    wall_ms += 1000;
    app_book_ticket_sample(2000,false);
    app_book_ticket_sample(3000,false);
    app_book_ticket_sample(4000,true);
    wall_ms += 500;
    app_book_ticket_sample(4500,true);
    ticket_count_turn();
    app_book_ticket_sample(4600,false);
    app_book_ticket_sample(60000,true);
    flush_ticket_stats();
    ticket_stats_t stats;
    stats_load(&stats);
    assert(s_session_read_ms == 1600 && s_session_turns == 1);
    assert(stats.total_ms == 137600 && stats.total_turns == 16);
    assert(stats_day(&stats,day).milliseconds == 1100 && stats_day(&stats,day).turns == 1);
    assert(stats_day(&stats,day-1).milliseconds == 100500 && stats_day(&stats,day-1).turns == 10);
    assert(stats_day(&stats,day-2).milliseconds == 36000);
    assert(stats_day(&stats,day+1).milliseconds == 0);
    wall_ms = 0;
    assert(book_ticket_local_time_ms() == 0);
    assert(book_ticket_record(0,500,1) == ESP_OK);
    stats_load(&stats);
    assert(stats.total_ms == 138100 && stats.total_turns == 17 && stats_day(&stats,day).turns == 1);
    char label[128];
    book_ticket_format_time(label, sizeof(label), stats.total_ms);
    assert(!strcmp(label,"2 分 18 秒"));

    txt = true;
    s_page = 0; reader_current_chapter(label,sizeof(label)); assert(!label[0]);
    s_page = 10; reader_current_chapter(label,sizeof(label)); assert(!strcmp(label,"第一章 漫长的章节"));
    // Position zero may be from a preceding physical TXT segment.
    s_chapter = 2; s_page = 50;
    reader_current_chapter(label,sizeof(label)); assert(!strcmp(label,"第一章 漫长的章节"));
    txt = false;
    s_selected_toc = 1; s_selected_toc_offset = 8;
    s_page = 7; reader_current_chapter(label,sizeof(label)); assert(!strcmp(label,"Chapter"));
    s_page = 8; reader_current_chapter(label,sizeof(label)); assert(!strcmp(label,"第一章 漫长的章节"));
    s_page = 11; reader_current_chapter(label,sizeof(label)); assert(!strcmp(label,"Chapter2"));

    book_progress_t position = {.file_size=300, .chapter=3, .byte_off=33, .pct=42};
    book_ticket_note_position("/sdcard/a.txt",&position,label,true);
    assert(position_for_ticket("/sdcard/a.txt",300,true,false,&position,label) &&
           position.pct == 42 && !strcmp(label,"Chapter2"));
    memset(&s_position,0,sizeof(s_position));
    assert(position_for_ticket("/sdcard/a.txt",300,false,true,&position,label) && !strcmp(label,"Chapter2"));
    position.byte_off = 36;
    assert(position_for_ticket("/sdcard/a.txt",300,false,true,&position,label) && !label[0]);
    assert(!position_for_ticket("/sdcard/b.txt",300,false,false,&position,label) && !label[0]);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kiiko-ticket-data-") as directory:
    tmp = Path(directory)
    unit = tmp / "ticket.c"
    unit.write_text(source)
    binary = tmp / "ticket"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(ROOT), str(unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: real reader clock/pause, midnight, legacy data, title lookup and position matching")
