# AGENTS.md

面向 AI agent 的仓库入口。人类也可读。产品概述、硬件表、引脚、编译命令见 [README.md](README.md)。

Agent entry for this tree. Humans can read it too. Product overview, hardware, pinout and flash commands live in [README.md](README.md).

新人（人或 agent）上手看 [docs/ONBOARDING.md](docs/ONBOARDING.md)；接手进行中的任务先看 [docs/HANDOFF.md](docs/HANDOFF.md)。

Newcomers (human or agent): read [docs/ONBOARDING.md](docs/ONBOARDING.md). Taking over an in-flight task: read [docs/HANDOFF.md](docs/HANDOFF.md) first.

## 当前工作：双板型本地整合 / Current work: local multi-board integration

用户2026-11-01明确要求推送在线刷机：本轮正式发布Pico rc104与S31 rc109，包含此前已测试的合并、书架及阅读交互修复和Pico普通翻页信息栏分离；同步GitHub Release、kiikoread.com与既有Pages OTA，保留rc103/rc108原字节及上一版本刷机/TF入口。本次指令取代本轮此前仅本地、不发布的限制。
User2026-11-01 explicitly authorizes online publication: release Pico rc104/S31 rc109 with the tested merges, shelf/reader fixes and Pico ordinary-turn metadata separation. Synchronize GitHub Releases, kiikoread.com and existing Pages OTA; retain original rc103/rc108 bytes and previous-version web/TF entries. This supersedes the earlier local-only restriction for this release.

用户2026-11-01追加：Pico普通翻页的信息栏对齐水波纹/速读，正常正文和图文翻页只输出阅读内容区域，顶部状态/标题和底部章节进度不再跟随擦白；页脚停下800ms后局刷。此要求取代PR55/62在Pico的信息栏合并范围，正文派生波形、手动/周期/入书清理及S31保持。
User2026-11-01 follow-up: align Pico ordinary metadata with ripple/Fast BW. Ordinary text/image turns present the reading-content region, excluding top status/title and bottom chapter/progress; update the footer after800ms idle. This supersedes PR55/62's Pico metadata scan extent, retaining body waveforms, manual/periodic/entry cleanup and S31 behavior.

用户2026-11-01追加：底部上滑回到非全屏并显示底部工具栏；顶部先显示时间、签名、电量，下一行恢复返回键、书名及收藏操作。进入书籍的非全屏也使用此布局；上方返回/书名/收藏与底部工具栏共同出现，进入全屏共同收起，所有工具栏入口先退出全屏。全屏关闭沉浸时仍只有状态栏，不显示书名。
User2026-11-01 follow-up: bottom swipes return to standard mode with bottom tools, a time/signature/battery row, then Back/book title/Favorite. Standard book entry shares this layout. Back/title/Favorite and bottom tools appear together and hide together on fullscreen entry; every tools entry first leaves fullscreen. Nonimmersive fullscreen retains its status-only header.

用户2026-11-01追加：全屏且关闭“全屏沉浸”时，顶部恢复时间、状态栏签名与电量，不再显示书名；预留完整状态栏，分钟更新与缓存翻页沿用原状态栏路径。沉浸开启时继续保留原铺屏样式。
User2026-11-01 follow-up: fullscreen with immersion off shows time, status signature and battery instead of the book title. Reserve the complete status row and retain its existing minute/cached-turn updates; immersion on retains the existing full-page style.

用户2026-11-01明确要求直接合并新PR65、66、67：采用GIF首帧、脚注卡片及JPEGDEC兜底，补齐解码边界和脚注状态回滚验证；保留当前书架、阅读手势和已确认的双板波形，不发布官网固件。
User2026-11-01 explicitly approves merging new PR65/66/67: GIF first frames, footnote cards and JPEGDEC fallback, with decoder boundary and footnote rollback checks. Preserve current shelf/reader gestures and approved board waveforms; do not publish website firmware.

用户2026-11-01明确要求直接合并PR61、62、64并按照作者实现：EPUB兼容按PR61，Pico/S31阅读输出按PR62，S31裁剪及补黑按PR64。此指令取代PR59/55在对应派生波形文件上的固定限制；原始表、板型隔离和供电/睡眠保护继续保留。界面与阅读交互保留本机最新改动，测试和基线按实际合入代码同步，不私自改写作者波形。
User2026-11-01 explicitly approves merging PR61/62/64 as authored: PR61 EPUB compatibility, PR62 Pico/S31 reader output, and PR64 S31 trimming/black boost. This supersedes PR59/55 pins for the affected derived-waveform files; retain raw tables, board isolation and power/sleep protections. Preserve the latest local UI/reader interactions; align tests/baselines without silently rewriting the author's waveforms.

