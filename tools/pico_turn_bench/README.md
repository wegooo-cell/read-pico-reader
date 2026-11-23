# Pico 翻页基准测试 / Pico turn benchmark

在真机上量每次翻页各阶段的耗时，作为改固件前后的对照。只用于本地测试，计时版固件不能交付。

Measures per-stage page-turn time on a real Pico, as a before/after yardstick for firmware changes. Local testing only; timing builds are never delivered.

## 组成 / Parts

| 文件 | 作用 |
| --- | --- |
| `make_bench_epub.py` | 生成固定的基准书 `kiiko-bench.epub`：纯文字章（按真实字频抽字）、插图章（JPEG 与 PNG）、短章、生僻字章。同一版本的脚本生成的书逐字节相同。需要 Pillow。 |
| `turn_bench.py` | `flash` 只写 ota_0（先核对分区表、启动槽，首次备份原应用）；`run` 跑一轮并汇总；`compare` 对比两次结果；`restore` 刷回备份。需要 ESP-IDF 的 Python（pyserial、esptool）。 |

固件侧：`--pico-turn-timing` 构建打开 `KIIKO_TURN_TIMING` 和本地 USB 测试命令，每次翻页在串口打一行 `turn: TURN …`。默认构建里这些代码都不编入。

Firmware side: a `--pico-turn-timing` build enables `KIIKO_TURN_TIMING` plus the local USB probe, printing one `turn: TURN …` line per page turn. Default builds compile none of it.

## 用法 / Usage

```
python3 tools/pico_turn_bench/make_bench_epub.py -o build/bench/kiiko-bench.epub   # 拷到 TF 卡根目录 / copy to the TF card root
python3 tools/build_firmware.py --board pico --idf-pico <IDF v6.1> --python <IDF python> --pico-turn-timing --output build/turn-timing
<IDF python> tools/pico_turn_bench/turn_bench.py flash
<IDF python> tools/pico_turn_bench/turn_bench.py run --label <tag>
<IDF python> tools/pico_turn_bench/turn_bench.py compare build/bench/results/<A>.json build/bench/results/<B>.json
<IDF python> tools/pico_turn_bench/turn_bench.py restore
```

一轮约 3 分钟。脚本会恢复测试前的翻页效果和低残影设置；只改基准书的进度（`perf open` 只接受路径含 `kiiko-bench` 的文件）。打开串口会让设备重启。基准书第一次打开后会进入书架，可以手动移出。字号不同的两次结果，排版相关数字不可比。

A run takes about three minutes. The script restores the turn effect and low-ghost settings, and only touches the bench book's progress. Opening the serial port resets the device. Results at different font sizes are not comparable for layout-dependent numbers.

各列和场景的含义见 [`.claude/skills/pico-turn-bench/SKILL.md`](../../.claude/skills/pico-turn-bench/SKILL.md)。/ Column and scenario meanings are in the skill file.
