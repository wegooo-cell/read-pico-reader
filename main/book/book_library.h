/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {BOOK_LIBRARY_UNSET, BOOK_LIBRARY_SELECTED, BOOK_LIBRARY_ALL} book_library_mode_t;
/// The selected library belongs to the medium; it never lives in per-book NVS keys.
book_library_mode_t book_library_mode(void);
esp_err_t book_library_error(void);
bool book_library_contains(const char *path);
/// Explicit imports are additive. Choosing all keeps every book under the configured roots.
esp_err_t book_library_begin(bool all);
/// Begin a finite additive snapshot; legacy ALL must first be seeded from the complete shelf.
esp_err_t book_library_begin_snapshot(void);
esp_err_t book_library_add(const char *path);
esp_err_t book_library_commit(void);
void book_library_abort(void);
/// 清空上架身份并持久化待导入，不删书籍或进度。/ Persist an unimported empty shelf without changing books/progress.
esp_err_t book_library_reset(void);
esp_err_t book_library_include(const char *path);
/// Drop only RAM on media ownership/change; retain both checked disk generations.
void book_library_invalidate(void);

/// Enumerate finite shelf identities, including unread books moved outside Books.
typedef bool (*book_library_visit_fn)(const char *path,void *context);
esp_err_t book_library_list(book_library_visit_fn visit,void *context);

/// Rename a finite member identity atomically; retain the old index on failure.
esp_err_t book_library_move(const char *old_path,const char *new_path);

/// 在当前事务移出成员，commit一次批量保存；不触碰文件和进度。/ Remove within a transaction; commit once for the batch.
esp_err_t book_library_remove(const char *path);