用户2026-11-01确认书架管理预览并要求修改固件：沿用文件管理搜索/分页，列表和图标切换，数量、书架排序与选择同排；长按书籍或点击选择才显示圆形标记及删除书籍/清理进度/移出书架/返回。排序标识改为“书架排序”，不再显示来源筛选；分页和排序保留选择。暂不交付测试包，不合并仅要求审查的PR。
User2026-11-01 approves implementing the management preview: file-style search/paging, list/grid switching, count/order/select on one row; long presses or Select reveal circular marks and delete/clear/remove/back actions. Label the sort “书架排序”, remove source filtering, retain selections over paging/sorting. No test bundle yet; review-only PR requests do not authorize merging.

用户2026-11-01要求：开书为非全屏，顶部时间/签名/电量，章节与进度置于底部工具栏上方，工具栏进入即显示；中间单击只进入全屏，底部上滑退出全屏并显示工具栏；原全屏两种样式和硬件波形保留。
User2026-11-01: open standard with time/signature/battery and chapter/progress above visible tools; centre taps only enter fullscreen, bottom swipes leave it and show tools; retain both fullscreen styles and native waveforms.

用户2026-10-31再次明确：网页刷机只在kiikoread.com，保留原有GitHub首页跳转；GitHub Pages仍仅承担既有默认OTA来源，不新增独立备用刷机入口。
User2026-10-31 reconfirms kiikoread.com as the only web-flashing entry; retain the existing GitHub homepage redirect and the existing Pages OTA origin.

用户2026-10-31确认将当前书架回退版推送在线刷机：正式发布Pico rc103与S31 rc108，同步GitHub Release、官网与Pages OTA，保留Pico rc102/S31 rc107原版刷机与下载入口。
User2026-10-31 approves online publication of the shelf rollback build: release Pico rc103 and S31 rc108 to GitHub Releases, the official flasher and Pages OTA, retaining original Pico rc102/S31 rc107 install/download entries.

用户2026-10-31要求液态玻璃书架恢复PR56前样式：Pico与S31底板/折射参数回退，其余PR55/57/58与S31底层保留；先做本地测试包。
User2026-10-31 restores the pre-PR56 liquid shelf on Pico/S31, retaining PR55/57/58 and native S31 drivers; deliver a local test build.

用户2026-10-31批准合并PR55–58：Pico阅读按PR55更新，S31硬件/阅读波形仍以PR59为准；图片与书架功能适配双板源码。
User2026-10-31 approves PR55–58: adopt PR55 for Pico reader timing, retain PR59 S31 hardware/waveforms, and adapt shared image/shelf features to both boards.

用户2026-10-31指定合并PR59：底层驱动、供电/睡眠及阅读波形以PR59为准，功能界面以本机7278bd2为基线接入最新引导、语言、管理与阅读状态。此指令取代此前本地缓存保护和阅读波形回退决定；板型隔离与禁止永久OTP/eFuse写入仍适用。
User decision2026-10-31: merge PR59's drivers, power/sleep and reader waveforms, integrating the latest setup/locale/management/reader state from local7278bd2. This supersedes prior local cache-guard and waveform rollback choices; board isolation and permanent OTP/eFuse prohibitions remain.

用户2026-10-18明确要求同一源码选择pico/metalio47编译，取代两套独立维护应用源码的要求。共享阅读/UI保留同一份文件，平台驱动由boards选择；不得把S31供电/波形/引脚/分区用于S3。用户2026-10-25明确要求将Metalio-S31-kiikoread仓库公开，取代此前源码私有的限制；此后统一仓库main维护两板源码，沿用现有分文件许可和第三方权利。官网继续按白名单发布已验证固件与安装资源，源码公开不改变官网部署范围。
The user explicitly requested one source tree for pico/metalio47 on 2026-10-18. Shared reader/UI code lives once; boards select platform drivers. Never apply S31 power, waveforms, pins or partitions to S3. On 2026-10-25 the user explicitly requested making Metalio-S31-kiikoread public, superseding the earlier private-source restriction. The unified main now maintains both boards with existing per-file licenses and third-party rights. Website deployment remains an allowlist of verified firmware and installer assets; repository visibility does not broaden the website artifact scope.

构建入口tools/build_firmware.py，--board pico|metalio47|all；独立SDK/配置/构建/输出目录。芯片、项目名和分区必须匹配。Metalio原始GC48/GL48/DU20不得手改，供电和VCOM保留既有顺序；不得写永久TPS OTP或ESP eFuse。用户2026-10-19已明确授权直接调试连接的S31实现300ms，可按原MAC/精确分区/启动槽/已验证回退备份保护流程刷写本地调试与交付应用；该授权不包括其他设备、清数据或发布。调试USB命令入口不得留在最终交付固件。
Use tools/build_firmware.py with --board pico|metalio47|all and separate SDK/config/build/output directories. Validate chip/project/partitions. Preserve Metalio's raw GC48/GL48/DU20, power/VCOM sequencing, and never write permanent TPS OTP or ESP eFuses. On 2026-10-19 the user explicitly authorized direct debugging of the connected S31 toward 300ms, including local diagnostic/delivery application writes guarded by the approved MAC, exact partitions/boot slot and verified recovery backup. This does not authorize other devices, erasing data or publication. Remove the diagnostic USB command reader from the final delivery firmware.

