# PICO 存储检查与修复 / Storage checks and repair

入口：设置 → 升级和恢复 → 存储检查与修复。

Open Settings → Upgrade and recovery → Storage checks and repair.

“检查并尝试恢复”会释放阅读和字体使用的文件，重新尝试挂载不可用的 TF 卡，分别检查书目录、机内存储与进度备用存储，再通知书架重新核对。它保留设置、书籍、阅读记录和上架名单；不会把重新扫描当成清空书架，也不会格式化 TF。机内文件系统格式无法识别时，检查结果会保留该错误；可用的 TF 书籍和进度备用存储可继续使用。

Checks close file consumers, retry an unavailable TF mount, report book-directory/internal/progress-storage availability separately and invalidate the shelf for reconciliation. They preserve settings, books, progress and enrollment without formatting TF or resetting the library. An unrecognized internal filesystem remains an explicit error; usable TF books and fallback progress remain available.

需要重建机内存储时：

1. TF 卡至少预留 6 MB，退出传输、USB 磁盘、设备同步和升级任务。
2. 点击“备份后重建”，阅读清空范围，再确认开始备份。这一步不会清空机内存储。
3. 等待原始分区备份完整写入 TF 并读回校验；同时逐块比较原分区和备份，校验完整 SHA-256。
4. 备份通过后，再次点击“确认清空并重建”。固件会重新核验备份与原分区，之后才清空并重新建立机内文件系统。
5. 查看最终结果。原始备份保留在 TF 根目录 `kiikoread-recovery/storage-…/`，包括 `storage.raw` 和描述大小、位置、SHA-256 的 `backup.json`。

For rebuilding, leave at least 6 MB free on TF and exit all transfers, USB storage, device synchronization and update tasks. Accept the first warning to create a backup only. After durable write, full readback/SHA-256 and comparison with the source, accept the separate clear/rebuild confirmation. The firmware revalidates source and backup before clearing the internal volume. Keep the resulting `storage.raw` and `backup.json` under `TF:/kiikoread-recovery/storage-…/`.

重建会清空机内书籍、缓存与机内记录；设置所在的 NVS、应用、TF 书籍/备用进度及 VCOM 保留。原始备份不会自动恢复旧文件，后续文件恢复需单独处理。正常可挂载的机内存储不会被此入口清空。

Rebuilding clears internal books, caches and records while retaining settings in NVS, applications, TF books/fallback progress and VCOM. A raw backup does not automatically restore old files; recovering its contents is a separate operation. A normally mounted internal volume is not cleared by this entry.

备份空间不足、读写/校验失败、备份或原分区变化、存储被占用、卡被移除、确认过期、取消、离页、锁屏或重启，均不能触发后续自动重建。开始清空后应保持供电；即使重建失败，已经验证的原始备份仍保留。重启后不会沿用上次确认自动继续清空。

Insufficient space, IO/integrity failure, changed backup/source, competing ownership, card removal, expired confirmation, cancellation, navigation, locking or reset cannot authorize an automatic rebuild. Keep power connected once clearing begins. A verified original backup remains even if rebuilding fails; a reboot never resumes an old destructive confirmation.
