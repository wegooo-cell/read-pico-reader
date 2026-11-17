#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "book_store.h"
#include "esp_err.h"

typedef char book_weread_path_t[BOOK_STORE_PATH_MAX];

enum {
    BOOK_WEREAD_OPTIONS = 4,
    BOOK_WEREAD_PREVIEW_W = 72,
    BOOK_WEREAD_PREVIEW_H = 98,
    BOOK_WEREAD_PREVIEW_BYTES = BOOK_WEREAD_PREVIEW_W * BOOK_WEREAD_PREVIEW_H,
};
#define BOOK_WEREAD_NO_ROW UINT32_MAX

typedef enum {
    BOOK_WEREAD_WAITING, BOOK_WEREAD_CHECKING, BOOK_WEREAD_NOT_FOUND,
    BOOK_WEREAD_MULTIPLE, BOOK_WEREAD_SAVING, BOOK_WEREAD_SAVED, BOOK_WEREAD_FAILED,
} book_weread_row_state_t;

typedef struct {
    book_weread_row_state_t state;
    unsigned options;
    esp_err_t error;
} book_weread_cover_row_t;

typedef struct {
    char title[192], author[128];
    // Preview can be requested again if the user reopens the option list.
    bool preview_checked, preview_ready, from_zlib;
} book_weread_cover_option_t;

typedef struct {
    bool active, running, saving, zlib_unavailable;
    unsigned total, done, saved, multiple, changes;
    esp_err_t error;
} book_weread_cover_status_t;

bool book_weread_cover_is_txt(const char *path);
bool book_weread_cover_metadata(const char *path, char *title, size_t title_cap,
                                char *author, size_t author_cap);
/// Persistent identity of a valid downloaded thumbnail, or zero when unavailable.
uint64_t book_weread_cover_stamp(const char *path);
bool book_weread_cover_read(const char *path, uint8_t *out, unsigned width, unsigned height);

/// 成功时接管由调用方分配的路径表。/ Take ownership of the caller's path array on success.
esp_err_t book_weread_cover_start(book_weread_path_t *paths, unsigned count);
void book_weread_cover_cancel(void);
book_weread_cover_status_t book_weread_cover_status(void);
unsigned book_weread_cover_revision(void);
book_weread_cover_row_t book_weread_cover_row(unsigned row);
void book_weread_cover_focus(unsigned row);
bool book_weread_cover_option(unsigned row, unsigned option, book_weread_cover_option_t *out,
                              uint8_t preview[BOOK_WEREAD_PREVIEW_BYTES]);
bool book_weread_cover_choose(unsigned row, unsigned option);