## 构建 / Build

双板型统一入口是 `tools/build_firmware.py --board pico|metalio47|all`，先读 [docs/BOARDS.md](docs/BOARDS.md)。必须使用独立SDK/配置/依赖锁和产物目录；共享功能修改主源码，生成的设备视图不可作为维护源码。已有主机测试用 `board_source_view.py --tests` 生成指定设备视图后运行；CI分别选择Pico和S31，使用匹配SDK并隔离构建。
Use the board build driver and isolated SDK/config/lock/artifact directories. Edit maintained sources, not generated device views. Legacy host tests run in the board test view. CI separately builds Pico and S31 with matching SDKs and isolated outputs.

Pico需要 ESP-IDF v6.1。`components/read_pico/read_pico_flash_hpm.c` 依赖 v6 才有的 `esp_flash_chips/spi_flash_override.h`。

Requires ESP-IDF v6.1. `components/read_pico/read_pico_flash_hpm.c` needs `esp_flash_chips/spi_flash_override.h` (v6 only).

```
python3 tools/build_firmware.py --board pico --python /path/to/idf-python/bin/python
python3 tools/build_firmware.py --board metalio47 --python /path/to/idf-python/bin/python
```

- 板级选择层：`boards/<设备>/` 拥有身份、板级 sdkconfig、分区表、依赖锁与硬件后端；`cmake/board.cmake` 在 `project()` 之前选板，`-DKIIKO_BOARD=pico|metalio47`。/ Board selection layer: `boards/<device>/` owns identity, board sdkconfig, partitions, dependency lock and hardware back end; `cmake/board.cmake` selects before `project()`.
- 板级配置只放在 `boards/<设备>/`，仓库根不再有 `sdkconfig.defaults`、`sdkconfig.ci` 或 `partitions*.csv` 副本；`tools/board_layer_host_test.py` 守着这条规则。/ Board config lives only under `boards/<device>/`; the repository root keeps no `sdkconfig.defaults`, `sdkconfig.ci` or `partitions*.csv` copies, and `tools/board_layer_host_test.py` enforces that.

两块板共用同一份 `main/`、`components/` 维护源码；只有硬件、驱动和确有平台差异的行为才放进 `boards/<设备>/src/` 覆盖。/ Both boards share one `main/` and `components/` source; only hardware, driver and platform-specific behaviour live in the `boards/<device>/src/` overlay.

### 版本号与本地测试 / Versions and local tests

用户 2026-10-06 确认：本地测试沿用当前正式发布的固件版本号，用测试日期和说明区分；正式上线时才递增版本号，并同步设备、刷机页、升级包与日志。不要为每次本地修复另增 rc 编号。
Per the user's 2026-10-06 decision, local tests retain the current published firmware version and are distinguished by test date and notes. Increment only for publication, keeping the device, flasher, upgrade image and changelog in sync; do not add an rc for each local fix.

本地包放在被忽略的构建目录，禁止用同版本的测试二进制覆盖 `flash/` 中已经发布的固定版本下载文件。联网 OTA 需要不同版本号的专项测试时，先获得用户对该测试版本的明确授权。
Keep local bundles in ignored build directories; never replace immutable published downloads in `flash/` with a same-version test image. Use a different version for an OTA-specific test only when explicitly authorized by the user.

### 正式版发布 / Official releases

用户 2026-10-09 确认：之后每次发布正式版，都在 `wegooo-cell/read-pico-reader` 创建对应版本的 Git tag 和 GitHub Release，附上经验证的 TF/OTA 应用 Bin、完整 USB 刷机包、SHA-256 校验值和简要更新日志；所有附件来自同一次正式构建。
Per the user's 2026-10-09 decision, publish every future official firmware version with a matching Git tag and GitHub Release in `wegooo-cell/read-pico-reader`. Attach the verified TF/OTA application Bin, complete USB flash bundle, SHA-256 checksums and brief release notes, all from the same official build.

GitHub Release 与官网刷机、TF 下载和联网 OTA 同步发布。保留官网现有刷机清单、固件及版本下载地址；OTA 清单继续使用固件允许的官方 HTTPS 域名。核对三种交付的版本、大小、校验值和基础包兼容性，避免影响正在刷机或升级的用户。本地测试仍只生成本地包，不创建正式 Release。
Publish GitHub Releases alongside the web flasher, TF downloads and online OTA. Preserve existing website manifests, firmware and versioned download URLs; keep OTA URLs on the official HTTPS origin accepted by the firmware. Verify matching versions, sizes, checksums and base-layout compatibility across the deliveries so ongoing flashes and updates remain valid. Local tests remain local bundles without official Releases.

