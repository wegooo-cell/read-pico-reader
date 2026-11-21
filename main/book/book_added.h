/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：书架首次收录顺序独立保存在对应介质，不占用逐书NVS或更改书籍内容。
 * English: Keep first-added ordering on the matching medium without per-book NVS or book edits.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *path; ///< 完整路径身份 / Complete path identity
    uint64_t *order; ///< 返回首次收录序号；失败保留调用方后备值 / First-added order; retain fallback on failure
} book_added_item_t;

/// 在非绘制阶段批量读取并登记；临时数组可重新排序。/ Load/register outside rendering; may sort the temporary array.
void book_added_load(book_added_item_t *items, size_t count);
/// 文件移动后保留首次收录顺序，回读确认后返回true。/ Preserve and verify first-added order after a move.
bool book_added_migrate(const char *old_path, const char *new_path);
/// 删除后清除旧身份的顺序，重新导入会重新登记。/ Forget deleted identities so a reimport registers anew.
void book_added_forget(const char *path);
