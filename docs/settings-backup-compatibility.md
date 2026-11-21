# 配置备份兼容性

新版两款机型共用 TF 根目录 `Kiikoread-settings.backup`。标准文件写历史 v25 格式，可直接由 S31 RC114、PICO RC108 及新版读取；保存阅读线格式等新字段另放 `Kiikoread-settings.full.backup`。`Kiikoread-settings.pair` 记录两者的完整指纹。复制备份时三个文件一起复制。上一代标准文件保留为 `.previous`。

新版仅在标准文件与完整文件的指纹均匹配时恢复完整配置。旧固件重新保存标准文件后，新版恢复旧固件刚保存的设置，不会覆盖成旧的完整副本。损坏的完整文件不影响有效标准文件。恢复会先完整校验并确认阅读记录存储可用，再写配置。

阅读范围扩展后，完整文件使用 v27：字号 20–120、行高 100%–300%、段距 −100%～100%（每 10% 一档）。标准 v25 文件将字号投影到最多 90、行高投影到 110%–150%、段距投影到旧版最近的可用档位；新范围的精确设置保留在配对完整文件中。旧备份里的 18／19 字号迁移为 20，旧段距迁移到最近的 10% 档。阅读位置不随字号迁移清零。负段距压缩段间的行间空白，最紧不重叠字格。

The v27 full copy preserves 20–120px type, 100–300% line height and −100–100% paragraph gaps in 10% steps. The v25 interchange projects settings into the recipient's older ranges; retain the paired full file for exact values. Legacy 18/19px settings migrate to20 without resetting reading positions; legacy paragraph gaps round to10% stops. Negative gaps compress leading without overlapping type cells.

S31 与 PICO 可互相恢复通用配置。机型不支持的硬件手势归零；不支持的翻页效果回普通；S31 的 20 页全刷间隔在 PICO 映射为 15 页。字体和图片备份保存的是路径，不包含资源文件，资源不存在时回退内建选项。

更老固件无法认识后来新增的字段。需要降到更早版本时，从完整备份导出目标支持的格式，保留原文件：

```sh
python3 tools/convert_settings_backup.py Kiikoread-settings.full.backup compatible.backup --format 20
```

| 历史固件 | 兼容格式 |
| --- | --- |
| PICO RC90 | 12 |
| PICO RC91 | 15 |
| PICO RC92–95 | 17 |
| PICO RC96–104、S31 RC98–109 | 20 |
| PICO RC105–106、S31 RC110–111 | 22 |
| PICO RC107–108、S31 RC112–114 | 25 |

导出后使用目标固件的备份文件名：较新版是 `Kiikoread-settings.backup`，更早版本是 `Pico-settings.backup` 或 `Metalio-S31-settings.backup`。旧版不支持的选项只保留在完整原件，不宣称旧固件能恢复其尚未实现的功能。工具拒绝校验失败、截断或覆盖原件。

可用仓库历史标签对真正的旧版解析器回归，测试不修改旧固件源码、不连接设备：

```sh
python3 tools/settings_backup_pair_host_test.py /tmp/test-native.backup
python3 tools/settings_backup_legacy_host_test.py --work-dir /tmp/kiiko-legacy-readers --native /tmp/test-native.backup
```