用户2026-10-21要求每次保留上一正式版的官网刷机与TF下载入口。替换正式清单前，使用tools/archive_current_release.py归档原Release的四件套和校验值；releases.json保留历次归档，manifest.previous_version指向上一版，发布工具校验该入口存在。不得以本地同号测试包代替原正式字节。TF包是完整应用镜像；引导程序、分区表及用户存储不由TF升级写入，整机四件套继续通过USB安装。
The user requires a previous-release web/TF entry for every publication. Archive the original Release parts with tools/archive_current_release.py before replacing metadata; retain earlier releases.json entries and verify manifest.previous_version. Never substitute a same-version local test. TF updates use a complete application image; bootloader, partition table and user storage remain untouched, with full base installation through USB.

## 目录职责 / Layout

用户2026-10-21明确指定GitHub Pages为默认OTA清单及应用来源，rc97真机已下载并校验原rc98成功。新固件优先GitHub清单、官网清单备用；原已发布rc97/98仍依照其原内置顺序请求。官网网页刷机独立保持，正式Bin及历史包继续归档GitHub Release；旧OTA不能直接使用会重定向的Release附件URL。保留双槽、HTTPS与完整SHA校验；不得因官网证书问题重现而自动改回官网应用地址，也不得外推为任意镜像或关闭TLS校验。后续更改默认来源先获得用户指示并验证旧信任库与实际下载。
The user selected GitHub Pages as the default OTA metadata/image source on 2026-10-21; a real rc97 device downloaded and verified original rc98. New builds prefer Pages metadata and retain the website fallback; released clients keep their baked-in order. Keep web flashing separate and archive original binaries/packages in Releases; legacy OTA cannot follow Release redirects. Retain dual slots, HTTPS and full SHA checks. Do not silently restore the incompatible site image origin or allow arbitrary mirrors; future default-source changes require user direction and legacy/device validation.

| 路径 / Path | 职责 / Role |
| --- | --- |
| `main/app_main.c` | 开机装配：板级 init、读设置、定 VCOM、开机图、字体，然后交给 `app_loop`。/ Boot wiring, then `app_loop`. |
| `main/app/app.h` | `app_desc_t` / `app_ctx_t` / `app_redraw_t` 契约。/ Page contract. |
| `main/app/app_loop.c` | 与页面无关的事件循环：触摸去抖、三键与菜单把手、切页、刷屏、锁屏、SD 字体延迟加载。/ Shared event loop. |
| `main/app/app_registry.c` | 菜单顺序。加页只改这张表。/ Menu table. |
| `main/app/app_config.h` | 均衡 / ALL_DU 刷新档位。/ Refresh profile. |
| `main/ui/` | `ui_kit` 排版常量与绘制原语；`ui_menu` 两层菜单。页面互不引用。/ Shared drawing; pages do not include each other. |
| `main/font/` | stb_truetype 字形缓存。`fallback.h` 是生成位图，勿手改。/ Glyph cache. Do not edit `fallback.h` by hand. |
| `main/factory/` | 设备功能自检与出厂 VCOM 标定。/ Device self-test and factory VCOM. |
| `main/display.*` `sleep.*` `settings.*` | 刷屏封装、睡眠/锁屏、NVS 设置。/ Present helpers, sleep/lock, NVS. |
| `components/read_pico/` | 板级 BSP：I2C、EPD 板定义与扫描时序、TF 卡、蜂鸣器、flash HPM。/ Board BSP. |
| `components/read_pico_search/` | 离线拼音/首字母/英文书名匹配，设备和网页共用；字表由工具生成。/ Shared offline pinyin, initials and English matching with a generated table. |
| `components/read_pico_pmu/` | CW32L010 协议主机端。线格式在 `read_pico_pmu_protocol.h`。/ PMU host. |
| `components/epdiy/` | 上游裁剪 fork（LCD 路径）。改动清单在其 LICENSE。不要为注释去改它。/ Trimmed upstream fork. Leave it alone for comment work. |
| `components/continuous_du/` | 连续 DU，触摸跟手。/ Continuous DU for finger tracking. |
| `components/cst836u/` `sc7a20h/` `fca9555/` `sy7636a/` | 芯片驱动。/ Chip drivers. |
| `components/e0470_epaper_waveform/` | 面板波形与裁剪。`waveforms/*.h` 是数据表。/ Panel waveforms. |
| `components/pwm_audio/` | PWM 音频（蜂鸣器底层之一）。/ PWM audio helper. |
| `docs/CHANGELOG.md` | 按版本简述对用户可见的功能变化与修复。/ Brief user-visible changes and fixes by version. |

功能操作见三份 README；页面契约以本文及 `main/app/app.h` 为准。/ See the READMEs for feature usage; this file and `main/app/app.h` define the page contract.

### 文档交付边界 / Documentation delivery boundary

