#pragma once
#include <stdbool.h>

static inline bool app_book_request_open(const char *path) { (void)path; return true; }
static inline bool app_image_request_open(const char *path) { (void)path; return true; }
static inline void app_files_request_folder(int folder) { (void)folder; }
static inline void app_book_request_manage(void) {}
static inline bool app_book_reader_body_visible(void) { return false; }
extern bool test_reader_open;
static inline bool app_book_reader_open(void) { return test_reader_open; }

#include <stddef.h>
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen);

void app_book_cover_mode_changed(void);
void app_home_cover_mode_changed(void);

#if KIIKO_BOARD_METALIO47
bool app_book_volume_key(app_ctx_t *ctx, int direction, app_redraw_t *redraw);
bool app_book_ai_bookmark(void);
#endif

// 中文：计时行为由实际阅读控制测试覆盖。/ English: Timed behavior is covered by reader-control runtime tests.
static inline void app_book_pause_auto(void) {}

static inline bool app_book_status_bar_visible(void){return true;}
