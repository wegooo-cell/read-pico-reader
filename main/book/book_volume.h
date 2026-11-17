#pragma once

#include <stdbool.h>
#include <stddef.h>

/// 卷对应一段可见目录位置。/ One volume spans a range of visible TOC positions.
typedef struct {
    size_t begin, end; ///< 半开范围 / Half-open range
    size_t title; ///< 原卷名索引或 SIZE_MAX / Source volume label or SIZE_MAX
    unsigned number; ///< 备用卷号，零为卷首内容 / Fallback ordinal, zero for front matter
    bool include_title; ///< 卷名目标可另行跳转 / Keep the volume's original target selectable
} book_volume_t;
