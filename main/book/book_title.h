/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：按图书路径保存用户书名覆盖，不修改原文件和阅读进度。
 * English: Keep per-path display titles without editing book files or progress.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

bool book_title_get(const char *path, char *out, size_t cap);
esp_err_t book_title_set(const char *path, const char *title);
esp_err_t book_title_clear(const char *path);
/// 从文件名生成统一显示书名；识别Z-Library来源、作者及长宣传尾注，不修改路径。
/// Derive a shared display title, recognizing Z-Library source/author/promotion tails without changing paths.
bool book_title_from_path(const char *path, char *out, size_t cap);
/// 清除导入元数据尾部的已知转换器残留。/ Strip known converter debris at the end of imported metadata.
void book_title_clean_import(char *title);

/// 有界文件名识别，不做IO；作者可为空，识别不确定时保留去扩展名的原名。
/// Bounded filename parsing with no IO; author may be NULL and uncertain names keep their original stem.
bool book_name_from_path(const char *path,char *title,size_t title_cap,char *author,size_t author_cap);

/// 返回自定义书名或识别后的文件名，作者沿用文件名识别。/ Resolve the saved title or parsed filename and its author.
bool book_display_name_from_path(const char *path, char *title, size_t title_cap,
                                 char *author, size_t author_cap);

/// 清理显示作者的国别和外文别名；返回是否有中文名，无中文名保留原文，不修改缓存源。
/// Remove country tags/foreign aliases for display; return whether Chinese remains, keeping original names when none exists.
bool book_author_clean_display(char *author,size_t cap);