对外交付功能实现、可复跑测试、功能说明及必要的维护契约。功能变化只在 `docs/CHANGELOG.md` 按版本轻量记录；代码演进由 Git 管理，不另建逐任务变更账本。
Deliver implementation, reproducible tests, feature documentation and necessary maintenance contracts. Summarize feature changes only in `docs/CHANGELOG.md`; Git owns code history, not per-task ledgers.

计划、检查点、过程审查、验收结果和调试日志在 `docs/local/` 或被忽略的构建目录内部闭环，不加入提交。长期有效的行为与限制整理进对应功能文档，不要求读者查内部计划。只有需要交接未完功能或责任时才按 `docs/HANDOFF.md` 提供最小必要摘要；本机账本仍不入库。
Keep plans, checkpoints, review records, acceptance results and debug logs in ignored `docs/local/` or build directories. Put lasting behavior and limits in feature documentation without dependencies on internal plans. Use `docs/HANDOFF.md` for the minimal necessary handoff of unfinished work or responsibility; the local ledger remains untracked.

### demo 页 / Demo pages

每个文件只导出 `app_desc_t`。顺序与 `app_registry.c` 一致。

One file, one `app_desc_t`. Order matches `app_registry.c`.

| 文件 / File | 演示 / Demo | 依赖 / Depends on |
| --- | --- | --- |
| `app_home.c` | 概览：I2C 普查、识别码、电池、构建时间。/ Overview. | `read_pico`, `read_pico_pmu` |
| `app_refresh.c` | GC16 / DU / 灰阶梯子与耗时。/ Refresh matrix. | `epdiy`, `e0470_epaper_waveform` |
| `app_reading.c` | 内置正文翻页。/ Reading demo. | `ttf_font` |
| `app_touch.c` | 两点跟手连续 DU、深睡唤醒。/ Touch + continuous DU. | `cst836u`, `continuous_du` |
| `app_axis.c` | 读数页 Live/Tap/Orient 与诊断页 Setup/Event/FIFO/Test。/ IMU pages. | `sc7a20h` |
| `app_power.c` | 电池与 SY7636A 只读。/ Battery + rails. | `read_pico_pmu`, `sy7636a` |
| `app_pmu.c` | CW32 协议五视图。/ Protocol views. | `read_pico_pmu` |
| `app_key.c` | 电源键原始事件。/ Power-key events. | `read_pico_pmu` |
| `app_sleep.c` | 浅睡 / 深睡 / 关机。/ Sleep modes. | `sleep`, `read_pico_pmu`, `sc7a20h` |
| `app_sd.c` | TF 卡与蜂鸣器。/ Card + buzzer. | `read_pico_sd`, `read_pico_buzzer` |
| `app_font_pick.c` | 卡上 TTF 列表与字重。/ Font picker. | `ttf_font` |
| `app_ioe.c` | FCA9555 Port-0。/ Expander pins. | `fca9555` |
| `app_selftest.c` | 设备功能自检入口。/ Device self-test UI. | `pmu_selftest` |
| `app_book.c` | TXT / EPUB 书架、目录、字号与逐书进度与 TF 阅读背景。/ TXT / EPUB shelf, TOC, size, progress and SD reading backgrounds. | `book_*`, `ttf_font` |
| `app_image.c` | 内置灰阶图与 TF 卡 JPG/PNG 图片对比。/ Built-in gray chart and SD JPG/PNG image comparison. | `book_cover`, `read_pico_sd` |
| `app_transfer.c` | 设备热点/已有 WiFi 传 TXT/EPUB，触屏/网页配网与热点二维码，离页停止；停止按钮返回进入前的位置。/ AP/STA upload with touchscreen/web provisioning and hotspot QR; stops and returns to the entry origin. | `read_pico_transfer`, `book_store` |

加页：在 `main/apps/` 新建文件，实现需要的回调，把它加入 `app_registry.c` 的 `s_apps[]`。锁屏前状态保存通过 `on_before_lock` 回调接入主循环。

To add a page: create it in `main/apps/`, implement its callbacks, and append it to `s_apps[]`. Use `on_before_lock` to flush state before lock rendering.

首次引导是启动专用页 `app_onboarding`，不注册普通菜单；`setup_state` 在默认迁移写入前读取本板设置命名空间，新机/空配置先提交待完成标记，老配置无标记时不强制引导。恢复出厂保留的空命名空间等同新配置；跳过/完成提交成功才退出，失败不擦除用户数据。引导复用本板 U 盘/Wi-Fi 传输服务导入 TF 字体，联网后后台对时，不跨板复制蓝牙、睡眠或刷新参数。中英覆盖引导及系统界面，书籍与用户输入保留原文。
Setup is a boot-only page, outside the normal registry. Read this board's setup state before default migrations; fresh/empty profiles persist Pending, legacy profiles without a marker skip setup. Skip/Finish must commit successfully; failure never erases user data. Reuse board services without copying Bluetooth, sleep or refresh parameters; bilingual coverage applies to setup and system UI while book/user content retains its text.

## `app_desc_t` 契约 / Contract

