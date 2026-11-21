/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {BOOK_IMPORT_NONE,BOOK_IMPORT_EXPLICIT,BOOK_IMPORT_COVERED} book_import_selection_t;
/// Choices are separate from shelf membership and survive directory/page/view changes.
book_import_selection_t book_import_selection(const char *path);
esp_err_t book_import_select(const char *path,bool directory,bool selected);
unsigned book_import_choice_count(void);
void book_import_reset(void);
/// Discovery is bounded and cancellable. One checked shelf commit follows a complete walk.
esp_err_t book_import_begin(bool (*hidden)(const char *),bool (*unhide)(const char *));
esp_err_t book_import_step(unsigned nodes);
bool book_import_running(void);
void book_import_cancel(void);
unsigned book_import_result_count(void);
unsigned book_import_unhide_failures(void);
