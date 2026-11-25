---
name: pico-turn-bench
description: 在连接的 Pico 上跑翻页基准测试（分段计时），并和之前的结果对比。用户说“跑基准”“翻页基准”“测一下翻页速度”“和上次对比”，或固件改动涉及翻页、预绘、刷屏、字体、插图时使用。Run the Pico page-turn benchmark on the connected device and compare with earlier results.
---

# Pico 翻页基准测试 / Pico turn benchmark

工具在 `tools/pico_turn_bench/`。脚本只打印一张汇总表，原始日志和 JSON 在 `build/bench/results/`。**不要读原始日志或 JSON 全文**，除非汇总表里有异常需要查某几行，那时用 grep 取那几行。

Tools live in `tools/pico_turn_bench/`. The scripts print one summary table; raw logs and JSON go to `build/bench/results/`. Do not read them whole; grep specific lines only when the table shows an anomaly.

下文的 `$PY` 指 ESP-IDF v6.1 的 Python 环境（自带 pyserial 和 esptool），路径因机器而异，先在本机找到它；生成书用带 Pillow 的 `python3`。

`$PY` is the ESP-IDF v6.1 Python environment (with pyserial and esptool); find it on the local machine first. Generating the book needs a `python3` with Pillow.

## 前提 / Preconditions

1. 设备用 USB 连着，只有一个 `/dev/cu.usbmodem*`。
2. TF 卡根目录有 `kiiko-bench.epub`。没有就先生成，再请用户拷进 TF 卡根目录（读卡器、U 盘模式或 Wi-Fi 传书都行；本工具没法自己往卡里写）：
   ```
   python3 tools/pico_turn_bench/make_bench_epub.py -o build/bench/kiiko-bench.epub
   ```
   同一版本的脚本生成的书逐字节相同，打印的 sha256 可以用来核对。
3. 设备上要是计时版固件（`--pico-turn-timing`）。只有它带 USB 测试命令和 `TURN` 日志，**不能作为交付固件**。

## 流程 / Steps

1. 构建计时版（要测哪份代码就在哪份代码上构建）：
   ```
   python3 tools/build_firmware.py --board pico --idf-pico <ESP-IDF v6.1 目录> --python $PY --pico-turn-timing --output build/turn-timing
   ```
   IDF 6.1 要求 GCC esp-15.2 工具链，它的 `bin` 目录要在 `PATH` 里。`--tools` 在装了多个版本时可能选到旧的 GCC，此时不要用它，改为手动设好 `IDF_TOOLS_PATH` 和 `PATH`；选错过一次后，删掉 `build/turn-timing/pico/build/CMakeCache.txt` 再构建。
2. 刷机，只写 ota_0：
   ```
   $PY tools/pico_turn_bench/turn_bench.py flash
   ```
   脚本会先核对设备分区表和包一致、设备从 ota_0 启动，首次运行时把原应用备份到 `build/device-backup/dev_ota0.bin`（已有备份就不覆盖）。引导程序、分区表、otadata、NVS 和用户存储都不动。不满足条件时脚本拒绝写入，**不要绕过**，告诉用户原因。
3. 跑基准（约 3 分钟），`--label` 写分支名或改动名：
   ```
   $PY tools/pico_turn_bench/turn_bench.py run --label <tag>
   ```
   打开串口会让设备重启，这是正常的。脚本会：
   - 记下用户原来的翻页效果和低残影设置，结束后（包括出错时）恢复；
   - 只改基准书的阅读进度（`perf open` 拒绝路径里不含 `kiiko-bench` 的书），不碰用户的书；
   - 第一次打开会让基准书进入书架，告诉用户可以手动移出。
4. 对比（A 在前，B 在后）：
   ```
   $PY tools/pico_turn_bench/turn_bench.py compare build/bench/results/<A>.json build/bench/results/<B>.json
   ```
   两次的字号（px）不同，排版相关的数字就不可比，脚本会提示。
5. 测完问用户要不要刷回原固件：
   ```
   $PY tools/pico_turn_bench/turn_bench.py restore
   ```

## 怎么读表 / Reading the table

| 列 | 含义 |
| --- | --- |
| hit | 预绘命中率（下一页在翻页前已经画好） |
| fn | 翻页函数耗时，扣掉跨章载入和存进度 |
| img | 插图/背景准备（TURN 行的 prep+pre） |
| draw | 正文绘制（命中时约 10 ms，只是拷贝） |
| push | 主扫描推屏 |
| join | 推屏结束后等后台把下一页画完 |
| total | 从按键到推屏结束 |
| frm | 实际扫描帧数 |
| miss | 未命中的字形数 |

| 场景 | 测什么 |
| --- | --- |
| text_fwd / text_back / alternate | 普通翻页往前、往回、来回对照；效果 0，低残影关 |
| lowghost / fastgray / ripple | 低残影、快速灰阶、水波纹 |
| image / after_image / image_ch_text | 图文页、图后第一页文字、插图章里的其他文字页 |
| chapter_fwd / chapter_back | 跨章 |
| cold_text | 换一批生僻字后的翻页（字形缓存冷） |

基线放在 `build/bench/results/`，新结果和最近一次基线对比。参考值（2026-11-23，rc110 加 PR #111 往回翻预绘，计时版，基准书 v2，字号 50）：
- 普通翻页命中时 fn 约 10.8 ms；推屏普通 485、低残影 669、快速灰阶 392、水波纹 546 ms。
- 往回翻命中率 0.50，来回对照 0.25。
- 图文页、图后第一页全部未命中：图文页 total 约 1190 ms（插图准备 428），图后第一页约 800 ms。
- 跨章 total 约 1660 ms；存进度每次约 157 ms。

汇总表之外只需要报告和上次相比的变化，以及 warning 行（比如某个场景意外跨了章，通常是字号太小、章节不够长）。