定义在 [`main/app/app.h`](main/app/app.h)。主循环按返回值刷屏，见 [`main/app/app_loop.c`](main/app/app_loop.c) 的 `app_present()`。

Defined in [`main/app/app.h`](main/app/app.h). The loop presents via `app_present()` in [`main/app/app_loop.c`](main/app/app_loop.c).

| 返回值 / Return | 主循环做什么 / Loop does |
| --- | --- |
| `APP_REDRAW_NONE` | 不刷。/ No update. |
| `APP_REDRAW_AREA` | 只推 `area_hint()`。回调必须先画好 fb。模式是 `APP_DYNAMIC_REFRESH_MODE`（DU）。/ Push `area_hint()` only. Caller paints fb first. |
| `APP_REDRAW_PAGE` | 调 `render()`，整页 `APP_PAGE_REFRESH_MODE`（均衡档是 GL16）。/ `render()` then page mode. |
| `APP_REDRAW_FULL` | 调 `render()`；`APP_PAGE_FORCE_FULL` 时整屏 GC16，否则仍走页模式。/ `render()`; GC16 when force-full is on. |
| `APP_REDRAW_DONE` | 页面自己已经刷完。/ Page already presented. |

回调纯度：

- `render()`：纯绘制。禁止 I2C 写、蜂鸣、睡眠、改设置。KEY2 整屏强刷会复用它。/ Paint only. No I2C writes, buzzer, sleep, or settings. KEY2 reuses it.
- `on_enter()`：上电、唤醒传感器、拉一次数据。/ Power-up and first sample.
- `on_before_lock()` 可选：锁屏绘制前保存阅读进度与统计。/ Optional pre-lock flush of reading progress and statistics.
- `on_media_lost()` 可选：主循环检测已挂载卡失效时调用，先停止文件/后台消费者，不画屏、不重挂载；返回后主循环回退字体并重绘。/ Optional mounted-media-loss callback: stop file/background consumers without drawing or remounting; the loop then falls back fonts and redraws.
- `on_exit()`：掉电、停传感器。/ Power-down.
- `present()`：自定义推屏。返回 true 表示已经刷过，主循环不再推。/ Custom present; true means done.
- `on_touch()` / `on_gesture()` / `on_key()` / `on_tick()`：可有副作用，用返回值要刷屏。/ Side effects OK; return the redraw.
- `on_key_long()` 可选：仅接管三键的页面在同键单指保持500ms后触发一次；`defer_middle_short` 页面中键短按在抬起时执行，长按不触发短按；滑出、多点、读错、睡眠、字体重载或切页取消。/ Optional owned-key hold callback fires once at 500ms; with `defer_middle_short`, a middle short press fires on release and a hold skips it. Leaving, multitouch, read errors, sleep, font reload and page switches cancel.
- `on_power_short()` 可选：页面可消费电源键短按；返回 `APP_REDRAW_NONE` 时主循环继续锁屏。阅读正文的“电源键翻页”开关开启后短按翻下一页，长按由主循环锁屏；其他页面保留原电源键逻辑。/ Optional short-power callback may consume the press; `APP_REDRAW_NONE` keeps the lock path. With reader power-turn enabled, short presses advance in the reader body and long presses lock; other pages keep their usual power behavior.
- `on_media_ready()` 可选：首次插卡、显式重挂或Pico后台恢复完成时通知当前页面更新目录；不得绕过失效后的句柄清理。/ Optional callback refreshes page listings after insertion, explicit remount or Pico background recovery; it must not bypass handle cleanup after invalidation.
- `on_gesture()` 可选；提供后不再接收 `on_touch()`。主循环负责识别与全局中断取消，页面解释动作。/ Optional gesture callback replaces `on_touch`; the loop recognizes and cancels, the page interprets.
- `on_key()` 默认只收 `UI_KEY_1`；`owns_keys` 页面在菜单关闭时接收三键。主页面按用户2026-10-16修订：左右键循环切换四个主页，中键长按全刷；书架中键短按切换分页控制并沿左右箭头轮廓描边，可通过导航随时打断。系统子页仍左返回、中首页、右返回；阅读正文三键短按可自定义，两套推荐默认上一页/工具栏/下一页；底部上滑固定呼出工具栏，三键短按和中键长按不强制工具栏映射；中键长按可自定义，默认返回首页，用户2026-11-01改为中间单击只进入全屏；底部上滑退出全屏并显示工具栏。新开书默认非全屏，顶部时间/签名/电量，工具栏上方显示章节/进度；工具栏进入即显示，翻页保留模式。原全屏两种样式保留。/ Normally KEY1 only; `owns_keys` pages receive all three outside the menu. Product system pages use Back/Home/Back; reader short actions are configurable, defaulting to previous/tools/next; bottom-edge swipes always open the toolbar without a required short/hold mapping; middle holds are configurable and default to home the user revised this on 2026-11-01: centre taps only enter fullscreen; bottom swipes exit and show tools. Standard entry shows time/signature/battery and chapter/progress above visible tools. Turns retain the mode; both existing fullscreen styles remain.

标志：

- `holds_pmu`：长时间独占 PMU（自检）。主循环的锁屏按键轮询让路。/ Page owns the PMU; lock-key poll yields.
- `owns_keys`：接管三键；不影响菜单打开时的全局键和菜单把手。/ Own all three keys outside the menu; global menu keys and the handle remain.
- `defer_middle_short`：中键短按在抬起时触发，以区分长按。/ Defer middle short action until release to distinguish a hold.
- `menu_handle_enabled`：按页面状态决定右下角菜单把手是否响应。/ Control bottom-right menu-handle hit testing by page state.
- `enter_full`：进页走 `APP_REDRAW_FULL`，避免差分刷留边。/ Enter with a full refresh.
- `navigation_visible`：可选查询；自有操作栏替代主导航时返回 false，让内容刷新包含完整底部。与只禁止点击的 `navigation_enabled` 分开。/ Optional visibility query: false for a custom action bar so page scans include the full bottom, independently of navigation input admission.

`app_ctx_t.request_app` 非空时，主循环在本轮末尾切页；`request_menu` 请求打开根菜单。同时设置时切页优先。/ At tick end, `request_app` switches pages and `request_menu` opens the root menu; a page request takes priority.

## 术语表 / Glossary

| 术语 / Term | 中文 | English |
| --- | --- | --- |
| DU | 快速差分刷新，只驱动变化像素，灰阶少、残影多。 | Fast differential update. Drives changed pixels only. |
| GC16 | 16 级灰度全像素刷新。区域内白像素先压黑再擦白，刷新处一条黑带。 | 16-gray full-pixel update. Whites go black then white; a black flash. |
| GL16 | 16 级灰度，白底保持，不先压黑。 | 16-gray update that keeps an already-white background. |
| 跟随 DU / FOLLOW DU | 开机生成的 8 帧短波形，一次刷新连 diff 带扫描，用来跟手数字或读数。 | Boot-built 8-frame short waveform: one refresh does diff + scan. Used for live digits. |
| 连续 DU / continuous DU | 每轮只扫一个相位，软件记每像素剩余相位。帧率与黑度解耦。 | One DU phase per scan; software tracks leftover phases per pixel. |
| 跟手 | 手指或传感器在动时用 DU/连续 DU 追画面。 | Live tracking with DU / continuous DU while the finger or sensor moves. |
| 定稿 | 动作停下后用 GC16/GL16 整页（或指定区）清残影。 | Settle after motion with GC16/GL16 to clear ghosting. |
| 把手 | 底栏菜单按钮；演示页的 KEY3 也可触发。打开/关闭全屏一级菜单。 | Bottom-right menu handle; KEY3 can also toggle it on demo pages. |
| 冻结 / Frozen | 文件头里的产品决策。agent 不得改行为去“优化”它。 | Product decisions in the file banner. Do not “improve” them. |
| 均衡配置 | `APP_REFRESH_BALANCED`：整页 GL16，周期 GC16 压灰底，动态区 DU。 | Default refresh profile: GL16 pages, periodic GC16, DU for motion. |
| ALL_DU | 实验档：整机都走 DU，只看速度。 | Experimental profile: everything is DU. |
| 锁屏 | 电源键短按后的锁页。2026-10-19用户改为双击电源键才唤醒，有密码时继续验证。 | Lock face after a short press; per the 2026-10-19 user decision, double-press power to wake, then verify an enabled PIN. |
| 软睡 | `SOFT_SLEEP`：PMU 拉低 EN，再短按开机，不是硬复位。 | Soft sleep: PMU drops EN; a short press powers on without a hard reset. |
| VCOM | 面板公共电压（绝对值 mV）。出厂写入 PMU，主机只读。 | Panel common voltage (absolute mV). Factory-written in the PMU; host is read-only. |

注释里不要再展开这些词。/ Do not re-explain these terms in comments.

## 硬约束 / Hard rules

- 用户 2026-10-09 确认项目品牌为小写 `kiikoread`，网页设备称「墨水屏」，页面底色采用明亮纯白。保留 `wegooo-cell/read-pico-reader` 仓库网址与已发布下载/OTA地址、备份文件名和内部协议标识。用户最新要求固件界面也统一命名，主站 OTA 使用 kiikoread.com，并保留 GitHub 回退源。/ Per the user’s 2026-10-09 decision, use lowercase `kiikoread` for the project and “墨水屏” for devices on the bright-white website. Preserve repository/download/OTA URLs, backup filenames and protocol identifiers. The latest instruction also rebrands the firmware UI, with kiikoread.com as the primary OTA host and GitHub retained as fallback.

`冻结 / Frozen:` 段落是产品决策，不是建议。改行为前必须先改这段，并说明为什么决策变了。

A `冻结 / Frozen:` block is a decision, not a hint. Change the behavior only after rewriting that block and saying why the decision changed.

- 面板 VCOM 存在 PMU 里。主机开机读一次，不写 ESP NVS，产品页不提供修改入口。标定页（`vcom_setup.c`）是未标定时的出厂拦住路径。/ VCOM lives in the PMU. Host reads once. No NVS copy. No product UI to edit it. `vcom_setup.c` is the factory gate when unset.
- 不要写 SY7636A 的 VCOM / VLDO / 放电 / 延时 / VCOMCTL。电源页只读。/ Do not write SY7636A VCOM / VLDO / discharge / delay / VCOMCTL. Power page is read-only.
- `sdkconfig.defaults` 的 120 MHz 时序依赖真机 Zbit flash。覆盖 HPM 表见 `read_pico_flash_hpm.c`。/ 120 MHz timing needs the onboard Zbit part. HPM override is `read_pico_flash_hpm.c`.
- 不要改 `components/epdiy/**` 来“整理注释”。/ Do not touch `components/epdiy/**` for comment cleanup.
- 不要手改 `main/font/fallback.h`、`main/font/stb_truetype.h`、`components/e0470_epaper_waveform/waveforms/*.h`。/ Do not hand-edit generated or vendored tables.
- 不改 UI 字面量或日志字符串，除非任务明确要求。/ Do not change UI strings or log text unless asked.

## 注释规范 / Comment style

### 授权声明 / License notices

用户 2026-10-15 要求新增原创内容仅限个人学习、不得未经书面授权商用。适用边界以根目录 [LICENSE](LICENSE) 和 [CONTRIBUTING.md](CONTRIBUTING.md) 为准；这是今后明确标注的新原创内容的许可，不撤销 rc93 及以前的授权，不覆盖第三方或其他贡献者的权利。新原创文件只有在权利人明确同意时使用 `LicenseRef-Kiikoread-Personal-Study-1.0`，保留真实版权人。不要将下面的 Apache 文件头示例机械用于新原创文件，也不要批量替换既有文件头。
Per the user's 2026-10-15 decision, explicitly marked new original material is for personal study, with commercial use requiring written permission. [LICENSE](LICENSE) and [CONTRIBUTING.md](CONTRIBUTING.md) define the scope; rc93 and earlier grants and third-party or other contributors' rights remain intact. Use `LicenseRef-Kiikoread-Personal-Study-1.0` on new original files only with the rights holder's explicit consent and accurate attribution. Do not mechanically apply the Apache example below to new original files or replace existing headers in bulk.

全仓中英双语。只改注释，不改逻辑。

Every comment is bilingual. Comments only; no logic changes.

文件头：

```
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：这个文件负责什么、边界在哪。
 *
 * English: what this file owns and where it stops.
 *
 * 冻结：不得做的事。
 * Frozen: things this file must not do.
 */
```

- 头文件公开 API 与 struct 成员用 `///`。/ Public API and struct fields: `///`.
- `.c` 内部用 `//`。不要 `/** */`。/ `.c` internals: `//`. No `/** */`.
- 单行：`// 中文。/ English.`
- 超过一行：中文一行，英文紧跟一行。/ Multi-line: Chinese line, then English line.
- `enum` 成员短尾注：`///< 中文 / English`
- 大 `.c` 用 `/* ---- 区块名 / Section ---- */` 分区。/ Large `.c` files use section banners.
- 术语见表，不在注释里重复解释。/ Use the glossary; do not redefine terms.

每个正式版本同时保留USB全量包与TF升级包，官网按所选版本分别提供下载入口。TF应用Bin保留兼容文件名Pico-update.bin，ZIP可附说明与校验值；不得将USB全量包当作TF升级镜像。
Retain both USB full base packages and TF application upgrades for each official release, with separate version-matched website downloads. Keep Pico-update.bin for TF compatibility; ZIPs may include instructions/checksums. Never present a USB base bundle as a TF upgrade image.

用户2026-10-21明确要求修复官网OTA，不切换GitHub固件镜像。OTA固件URL保持https://kiikoread.com/；备用清单不能自行更换固件下载源，证书链问题须在官网或经确认的兼容固件中修复，保持TLS/SHA校验。
The user explicitly requires the official OTA origin, not a GitHub firmware mirror. Keep firmware URLs on https://kiikoread.com/; do not change the download origin through fallback metadata. Repair certificate compatibility on the site or in approved firmware while retaining TLS and SHA checks.

发布前及证书续期后按 `docs/OTA_MAINTENANCE.md` 运行原正式固件信任链、完整下载和Range检查，并用已发布设备验收；浏览器可用不能代替设备校验。临时Cloudflare令牌不入仓库。
Before publication and after certificate renewal, run the released-trust-store, download and Range checks in `docs/OTA_MAINTENANCE.md`, plus a released-device acceptance test. Browser success is insufficient; never commit temporary Cloudflare credentials.
