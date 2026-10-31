# 版本变更 / Changelog

## 未发布 / Unreleased — 最新功能界面 / Latest interface

- 合入PR55–58：Pico普通与图文翻页采用全像素GL16，保留手动/周期GC16；两板液态玻璃书架加深折射，支持静态WebP封面、插图及TF图片。Pico渐进式JPEG按内存预算使用lowzag完整解码并修复插图缩放复位，S31保留原生JPEG硬解与libjpeg路径。
  Merge PR55–58: full-pixel GL16 for Pico ordinary/image turns with manual/periodic GC16 retained; deepen liquid-shelf refraction and add static WebP covers/illustrations/TF images on both boards. Pico gains budgeted full progressive JPEG via lowzag and stable image zoom; S31 retains native hardware/libjpeg decoding.

- 合入PR59双板统一源码，底层驱动、供电/睡眠与阅读波形按PR原实现；接入最新功能界面，Pico/S31各用独立后端、配置和产物。
  Merge PR59's unified tree with its original drivers, power/sleep and reader waveforms, integrating the latest interface with isolated board backends/configs/packages.
- 首次引导覆盖语言、Wi-Fi与卡内字体，支持U盘/扫码传入及分页选择，选中字体同时应用系统与阅读；已有配置升级不重复引导，失败保存不退出。系统界面可切换中英文，联网后后台对时。
  Setup covers language, Wi-Fi and card fonts via USB/QR transfer and a paged picker, applying the choice to UI and reading. Existing profiles skip setup and failed saves remain retryable. Add system localization and background time synchronization.
- 文件加入多级递归搜索、列表/图标视图、递归书籍统计、跨页多选、目标目录移动及确认删除。移动不覆盖同名目标，失败项保留待重试；书架管理按名称、最近阅读或首次收录升降序排列，显示统一封面。网络私有存储保存八个Wi-Fi配置，开启蓝牙后尝试重连最近设备。
  Files adds recursive search/counting, list/grid views, persistent multiselection, destination moves and confirmed deletion, with no overwrite and failed-item retries. Shelf management sorts by name/recent/first-added in either direction with consistent thumbnails. Save eight private Wi-Fi profiles and reconnect the most recent Bluetooth peer on enable.
- 新开书默认标准模式及工具栏，中间单击切换全屏，沉浸偏好独立决定全屏样式。切换保留阅读锚点、翻页保留模式，信息栏空闲后局刷，手动与周期清理保留。四个根页、文件管理、设置和引导操作栏同步最新布局；EPUB标准封面缺失时按明确文件名回退。
  New books open with standard tools; centre taps toggle fullscreen independently of the immersive style. Preserve anchors/modes, update metadata locally after idle and retain manual/periodic cleanup. Update root/file/settings/setup layouts and add explicit-filename EPUB cover fallback.

- Pico 与 S31 阅读翻页保留整屏擦白清残留，并增加擦白轮次、补重推黑：Pico 普通/图文翻页插入 12 相擦白 + 4 相重推黑（水波纹保持 4 相），S31 普通与水波纹插入 16 相擦白 + 4 相重推黑，原厂 GC48/GL48/DU20 表不改。强光下黑字不再被整屏白推冲淡。
  Pico and S31 reader turns keep the whole-panel erase that clears ghosting but add extra erase rounds with a black re-drive: Pico ordinary/image turns insert twelve white phases plus four black re-drive phases (the ripple keeps four), and S31 ordinary turns and ripple insert sixteen white phases plus four black re-drive phases, with the vendor GC48/GL48/DU20 tables untouched. The extra white push no longer washes the black ink out in strong light.

## 双板型源码并入 main / Unified two-board source in main

- 本仓库 main 现在同时维护 Pico 与 Metalio S31：`boards/<板型>/` 保存板级身份、sdkconfig、分区表、依赖锁和硬件后端，统一构建入口为 `tools/build_firmware.py --board pico|metalio47`（Windows 用 `tools/build_board.ps1`）。共享 `main/`、`components/` 只保留一份，固件仍不能跨板型刷写；官网发布范围与第三方许可不变。
  Main now carries both Pico and Metalio S31 sources: `boards/<board>/` owns identity, sdkconfig, partitions, dependency lock and the hardware back end, and `tools/build_firmware.py --board pico|metalio47` (Windows: `tools/build_board.ps1`) is the single build entry. Shared `main/` and `components/` stay in one copy; images remain board-specific, and the website release scope and third-party notices are unchanged.

## Metalio S31 0.3.3-rc107 / S31 refresh and reading update

- S31普通阅读沿用Moku的16灰GL37加六相白（43相），正文、图文与离开图片统一维护整页白底，等黑/等灰保持；水波纹保留原22相迁移并加六相白（28相），速读使用厂家DU14加四相白（18相）。均保留原页数全刷设置及原厂GC48恢复。
  S31 ordinary reading retains Moku's16-gray GL37 plus six white phases(43), maintaining the whole white background for text/images/image exits while holding equal black/grays. Ripple retains22 transition phases plus six white phases(28); Fast BW uses vendorDU14 plus four whites(18). Retain user cleanup intervals and nativeGC48 recovery.
- 主页快刷/水波纹切换将六/八相补白覆盖到未变化留白，涉及书架八相、其余六相；合并黑白、灰阶与横条输出，保留底栏图标。周期清理使用真实原厂GC48，成功后仅抵扣实际清理范围。
  Fast/ripple root switches extend the same six/eight white phases to unchanged blanks(eight involving the shelf,six otherwise), combining BW/grays/marker while protecting navigation icons. Periodic cleanup uses actual nativeGC48 and clears only successfully covered accounting.
- 设置/蓝牙列表保留文字与头像灰阶，上部八相、下部十相背景维护在同轮输出（58/60相）；每八次实际变化的滚动以原厂GC48区域清理替换当次输出，不在松手或空闲时追加刷新。
  Settings/BLE lists retain glyph/avatar grays with upper-eight/lower-ten white drives in one58/60-phase output. Every eighth changed scroll replaces that output with exact-crop nativeGC48; release/idle adds no scan.
- 收录液态玻璃书架、英文单词/TeX断词及词间对齐、TXT分卷目录与快速选卷。未设置时默认液态玻璃书架、Moon Clock与主页强刷；保留已有配置和阅读进度。
  Include the liquid-glass shelf, English word/TeX hyphenation and spacing, TXT volume-aware TOC and quick volume selection. Unconfigured devices default to the liquid-glass shelf,Moon Clock and full Home refresh; existing preferences and reading positions remain.
- 本次只发布Metalio S31，Pico正式固件及下载保持rc102。支持保留数据更新，并保留rc106原包回退；刷新观感依面板状态而异。
  Publish Metalio S31 only; Pico remains rc102. Support data-preserving updates and retain originalrc106 rollback packages; optical results depend on panel condition.


## 未发布 / Unreleased

- S31本地残影优化：主页面切换涉及书架补八相变白，其余补六相，保留原灰阶、固定底栏和两相释放；设置/蓝牙列表使用黑白装饰、灰阶字缘/头像，列表滚动采用本机完整48相加八相擦白及两相释放，一次区域输出，不在松手后补刷。列表优先清残影、反馈会比原黑白快刷慢，光学效果待实屏确认；阅读模式保持独立。
  S31 local ghosting work: root changes add eight changed-white phases involving the shelf or six otherwise, retaining native grays, navigation and two release holds. Settings/BLE lists use BW decoration and gray glyphs/avatars; scrolling uses native48 plus eight whites/two releases in one regional pass without release-time output. Lists favor cleanup over prior BW latency; optical acceptance remains pending and reading modes stay independent.

- Pico本地实验：阅读水波纹保留本板灰阶序列与8条带方向，定稿前增加四相变白（41相），正文/进度同轮完成；主页面切换等灰保持、变白加四相，普通/水波纹合并底部指示条，保留固定底栏及失败恢复。设置/蓝牙列表改黑白框线、分隔线和图标墨色，文字/头像灰阶保留；滚动恢复完整擦白并加八相（45相），仅刷新列表，不在松手后追加全屏清理。
  Pico local experiment: native eight-band reader ripple adds four changed-white phases before settling (41) and fuses progress. Root changes hold equal grays, add four changed-white phases and fuse the marker for ordinary/ripple while preserving navigation and failure recovery. Settings/BLE lists use BW decoration with glyph/avatar grays retained; scrolling restores native whitening plus eight phases (45) within the list, without a release-time full-screen pass.

- 两机同步图片设置弹窗：Pico文件管理打开图片后，点按中间可设为壁纸/透明书封或取消，左右滑动切图，底部显示提示；共用玻璃弹窗绘制，JPEG不允许设透明书封，取消不改设置。Pico沿用本板完整灰阶表，一轮输出切图/弹窗，不先擦白再重放；设置中的独立选择页保持原流程。
  Shared image actions: Pico Files previews now offer wallpaper/transparent-cover/cancel on a center tap, sibling swipes and a visible hint. Reuse the glass menu; reject JPEG transparent covers and preserve settings on cancel. Pico uses its native full gray table in one image/menu output; existing dedicated settings pickers remain.

- Pico本地版对齐已合PR54：普通及图文页共用本板柔和灰阶差分，保留厂商原序、等灰保持，在定稿前仅为变白像素追加四相，并合并页脚输出；手动及周期全刷保持完整GC16。同步现有液态玻璃书架、英文TeX断词对齐、TXT懒目录/分卷选择及无配置默认外观；Pico根页采用本板独立擦白档，不引入S31波形或睡眠参数。
  Pico local parity with merged PR54: board-native soft gray differences for ordinary/image turns, unchanged-pixel holds and four changed-white phases before settling, with fused footer output and full native GC16 cleanup. Retain shared glass shelf, TeX English layout, lazy TXT volume navigation and unconfigured appearance defaults; keep Pico-native root whitening and hardware isolation.

- S31普通/图文原样采用Moku GL37加六相白（43相）；速读保留本机DU并增加四相白（DU14实验18相、原厂DU20为24相），恢复模式分流与失败保护。GL37不可用时保留完整灰阶回退和等黑灰保护，手动/周期GC48不变。
  S31 ordinary/image turns retain Moku GL37 plus six whites (43); FastBW retains board DU plus four whites (DU14 experiment18/nativeDU20 24),mode isolation and failure history. Preserve gray fallback/equal black-gray protection without GL37 and retain manual/periodic GC48.

- 两机同步：分卷目录底部新增“选择卷”，只显示一级卷名并分页；选卷后展开定位，取消返回原章节目录页，选卷不加载正文、额外分配内存或改变阅读进度。未分卷书保留原布局。
  Both boards: add a paged volume-only picker to grouped TXT directories. Selection expands and locates a volume; cancel restores the previous chapter page without body loading, extra allocation or progress changes. Flat books keep their layout.

- 两机同步：TXT目录支持“卷→章”展开/折叠，优先明确卷名，缺少卷名时结合章号重置及后续2/3章保守分组；卷末总结/感言归原卷。进入目录定位当前卷/章，重复章号不排序或去重。卷表最多256组/2KiB，标题仍按需读，资源不足退回平铺；只重建索引缓存，旧阅读进度、书签和原文件位置保留。
  Both boards: add an accordion TXT volume/chapter directory, preferring explicit volume names and conservatively confirming restarted chapter sequences. Keep end notes with their volume, locate the current chapter on entry and retain duplicate ordinals in source order. Bound groups to256/2KiB with lazy titles and flat fallback; rebuild only index caches while preserving source/progress/bookmark positions.

- S31十四相本地实验：首页快刷真实切页和阅读速读黑白在原末三相维护白底，未变黑/灰保持；内容区一次覆盖输出，不加相数、反黑或第二轮补刷，普通/水波纹与清理全刷保留。

- S31本地实验：新增默认关闭的厂家DU14编译开关，仅替换速读黑白与纯黑白界面访问器；保持原扫描、灰阶底栏合成、普通阅读、封面和清理全刷，实验标志写入本地包清单。

- 两机同步：TXT目录补充英文Part/罗马数字、Markdown一级至三级标题、序章/附录/册及廿卅数字，校验标题边界，正文另有章节时去除卷首目录清单；索引版本更新但旧阅读分段、进度和书签编号保留，仍按页分配目录与按需读取标题。

- 两机同步：英文正文改用有界Liang/TeX英语断词表，按词间空格伸缩代价选择整词或合法连字符断点；只调整空格，不拉散字母，段末保持自然宽度，保留原文进度/书签偏移与竖排。断词表只读存Flash、不读卡、不为匹配分配堆。

- 两机同步：新增「液态玻璃书架」，三列九本、薄玻璃挡条、细层板与不透明封面；沿用现有菜单玻璃算法和有界合成缓存，普通/水波纹保留灰阶、快刷使用稳定点阵，同次输出不加补刷。样式独立保存为6，原样式、旧3/4兼容和备份保留，选择页支持滚动与定宽文字。
  Both boards: add a nine-book liquid-glass shelf with thin refractive rails, ledges and opaque covers. Reuse bounded glass composition/cache with grays in normal/ripple and stable fast-mode dots in the same output. Persist style6 without replacing existing styles or retired-ID compatibility; retain backup support and add bounded picker scrolling/labels.

- 两机同步：TXT/EPUB横排英文优先整词换行，兼容词内撇号、重音字母和普通连字符；正文词间空格适度对齐，段末保持自然宽度；整词换行留下过大空白时有界拆词并加显示用连字符，超长词及极窄行保留安全拆分。原文、进度及书签偏移不变；重新排版后的页数可能变化，竖排和中文禁则保持原规则。
  Both boards: prefer whole English words in horizontal TXT/EPUB layout, including internal apostrophes, accented Latin and existing hyphens. Justify moderate body-line slack between words while keeping paragraph endings natural; excessive whole-word slack uses bounded display-only hyphens, retaining the safe overlong-token and narrow-row fallback. Source/progress/bookmark offsets stay unchanged, reflow may change page counts, and vertical/CJK rules remain.

- 两机同步：TXT新增独立可见目录，兼容长章名、空格、中文/英文编号、统一分隔线和保守数字标题；按页分配定位表、按需读取标题，保留旧阅读分段和进度/书签编号。目录最多16384项、128KiB，候选过多或分配失败时保留原分段阅读；索引缓存增加结构与校验和验证。
  Both boards: add demand-paged TXT navigation with long/indented/numbered/separator headings, lazy titles and exact in-segment targets. Retain legacy segment/progress/bookmark IDs; cap navigation at16384/128KiB, fall back without unbounded growth, and validate cache structure/checksums.

- 两机同步：改进书名、作者与已知来源后缀识别，清理重复作者、译者和冗余宣传尾注；保留原文件及操作完整路径，阅读进度不变。
  Both boards: refine title/author and known source suffix recognition without renaming files or changing reading progress; file actions retain complete paths.
- Pico构建对齐现有rc101的字体路径保护、黑白设置开关、白底密码框、可选密码宽限期和标题提示排版；宿主夹具按机型选择，S31电源与显示后端保持独立。
  Pico builds retain the existingrc101 font-path guard, switches, white PIN prompt, opt-in grace and title notices; host fixtures select the board without replacing S31 power/display backends.

- 两机同步：兼容部分 EPUB 制作工具写入的本地头全零 ZIP64 预留字段，修复小书被误报为资源超限；保留32位尺寸、范围和CRC校验，真正的ZIP64仍不支持。
  Both boards: accept a narrowly validated zero-filled local ZIP64 placeholder without admitting real ZIP64 archives; retain size, range and CRC checks.

- S31恢复睡眠按键扫描时增加双采样，避免方向切换后单次低电平或I²C读错被当作解锁短按；真实按键、双击和密码验证保留。
- S31锁屏恢复启动时保留持久化锁定标记，避免门禁前再次复位丢失锁定状态；真实解锁后清理，自动开书仍先消费恢复入口。
- 两机启动日志补充恢复保护的具体原因位，区分异常复位、未完成启动/开书和NVS失败；不放宽保护或修改记录格式。

## Pico 0.3.3-rc102 · 2026-10-29

- 主页面切换补白：来源或目标是书架时八相，其余根页六相；实际Pico事件循环限定一次输出作用域，保留原底栏和整刷优先级。
- 设置列表补八相至45相，框线/分隔线黑白，文字与头像保留灰阶；阅读水波纹41相并合并进度，普通翻页保留已合PR54方案。
- 发布液态玻璃书架、无配置外观默认、英文TeX断词及空格对齐、TXT懒目录/卷章/快速选卷、文件看图设置壁纸等现有本地改进；保留有效设置、进度和书签。
- 官网刷机、GitHub Release、TF/USB与双域OTA同步，保留原rc101回退；S31正式交付未改。本次产品构建开启Pico独立main-clean参数，不使用S31底层。

## Metalio S31 0.3.3-rc106 · 2026-10-27

- 改善锁屏后TF与字体恢复：优先恢复可读卡会话，释放旧句柄后再重开保存的字体；保留异常启动内建字体保护。
- 修复睡眠恢复时单次异常按键电平可能触发解锁，以及启动门禁前再次复位丢失锁定状态的问题；真实按键、双击与密码验证继续保留。
- 浅睡十分钟后保持锁屏画面、同步外挂RTC并硬件关机，交接时不重新点亮前光或恢复整套媒体；滑动关机显示已关机提示，充电与拔线边界保留。
- 壁纸锁屏改为合成完成后一次推屏；文件管理支持较长中文EPUB文件名。
- 兼容EPUB本地头的全零ZIP64预留字段，修复小书被误报资源超限；真正ZIP64仍不支持，尺寸/范围/CRC校验保留。
- 正式包使用常规引导程序，支持兼容底包直接OTA/TF升级；十分钟硬件关机后按长按电源键开机使用。未采用本地短按引导实验，未更改原厂波形和分区。
- 本次仅发布S31；短时USB启动、挂卡和导入系统字体已验证，电池长时间待机、充电与所有字体恢复场景仍需持续实测，不能保证完全杜绝误保护或异常唤醒。保留rc105回退下载。

## Metalio S31 0.3.3-rc105 · 2026-10-26

- 同步Pico PR46/47/48/49：设置开关改黑白轨道；密码键盘纯白底、等待和取消保留锁图；新增默认关闭的密码宽限期；设置提示与标题按实测字面对齐并限制长度。S31宽限仅在真实按键浅睡唤醒后判断，硬件重新开机、过期、无效时间或时钟回拨仍验证密码。

- 日常锁屏改为浅睡十分钟后同步外挂PCF8563并硬件关机，不设RTC闹钟；关机前安全关闭书籍、字体及TF，失败保持锁屏并有界重试，配置和进度保留。
- 合入原厂2.0.66的S31浅睡SDK唤醒修复，补齐MPLL校准和PSRAM时钟交接，保留看门狗与原厂供电顺序；添加有界复位/睡眠阶段诊断。
- 共享中断需核验真实电源键后才补记解锁，定时及USB唤醒不直接解锁；异常复位禁用开书/导入字体恢复时仍保留已验证记录的锁屏门禁。硬件重新开机单击恢复不再暗中等待第二次按键，双击/密码及异常启动字体保护继续保留；无卡且无活动句柄时也允许浅睡。
- 修复字体恢复宿主测试的平台选择，保留两机独立覆盖。
- 本次仅发布S31，Pico版本保持。USB连接下的切页与短时锁屏唤醒已测试；十分钟以上电池待机、关机与续航仍待实机验证，不能保证所有自动解锁反馈均已解决。实测一次TPS屏幕电源启动超时，后续输出恢复，该偶发现象仍在排查；保留rc104回退下载。

## Metalio S31 0.3.3-rc104 · 2026-10-25

- 壁纸、透明书封入口共用独立图片浏览器，保持16级灰阶与左右滑动；各入口只允许对应设定，透明书封仅列出PNG。图片切换和设定弹窗取消额外预清屏，统一圆角卡片和底部操作提示。
- TXT新增UTF‑16大小端流式读取，支持BOM及有明确特征的无BOM文件，处理跨块字符、代理对与坏编码；保留原文件字节进度，自动重建旧索引，UTF‑8/GBK兼容保留。缺少特征的无BOM编码仍可能无法自动判断。
- 图片增加受控JPEG硬件解码与渐进式JPEG软件回退，保留PNG、内存预算、异常回退和原灰阶合成。
- TF挂载增加降频、单线兼容与成功档位记忆；解锁优先恢复输入，避免前台长时间等待媒体。设置“升级和恢复”新增“内存卡恢复”，安全释放消费者后异步重挂并重试原字体，不格式化、不清配置；USB读卡、传书和升级占用时禁止重挂。
- 普通模式书架封面、书夹与外围采用官方裁剪16灰37相，跨主页面切入保留两相擦白；手动、周期全刷与故障恢复保留完整路径。
- 四主页面切换、书架返回首页下半区，以及离开阅读图片页时，同轮增强变白像素擦除，保持灰黑层次，不增加第二轮输出。其他场景保持原刷新规则。
- 选择书架拼贴不再在设置页同步预生成，避免阻塞切换主页；实际锁屏沿用缓存生成，首次无缓存仍需等待。
- 首页底栏按下即震动，当前页也反馈；开书、继续阅读、文件和书架分页、下拉/AI/电源菜单补齐单次反馈。底部三键每次按住只震一次，遵循系统震动开关。
- AI菜单最后一项恢复“不设功能”，选择后短按不执行动作；取消AI锁屏映射，旧的该选项自动对应为不设功能；保留S31前光、USB、分区、原厂波形及异常启动字体保护。

## Metalio S31 0.3.3-rc103 · 2026-10-25

- “关机睡眠”只保留默认“浅睡10分钟后进入深睡”和“彻底断电”，移除单独的锁屏省电方式；旧保留待机选项不再生效。普通锁屏统一先浅睡，保存检查点后进入深睡，USB及密码验证边界继续保留。
- 浅睡维护按十分钟截止时间缩短最后一段，临时失败退避重试；深睡前暂停字体文件、触摸、屏幕总线及外设电源，交接失败恢复并保持锁屏。续航与完整唤醒流程仍需实机验证。

## Metalio S31 0.3.3-rc102 · 2026-10-24

- 正常待机解锁后继续恢复 TF 卡、字体和书籍句柄；临时读卡或内存失败按间隔重试，手动重新加载成功会解除旧失败记录。保留异常启动时使用内建字库的保护，不覆盖用户保存的字体选择。
- 系统与阅读字体列表上限提高到128款；按实际文件路径区分字体，长标签完整截断UTF-8并区分重名。仅收录TTF/OTF，超过路径容量的文件名会安全缩短，字体内容保持不变；字体上传自动处理不适合存储的名称符号。
- 系统字号100%–200%时，系统字体页保持四行可翻页列表；字体预览借用临时槽，避免替换正在使用的阅读字库。
- 文字设置、阅读设置采用局部四灰快刷；打开时统一重建灰色底色，修复字号滑条等位置透出底层正文。进入使用一次26相输出，同面板调节保留16相；故障仍回退完整路径。
- 图片查看保持完整灰阶，透明书封合成时保持接近不透明的人物区域，减少非透明主体透底。
- 仅更新Metalio S31，保留前光、振动、USB、分区和原厂波形。编译及针对性宿主回归通过；实际待机恢复与残影改善仍需实机确认，不承诺所有画面均无残影。


## Metalio S31 0.3.3-rc101 · 2026-10-24

- 下拉菜单与 AI 菜单收回后，恢复底页并进行一次整页清理，替代原来的矩形局部输出，改善边框残影；菜单选项不变，收回时有一次全刷闪动。
- 图文翻页合并正文与进度输出，保持未变化的像素；黑白变化使用原厂 DU20，中灰变化保留完整 GL48，不降低图片灰阶。
- 阅读音量上、下键固定为上一页、下一页，不受底部按键自定义影响；修正按键自定义页圆圈键与左键的排列。
- 保留 S31 独立驱动、前光、振动、设置及分区；Pico 版本保持 rc100。

## Metalio S31 0.3.3-rc100 · 2026-10-24

- 启用原厂 SDK 的交叉签名证书链验证，使官网备用 OTA 可使用已有的现代信任根；不关闭 TLS、主机名或真实证书有效期校验，不加入废弃根证书。
- 保留 rc98 固定下载；新安装推荐 rc100，GitHub 仍为优先更新源。


## Metalio S31 0.3.3-rc98 · 2026-10-24

- 启用独立联网 OTA：GitHub Pages 优先，官网备用；保留 HTTPS、整包 SHA-256、S31 芯片/项目/5 MiB 双槽校验及取消恢复。
- 官网增加小纸 Pico / Metalio EINK 4 Plus 机型选择；旧 S31 首次通过 USB 或 TF 安装本版，之后可在系统升级检查联网更新。


按版本简述对用户可见的变化；使用方法见 [README](../README.zh-CN.md)。
User-visible changes by release; see [README](../README.md) for usage.

## S31本地测试 / S31 local test / 2026-10-24

- 新增只作用于字体设置、阅读设置两个主面板的四灰快刷实验，保留黑/深灰/浅灰/白，近白控件底色保留浅灰；不扩展到正文、书架或封面。独立16相组合复用短端点和FOLLOW灰推并保留末三相，不裁GL48或正文22相；普通构建默认关闭，扫描故障保留37/48相回退和完整清理。200毫秒是待验证目标，16相名义扫描约192毫秒，墨色/残影需实屏验收。
  Add an opt-in four-tone experiment for the two primary Font/Reading settings only,retaining black/dark/light/white and near-white control fills without extending it to body/shelf/art. Combine short endpoints/FOLLOW grays in an isolated16-phase profile with three final holds,without trimming GL48/text22. Normal builds disable it and faults retain37/48-gray fallback/full recovery.200ms remains unverified; nominal scanning is192ms and ink/ghosting require hardware comparison.
- 工具栏进入文字设置、阅读设置时不再先为按下态刷新DU20，松手只提交一次最终面板；取消/移出不补刷，其他按钮保留原反馈。保留当前灰阶波形，未实测总响应或承诺200毫秒。
  Skip the S31 toolbar DU20 press pass for font/reading settings and present the final sheet once on release; cancellation/outside releases do not flash,while other buttons retain feedback. Keep current grayscale waveforms without an unmeasured200ms claim.
- 可显式构建阅读面板GL37试验：使用Metalio官方擦除/饱和裁剪，保留16级输入、灰阶尾段与末尾保持，等灰不补推；仅用于确认同一底页的面板和滑块。默认构建、正文重排、图片/书架与全刷仍原路径，内存不足或扫描故障回退48相；画质和时延仍需实屏对照。
  Add an opt-in reader-panel GL37 experiment using Metalio's erase/saturation trimming with sixteen input levels, retained gray tails/final holds and equal-pixel holding. Scope it to verified same-body panels/sliders; default builds,reflow,images/shelves and cleaning keep their paths, with native48 fallback on OOM/scan faults. Optical quality and latency still require hardware comparison.
- 系统字体页在100%–200%字号下保留四行选择，压缩字号/预览区，显示字体总数并提供前后翻页按钮；绘制与命中共用几何，阅读字体仍需确认后同步。
  Retain four S31 system-font rows at100%–200% with compact size/preview sections, a total count and visible page controls sharing paint/hit geometry; reading-face synchronization remains opt-in.
- 阅读滑块数值未变化时不重绘；确认同一底页后只重绘面板并局部提交，跳过正文/插图准备和预绘。同一字体/阅读设置或字体列表翻页可复用底页，重排/图片/故障等继续回退完整路径；完整48相图形灰阶保持，不宣称实测提速。
  Deduplicate unchanged S31 slider values and reuse verified body frames for panel-only drawing/presentation without body/image/prefetch work. Same-panel settings/font paging retain guarded reuse; reflow/image/fault paths fall back. Preserve full48-phase graphical grays without an unmeasured latency claim.
- 补齐阅读/唤醒后自动回退内建字体的三次提示：同次开书累计实际回退事件，持续故障只记一次，临时恢复不清零；手动选内建、离书或关闭提示后重置。异常启动保护不计入，重启仍由用户确认，保存进度失败不重启。
  Include S31 runtime/wake font fallbacks in the per-book three-episode dialog, deduplicating sustained faults without clearing counts after temporary recovery. Reset on explicit built-in/leaving/dismissal; exclude abnormal-start protection and retain confirmed restart/save-failure guards.

- 透明书封合成时将alpha250–255的近不透明像素归为不透明并还原预乘灰度，避免少量底图渗透被十六灰抖动放大；保留更低透明度、透明边缘及原图缓存，旧完整书封帧自动重建。
  Snap near-opaque alpha250–255 during S31 cover composition and unpremultiply gray to prevent backdrop leakage amplified by sixteen-gray dithering; retain lower alpha, edges and source caches, rebuilding old composed cover frames.

- 透明书封先从最后成功显示的页面合成，再清除物理旧态并输出完整灰阶，减轻不透明主体下的旧文字透影；保留原始alpha缓存；alpha低于250的真实半透明区域保持。
  Compose transparent covers from the last presented page before clearing optical history and presenting full grayscale, reducing old-text ghosting beneath opaque artwork while retaining source-alpha caches and semitransparency below250.

- 修复文件管理打开图片时被系统黑白快刷规则二值化的问题：预览恢复十六级灰阶及独立完整灰阶输出，图片文件列表继续黑白快刷；壁纸锁屏及原始波形不变。
  Restore sixteen-level image previews from Files by bypassing the system BW policy; keep image lists fast BW and retain wallpaper rendering and native waveforms.

- 图文翻页按实际变化分流：黑白目标用原厂DU20，有中灰变化时保留完整GL48中灰相序，未变像素保持，正文与进度合并一次输出；不量化图片。S31下拉/AI菜单收回改为恢复底页后一次GC48清理，不追加菜单功能或先局部再全刷。
  Route image turns by actual changed targets: native DU20 for BW, complete GL48 midgray sequences otherwise, equal-pixel holding and fused progress without image quantization. S31 quick/AI close cleans the restored page once with GC48, without new menu actions or a preceding local output.

- 修正S31按键自定义页圆圈键与左键配置错位：图示、显示与触摸共用物理排列，长按/推荐说明同步为圆圈键；已有按键保存值保持。
  Align S31 key customization diagrams, displayed bindings and touch cards in physical circle/left/right order, with circle-key hold/preset labels and saved assignments preserved.

- 阅读音量上键固定上一页、下键固定下一页，不再跟随底部左右键自定义；阅读设置与确认面板保留输入保护。
  Fix S31 reader volume up/down to previous/next independently of bottom-key bindings, retaining settings and confirmation guards.

- 修复纯图片EPUB开章时为测尺寸完整解压每张图的冗余读取：PNG优先32字节头，JPEG按需扩大探测，SVG与晚帧头保留兼容回退；ZIP前缀探测采用有界小块，实际显示仍完整读取并校验CRC。保留原图、画质、分页与进度。
  Remove redundant full-image extraction during comic chapter sizing: start PNG with a32-byte header, expand JPEG probes as needed and preserve SVG/late-frame fallback. Bound prefix input reads while retaining full payload CRC for display, artwork quality, pagination and progress.

- 同步六项Pico改进：双击解锁移到显示主列表，新配置默认单击并保留旧选择；阅读票根去除二次灰阶量化；连续三次手动选字回退内建时提供保存进度后重启的提示，保存失败留在原框；时钟选择失败后合并待更新范围；首页冷数据移到PSRAM；冷启动使用现有内建字形显示极简提示并移除全屏启动位图。保留S31硬件、PIN、睡眠及异常启动字体保护。
  Sync six Pico improvements: root-list unlock preference with a single-press default and saved-choice compatibility; single-pass ticket grayscale; an opt-in restart dialog after three explicit font fallbacks with progress-save failure protection; pending clock-update bounds across failed outputs; cold Home metadata in PSRAM; and an embedded-glyph splash replacing the linked full-frame bitmap. Retain S31 hardware, PIN/sleep flows and abnormal-startup font protection.

- 同步Pico透明书封缓存优化：灰度和透明度按行无损压缩，空闲PSRAM充足时直接在内存生成缓存，减少TF读写；保留完整灰阶、原透明度及阴影，修复FAT换图缓存无法替换的问题。旧缓存首次使用自动重建。
  Port lossless transparent-cover row caches and spare-PSRAM first builds to S31, preserving grayscale/alpha/shadows and the S31 cache directory; fix FAT cache replacement and rebuild older cache formats on first use.

- 时钟选择页箭头切换合并为一次刷新，按钮内容不变时缩小更新范围；停留空闲时逐个准备相邻表盘，触摸/切换中暂缓，低内存回退按需绘制。选择页继续保留完整灰阶。
  Merge S31 clock-picker arrow changes into one output, crop unchanged apply controls, and prepare adjacent previews during idle with touch/memory guards, retaining complete grayscale.

- 时钟样式选择页改用固定10:10:30的静态预览；七款表盘首次按需生成后复用缓存，浏览时不再刷新秒针或猫眼。实际首页与时间详情页继续走时，字体或方向变化后重建匹配预览。
  Use fixed10:10:30 previews in the S31 clock picker, caching each of seven styles on demand without live second/eye updates; Home and time-detail clocks remain live with font/orientation-aware invalidation.

- 对照Metalio官方S31实现，TF使用固定4KiB内部DMA中转，隔离外部RAM的SD DMA/cache路径；保留挂载降档、USB独占和失败回收，吞吐以实机验证为准。
  Use a bounded4KiB internal SD DMA bounce buffer following the Metalio S31 reference, retaining mount fallbacks,USB ownership and cleanup; measure throughput on hardware.

- 传书时每两秒局部更新设备上的文件名、接收字节和进度条，无变化不刷新，失败更新可重试；网页显示平均速度与耗时，并区分已发送和设备确认保存。
  Show bounded two-second receive progress on the S31 with small monochrome differences and scan retries; the web page reports average speed/elapsed time and waits for device save confirmation.

- 传书期间的硬件接收缓冲与AMPDU接收窗口对齐，PSRAM传书对照档采用16槽，普通档不变；日常联网、对时及蓝牙共存仍沿用原预算，状态页报告实际生效数量。大文件吞吐改善须以本机对照测试为准。
  Match bulk-transfer static RX buffers to the AMPDU window (16-slot PSRAM comparison profile), retaining ordinary connection/time/BLE budgets and reporting effective counts; throughput requires same-file hardware comparison.

## S31本地测试 / S31 local test / 2026-10-23

- 修复导入字体缺少“行”等字形时标题漏字：仅使用现有内建字形补齐缺字，统一测量、基线和对齐，保留选定字体、设置及全部字库资产。
  Fill missing imported title glyphs from existing UI glyphs with matching metrics/baseline/alignment, retaining font selection, settings and unchanged font assets.

- 修复阅读票根封面遗漏：兼容完整比例/网格及无作者旧缓存，阅读空闲时复用有界封面任务准备当前书籍，不在锁屏按键路径解码；标题匹配实际字体缩放或换行，保留完整源书名和自定义书名。
  Repair ticket artwork with natural/grid and legacy authorless cache reuse, bounded reader-idle preparation without power-key decoding, and face-matched title shrinking/wrapping without mutating book names.

- 前光亮度滑轨前半段调整0–20%、后半段20–100%，扩大暗光微调行程；保留原PWM/设置与线性色温，关闭前光时拖动不点灯，细微移动仍逐像素反馈，松手不追加刷新。网络接收窗口/报文队列采用有界64KiB对照档，静态DMA/原厂射频策略不变。
  Expand low-light travel: half the brightness track covers0–20%, preserving stored PWM, linear warmth, off-state darkness and per-pixel feedback without release settling. Compare bounded64KiB network receive/packet queues while retaining static DMA/vendor radio policy.

- 对照Metalio公开网络实现统一正常连接、传书、临时对时和扫描的S31运行策略，采用b/g/n、关闭省电与请求12dBm功率上限；IRAM热路径、PSRAM优先分配及有界静态TX/RX预算，保留32KiB TCP窗口和顺序双缓冲写卡。吞吐以本机同文件测试为准。
  Align S31 connection/transfer/time-sync/scan policies with public Metalio sources: b/g/n, PS_NONE, requested12dBm ceiling, IRAM hot paths, PSRAM-preferred allocations and bounded static TX/RX, retaining32KiB TCP and ordered double-buffered card writes. Throughput requires same-file device measurement.
- 同步章节内跳页与撤销、EPUB冒号路径/无题页修复、卡内子目录删书、单幅插图居中及双指缩放/拖动；保护空缓存、布局代际和失败重试，双指不穿透AI/电源/下拉层。保留S31原厂波形、前光、振动、USB、设置区与AT尾标兼容。
  Sync chapter jumps/undo, EPUB colon paths/untitled pages, subdirectory deletion, lone-illustration centering and image pinch/pan; guard null/stale caches, retries and modal input, retaining S31 waveforms, light, haptics, USB, settings and AT-tail support.
- S31通过有界CMD13健康确认和异步退避恢复TF，USB/睡眠主动卸载不触发自动重挂；浅睡等待实际挂载完成才恢复字体/书籍句柄，失败保留路径与重试。新增双击解锁开关（默认开启），关闭后单击解锁，仍验证PIN；异常启动内建字体保护不变。
  Add bounded CMD13 health checks and asynchronous TF recovery with intentional USB/sleep unmount exclusion. Wait for real mount completion before reopening font/book IO, retaining paths/retries after failures. Optional single-press unlock defaults to double with PIN and abnormal-boot font protection retained.

## 本地整合 / Local integration / 2026-10-22

- 整合 Moku 的双板级身份/能力/预算清单与板级门面，兼容 `metalio_s31` 构建别名，保留现有共享阅读代码、S31 专属修复和独立芯片/分区校验。CI 分别编译两板，网页完整性测试改用独立固定输入，私有仓库不部署官网。
  Integrate Moku's board identities/capabilities/budgets and facade, alias `metalio_s31`, retaining shared reader logic, current S31 fixes and strict chip/layout checks. Build both boards in CI and run self-contained web integrity regressions without deploying the public site.


- S31阅读按键示意按实体位置改为左圆、后两横；自动全刷恢复不全刷/5/10/15/20/30页，删除短灰阶及速读的十页强制上限，20页档支持重启与备份恢复。关闭书内图片的文字与开关分列，避免大字号覆盖。
  Match S31 reader key diagrams to its circle/bar/bar hardware; honor Off/5/10/15/20/30 cleanup intervals without short-gray/FastBW caps, including persistence/backup. Reserve separate hide-image text and switch columns at every UI scale.

- 修正S31传输接收窗口与无线缓冲不匹配：DMA16/动态RX32/BA32配32KiB TCP窗口，加入有界乱序缓存与选择确认。文件传输和临时扫描的BLE租约真正阻止重启，退出后保留最近设备重连；仅联网仍允许共存。增加实际网络配置、内部内存及固件身份诊断，修正不等于已验证吞吐提升，需实机对照。
  Match S31 DMA16/dynamicRX32/BA32 to a32KiB TCP window with bounded out-of-order/SACK support. Enforce BLE leases for file-transfer/temporary scans and preserve peer reconnect; network-only retains coexistence. Expose actual profile,internal heap and firmware identity; throughput improvement still requires device comparison.

- S31大文件上传双缓冲交叠网络接收与顺序写卡，资源不足沿用单缓冲；失败先收齐写入任务再清理临时文件，保留旧文件与USB独占。TCP接收窗口扩大，USB读卡块由8KiB增至16KiB，卡时钟仍保守20MHz；实际速度待同卡同网络测量。
  Overlap S31 large-upload reception and ordered SD writes with two bounded slots and single-buffer resource fallback; join before fault cleanup and preserve old-file/USB ownership. Increase TCP receive capacity and MSC blocks from8to16KiB,retaining20MHz USB card timing; real throughput requires same-card/network measurement.
- 同步拼贴缓存FAT替换与无效进度计数修复，冷生成先查尺寸、跳过屏外封面；深睡恢复暂时缺卡时保持物理锁图，真实参考恢复前不做差分提示。锁图只在成功推屏后、密码覆盖前保存，保留S31灭灯、密码与USB流程。入书/目录返回正文单次完整清理，后续正文刷新不变；阅读辅助线限定到横竖排文字范围。
  Repair FAT collage replacement/foreign-progress counts and probe sizes before decoding visible art. Retain physical deep-lock art while media is pending,withholding difference hints until references are known; persist successful art before PIN. Keep S31 light/PIN/USB rules; clean reader entry/TOC returns once and bound horizontal/vertical guides to text spans.

- 根据实机反馈撤回图形区域四灰22相实验：恢复书架/封面/亚克力与菜单原灰阶路径，阅读字体/设置的滑轮和开关用原厂GL48完整灰阶区域定稿，避免浅灰消失、封面发黑与目标灰阶不一致。保留正文22相、原有缓存/面板叠加与完整故障恢复；图形扫描时间回到完整灰阶，后续短波形须单独实屏标定。
  Withdraw graphical four-gray22-phase output after optical defects. Restore native cover/acrylic/menu grays and vendorGL48 regional settling for reader controls, retaining text22-phase,caches/overlay reuse and full recovery. Complete-gray scan latency returns; shorter graphical waveforms require separate panel validation.

- S31 AI快捷菜单改为上移的横向五圆键、下方标签和右上选中点；修正标题误反白。AI、电源仅在按钮和滑条内部模糊，下拉层提亮玻璃并模糊底字；AI标签不加单独底色，控件文字和图标最后清晰绘制，保留单次开合与缓存局刷。电量百分比核实为BQ27220 SOC直读，补齐电压变化不改变SOC及计量读错不回退电压估算的回归。
  Move S31 AI choices into a higher horizontal icon/label row with an inset selected dot; fix inverted title ink. Confine power frost to controls,preserve the clear page,and draw AI labels directly on glass without backing; retain one presentation and cached input. Verify direct BQ27220 SOC with voltage-independence and failed-SOC/no-estimate regressions.

- S31长按AI键打开液态玻璃短按功能菜单，支持全刷、书签、前光、首页或关闭短按；选择后保存并收起，默认全刷。开合各一次区域更新，保留原图与已有完整灰阶；锁屏、USB、升级和传输写入保留保护。
  Hold S31 AI for a glass tap-action chooser: refresh(default),bookmark,frontlight,home or disabled. Persist without executing,then dismiss in one bounded update,retaining the original underlay,grays and guarded lock/USB/update/transfer paths.

- S31锁屏单击电源键后局部提示“双击电源键解锁”，三秒后还原锁屏图；双击与密码规则保留。上下音量键在四个主页面复用屏幕左右键，包含书架分页控制，阅读与子页继续沿用原映射。
  S31 shows a local three-second double-press hint after one locked power press, preserving authentication. Volume keys share root side-key navigation/shelf paging while reader and child mappings remain page-owned.

## 待发布 / Unreleased

- 自动简化Z-Library格式的显示书名和作者：清理来源尾缀、嵌套重复作者、国别与外文别名、明确的译者说明、英文并列长标题及省略号截断的宣传尾注；保留TF原文件名、路径和阅读记录。文件列表不再因128字节名称缓冲而跳过长名，删除兼容书名中的省略号及接近长度上限的文件。
  Simplify Z-Library display titles/authors, removing source tails, duplicate nested authors, country tags and foreign aliases, explicit translator credits, long parallel English titles and ellipsis-truncated promotion while retaining original SD filenames, paths and reading records. File lists no longer discard names at a128-byte label limit; deletion accepts literal ellipses and files near the filename-length limit.

- 合入 EPUB 特殊章节路径兼容、嵌套目录书籍删除、无标题页面不再补占位名及单幅插图居中；新增插图双指缩放、双击放大与拖动。图片缓存失效和刷新失败可安全重试。
  Accept EPUB colon paths, nested book deletion, unnamed-page titles and lone-image centering. Add pinch/double-tap image zoom and drag, with stale-cache and failed-refresh recovery.
- 设置 → 显示 → 关机睡眠新增「双击解锁」开关；默认开启，关闭后单击电源键解锁，深睡唤醒沿用同一选择，锁屏密码仍需验证。
  Add the double-press unlock switch under Display / Power sleep: on by default, off uses one press. Deep wake shares the choice and PIN validation stays required.

- 阅读统计新增「本章跳转」：减/加逐页预选，或点按页码打开数字键盘，确认后跳转到当前排版的本章指定页。跳转后短暂提供「返回跳转前」，保留文字锚点；越界输入不改变阅读进度。
  Add chapter-page navigation in Reading statistics: step a draft or enter a page on the numeric keypad, then confirm against the current chapter layout. A short-lived return action retains the original text anchor; invalid pages never move progress.

- Pico新增TF卡异常自动恢复：插卡信号防抖，后台确认持续掉线，释放旧文件后重新挂载；首次重挂失败后再检查GPIO并退避重试。恢复成功后更新目录与保存的字体，USB读卡期间暂停自动恢复，不自动格式化。
  Add Pico TF recovery with debounced detect, background bus checks and stale-consumer cleanup before remount. Check GPIO after the first failed mount and back off retries; refresh directories/fonts on success, suspend recovery during USB ownership and never autoformat.

- Pico新增可选「速读黑白」，纯文字正文和页脚二值化后一次提交本板原厂DU；保留原普通/水波纹、插图/背景和自动全刷规则，兼容已有设置及TF配置备份。字缘更硬、灰色细节减少，速度与残影需实机确认。
  Add optional Pico Fast BW using one native-DU batch for binary text/footer targets, retaining ordinary/ripple, image/background and cleanup paths plus existing settings/TF backup compatibility. Harder edges and fewer gray details require panel speed/ghosting validation.

- 拼贴锁屏只解码画面内可见封面，减少首次合成运算；修复旧非书籍记录干扰缓存和TF缓存覆盖失败，复用完整拼贴时不再重复生成，保持原灰阶与排版。
  Decode only visible collage covers and reduce first-render work. Ignore foreign progress when validating cached artwork and safely replace FAT caches; reuse complete collages while preserving grayscale and layout.

- Pico深睡启动保留原锁屏画面，挂卡就绪后恢复缓存，不再超时覆盖成小锁；修复重复等待丢失缓存标记和TF已有锁图无法覆盖保存。未使用的旧内嵌锁屏位图不再占用应用空间，保持原分区与解锁规则。
  Preserve Pico lock art across deep boot, restoring references when media is ready without a timeout badge. Retain cached checkpoints across repeated waits and replace existing FAT lock snapshots safely. Unlink obsolete unused embedded art while keeping partitions and unlock rules.

- Pico设置列表上下滑动改用已有八相灰阶差分，保留文字抗锯齿目标和头像灰阶，不追加清白或松手定稿；题头与灰底栏保持，实屏深浅与残影仍需对照验证。
  Pico settings scrolling uses existing eight-phase gray differences, retaining antialiased targets and avatar grays without white prepasses or release settling. Keep headers/gray navigation; optical tones and ghosting still require panel comparison.
- 锁屏白底徽标和关机取消按钮统一使用图标库素材；Pico新增单击或双击电源键唤醒选项，默认双击，保留密码验证与原解锁提示。
  Use icon-set artwork for the white lock badge and power cancel mark. Pico can wake with one or two power presses, defaulting to two, retaining PIN verification and the existing unlock hint.
- 阅读辅助虚线只覆盖实际文字范围，保留缩进与行尾空白，横排和竖排共用固定虚线节奏。
  Limit reading guides to actual text spans, keeping indentation and trailing space clear with a shared page-relative rhythm in horizontal and vertical layouts.
- 联网OTA优先使用GitHub Pages清单与固件下载，官网清单保留备用；保留断点续传、失败重试、完整校验及空闲分区写入保护。
  Prefer GitHub Pages OTA metadata/images, retaining website metadata fallback, Range resumes, retries, full verification and inactive-slot protection.


## 0.3.3-rc99 / 2026-10-22

- 基于已确认本地版本51e7d8d发布。恢复设置、文件管理及二级页面文字抗锯齿，调整设置列表灰阶刷新，保留头像与底栏灰阶。
  Release the confirmed 51e7d8d baseline with antialiased system/file text and adjusted settings scrolling, retaining avatar/navigation grays.
- 修正充电状态判断，改善图标与百分比更新；新增单击/双击电源键唤醒选项，默认双击，密码验证与原解锁提示保持。此项保存在设备，尚未加入现有配置备份格式。
  Correct charging-state interpretation and add one/two-press wake, defaulting to two. Retain PIN checks and the existing hint. Store this preference on-device; existing backups do not include it yet.
- 锁屏白底徽标、关机取消按钮使用图标库素材；阅读辅助线按实际文字范围裁剪，横排与竖排的缩进和行尾空白不画线。
  Use icon-set lock/cancel marks and limit reading guides to text spans, leaving indentation/trailing space clear in both layouts.
- 进入阅读正文时完整清理旧画面；普通文字翻页保留校准灰阶与连续预读，正文和底部进度合并输出。
  Clean the preceding screen on reader entry and fuse ordinary body/progress output while retaining calibrated gray transitions and prefetch.
- 固件优先使用GitHub Pages OTA源，保留官网清单备用与下载校验；官网保留原rc98及更早正式版，继续提供USB全量包与TF完整应用升级包。
  Prefer GitHub Pages OTA with website metadata fallback and verification; retain original rc98/older web choices with USB base and complete TF application packages.

## 0.3.3-rc98 / 2026-10-21

- 透明书封采用按行读取与缓存合成，减少与书内字体、插图争用大块内存，并加快阴影和PNG图案读取；保留透明度和边缘连通去白规则。锁屏图片不可用时显示白底锁标志。
  Stream/cache transparent covers with lower peak memory and faster shadow/art reads, preserving alpha and edge-connected white removal. Unavailable art shows a lock on white.
- 下拉菜单直接复用当前画面，收起恢复底页；阅读页玻璃后的文字稍加模糊。下拉锁屏先收回菜单，全刷按钮自动收回菜单再刷新，按钮沿用局部反馈。
  Reuse/restore the existing quick-menu background with slightly softer reader glass. Dismiss it before locking or explicit full refresh; retain local button feedback.
- 电量高段显示更稳定，降低插拔后的突降；大容量TF卡先就绪后统计空间，减少等待。保留真实低电提示、原PMU读数与保护，容量估算仍受电池状态影响。
  Stabilize upper battery percentages and defer large-card capacity accounting until after mount. Retain low-power alerts, raw PMU readings and protection; battery estimates depend on cell condition.
- 加快首页时钟、书架封面及主页切换，合并可合并的推屏；保留原灰阶底栏。设置、文件及二级页面底栏常驻，右滑返回可从内容区起手，动势从左向右；头像保留灰阶。
  Reuse clock/cover caches, fuse eligible updates and keep gray navigation across system children. Rightward back swipes start within content and animate left to right; retain avatar grays.
- TF目录恢复「设为书籍目录」「设为字体目录」；继续阅读书摘跟随当前章节。设置最下方新增两次确认的恢复出厂设置，重置偏好与连接信息，保留TF文件、阅读记录和硬件校准。
  Restore custom book/font roots and current-chapter excerpts. A twice-confirmed factory reset clears preferences/connections while retaining TF files, reading records and hardware calibration.
- 锁屏后双击电源键解锁，有密码时继续验证；深睡等待解锁不再误显示准备资源。普通文字翻页改善非全刷后的字重加深与抗锯齿边缘，增加现有行预填以减少供数不足恢复；阅读自动全刷设置保留。
  Double-press power to unlock, then verify an enabled PIN. Avoid resource-loading text while waiting after deep sleep. Retain calibrated ordinary text gray transitions and larger existing line prefill; keep automatic full-refresh settings.
- 官网保留上一正式版的网页刷机与TF下载入口；TF包为完整应用镜像，USB包包含引导程序、分区表、应用和启动选择数据，均提供SHA-256校验。升级保持原双槽分区。
  Retain the previous official web/TF choice. TF contains the complete application; USB contains the base components with SHA-256 checksums. Keep the dual-slot layout.

## 本地整合 / Local integration / 2026-10-21

- Pico设置列表滚动去掉额外四相补白，保留原DU擦除及文字、头像的灰阶；每次手势仍只输出一次，题头与灰底底栏保持稳定。
  Omit four added whitening phases during Pico Settings scrolling while retaining native DU erasure and text/avatar grays. Keep one output per gesture with stable headers and gray navigation.
- Pico从首页、书架或目录进入阅读正文时，用一次完整GC16显示目标页并清理旧画面；普通文字翻页恢复校准GL16的前导擦除，保留等灰保持、连续预读及正文/进度合并输出。
  Present Pico reader entry from Home, shelf or TOC with one complete target-page GC16. Restore calibrated GL16 erasure on ordinary text turns while retaining equal-gray holds, prefetch and fused body/progress output.
- Pico设置、文件管理及其二级页面恢复文字抗锯齿；主页显示与周期残影清理恢复原rc96规则，普通阅读恢复原rc97直接灰阶差分，保留连续翻页预读与防欠载行预填。
  Restore antialiased Pico system text, rc96 presentation/cleanup, and rc97 direct ordinary text differences while retaining continuous-turn prefetch and underrun protection.
- Pico普通文字翻页将正文和变化的阅读进度合并为一次灰阶差分输出，减少正文后的串行等待；保留原抗锯齿目标、周期全刷及图片、水波纹的刷新规则。
  Fuse Pico ordinary text and changed reading progress into one gray-difference output, reducing serial footer work while retaining antialiased targets, periodic cleanup, image and ripple paths.
- 充电百分比与图标采用同一有效充电状态，避免PMU状态枚举滞后时高段电量无法增长；原低电提示与电池保护保持。
  Use the same valid charging state for the percentage and icon, allowing upper-SOC increases despite a lagging PMU state enum while retaining low-power alerts and protection.
- 锁屏单击电源键后提示「双击电源键解锁」，短暂支持点屏提示后恢复触摸休眠；原双击电源键及密码验证规则保持。
  Show a double-press-power hint after a single press, briefly enabling touch hints before returning touch to sleep; retain physical double-press and PIN checks.

- 墨水屏普通文字翻页保留完整灰阶迁移和擦除，以改善非全刷后字重加深及抗锯齿边缘；阅读扫描提高现有行预填，减少首轮供数不足引起的保护性整屏恢复。保留单轮输出、原相数与自动全刷设置；灰阶观感需实屏确认。
  Ordinary text turns retain calibrated gray transitions and erasure to improve heavier strokes and AA edges between full refreshes. Reader scans use the existing larger line prefill to reduce first-scan underrun recovery. Keep one output, phase counts and automatic full-refresh settings; optical quality requires panel confirmation.

- 透明书封在有少量PSRAM时合并阴影与PNG图案的顺序读取，减少一遍完整TF缓存读取，保留原透明度、阴影和像素效果；低内存仍可使用原路径，途中读卡失败不在半成品上重复叠图。
  Fuse transparent-cover shadow/art reads using a small PSRAM row ring, saving one full SD-layer read while retaining original alpha,shadow and pixels. Keep the original low-memory path and avoid recompositing over incomplete frames after I/O failure.
- 锁屏图片生成失败、缺卡或深睡缓存无效时统一显示白底居中圆锁，不显示资源加载提示；旧兜底缓存失效重建，密码与双击解锁规则保留。
  Unavailable lock art,missing media or invalid deep-resume caches show a centered round lock on white rather than resource-loading text. Invalidate old fallback caches while retaining PIN and double-press rules.
- 阅读正文展开下拉菜单时，玻璃后文字轻微增加模糊，保持按钮和状态栏清晰；仅展开时处理，点按仍局部刷新，收起还原原文。
  Slightly soften text behind quick-menu glass over the reader body,retaining crisp buttons/status. Process only on open,keep local input updates and restore the original text on close.

- 设置、文件管理及配网二级页右滑返回与阅读翻页共用识别逻辑：内容区不再限定从左侧边缘起手，去除额外的两倍方向限制。主页刷新模式、系统对比度等选择页同样适用；保留键盘自身手势、底栏、不可中断任务保护和本板原左向右返回动画。
  Settings, Files and network children reuse reading swipe recognition for back: start anywhere in content without the former left-edge or double-axis restriction, including home-refresh and system-contrast selectors. Retain keyboard gestures, navigation, noninterruptible-job guards and each board's existing left-to-right back animation.

## 本地整合 / Local integration / 2026-10-20

- Pico设置与文件管理二级页恢复原卡片、选中项和输入区底色，浅灰恢复完整灰阶驱动；黑白目录翻页在同次输出内加强旧墨擦白，保留常驻灰底栏、局部反馈和单次差分，不追加整屏补刷。
  Restore original Pico Settings/Files child card, selection and input backgrounds with complete light-gray drive. Strengthen old-ink whitening within the same binary directory update, retaining fixed gray navigation, local feedback and single-pass differences without another whole-screen settle.

- S31本地普通翻页改为与当前水波纹相同的四级22相，整页同步提交，不作条带动画；保留正文/进度合并及十页内原厂GC48清理。撤回实机导致欠载的128行文字备行，恢复192行。未启用实验或失败降级仍用完整GL48；速读黑白DU20、图片和手动全刷保持独立路径。
  Local S31 Ordinary now shares Ripple's four-level22-phase sequence in one synchronous output, retaining fused progress and vendorGC48 within ten turns. Revert the128-row reader prefill that underruns on hardware to192 rows. Builds without the experiment and failed-scan fallback retain fullGL48; DU20 FastBW,images and manual full retain their paths.

- S31阅读水波纹改为8带、末两带同步收尾，短波形调度由31拍减为28拍；各带完整相位保持。完整波形回退也合并正文与页脚，避免动画后再补刷进度条。主页/侧滑动画保持原调度，实际观感与耗时待实机确认。
  Use eight S31 reader-ripple bands with the final pair finishing together, reducing short-wave scheduling from31 to28 ticks while retaining every phase per band. Full-waveform fallback also fuses the body/footer to avoid a trailing progress scan. Main/edge-back scheduling stays unchanged; optical results and latency await hardware validation.

- S31普通阅读翻页改为整轮提交，复用内部1KiB相位查表和扫描配置，去掉逐相重新提交及外部64KiB相位缓存依赖。原灰阶、抗锯齿、所有有效/尾相和白底维护保留，失败不提交参考帧；水波纹/插图/周期清理仍使用各自路径。
  Batch S31 ordinary reading through the existing internal1KiB phase LUT and scan setup, removing repeated phase submissions and dependence on the external64KiB phase cache. Retain grays,AA,all active/tail phases and white maintenance, committing history only after success; ripple,images and cleanup retain their paths.

- 首页组件/阅读目标、设置、文件目录及配网传输二级页恢复常驻灰底栏；分页、系统键盘和设置密码键盘上移，底栏触摸可直接切页，升级写入等不可中断阶段保留保护。二级切换与侧滑仅刷新底栏上方内容，底栏恢复固定图标、局部选中横条的刷新规则。
  Home widgets/goals, Settings, file directories and network/transfer children retain gray navigation. Paging, system keyboards and the settings PIN pad move above it; tabs respond directly while noninterruptible update stages remain guarded. Child transitions/edge returns scan content above the fixed icons and locally updated selection marker.

- Pico文件目录和设置列表在三种主页模式下统一使用快刷差分，资料卡头像和底栏保留灰阶；去除二级页面及首次切换水波纹时的周期全刷，侧滑返回改用系统柔和灰阶水波纹，减少选中灰底及底栏黑闪。手动全刷、阅读自动全刷和真正扫描故障恢复保留。
  Pico file directories and settings lists share fast differences in all home modes while retaining avatar/navigation grays. Child navigation and first ripple selection no longer accrue periodic full output; edge returns use soft system-gray ripple to reduce selection/nav flashes. Explicit full output, reader auto-cleanup and real scan-failure recovery remain.

- Pico深睡恢复在锁屏缓存缺失、损坏或尚未挂卡时显示锁屏底图，不再把等待双击电源解锁显示为「准备资源」；挂载等待超时后不读取卡上缓存。有效缓存仍直接恢复前后帧，密码与双击唤醒规则保持。
  Pico deep resumes show a lock fallback when cached art is absent, corrupt or media is not ready, instead of disguising double-press unlock waiting as resource loading. Skip cached-art I/O after mount timeout; valid caches still restore both references, preserving PIN and double-press rules.

- Pico系统子页使用全高内容区，重排文件操作/分页、字体列表和系统键盘；左边缘右滑复用本板既有水波纹，从逻辑全屏左向右展开，仅侧滑触发。普通进入、按钮/按键返回和分页保留原路径；四个主页底栏保留灰底，系统和阅读保留原灰阶。
  Pico system children use the full viewport with relocated file actions/paging, font lists and keyboards. Left-edge back swipes reuse the board's existing left-to-right ripple over logical full-screen bounds; taps, buttons, keys and paging retain ordinary paths. Main navigation keeps its gray background and existing system/reader grayscale.
- Pico充电闪电采用固定黑底与白色描边，低电量仍清晰；电量/充电变化只更新可见头栏，覆盖层或沉浸阅读不插入更新。高段百分比加入中位数与按时间收敛，避免插拔后电压回落造成瞬时跳数；真正低电/低压立即采用有效原始读数。原PMU查表与原始诊断保持，绝对容量精度仍需放电校准。
  Pico charge icons use a fixed black well and white bolt, with power changes updating only visible headers outside overlays/immersive reading. Upper-SOC median/time convergence smooths cable-related jumps; valid low-SOC/voltage samples bypass smoothing. Preserve PMU tables/raw diagnostics; absolute accuracy still needs discharge calibration.
- Pico挂卡先发布可读状态，容量统计延后到文件管理后台请求；统计不再伪装成重新挂载，重挂/格式化与统计互斥，适用于大容量FAT卡。未知空间显示统计中，保留真实错误与拔卡检测。
  Pico publishes readable mounts before file-manager background allocation accounting, which no longer reports a remount. Serialize accounting with remount/format, preserve removal/errors, and show pending space rather than false free capacity on large FAT media.
- S31返回主页恢复底栏时，浅灰底从白用既有轻量驱动，旧文字先沿原DU擦白，不再使用灰阶黑复位；保留底栏灰底、黑色图标文字与内容区水波纹，普通返回同样适用。
  Restore S31 navigation with existing light-gray driving from white and original DU whitening for old ink, avoiding gray black resets. Retain the gray background, black icons/text and content ripple; ordinary back uses the same policy.

- S31设置/文件内容黑白、底栏恢复原灰阶底色；主页面切换遵循所选水波纹模式。修正侧滑动画的物理/逻辑屏幕尺寸混用，避免裁剪失败后误入整屏恢复，二级页面仅从左边缘向右返回使用LTR条带动画，点按进入/左上角或物理返回/分页仍普通刷新。充电图标黑底白描边，并用实际电流与有界异常保留改善漏显示。
  S31 Settings/Files keep BW content and the original gray navigation background; main-tab changes honor Water mode. Correct physical/logical bounds that caused edge animation crop failures and full recovery. Only rightward child back swipes animate LTR; child taps,top-left/physical back and paging remain ordinary. Keep the black charging well/white outline with current-assisted detection and bounded fault retention.


- S31二级页面扩展到完整高度：目录操作、分页、系统键盘及传输底部控件重新定位；字体列表和样式预览不再裁于旧底栏。侧滑返回使用柔和端点并将周期清理合入同轮十条带扫描，减少额外黑闪，保持灰阶与故障恢复。
  Expand S31 child layouts to the full viewport, anchoring file actions,paging,keyboards and transfer footers below the old navigation boundary. Back swipes use soft endpoints and incorporate periodic cleanup into one ten-band pass, retaining gray targets and recovery.

- S31普通文字翻页按真实前后帧跳过全保持前导相，保留一相准备及GL48全部有效驱动/尾相，进度继续一次输出；不固定裁相、不降低普通阅读灰阶。
  S31 ordinary text skips only actual all-hold leading phases, retaining one setup phase and every active/tail GL48 phase with fused progress. No fixed trim or reduced ordinary-reading grayscale.

- S31新增显式本地文字水波纹实验包：四级目标、22相/8带，正文进度一次输出，最多十页原厂GC48清理；保留普通构建、主页面和图片完整灰阶，扫描失败后本次开机降回原文字路径。实验灰阶尚需实屏验证。
  Add an explicit local S31 text-ripple experiment: four target levels,22 phases/8 bands and fused reading progress, with vendor GC48 within ten turns. Regular builds/main pages/images retain full grayscale; scan failures disable the experiment for that boot. Optical tones still require panel validation.

- S31切页把TPS唤醒等待与绘制重叠，整帧差分复用Core1供屏任务，1KiB预置查表减少逐行运算；空闲根页保留128行、射频活动保留192行预填，保留原波形、相位下限和每次断轨。DMA供数按官方缓存安全配置运行，队列指针表固定内部RAM并修复过量分配；扩大内部DMA缓冲并分段描述符，提高中断优先级并过滤准备/停止期旧中断，全显示输出与SDK Flash操作互斥，未完成扫描仍恢复。
  Overlap S31 TPS wake waiting with paint, reuse the Core1 feeder for whole-frame differences and pre-position the existing1KiB LUT. Retain128 idle-root rows and192 rows during radio activity, with original waveforms,phase minima and power-off after output. Use official cache-safe DMA settings and a correctly sized internal-RAM queue pointer table; enlarge internal DMA buffers with bounded descriptor chains,prioritize feeding and reject stale prepare/stopped interrupts. Incomplete scans still recover.
- 书架处理后的灰阶封面共用既有LRU，源像素、旋转、尺寸、对比度和点阵相位变化时重建，低内存直接绘制；首页分钟更新合并状态栏与静态表盘，减少重复扫描。
  Cache finished grayscale shelf artwork within the existing LRU, rebuilding on source,rotation,size,contrast or dither-phase changes and drawing directly on low memory. Fuse Home minute header/clock updates to avoid duplicate scans.

- S31本地PIE验证：显式实验包的Core1供屏掩码采用128位向量运算，对齐差分支持同核向量路径，Core0主任务及未对齐/尾部保持C，不强迁移任务。保留原始波形、扫描节拍和供电；运算微基准不等同于界面切换耗时，常规构建默认关闭PIE，交付构建关闭USB测试命令。
  Local S31 PIE validation: explicit experiments use128-bit Core1 masks/aligned differences, retaining C onCore0 and for unaligned/tail inputs without migrating tasks. Preserve waveforms,scan cadence and power; kernel microbenchmarks are not UI latency. Normal builds disable PIE,and delivery excludes USB test commands.

- S31下拉菜单按Pico用完整灰阶显示玻璃，启用圆钮只显示状态点，保留折射底图和黑色图标；保留2×2布局、前光和双滑块的局部跟手反馈。网络状态栏单独灰阶更新，不扩大滑块刷新区域。
  Present S31 quick-menu glass in full grayscale like Pico; active keys retain refracted backgrounds/black icons and show a badge. Keep the2×2 layout,frontlight and local slider feedback. Update network status separately in grayscale without expanding slider updates.

- 玻璃控件复用四个对称位置的折射计算，整数模糊热路径单独优化；保留原有灰阶像素、形状、布局与关闭释放规则，不增加常驻缓存。
  Share refraction geometry across four symmetric glass pixels and optimize the integer blur hot path, preserving pixels, shapes, layout and release-on-close without retained caches.
- S31完全相同的主页面重绘不再唤醒屏幕高压；空闲普通/黑白切页减少备行等待，传输或欠载后保留保守余量。原始波形、相位下限、供电顺序与手动全刷保持。
  Avoid waking S31 display HV for identical main-page repaints. Reduce prefill waiting for idle ordinary/BW transitions while retaining conservative margins during transfers or after underruns, with original waveforms, minimum phase timing, power sequencing and manual full refresh.
- 修复差分区域右边缘误用左边缘像素奇偶、导致边界多算或漏算的问题，补充裁剪与半字节边界回归。
  Fix difference bounds testing the left edge's parity at the right edge, which could enlarge or truncate the result, with crop/nibble regression coverage.

## 本地整合 / Local integration / 2026-10-19

- 加快首页时钟：小屋与IKEA缓存字体无关的房体/刻度，数字和指针保持实时；猫咪眼睛不再重复解码底图，Kehai圆环轨迹与圆点复用分钟底图的局部区域。月相、星号、融化时钟及放大时间页共用有界PSRAM缓存，保留原灰阶与抗锯齿；缓存不足直接绘制，进入阅读前释放。
  Speed up Home clocks: cache font-independent House/IKEA bodies and ticks while keeping numerals/hands live. Cat eyes reuse their decoded face, and Kehai trail/dot crops reuse a minute base. Moon, Asterisk, Dali and enlarged time views share bounded PSRAM caches with unchanged grayscale/AA; draw directly when scratch is unavailable and release it before reading.
- 原版墨水屏的主页差分刷新合并底栏横条，省去一次单独推屏；固定图标与标签保持，周期清理、失败恢复及原波形相位不变。
  Fuse the original board's navigation marker with differing main content to remove a separate scan, retaining fixed icons/labels, periodic cleanup, recovery and all original waveform phases.

- 下拉菜单直接复用当前画面，顶部缓存用于收起还原，不再重复重画整页；玻璃模糊每个源像素只解包一次，保持原有灰阶、折射及圆键局刷。
  Open quick controls over the existing page and restore a top-only snapshot on close instead of repainting the page. Unpack each glass source pixel once, preserving grayscale, refraction and local key feedback.
- 恢复TF目录中的「设为书籍目录」「设为字体目录」，重启后保留；移动或改名时更新引用，删除后回退默认目录。设置保存失败时保留原选择，目录按钮与分页控件分开。
  Restore custom book/font roots in the TF browser and persist them across restarts. Rebase moved/renamed roots, fall back after deletion, and retain the prior choice if saving fails; keep root buttons separate from pagination.

- 下拉菜单锁屏前先还原底页，透明书封不再包含菜单；点全刷自动收起菜单，只全刷还原后的页面。保留圆键的局部点按反馈。
  Restore the underlying page before quick-menu locking so transparent covers exclude the menu. Full refresh dismisses the menu and refreshes the restored page, retaining local button feedback.
- 继续阅读书摘按当前已保存章节校验并更新，正文切入成功后提取完整短句；不再长期沿用整本书第一次缓存的句子。标题、目录、图片说明及无完整句子的章节不使用旧章书摘；首页不额外打开或解析书籍。
  Validate Continue Reading excerpts against the saved chapter and extract a complete sentence after successful chapter entry. Never reuse a book's first quote across chapters or old chapter excerpts for headings, navigation, images or chapters without complete prose. Home does not open or parse books for this.

- 透明书封按行解码并缓存PNG叠加层，锁屏仍使用当前已显示页面；减少与书内字体、插图争用的大块连续内存。保留原alpha与边缘连通去白规则，缓存校验失败时重建。
  Stream and cache the PNG cover layer over the actual displayed page, avoiding full-layer allocations competing with embedded fonts/illustrations. Preserve alpha and edge-connected white removal; rebuild invalid caches.
- 设置最下方增加「恢复出厂设置」，二次确认后重置偏好、资料卡、Wi-Fi、蓝牙配对和锁屏密码；保留TF文件、阅读记录、硬件校准及分区。失败会提示重试。
  Add a twice-confirmed Factory Reset at the bottom of Settings, resetting preferences/profile/Wi-Fi/Bluetooth/PIN while retaining TF files, reading history, calibration and partitions. Report failures for retry.
- 单击电源键仍锁屏；锁屏后两次短按释放间隔60–600ms才唤醒，有密码时再进入验证。拿起、单击和睡眠异常不直接解锁；主机深睡的硬件上电阶段仍受设备启动延迟影响。
  Keep single-press locking; require two short releases 60–600ms apart to wake, then verify an enabled PIN. Pickup/single presses/sleep faults do not unlock. Host power-up from deep sleep remains subject to board boot latency.
- 普通纯文字翻页使用与水波纹相同的灰阶扫描时序，全部相位同步输出，不追加补灰刷新；保留周期全刷、图片和背景路径。最终观感与耗时需真机确认。
  Ordinary text turns share ripple gray scanning, outputting all phases synchronously without another gray pass. Retain periodic full refresh, illustrations and backgrounds. Optical quality and elapsed time require hardware confirmation.

- Metalio首页继续阅读卡片改为纯白。设置主列表与文件管理内容使用黑白目标和完整DU20，切入与设置列表滚动不再等待灰阶补刷；固定底栏沿用已显示像素，首页/书架灰阶与设置子页的图片预览保留。
  Keep the Metalio Continue Reading card white. Root settings and file content use binary targets and complete DU20, avoiding gray completion passes for entry/settings scrolling. Retain displayed navigation, Home/Bookshelf grays and image previews in settings subpages.

- S31月相、星号和达利时钟复用现有可释放的分钟缓存，裁剪/方向/样式变化时重绘。普通空闲切页减少备行等待，欠载后恢复保守余量；原始波形、相位脉宽及屏幕供电顺序保持。
  Reuse the existing releasable minute cache for S31 Moon/Asterisk/Dali clocks, redrawing changed clips/rotation/styles. Reduce prefill waiting for idle ordinary transitions, restoring the conservative margin after underruns; retain raw waveforms, phase pulses and power sequencing.

- Metalio普通主页切换复用书架外框的定向黑白端点，保留完整中灰相位，减轻白底先压黑；周期整理与底栏横条合并同轮扫描，低内存回退不误记已完成整理。文件管理首帧已扫描完时不再重复统计并整页刷新，保留挂载与分配失败重试。
  Metalio ordinary root transitions share directional shelf-chrome endpoints while retaining complete midgray phases. Fuse periodic cleanup with the footer marker and retain cleanup debt after memory fallback. Avoid a duplicate completed file-entry scan/present while preserving mount/allocation retries.

- 保留S31原厂GC48/GL48/DU20与供电时序；新增切页分段耗时日志供实机验收。当前普通灰阶配置48相、每相至少12ms，不能将完整刷新标为300ms以内。
  Retain S31 vendor GC48/GL48/DU20 and power timing, with stage timings for hardware acceptance. The current ordinary gray profile has 48 phases at a minimum 12ms each; complete refresh is not claimed below 300ms.

## 本地整合 / Local integration / 2026-10-18

- Metalio同步正式rc97的锁屏读取失败放行与重试、紧凑启动检查点和旧记录迁移；共用启动记录实现但保留各板命名空间。补齐快捷蓝牙关闭取消、合成帧直接全刷及书架白底，保留S31前光双滑块、振动、字体事务与独立电源/睡眠流程。
  Align Metalio with rc97 lock-setting recovery and compact startup checkpoints, sharing record logic while preserving board namespaces. Complete quick Bluetooth off/cancellation, composed-frame full refresh and white shelf chrome, retaining S31 light sliders, haptics, font transactions and power/sleep handling.

- Metalio文件管理、设置及其传输/图片子页隐藏底栏；左侧32px边缘向右滑返回上一层并使用一次十条带水波纹。主页水波纹模式也适用于文件/设置及二级页切换，保留灰阶、上传/升级门禁和定期清理。
  Metalio file/settings children and their transfer/image pages hide navigation. Swipe right from the left32px edge to return with one ten-band ripple. Water mode also applies to Files/Settings and child-page changes, retaining grays, upload/update guards and periodic cleanup.

- 新增板型选择层，同一份阅读器/UI源码可选择 `pico`、`metalio47` 或一次生成两个本地固件包；驱动、波形、供电、编译目标、设置空间与分区按设备隔离，校验镜像身份和烧录地址。Metalio保留V1.5硬件适配及本地OTA策略，原始波形表不变。构建说明见 [双板型构建](BOARDS.md)。
  Add board selection for one maintained reader/UI source and separate Pico/S3 and Metalio 4.7/S31 builds, with isolated drivers, waveforms, power, targets, settings and partitions. Validate image identities and flash plans. Retain Metalio V1.5 hardware and OTA policy; raw waveform tables remain unchanged. See [board builds](BOARDS.md).

## 0.3.3-rc97 / 2026-10-18

- 刷机页修复通用Bin地址的旧缓存风险：按固定版本下载，逐一校验四个文件的大小、SHA-256和应用内部版本，验证后再将独立快照交给安装器。文件不匹配时不打开刷写连接；TF下载也使用固定版本地址。固件镜像保持原rc97不变。
  The web flasher downloads version-pinned files, verifies all four sizes/hashes and the embedded app version, then supplies an independent verified snapshot to the installer. Mismatches stop before opening a write connection; TF downloads use immutable version URLs. Firmware bytes remain unchanged.
- 修复锁屏设置读取失败被缓存整个开机周期、导致未设置密码的设备也显示四位密码的问题。读取失败或记录损坏时直接进入系统，不再用密码界面阻拦启动；不创建默认密码、不清空原记录。能正常读取的已设密码仍需验证；存储异常期间密码保护暂时不生效，恢复后可重新读取。
  Failed or malformed lock-setting reads no longer block startup or create default credentials. Readable enabled PINs still require verification; protection is temporarily bypassed during storage faults, without erasing the saved record, and subsequent checks can retry.
- 开机在大缓冲与字体加载前读取小型锁屏设置，减少资源紧张造成的读取异常；只有成功读取才缓存结果。设置不可读时，密码设置页显示暂不可用，禁止覆盖旧密码。
  Read the small lock setting before large buffers/fonts and cache only conclusive results. Unavailable settings cannot overwrite saved credentials.
- 修复字间距点按仍沿用旧上限、反复跳到-11px的问题，点按与拖动统一为-15至+15px，每次1px；重排失败恢复原间距和阅读位置，不保存错误值。段距两端按钮按现有11档逐级调整，可到100%。
  Tracking taps and drags share the complete -15 to +15px range in 1px steps. Failed reflow restores the original spacing and position without saving an incorrect value. Paragraph endpoint buttons follow all 11 existing stops through 100%.
- 目录书名与章节名统一使用标题绘制路径，沿用当前系统字体与整行字体来源，直接按目标字号生成抗锯齿；内建补充点阵保持原生大小，避免放大后模糊和笔画不均。当前章节的灰底正确混合字形边缘，不留下白边；正文抗锯齿设置保持。
  Directory book/chapter names share the title path with the active system face, stable whole-line font selection and native-size AA. Keep bitmap supplements at their native size, blend selected-row edges into gray and preserve the body's AA setting.
- 延续rc96的功能、分区与数据格式，保留用户配置、原密码记录和阅读进度。
  Retain rc96 features, partition/data compatibility, user settings, saved credentials and reading progress.

## 0.3.3-rc96 / 2026-10-18

- 设置页题头与列表外围改为纯白。普通模式进入设置及周期整理复用书架的同轮分区刷新：状态栏/题头柔和，列表白色仅向白擦除，保留内容灰阶；普通列表滑动使用柔和差分，手动全刷及原离散滚动保持。
  Keep settings chrome and list whitespace white. Ordinary entry/periodic cleanup share the shelf's simultaneous scan: soft status/title, directional white-list erasure and retained content grays. Use soft ordinary list differences while retaining manual full refresh and discrete scrolling.

- 启动记录改为按路径实际长度保存，兼容旧记录；NVS不足时只回收本模块旧检查点再尝试写入，保留设置、配对、书签与进度。普通版忽略不支持的缓存恢复入口，真实TXT/EPUB仍需成功记录后才允许开书，断电/解析失败保护继续生效。
  Store compact startup records by actual path length with legacy migration. On low NVS space reclaim only the old checkpoint; preserve settings, bonds, bookmarks and progress. Ignore unsupported cache recovery entries while requiring a successful checkpoint before TXT/EPUB parsing.

- 书架留白改为纯白；普通切页白像素仅向白擦除，封面/材质其余十五级保留校准序列，同步输出与手动全刷不变。
  Keep shelf whitespace white and whiten it directionally during ordinary transitions. Retain calibrated art grays, simultaneous output and manual full-refresh quality.

- 快捷蓝牙按钮支持未连接时再次关闭，并取消未启动的最近设备连接请求；快捷全刷直接输出当前合成画面，省去底页重绘和玻璃重新计算。
  Toggle quick Bluetooth off even before connection and cancel an unstarted recent-peer request. Quick full refresh presents the composed frame directly without re-rendering the underlay or regenerating glass.

- 普通书架改为同步分区输出：题头、封面/材质、分页与选中横条在同一轮逐相呈现，取消先刷上下再刷中间的三段等待；外侧保留柔和刷新，中间保留原校准灰阶及周期清理，临时PSRAM仅2048B，失败统一恢复。
  Present ordinary shelf chrome, calibrated artwork, pagination and the selection marker in shared scan phases instead of three serial regions. Retain soft outer output, calibrated middle phases and periodic cleanup, with 2048 temporary PSRAM bytes and common failure recovery.

- 阅读字号扩展为18–90px、每档1px，设置/备份/逐书进度统一范围；普通模式进入书架时，强灰阶整理只限封面与书架材质区域，状态栏、题头和分页采用柔和差分刷新。
  Extend reader type size to 18–90px in one-pixel steps across controls, settings, backups and per-book progress. Ordinary shelf entry confines strong grayscale settling to cover/furniture art; status, title and pagination use soft differential output.

- 下拉快捷菜单按下即给圆内局部反馈；Wi-Fi和蓝牙状态仅刷新对应圆键及必要的状态栏，不重画底页或重复生成玻璃。四圆键缓存关闭即释放。蓝牙按钮开启蓝牙并异步连接最近成功使用的翻页器，保留网络内存保护、重试退避和设置页关闭入口。
  Acknowledge quick-control presses locally; update Wi-Fi/Bluetooth keys and the necessary status strip without repainting the page or regenerating glass. Release four small key caches on close. Bluetooth enables and asynchronously reconnects the most recently successful turner, retaining network-memory guards, retry backoff and the Settings off switch.

- 顶部下拉快捷菜单及四个圆键采用与密码锁屏一致的液态玻璃，保留清晰底图和状态栏；三个主页模式统一灰阶，临时PSRAM绘完即释放，内存不足退回清晰实心控件。
  Share PIN-style liquid glass with the top quick panel and its four controls, keeping artwork/status crisp and grayscale in all main modes. Free temporary PSRAM after painting, with legible solid controls on low memory.

- 刷回普通版时，书架、首页续读和启动恢复会忽略其他书源的缓存清单，仅接收TXT/EPUB；保留原缓存与进度，避免不可读条目占用入口。
  When returning to the standard firmware, ignore other-source manifests in shelf history, Home continuation and startup requests; retain caches and progress while admitting TXT/EPUB only.

- 参考 MokuReader，为关机滑条、重启/取消圆钮和密码锁屏数字圆键增加液态玻璃效果；保留清晰页面底图，玻璃内加入模糊、边缘折射和亮边。滑动/输入只恢复局部4位灰阶缓存，关闭释放，内存不足退回实心控件。
  Adapt MokuReader liquid glass for power controls and PIN keys, retaining sharp background artwork with blur/refraction/highlights inside the controls. Input restores local packed grayscale snapshots without reblurring; close releases caches and low memory falls back to solid controls.

- 段距扩展为11档，25%位于中间；字间距扩展为−15..+15px、每档1px，0px居中。旧设置与备份按实际间距迁移，旧+16px收敛为+15px。
  Center 25% among eleven paragraph-gap stops and 0px among 31 tracking stops (−15..+15px). Migrate saved spacing by physical value, clamping legacy +16px to +15px.

- 非全屏阅读题头的收藏图标统一使用 AnimateIcons Bookmark / Bookmark Plus，已收藏显示加号书签，保留原收藏逻辑、触控范围与局部刷新。
  Use AnimateIcons Bookmark/Bookmark Plus for unsaved/saved header favorites while preserving favorite storage, hit targets and local presentation.
  修复图标生成器遗漏 SVG 线段数字坐标属性的问题，确保收藏后的加号实际显示。
  Preserve numeric SVG line attributes so the saved favorite's plus is visible in the generated artwork.

- 全部圆形返回键统一使用 AnimateIcons Circle Chevron Left，包括系统子页与阅读面板；按压保留原位和圆内反馈。
  System and reader-sheet circular backs share AnimateIcons Circle Chevron Left, retaining their positions and interior press tint.
- 底栏四图标恢复按下放大、随后回位；当前页、待切页和加载中的点击均有独立局部反馈，快速连点同一图标可重触发。
  Restore footer enlargement/settle on every contact, including active/pending/loading pages and rapid repeated taps, with local feedback independent of navigation.

- 阅读纯文字水波纹改用文字专用灰阶序列，保持未变化的抗锯齿边缘，并减轻灰色笔画先加深再回浅的过渡；保留最终字形、8条带、11090us节拍及原有全刷/插图/失败恢复。
  Text-only ripple shares the existing softened text phases, holding unchanged antialias pixels and reducing dark-edge transients while preserving glyph targets, eight bands/11090us, cleanup, image and recovery paths.

- 首行缩进微调支持长按加减，保持每步1px，松手后统一重排并保存；增加按钮下沿触控容错，排版空白不再误触“返回字体设置”，滑出、多指、锁屏与切页取消连续调整。
  Hold the indent adjustment buttons for 1px repeats, reflow/save once on release, tolerate lower-edge taps and keep layout gaps inert; leaving, multitouch, lock or navigation cancels repetition.

- 分页栏采用 AnimateIcons 的 Left/Right/First/Last 轮廓，首末页改为止点竖线；书架与文件管理分页栏上移，目录列表保留独立点击区域。
  Share AnimateIcons chevrons and first/last stop bars, raise shelf/file pagination and keep directory rows separate from paging targets.
- TF 目录移除“设为书籍目录”和“设为字体目录”；统一读取 books/fonts，旧设置与备份按固定目录使用，移动/改名/删除仍可用。
  Remove custom media-root actions and retain fixed books/fonts roots across legacy settings/restores, while preserving ordinary file management.

- 文件管理与书架共用“最前 / 上一页 / 下一页 / 最后”分页栏，书架保留书籍数量和页码；箭头加大，按键分页轮廓加粗，按压仅局部刷新。

- 阅读边距恢复九个真实像素档位，横排、竖排均保留中间值；首行缩进按正文汉字宽度计算，微调扩展至 ±100 px，并兼容旧配置。
- 字间距扩展至 −15～+15 px，每档 1 px，0 px 居中；旧设置与备份按实际间距迁移，旧 +16 px 就近收敛为 +15 px。
- 主页面左右按键切换页面，中键长按全刷；书架中键短按进入或退出分页控制，沿分页箭头轮廓描边并提供局部按压反馈，可随时切页中断。
- 阅读字体名称直接使用所选阅读字体预览；补齐“抗锯齿”的原生轮廓字形，界面标签不受正文抗锯齿开关影响。
- 字重微调继续每档 1/16 px，改用轮廓距离扩张，减少额外的灰色边缘。
- 自动全刷使用完整灰阶定稿的一次输出；手动全刷保留 rc90 路径。
- 深睡恢复复用已验证的实际锁屏帧，减少开机推屏和重复字体装载；旧缓存、缺卡或校验失败沿用安全启动路径。
- 锁屏新增透明 PNG 书封，以锁屏前的当前页面为底，保留 PNG 原画布比例和透明度，加入轻微悬浮阴影。推荐 684 × 1216 px，文件放入 TF 卡 pictures 目录。
- 白底 PNG 自动去掉与画布边缘连通的白色背景，保留封闭图案内部白色；已有透明度的 PNG 沿用原始透明通道。

## 0.3.3-rc95 / 2026-10-16

- 修复无变化差分或异常返回后像素驱动可能保持开启的问题：结束时统一释放驱动，短暂 I2C 失败有界重试，避免静止白底持续受驱动；不增加刷新或改变波形、电压与画质。
  Release pixel drive after unchanged differentials or failed output, retrying transient I2C errors without extra scans or changes to waveforms, voltage settings or image quality.

## 0.3.3-rc94 / 2026-10-16

- 阅读手动与周期全刷恢复 rc90 的原表 GC16 目标页输出，取消纯文字全刷前的独立擦白和后续文字补刷；保留普通翻页预绘、插图与水波纹设置。
  Restore rc90's original-table GC16 target-frame output for manual and periodic reader refresh, removing the separate white clear and text pass while retaining ordinary prefetch, images and ripple settings.
- 接入 PR #34：修正竖排阅读辅助线的位置和虚线节奏，补齐横线类符号旋转，并按字体原生全角步进对齐章节数字编号。
  Integrate PR #34 for vertical reading-guide placement and dash phase, dash-like punctuation rotation and chapter-number alignment using native fullwidth metrics.
- 接入 PR #32：RTC 轨迹记录开机各阶段耗时；首帧后停止开机标记，普通回首页不挤占记录。保留后台封面缓存加载。
  Integrate PR #32 boot-phase RTC timing, stopping boot marks after the first frame and retaining background cover loading.
- 接入 PR #33：设置 → 升级和恢复 → 屏幕修复，手动执行面板黑白恢复与当前页面整屏重建；结果提示随最终画面一次显示，输出失败保留恢复路径。正常阅读波形、底栏边界和分段滑动不变。
  Integrate PR #33 manual panel repair in Settings → Upgrade and recovery, restoring the complete current page with its final notice once and retaining output-failure recovery. Normal reading waveforms, navigation bounds and discrete scrolling remain unchanged.

## 0.3.3-rc93 / 2026-10-15

- 优先优化连续翻页：下一页预绘与当前页推屏重叠，连按复用已准备内容，未完成的目标接着绘制。保留正式 rc92 的阅读波形、插图、水波纹和周期清残影规则。
  Prioritize rapid turns with output-overlapped prefetch and partial-target reuse; retain rc92 reading waveforms, images, ripple and periodic cleanup.
- 首页和书架封面使用有界后台任务，优先复用 TF 成品缓存；过期结果按代次丢弃，书架整组显示。普通亚克力书架题头与内容合并为同一轮校准灰阶输出，固定底栏保持。
  Prepare covers in bounded background jobs, reuse processed TF caches, reject stale results and reveal shelf groups. Ordinary acrylic headers/content share one calibrated gray scan with fixed navigation.
- 拼贴锁屏复用小型书库索引，长章节行缓冲按实际文字增长，减少重复扫描与内存峰值；保留锁屏画质和低内存回退。
  Reuse a small collage catalogue and grow line scratch on demand, retaining artwork quality and low-memory fallbacks.
- 首页继续阅读整块可点，并增加封面反馈；「返回主页」按开书来源回首页或原书架分类页。资料卡头像后台准备，不再阻挡首页与设置操作。
  Make the entire continue card actionable with cover feedback, return to the opening's Home/shelf origin and prepare avatars without blocking page input.
- 改进 S3 RTC 计时，已联网每小时校准，离线空闲每六小时短连已保存 Wi-Fi 对时；休眠和其他网络操作可取消后台对时。
  Improve RTC timekeeping with hourly online and six-hour idle offline calibration; background sync yields to sleep and other network operations.
- 增大底栏触控范围并在首次接触时响应，优化普通主页切换、底栏边界和状态签名显示。三种主页模式的设置列表恢复分段滑动，取消此前跟手与惯性方案。
  Expand root-tab targets and respond on initial contact, refine ordinary transitions, navigation boundaries and status signatures. Restore discrete settings swipes in all main modes, removing follow/inertial scrolling.
- 底部上滑固定打开阅读工具栏，取消按键必须映射工具栏的限制；首行缩进控件增加字号与间距，返回字体设置统一黑底白字。
  Keep bottom-edge Tools independent of key mappings, enlarge and separate indent controls and use the matching dark return button.
- 官网网页刷机、TF、联网 OTA 与 Release 附件来自同一次正式构建；保留旧版本固定下载地址。
  Publish one build across web, TF, OTA and Release assets while retaining immutable older downloads.

## 0.3.3-rc92 / 2026-10-14

- 首页新增 Moon、Kehai、Cat、Dali、Asterisk、小屋时钟与最近阅读书夹，保留 IKEA PS1995；长按选择样式，单击打开对应时间页或书架。阅读记录新增月历、每日目标与“已闭环”。
  Add selectable clocks and the recent-reading folder, matching time pages, monthly records and daily goals.
- 猫眼与 Kehai 秒针在普通、快刷、水波纹模式使用短局部刷新；Kehai 保留一圈浅色轨迹，避免动态区域被主页水波纹改写为较重刷新。修复书夹预览、边线与返回键反馈。
  Preserve short local clock motion in all main modes, a bounded Kehai trail and exact feedback borders.
- `books` 下的子目录可作为书架分类，从书架题头下拉选择；切页往返保留分类与当前页。书架布局位置及封面显示同步调整。
  Select shelf categories from books subdirectories and retain category/page positions across navigation.
- 设置列表使用跟手黑白滚动并加入可打断的减速惯性；普通与水波纹停止后补回灰阶，快刷保持黑白。滚动复用有界缓存，低内存回退原绘制。
  Add interruptible inertial settings scrolling with bounded binary caches and gray settling where needed.
- 阅读字重改为胶囊减/加，每次微调 1/16 像素覆盖率，旧配置保留实际粗细；自动全刷后的纯文字沿普通文字成像路径，减轻第一页偏淡。
  Refine weight by 1/16-pixel coverage steps, preserve legacy thickness and align automatic text cleanup with ordinary text formation.
- 合入 PR #27、#28、#31 原实现：竖排阅读、翻页器完整报告帧学习与连接订阅修复、RTC 崩溃记录及锁屏图标统一。
  Include the original PR #27/#28/#31 implementations for vertical reading, Bluetooth reports/link reliability, crash records and lock icons.
- 关机菜单采用滑动确认与圆形重启/取消；头像成品缓存到 TF 卡，票根使用系统字体与抗锯齿，首页边线及标题可读性优化。
  Add slide-to-off, round restart/cancel controls, cached avatars and clearer system-font ticket/Home rendering.
- 官网刷机、TF、联网 OTA 与 GitHub Release 来自同一次产品构建，保留历史固定下载地址。
  Publish one product build for USB, TF, OTA and GitHub Releases while retaining immutable older downloads.

## 0.3.3-rc91 / 2026-10-12

- 合入 PR #24、#25，支持 EPUB 内嵌 TTF/OpenType 字体，修复开书与关闭书内字体的栈溢出，减少大型章节索引和多字体缓存占用。
  Include PR #24/#25 for embedded EPUB faces, stack safety and bounded index/glyph memory.
- 阅读新增五档字重和抗锯齿开关；重排字体与阅读设置，中键长按默认返回首页，底部上滑始终打开工具栏。
  Add five stroke weights and optional antialiasing, paired reading controls, default Home on middle hold and bottom-edge Tools.
- 系统设置重新分组，字号并入系统字体，选择字体可确认同步阅读；100–200%保持单行，阅读工具与子面板适配系统字号。
  Regroup Settings, confirm reader-font sync and fit large system sizes in Settings and reader panels.
- 修复边距滑杆、居中章节题头的两行对齐和间距，字体丢失稳定回退；水波纹插图页使用一次灰阶更新。
  Fix margin stops, two-line heading alignment/spacing and missing-face fallback; present illustrated ripple pages once in gray.
- 书架、首页续读与拼贴复用TF成品封面；书架整页准备好后一起显示，修复偶发封面漏刷，保留冷缓存有界准备和低内存回退。
  Reuse prepared TF covers, reveal whole shelf pages and retain retries and bounded cold-cache work.
- 壁纸锁屏缓存最终16级灰阶抖动画布；拼贴优化缓存准备，主动锁屏先清白显示圆锁再生成完整画面，保留画质。
  Cache full-quality wallpaper locks and streamline collage preparation with immediate white/lock feedback.
- 主页和阅读水波纹采用8条带、11,090µs目标节拍，保留完整37相与固定底栏；普通纯文字翻页减轻短暂压黑。
  Match main/reader ripple launch timing while retaining gray phases and navigation; soften transient darkening on ordinary text turns.
- 设置只绘制可见行并复用头像，入口箭头独立于字体；阅读状态栏按分钟更新时间。
  Cull Settings rows, reuse avatar pixels, draw independent chevrons and update the reading clock each minute.
- 修复 WiFi 断联重连和确认遗忘；TF容量使用真实挂载分区并重算过期FAT32提示；票根长统计保持边界。
  Fix WiFi reconnect/forget, partition allocation counts and ticket statistic bounds.
- 合入官方 a2a63a6 的睡眠/关机外设与引脚低功耗处理，保留密码、按键唤醒和阅读恢复；功耗以实机为准。
  Port official low-power peripheral/pin parking, preserving PIN, key wake and reader recovery; power draw requires hardware measurement.
- 在线刷机、TF与OTA使用同一次产品构建，Bin、完整USB包和SHA-256发布至GitHub Release；原固定版本地址保持。
  Publish one verified product build for USB, TF and OTA with immutable older URLs and Release checksums.

## 官网说明书 / 2026-10-10

- 上线 21 章、135 项操作、30 幅界面图解的详细说明书和 69 页 PDF；新增「问说明书」，支持关键词、口语问句与模糊检索，直接定位到操作步骤。保留旧说明书链接及 PDF 下载地址，固件仍为 rc90。
  Publish the illustrated manual and searchable task finder; retain existing manual/PDF links and the rc90 firmware.

## 0.3.3-rc90 / 2026-10-09

- 固件名称统一为 kiikoread；资料卡移除署名与电量，姓名和编辑垂直居中。水波纹主页使用与普通模式相同的灰阶画面。
  Rebrand the firmware as kiikoread, simplify the profile card and center its name/edit action; ripple and normal main pages share gray artwork.
- 新增 kiikoread.com 主站联网升级，失败回退 GitHub；保留原刷机与更新入口。正式二进制统一保存到 GitHub Releases，网页与 OTA 使用经过 SHA-256 验证的同一构建。
  Add primary-site OTA with GitHub fallback, preserve existing flashing/update entry points and store official binaries in Releases with verified website/OTA mirrors.
- 合入 PR #22：缺失系统字体不再每 750ms 反复加载内建字体并触发全刷，字体回到卡内后仍能自动加载。
  Merge PR #22: missing system fonts no longer repeatedly reload the built-in or trigger full refresh; retries still pick up restored files.
- 合入 PR #23：只读确认卡内为不支持的文件系统时提供格式化提示，需再次确认清空全部数据；初始化、供电与读写错误不误提示格式化。
  Merge PR #23: offer formatting for read-only confirmed unsupported media, with explicit second-tap data-loss confirmation; avoid misleading hints for initialization, power and I/O failures.


- 官网品牌改为 kiikoread，采用纯白界面；增加可切换放大的界面展示、功能介绍、统一版本的刷机/下载/日期/日志，以及同源在线和 PDF 使用说明。保留原发布包、历史版本与升级网址。
  Rebrand the bright-white website as kiikoread, with selectable enlarged screen previews, features, consistent release flashing/download/date/notes and shared HTML/PDF documentation. Preserve published binaries, archives and upgrade URLs.
- GitHub 刷机首页跳转至 kiikoread.com，保留版本选择与页面位置；原固件下载、使用说明和联网更新地址继续可用。
  Redirect the GitHub flasher homepage to kiikoread.com with the selected version and page anchor preserved; existing firmware, manual and OTA URLs remain available.

- 应用确认的较小列表封面120×156，保持原中心点、右侧书名/作者/进度及局部弹动，等比例填充并居中裁切。
  Apply the approved 120×156 list covers, preserving their center, text/progress positions and local bounce with aspect fill and centered cropping.

- 列表封面点击改为小幅上弹后回位，复用现有封面，仅刷新移动范围；快刷黑白、普通/水波纹灰阶，无额外反馈框。
  List covers bounce and settle using existing artwork and movement-only local differentials; fast retains BW and ordinary/water grays, without an added frame.

- 密码锁屏缓存增加样式与壁纸身份校验，切换后深睡唤醒/冷启动不再误用之前的拼贴图；自动休眠与手动锁屏继续使用当前选择。旧缓存自动重建，密码和配置不受影响。
  PIN artwork caches now check the selected style and wallpaper identity, preventing stale collage reuse on cold/deep wake. Auto/manual lock keep the current choice; old artwork is regenerated without changing credentials or settings.

- 修复 TF 卡检测通信失败被误判为拔卡的问题；高速初始化失败先等待重试，再降至 20/10 MHz，`ESP_ERR_INVALID_STATE` 不再直接终止重试。初始化错误不自动建议格式化；保留真实拔卡失效、显式重挂及 USB 独占保护。
  TF detection I/O failures no longer masquerade as removal. Failed high-speed initialization settles and retries before 20/10 MHz fallback, including `ESP_ERR_INVALID_STATE`. Init failures do not suggest formatting; true removal, explicit remount and USB ownership guards remain.

- 新增 D 版书架拼贴锁屏：原比例真实封面、15° 倾斜、圆角与16级灰阶；资料卡名字及大小分明的书籍数量使用导入系统字体原生绘制，TF缓存、有界工作内存与低内存回退。
- 移除半透明书袋和书脊样式，旧设置及备份回退亚克力；保留票根、壁纸、两种原书架样式及原睡眠恢复逻辑。
- 浅色拼贴封面增加沿真实圆角边缘的细灰描边，缓存自动重建；深色封面保持原样。
- 新增可选四位数字锁屏密码：开启确认、验证原密码后修改/关闭，真实锁屏模糊背景、导入系统字体、圆内按下反馈及局部黑白快刷；浅睡/深睡/冷启动验证后恢复页面，取消保持锁定。凭据使用加盐摘要，普通配置恢复不覆盖；低内存、触摸读错和多指取消不会绕过验证。
- 密码键盘输入仅局部更新圆内反馈及密码圆点，不处理背景或字形，优化数字、删除与取消的响应速度。
- 新增每页四本的列表书架：统一尺寸的等比裁切封面、作者、阅读进度、收藏标记及双行长标题，支持翻页、重启和配置恢复。
- 拼贴封面新增柔和投影，白色文字描边减细；锁屏图像使用16级灰阶抖动表现256级输入亮度，保持有界工作内存。
- 密码顶部锁图标改为平滑圆角，减轻背景模糊；密码入场一次显示完整画面，不补刷第二段背景，低内存与冷启动直接显示最终密码页。
- 列表书架外框和分隔线加深至双像素，常用按钮、设置卡片边框同步加强，快刷与水波纹模式保持可读。列表封面统一120×156像素，等比放大后居中裁切。
- 首行缩进新增1像素微调（-20～+20，默认0）、数值点击复位及重启/配置备份恢复，正文标题与对齐块保持原位。

Add a D-layout library collage lock with aspect-preserved real covers, native imported system typography, SD caching, bounded workspaces and safe memory fallback. Pale covers gain a fine rounded gray outline. An optional four-digit PIN verifies wake/cold starts, retains the saved page, confirms setup and requires the old PIN for changes. Cached frosted lock art and native system glyphs use local feedback; faults cannot bypass authentication. Salted credentials are excluded from ordinary backups. Retired pocket/spine settings migrate to acrylic.

- 文件页的存储卡卡片在“卡在但文件系统读不了”时改为显示“需要格式化 / 格式化可能可以解决此问题”，点一次询问、再点一次才格式化，点别处取消。此前这种情况与“卡没插”显示同一句话，把用户指向了错误的排查方向。
  The storage card on the files page now reads "needs formatting / formatting may fix this" when a card is present but its filesystem cannot be read, with one tap to ask and a second to format (a tap elsewhere cancels). It used to show the same line as an absent card, which pointed at the wrong problem.
- 加快主页水波纹：使用与阅读相同的一拍一带调度，将24带83拍改为16带52拍，保留12ms目标节拍、完整37相、原灰阶波形、底栏范围与异常恢复。软件目标由996ms缩至624ms，实际速度与残影需实屏确认；阅读翻页、刷新模式默认值不变。
  Speed up main-screen ripple with reader-style compact launches: 16 bands/52 ticks instead of 24 bands/83 ticks, retaining the 12ms target, all 37 phases, gray waveform, navigation bounds and recovery. The software target drops from 996ms to 624ms; hardware speed and ghosting require device checks. Reader turns and refresh-mode defaults are unchanged.

## 0.3.3-rc89

- 移除微信读书登录、在线书架、同步、下载功能与相关源码、测试及网页预览。文件管理直接显示 WiFi 传输、热点传输、USB 传输三个入口，加深传输卡片边框。
- 联网升级前释放闲置封面缓存，低内存时安全提示；屏幕扫描与固件擦写互斥，进度条只追加新增黑段及百分比，避免频繁整页清理。保留有界重试、SHA256 校验、取消等待与双槽回退。
- USB 完整包、TF 升级包和 OTA 清单统一更新，基础包分区与用户数据地址保持兼容。

## 0.3.3-rc88 · 2026-10-08

- 优化启动、EPUB 元数据与封面加载的栈占用；快刷只缓存黑白封面，普通/水波纹只缓存灰阶封面，切模式释放旧格式，低内存时安全回退。
- 异常开书保护：开书被断电或异常重启打断后，停止自动重试该书，进入可操作页面；已记录书名时提供返回首页及确认删除，不自动删除文件。
- 深睡前保存书籍、阅读位置、全屏和主页面状态，唤醒后恢复；恢复记录只消费一次，避免失败后反复尝试。
- 中键长按可自定义，包含返回首页；三键短按或中键长按至少保留一个阅读工具栏入口，旧配置/恢复配置缺少时补为中键短按。配置备份 v11 兼容旧版本。
- 新开书默认全屏；翻页收起工具栏并回到全屏，保留旧布局阅读锚点，避免重排跳读。中间双击独立切换全屏。
- 下拉快捷面板及四个圆形按钮加深框线，三种主页模式统一灰阶；票根统一系统字体，浅睡解锁恢复此前阅读字体。
- 首页继续阅读书名使用系统字体，阅读书名与章节标题使用阅读字体，复用当前字体按原 40/32/48 像素直接生成字形；不增加内建字库或同时加载两份字体。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，保留双槽回退与已有数据；下架 rc87，保留原发布的 rc85/rc86 可选。

Startup and EPUB/cover stack use are reduced, with one bounded cover format per main refresh mode and safe low-memory fallback. Interrupted book opening stops automatic retries and offers Home or confirmed deletion for a recorded book. Deep sleep checkpoints the reader and consumes resume once. Custom middle-hold actions, persistent Tools-entry repair and compatible v11 backups keep controls accessible. Page turns close Tools and restore full screen without skipping the old anchor. Quick controls use clear gray borders in all modes; tickets use the system font. Home titles reuse the active system face at 40px, reader titles the active reading face at 32/48px, without extra embedded faces or concurrent font loads. USB, TF and OTA share a compatible rollback build. rc87 is withdrawn; original rc85/rc86 remain available.

## 0.3.3-rc87 · 2026-10-08 · 已下架 / Withdrawn

- 在线刷机页新增正式版本选择：默认最新版本，也可刷入原发布的 rc86 或 rc85；显示对应发布时间并提供该版 TF 升级 Bin。
  The web flasher offers the latest release plus original rc86/rc85 builds, showing each publication time and matching TF image.
- 主页刷新模式：系统设置可选普通、快刷、水波纹，默认普通。普通及水波纹保留灰阶；快刷使用黑白文字与封面，亚克力书架采用细腻规则的棋盘格点阵。
- 四个主页面切换保留底栏图标与文字，选中小横条单独局部刷新。修复锁屏返回后底栏消失，并限定内容刷新边界；手动全刷、首次进入、解锁和异常恢复保留完整刷新。
- 底栏图标、圆形返回键与书架管理/导入按钮增加点击反馈；修复底栏放大动效周围的矩形白边。三种主页模式统一保留原灰底，按真实变化缩小推屏范围，释放、滑出、多指或切页时复位。
- 新增顶部下滑快捷菜单：从顶部边缘下滑可连接最近保存的 WiFi / 再次断开、切换蓝牙、全刷与锁屏。向上滑、点击外部或返回键收起；传输忙碌时保留网络，蓝牙沿用现有内存保护。
- 阅读按键可自定义，提供两套推荐：设置1为左上一页 / 中阅读工具栏 / 右下一页；设置2为左首页 / 中切换全屏 / 右阅读工具栏。中键长按固定全刷，中间双击切换全屏不随设置变化；单击左右各半或上1/3、下2/3翻页。配置随重启及保存恢复保留。
- 阅读水波纹替换为快速动画；灰阶相位保持完整，插图与手动/周期全刷仍使用原路径。优化主页面水波纹条带与快刷退出书架的同向擦白；缓存最多九张黑白封面，切页与锁屏不回放旧画面。
- 改善阅读题头与首页书名中补充汉字放大的锯齿，保持原字宽与排版。
- 在线刷机、TF 升级 Bin 和联网 OTA 来自同一次产品配置编译；核对双槽基础包、升级校验值和版本，保留已有配置、阅读数据与 TF 卡内容。联网更新确认页显示简短日志。

Main screens offer ordinary, fast and water refresh modes. Ordinary/water retain grays; fast uses monochrome artwork and fine checkerboard acrylic. Retained navigation has local marker and press feedback, with actual-change bounds protecting its background and labels. A top-edge quick layer controls saved WiFi, Bluetooth, refresh and lock. Reader keys are configurable with two presets, fixed middle-hold refresh and independent center double-tap full screen. Fast reader ripple, bounded cover caching and interpolated supplemental Han glyphs improve the display. USB, TF and OTA share one product build with compatible slots, rollback and release notes.

## 0.3.3-rc86 · 2026-10-08

- 新输入法：九宫格只输入中文拼音；全键盘支持中英文与大小写切换，数字和符号使用独立页面。文件/文件夹改名、资料卡名称、状态栏签名、书架搜索、书名编辑及 WiFi 密码共用这套界面。
- 连续拼音支持 488 个离线常用词组与选词后联想；同音字支持跨批次翻页，九宫格「更多」可选择其他读音。拼音未确认时保留输入，确认后再保存；内建黑体补齐 GB2312 的 6763 个汉字。
- 补齐常亮输入光标：轻点文字或左右箭头调整位置，在光标处插入或删除；长内容跟随光标滚动，完整处理 UTF-8 字符、密码显示/隐藏及大系统字号。
- 输入按键按下加深并轻微下沉，松开或移开恢复，只刷新该键。删除键长按约半秒后连续删除，先删待选拼音，再删光标前的文字，松手、移开、多指或离页立即停止。
- 优化打字刷新：连续输入使用局部快刷；联想词栏累计变化六次后，停手约 700 毫秒只做一次局部灰阶整理，减轻残影。打字和长按删除期间延后整理，静止画面不反复刷新。
- WiFi / 热点传书网页新增状态栏签名设置，可保存或清空，重启后保留。
- 修复部分 EPUB 兼容性，避免局部对齐样式影响整篇正文；采用 PR9 图文混排核心，补齐图片缓存与异常回退，优化图片缩放。网页刷机增加设备筛选和中文连接提示，保留擦除确认。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在设备更新确认页；继续保留双槽回退、已有配置与阅读数据。

The shared keyboard now supports Pinyin-only T9, bilingual QWERTY, case switching and separate number/symbol panels. Continuous Pinyin uses 488 offline phrases, continuations and pageable homophones; the embedded black face covers 6,763 GB2312 Han characters. Steady carets, insertion-point editing, local key feedback and held backspace are available across native input scenes. Typing stays on fast local updates; six candidate-strip changes followed by a 700 ms touch-free pause trigger one bounded gray settle. AP/STA web transfer can save or clear the persistent status signature. EPUB alignment, image resampling and PR9 image flow are improved. USB, TF and OTA share one build, with release notes, rollback and user-data preservation.

## 2026-10-07 · mindreset

- 合并 PR12：封面和插图采用中心对齐的双线性缩放，改善细线与图中文字的显示；修正映射运算及损坏 PNG 的缓冲区释放。
- 合并 PR13：网页刷机默认保留用户数据，擦除需主动勾选；仅列出 Espressif USB 串口，安装对话框改为中文，并避免翻译触发反复处理。保留当前 rc85 固件包与升级版本。

PR12 adopts center-aligned bilinear image resampling with safe coordinate arithmetic and PNG cleanup. PR13 defaults to preserving user data, filters Espressif USB ports and translates installation dialogs without repeated mutation handling. Published rc85 binaries remain unchanged.

## 0.3.3-rc85 · 2026-10-07

- 优化阅读文字翻页：纯文字页之间使用 CrossMux / FreeInk 文字波形与灰阶差分，减少重复擦白及新字等待；普通阅读与全屏均生效。涉及插图或图文混排时保留原灰阶路径，水波纹、周期及手动全刷继续有效。在“阅读设置 → 翻页效果”选择“默认效果”体验。
- 图文兼容修复：补齐 SVG 包装图片及较长 JPEG 头部的尺寸识别，缺失插图记录时继续绘制正文；保留章节分页、标点规则、阅读线和未知图片回退。
- 蓝牙扫描隐藏没有广播名称的设备；后续扫描响应补齐名称后可显示，点击对应正确设备。
- 翻页器适配：扩展原生按键支持，保留手动映射优先及键盘修饰键保护。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在 rc84 及以后固件的更新确认页；保留兼容双槽布局与用户数据。

Text-only turns use the CrossMux / FreeInk waveform and grayscale differentials in normal and full-screen reading. Image/mixed-page transitions, water effects and periodic/manual cleanup retain their existing paths. This release also includes SVG/JPEG image-flow fixes, named-device Bluetooth filtering with later name updates, and Bluetooth pager compatibility with manual-mapping precedence. USB, TF and OTA share one build, preserving the dual-slot layout and user data. Devices running rc84 or later display the release notes before online installation.

## 0.3.3-rc84 · 2026-10-07

- 图文混排：小图与正文在同一页排版，按比例缩放，多图接排。
- 滑动翻页：默认支持左右滑动，兼容上下翻页模式。
- 自动休眠：可选 1 分钟、5 分钟、10 分钟或关闭。
- 蓝牙翻页器优化：学习映射显示识别到的实际按键。
- 图标 UI 优化：升级、BOOT 和阅读设置使用独立图标。
- 文件管理排版优化：统一操作和分页按钮，增强文字与线框可读性。

联网更新确认页新增简短更新日志，提供“稍后 / 开始更新”。从 rc84 开始，后续更新可在设备上查看日志；旧固件升级本次版本前可在在线刷机页查看。完整刷机、TF 升级和联网 OTA 使用同一编译，保留兼容的双槽基础包、现有设置与阅读数据。

The release adds inline images, swipe page turns, configurable idle lock, clearer learned Bluetooth keys, distinct UI icons and improved file-management layouts. From rc84 onward, the online update offer displays short release notes before installation. USB, TF-card and online updates share one build and retain the compatible dual-slot layout and user data.

## 2026-10-07 · Codex

- rc83 本地测试：删除“全局刷新模式”测试项及实验刷新引擎，旧 NVS / TF 备份不再开启该模式，保留标准刷新及阅读全刷设置。晃动翻页改用新加速度采样区分左右：左晃上一页、右晃下一页；一次横向动作触发，过滤倾斜、其他方向及回弹，触摸和工具栏期间禁用，离页恢复传感器睡眠。方向与灵敏度已通过模拟轨迹回归，仍需实机试用。

- rc83 本地测试：采纳 PR #7 的比例缩放、页内多图及后续正文接排；保留有界尺寸探测，补齐未知尺寸回退与每张图失败提示，按布局代次更新缓存，章节加载失败保留原章及进度。混排页保持灰阶并沿用普通翻页刷新，纯图页保留完整灰阶刷新。采纳 PR #8 的蓝牙学习按键值显示；已覆盖多图、空间边界、重排、隐藏图片、内存失败与按键结果回归，真实图片观感及翻页器待实机验证。

- rc83 本地测试：合并 PR #5/#6，USB 停止失败时保留存储卡所有权并允许重试，恢复串口 PHY；补齐 CI 的 USB 配置。管理书架移除旧菜单入口，修复“下一页”误进示例菜单，统一筛选、操作和分页按钮，保留跨页勾选。EPUB 同文件内的正文章节从新页开始，标题与开头正文同页；过滤目录链接、简介及版权信息，普通小节继续接排。本地版本号保持 rc83，固件尚未发布到在线刷机页。
## 2026-10-07 · mindreset

- 蓝牙翻页器的按键映射改为显示实际学到的键：以前只写“自定义按键”，现在直接给出该键的原始字节（如 0x88），字节下标或报告号非零时一并补上，两个绑定不会再看起来一样。
## 2026-10-06 · mindreset

- 正文图片改为图文混排：插图不再占据整页，而是作为页面流里的一个块放在正文之间，图下方接着排后续正文；只有当前页放不下时才翻页。图片按栏宽等比缩放、不超过一页高、不放大，水平居中。
- 排版仍然不依赖图片解码器：尺寸由调用方通过回调提供并在本章缓存；优先使用已缓存的有界头部尺寸，无缓存及回调时保留整页显示。

## 2026-10-07 · mindreset

- 蓝牙扫描列表不再列出没有广播名的设备：那些行只能显示地址，认不出是什么设备，地址还会把信号强度挤掉。
- 翻页器适配：新增按键协议的原生支持：0x01 上一页、0x02 下一页，无需学习。

## 2026-10-06 · Codex

- 固件 0.3.3-rc83：
  - 书架封面抽出与取消改用局部灰阶差分，只驱动变化的像素，减少点击时的黑闪。
  - 设置主页、蓝牙及扫描页滑动更早响应，一次至少移动三行；各设置列表仅刷新内容区，取消滑动时按次数强制黑白清屏，到边界不重复刷新。
  - 状态栏时间、电量百分比增至 28 像素，个性签名增至 26 像素；按实际字形与图标中心对齐，长签名避开两侧信息。
  - 修复蓝牙翻页器及扫描页面叠出设置资料卡。
  - 修复文件管理首次打开图片时重启；支持直接打开根目录或其他目录中的图片，并浏览同目录图片，保留灰阶层次。
  - WiFi 与热点传书网页新增“添加文件到当前目录”，支持根目录、多级目录、任意格式与空文件；保留原名称，同名覆盖需确认，失败保留原文件。
  - 系统字号上限提高到 200%，随重启及配置备份保留；合并屏幕 PMIC 的 VCOM 门控更新，重新开启 VCOM 后等待 2 毫秒再扫描。
  - 完整刷机包、TF 升级 Bin 和联网 OTA 使用同一固件与版本；基础包保留双槽升级、首次启动确认及异常回退能力。已通过产品配置编译和宿主回归，实际刷新观感与硬件门控效果仍需实机验证。

- 固件 0.3.3-rc82：联网检查发现新版本后直接显示“稍后 / 开始更新”，修复任务收尾后按钮仍显示“取消联网更新”的问题；下载进度至少间隔 5 秒且变化达到 5% 才更新，仅刷新百分比与进度条区域，不累计整页清残影。更新期间关闭 WiFi 省电并在结束后恢复；连接等待延长，短暂断网允许最多三次重连续传，核对续传范围、文件长度与完整 SHA256，失败保留原版本。阅读页返回键、书名与收藏图标统一视觉中心线，内建字体按实际字形边界居中。基础包、TF 升级 Bin 和联网清单同源发布，保留双槽回退及已有数据；已发布的版本下载地址继续保留，避免旧清单缓存导致更新文件失效。已通过产品配置编译、宿主回归和原生渲染核对，真实无线环境与墨水屏观感待验收。
- 在线刷机页在当前版本下显示更新时间，统一使用北京时间；后续打包自动更新记录。
- 固件 0.3.3-rc81（联网测试版）：修复 WiFi/热点选择页按下反馈误触发整页刷新、取消点击而无法进入传输页的问题；已保存 WiFi 时直接启动传输，未配网时进入网络选择。状态栏 WiFi、蓝牙图标使用与电池相同的黑色，稍微缩小并靠近电量百分比。所有圆形返回键外圈统一加深。
- 书架按下书籍向上抽出，取消触摸恢复；页码两侧新增对齐的上一页/下一页箭头，首末页相应箭头置灰。蓝牙翻页器上/左键上一页，下/右键下一页；支持已学习的原始按键，同一报告只执行一次。书架翻页复用局部刷新，各书架样式均生效。基础包与升级包仍使用兼容双槽分区，提供官网 HTTPS 联网更新与 TF 升级 Bin；实机联网升级及观感待用户验收。
- WiFi 连接页采用加深双层卡片边框、清晰分隔线和较大字号；已连接时提供断开按钮，断开保留凭据，传输中拒绝断开。阅读设置新增翻页模式：上下模式以正文高度三等分，上区上一页、中区切换全屏、下区下一页；普通及全屏阅读均生效，支持上下滑动翻页。左右模式保持原有规则，偏好随重启及配置备份恢复。
- 固件 0.3.3-rc80（本地验证版）：蓝牙翻页器移到 WiFi 下一行，状态栏加入 Lucide 蓝牙图标并支持与 WiFi 同时显示；按键学习主动响应，顶部显示「识别成功」，清理旧输入并修正带报告 ID 的键盘释放识别。网络分配前串行暂停蓝牙，防止同时初始化；保留内存门限，检查蓝牙任务创建结果，失败回收资源，网络退出后恢复蓝牙。修正 BOOT 行点击范围。
- 阅读「刷新设置」改为「阅读设置」，集中放置全屏沉浸、电源键翻页、关闭书内图片及长按圆圈键全刷；删除系统设置「阅读操作」。长按圆圈键默认回首页，开关开启后整屏全刷，并加入配置备份。普通阅读题头增加圆形返回键，返回进入书籍前的页面。首行缩进按实际字体字宽计算，并校正段首引号、括号的可见边缘。
- 固件 0.3.3-rc79（本地验证版）：合并 PR #3 的渐进式 JPEG 封面及比例裁剪、PR #4 的 BLE 蓝牙翻页器和大缓冲 PSRAM 分配。蓝牙设置、扫描及按键学习页按当前设计统一，修正滑动、返回与停止扫描；WiFi/微读/OTA 工作时暂停蓝牙以减少内部内存竞争。
- 设置新增“升级和恢复”分组：系统升级、保存与恢复、BOOT 刷机独立成行；文件管理移除 BOOT 按钮。系统升级支持已有 TF 卡升级和 HTTPS 联网 OTA：验证官方源、设备型号、版本、应用槽、完整大小、SHA256 与镜像，只写空闲槽，完整验证后才切换启动。官网发布工具同时生成全量基础包、TF Bin 与 OTA 清单，发布前验证基础包能力与分区兼容性；本地页面增加 Bin 下载，尚未发布新版在线固件。
- 微读同域完整响应复用 HTTPS 会话，清除旧请求鉴权头与回调；延长建连与无数据等待，临时断网允许重连和有界重试，避免逐章重复握手。真实账号下载和 BLE 设备配对仍需实机验证。
- 电量显示统一采用 PMU 软件油量表，优先读取带 CRC 与有效位的轻量快照，无有效读数显示未知，资料卡和状态栏使用相同取整。此修订不改变 PMU 电压或保护参数，实际电量误差仍需充放电记录验证。新增蓝牙开关加入配置备份，兼容旧备份。

- 固件 0.3.3-rc78（本地验证版）：微读返回键明确返回文件管理，书籍详情进入独立二级页，不再显示书架同步与退出登录；下载文件使用微信书架书名，不再追加书号。封面始终下载，与正文插图开关分离；封面获取失败会提示重试。微信书架增加跨页多选、全选本页及逐本批量下载，一次最多选择 1024 本，取消或离页会停止队列，已完成文件保留。保留 rc77 的目录字体、可读性与图片空白占位修复。已通过本机编译及主机回归，真实账号下载与实机操作仍待验证，未上传在线刷机页。
- 固件 0.3.3-rc77（本地验证版）：阅读目录全部使用当前系统字体，返回正文恢复独立的阅读字体；加深目录卡片及按钮边框，增大上页、下页、快翻十页和百分比跳转文字。排版跳过插图前后的空白段落、全角及零宽占位，减少多余空行和空白页，关闭图片时正文正常接续，保留原文定位。保留 rc76 的传书与微读功能；未上传在线刷机页。
- 固件 0.3.3-rc75（本地验证版）：补齐已确认的首页排版，题头与书架使用相同位置、字号和绘制方式，横线统一在同一高度；继续阅读区整体下移 40 像素，点击范围与封面局部刷新同步调整。近七天图表保持原布局。
- 固件 0.3.3-rc74（本地验证版）：采用 PR #2，从文件管理打开并保存进度的 TXT/EPUB 自动加入书架；管理页新增「移出书架」，保留文件及阅读进度，再次阅读自动上架。批量按钮改为两行三列；移出状态随 TF 卡配置备份恢复，兼容旧备份。保留 rc73 的 Lucide 图标。已通过产品配置编译与主机回归测试；实机显示和操作待验收。

## 2026-10-05 · DeepSeek Harness

- 统一采用 Lucide 图标：底栏、WiFi、返回键、阅读工具栏、设置、文件管理与电源弹窗使用同一套线条样式；已添加书签显示轮廓加勾。同步图标生成工具与许可声明。
- 书架收录读过的书：从文件管理打开并保存进度的 TXT/EPUB 可以位于 TF 卡任意位置，返回书架时自动补入；已删除或文件大小已变的记录不补入。管理页新增「移出书架」，只隐藏条目，文件和进度保留，再次打开重新上架；批量按钮改为两行三列。

## 2026-10-05 · Codex

- 固件 0.3.3-rc73（本地验证版）：完整采用 PR #1 的 Lucide 图标方案，尺寸、线宽和灰度与确认预览一致。已通过产品配置编译与主机测试；实机显示待验收。
- 固件 0.3.3-rc72：修复部分 EPUB 兼容性。
- 固件 0.3.3-rc71（本地验证版）：关闭书内图片时，排版跳过插图块并让前后正文接续，不再保留插图页或显示「此插图暂无法显示」占位提示；纯插图章节仍沿用跳过逻辑。尚待实机验证，未上传在线刷机页。
- 固件 0.3.3-rc70（本地验证版）：全屏阅读底部细进度条上移 4 像素，并同步调整独立刷新区域，避免翻页时正文刷新影响进度线。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc69：修复票根锁屏遇到持续按键中断时可能停在静态画面的问题；浅睡期间定时补查电源键，睡眠连续失败时返回可操作页面。票根仅使用已有封面与元数据缓存，避免锁屏时解析大 EPUB；书架多级目录扫描改为固定栈占用，文件管理递归目录操作的路径与复制缓冲移到外部内存，降低重启风险。配置恢复先校验再替换旧阅读记录，并核对写回的 WiFi 凭据。阅读「刷新设置」增大文字并加深选项边框，字体列表长名称按卡片宽度截断。已通过产品配置构建和主机回归测试；仍待实机验证长时间锁屏、书架进入与配置恢复。详细说明见 [rc69 更新说明](RELEASE_NOTES_0.3.3-rc69.md)。
- 固件 0.3.3-rc68：将「封面与书脊」样式标为测试版、非正式版本；在线刷机固件汇总 rc59–rc68 的双槽升级、书架收藏、排版、壁纸、备份及 WiFi 内存优化。详细功能、验证范围与注意事项见 [rc68 更新说明](RELEASE_NOTES_0.3.3-rc68.md)。
- 固件 0.3.3-rc67（本地验证版）：书架封面和书脊收藏标记统一为黑色，深色背景增加贴合图标轮廓的白边。优化 WiFi 在线时打开图书的内存余量：开书前释放可重建的封面与预渲染缓存、按需启动预渲染，并清理损坏 EPUB 索引的临时分配；书架打开失败会显示原因。尚待实机联网上书验收，未上传在线刷机页。
- 固件 0.3.3-rc66（本地验证版）：TF 卡壁纸按原比例居中裁切铺满锁屏，横图和方图也能在解码器限制内显示；配置备份增加已保存的 WiFi 名称与密码，兼容旧备份，并提示妥善保管 TF 卡。设置页卡片边框与分隔线加深、加粗；所有书架样式都将收藏书排在前面，再按当前排序规则排列。含 rc65 的收藏标记白底修复。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc65（本地验证版）：书架封面和书脊左上角的收藏标记去除纯白矩形底块，按封面局部灰度使用深色或浅色实心标记；阅读页标题旁的收藏标记保持原有深色样式。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc64（本地验证版）：保存书签后图标完整填黑；配置备份新增每本书的阅读进度、阅读时长、书签、收藏和自定义书名，继续保存设备资料与状态栏签名，并兼容旧备份。阅读首行缩进不再叠加书籍原文自带的段首空格；修复书架管理页返回键、筛选点击区域与未选书时的操作提示。尚待实机视觉及 TF 卡恢复验收，未上传在线刷机页。
- 固件 0.3.3-rc63（本地验证版）：新设备的阅读行距默认改为滑块中档 130%，保留已有手动设置；普通阅读正文上缘适当下移、下缘延伸到页脚进度区前，全屏正文上缘增加留白、底部细线移近屏幕下沿，正文与刷新范围同步调整，减少页面下方留空。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc62（本地验证版）：资料卡头像与圆角遮罩使用同一边界，并自动裁去明显的白色留边；阅读工具栏的书签在保存后显示可辨认的选中态，收藏图标的 V 形尾部完整填充，图标线宽与深浅统一。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc61（本地验证版）：调整热点与同 WiFi 传书服务的启动顺序和内存占用，修复 `ESP_ERR_HTTPD_TASK` 导致网页无法启动；减少上传时的碎片写入、关闭传书期间的 WiFi 省电，并延长弱信号重连窗口。书架支持扫描 books 内的多级目录、收藏书籍和书脊样式；统一阅读字号与最近阅读排序，增加关闭书内图片选项。改善插画刷新、壁纸填充与灰阶、正文两侧对齐和全屏排版；修复设备名称与状态栏签名保存失败时无提示的问题，并对齐设置资料卡中的电量与编辑文字。尚待实机验收，未上传在线刷机页。

## 2026-10-04 · Codex

- 固件 0.3.3-rc60（本地 OTA 验证版）：作为 TF 卡升级包验证双槽切换、首次启动确认和异常自动回退；功能与 rc59 相同，版本号递增用于确认设备内升级成功。
- 固件 0.3.3-rc59（本地 OTA 基础版）：16 MB Flash 改为双应用分区，保留原 NVS 与内部存储地址；文件管理新增 TF 卡本地升级，可校验并安装根目录的 `Pico-update.bin`。升级只写空闲槽，完整性校验通过后才切换；新槽首次启动失败会自动回退。首次启用仍需通过电脑完整刷入本基础版。
- 固件 0.3.3-rc58：修复部分 EPUB 打开时设备自动重启的兼容问题；目录解析的大块临时数据改用外部内存。实机已验证可正常打开并翻页。同步发布 rc51–rc57 的目录、阅读、界面、锁屏及配置备份改进。
- 固件 0.3.3-rc57（仅本地构建）：设置主页恢复原行高并支持上下滑动，固定状态栏、标题和底栏；资料卡头像居中裁切到圆角遮罩，状态栏签名按屏幕中心对齐，输入法显示中文、英文小写和英文大写切换状态。首页模块边框、分隔线和书架横线加深；无封面书籍的第三种模板换成上深下浅的新样式并重建旧缓存。新增的设备资料、显示、睡眠和阅读选项随 TF 卡配置备份与恢复。
- 固件 0.3.3-rc56（仅本地构建）：壁纸与票根锁屏均在浅睡十分钟后转深睡；电源菜单可选彻底关机或先浅后深。修复全屏阅读底部进度线；设置新增首页强刷、设备名称与头像、状态栏签名；BOOT 入口移到文件管理。
- 固件 0.3.3-rc55（仅本地构建）：首页、书架、设置与文件管理切换改用完整 16 级灰阶表，每 3 次切换做一次同屏灰阶整理，保留每 12 次完整清残影；文件管理的卡片边框、列表分隔线与辅助文字加深、加粗或放大。阅读线样式与上下位置移入独立二级菜单；全屏翻页将最底部进度线单独差分，减少反复闪动。补齐 rc54 阅读线位置调节的固件实现。
- 固件 0.3.3-rc54（仅本地构建）：阅读线加深至双像素，可在字体设置中上下微调位置并随配置备份；全屏阅读扩展正文区域，屏幕底部增加细的全书进度线；字体设置同级卡片统一高度。刷新设置新增每 30 页全刷与「不强制全刷」，保留手动整屏全刷；旧配置仍可恢复。
- 固件 0.3.3-rc53（仅本地构建）：兼容部分 EPUB 对普通 ZIP 条目设置的扩展标志，修复文件被误判为不支持、无法打开的问题；仍核对压缩方式、文件结构和 CRC，并完成正文读取验证。
- 固件 0.3.3-rc52（仅本地构建）：TXT 和缺少有效封面的 EPUB 自动生成三种确定性灰阶封面，书架、首页与阅读票根共用同一封面缓存；书内有效封面继续优先显示。延续 rc51 的目录快翻、百分比跳转和 EPUB 目录识别修复。
- 固件 0.3.3-rc51（仅本地构建）：目录每次可快翻 10 页，新增百分比跳转；目录标题使用更清晰的固定字号。EPUB 的 NCX 目录缺项时补入 NAV 中可用的章节与同文件锚点，重复项保留 NCX 名称，并重建旧目录缓存。修复阅读排版在默认字间距下调节边距时变化不明显的问题。
- 固件 0.3.3-rc50：阅读正文插图直接使用解码后的灰阶，不再叠加深色增益与系统对比度；翻到插图页时用完整灰阶波形从白底呈现，保留更多暗部层次。系统对比度的首次默认值改为最低档 100%，已有手动保存的设置继续保留。
- 固件 0.3.3-rc49（并入 rc50）：图片页改用独立的完整灰阶波形，先清除上一画面，再从白底呈现原始灰阶，改善插画与照片的层次。阅读统计中的书签支持多选删除；文件管理长按文件或文件夹可重命名、复制、移动或删除，轻点保持打开；书架“管理”与“导入”入口改为当前设计风格。
- 固件 0.3.3-rc48（并入 rc50）：阅读正文按整字列居中，改善右侧留白明显更宽的问题；排版设置新增首行缩进 0／1／2／3 字，默认 2 字，并纳入 TF 卡配置备份与恢复。中文右标点与前字同排，避免标点单独出现在下一行开头或悬挂到右侧留白。

## 2026-10-03 · Codex

- 固件 0.3.3-rc47：修复全屏阅读时水波纹翻页误回退为默认效果；退出全屏后整页重绘章节与进度信息。系统设置新增 TF 卡配置备份与恢复，涵盖字体、排版、显示和阅读操作；恢复前校验文件并对缺失的外部资源安全回退。EPUB 清单与章节按实际数量分配内存，扩展到 8,192 项，并优化大书目录索引与打开速度；修复 1,546 章 EPUB 被旧的 1,024 章上限拒绝的问题。
- 固件 0.3.3-rc46：阅读线改为按字号与行距定位在相邻两行文字之间，并加深虚线和点线，避免线条贴近下方文字；网页刷机页自动读取刷机包版本，修复页面仍显示 rc42 的问题。
- 固件 0.3.3-rc45：修复首次无 TF 卡开机后插卡不会重新探测的问题；挂载完成时首页、书架和文件管理重新读取目录。拔出已挂载的卡仍须安全关闭旧文件并显式重挂，避免误写。
- 固件 0.3.3-rc44（预览构建，未刷机）：字体设置主面板只保留字号、阅读字体、晃动翻页和阅读线；边距、行距、段距和字间距集中到「排版设置」二级面板，保留原有数值与拖动行为。
- 固件 0.3.3-rc43（预览构建，未刷机）：阅读正文轻点中央切换全屏并重新排版；系统设置新增“阅读操作”，可选电源键短按下一页、长按锁屏及全屏隐藏状态栏。字体设置按参考图重排，正文默认首行缩进两字，字间距默认居中并可向两侧微调；新增段距和点击轮换的无／虚线／点线阅读线。
- 固件 0.3.3-rc42：设置页点击 Pico 设备卡片可进入固件升级页，选择“进入 BOOT 模式”后保存 TF 卡写入并重启到 ROM 下载模式；USB 读卡结束时重新将内部 USB PHY 交给串口/JTAG，供电脑再次识别和刷机。网页刷机指南补充 BOOT 模式入口与使用时机。
- 固件 0.3.3-rc41：阅读页底部只显示章节名，移除自动补上的章号；首页续读作者右侧不再显示章号。续读书名优先按当前字体实际宽度缩小字号完整显示，保持原布局位置。
- 固件 0.3.3-rc40：内建系统字体改为稍厚的思源黑体 Medium（500 字重），保持与阅读字体分离；锁屏壁纸选图增加整图预览、上下滑动换图和设定确认；书架样式预览改用无书名的通用封面；TF 卡挂载后按需建立 books、fonts、pictures；目录跳转后立即更新底部章节与百分比；EPUB 大图改用更省内存的渐进缩放解码，改善扉页插图显示。
- 固件 0.3.3-rc39：EPUB 首次打开目录时检查正文中的“第一章 / 第1章”等独立标题；识别到完整章节序列时优先据此生成目录并精确跳转，识别不完整时只校正对应标题并保留书内目录。过滤普通段落提及的章节字样，结果写入索引，后续直接读取。
- 固件 0.3.3-rc38：参照 CrossMux 将 EPUB 正文 spine 与 NCX/NAV 目录分开，保留同一 XHTML 中多个章节的标题与锚点；点击目录跳转到对应正文位置，目录当前项按条目高亮。旧 EPUB 目录索引自动重建；书前作者、出版社资料仍从目录中排除。
- 固件 0.3.3-rc37：首页继续阅读在最近打开的书籍变化后立即更新，复用封面缓存而不重复探测 TF 卡；从首页打开书籍直接显示阅读页，长按中键退出时返回首页。EPUB 目录优先采用书内导航，缺少导航时过滤书前作者、出版社、版权等资料页，阅读进度仍沿用原章节位置。
- 固件 0.3.3-rc36：阅读页刷新设置新增可持久化的「默认效果 / 水波纹效果」，升级后仍默认沿用原刷新规则。水波纹采用 16 条带、37 相错相 GL16，单次差分计算，沿逻辑翻页方向执行；周期全刷仍覆盖整屏。动画异常时重建前后帧基准，缺少相位或低内存时回退普通 GL16。原先移除方向动画的产品决定改为「默认关闭、用户可选」，阅读字体设置面板保持原样。
- 固件 0.3.3-rc35：放大状态栏时间、WiFi、电量百分比与电池图标，并统一垂直对齐；文件管理根目录的文件夹名称与图标居中对齐。
- 固件 0.3.3-rc34：实机验证发现网页上传书籍后写入阅读进度时会触发重启；传输服务任务栈改回内部内存，并在同 WiFi 模式下继续验证上传与目录操作。
- 固件 0.3.3-rc33：热点与同 WiFi 传书共用新版 TF 卡文件管理网页；新增手机锁屏壁纸裁剪，支持拖动位置与缩放，按 Pico 684 × 1216 像素导出 JPG、上传到 pictures 后自动设为壁纸锁屏。目录重命名后同步当前传书目录，防止本次会话的后续上传写向旧路径。

## 2026-10-02 · Codex

- 固件 0.3.3-rc32：热点传书网页改为 TF 卡目录浏览与管理，可逐层查看根目录、分页、新建文件夹、重命名、复制和删除文件或文件夹；书籍、字体、图片分别上传到 books、fonts、pictures。移除网页的图片对比等多余入口，热点限制单设备接入，传输时暂停设备页面交互与定时刷新。文件改名或删除时同步维护阅读进度、字体、壁纸和自选目录设置。
- 固件 0.3.3-rc31：USB 读卡页重排连接状态、存放目录与安全弹出步骤；修复连续写入时缓冲区覆盖和扇区偏移丢失，写卡失败会向电脑返回错误，USB 模式使用更保守的 TF 时钟。
- 固件 0.3.3-rc30：阅读设置刷新图标换成提供的图样，与其他工具图标统一中心和灰度；阅读页放大书名、章节及进度文字；状态栏文字固定字号并与 WiFi、电池对齐；首页统一细节左边距、七日标题基线并加深图表分隔线。
- 固件 0.3.3-rc29：首页按布局编辑器保存的画面重排，移除继续阅读白色卡片与深色按钮底框；封面、书名、作者、章节、进度和七日阅读标题对齐新坐标，柱形图继续显示真实阅读记录。
- 固件 0.3.3-rc28：首页改为继续阅读卡片与近七天阅读时长圆角柱形图，读取已有的每日阅读记录；首页只加载当前阅读封面，避免多本封面逐一刷新。状态栏的时间、WiFi、电量百分比和电池统一垂直中心线。
- 固件 0.3.3-rc27：WiFi/热点传书启动前释放字形缓存；成功的 WiFi 配置请求正常返回，重复文件等请求错误不再把无线服务显示为故障。上传中禁止切页打断文件。USB 读卡禁用主机弹出时的自动二次挂载，先断开主机并排空写入再退出；TF 卡卸载失败不再交给电脑接管。USB 传输缓冲区由 512 字节增至 8192 字节，入口先显示准备页。
- 固件 0.3.3-rc26：阅读目录改由独立页面模块绘制和处理点击，不再复用书架列表与旧局部翻页逻辑。目录使用新版浅灰底与白色圆角章节卡片；章节标题合并换行和异常空白、限制为单行并在超宽时截断，每页 7 节。增加显式翻页入口和章节选中态，目录长按中键不会进入旧示例菜单。
- 固件 0.3.3-rc25：阅读设置的刷新图标按预览改为清晰的粗线回环；阅读页底部进度条与章节、页码信息铺满内容宽度。系统默认使用内建思源黑体，移除“默认无衬线”名称；阅读和系统字体选择后返回设置页仍显示中文字体名称。首页题头改为“首页”，坐标与字号对齐书架页。
- 固件 0.3.3-rc24：阅读设置底栏第四项新增刷新设置，可手动整屏全刷，或选择每阅读 5、10、15 页整屏清残影一次，默认 15 页。文件管理首页直接提供 WiFi 传书、热点传书和 USB 读卡入口；书架「导入」进入文件管理，传输结束后返回文件管理并省去额外白屏。
- 固件 0.3.3-rc23：阅读翻页和周期清残影仅更新正文与底部进度区，顶部状态栏和书名保持稳定；进度条下方的章节信息从 15 像素增至 20 像素，并独立保留右侧页码与全书进度。
- 固件 0.3.3-rc22：联网后对时直接复用已连接的 WiFi，并在写入设备时钟后核对结果；配网列表和遗忘确认页的底栏恢复切换功能。首次刷入本版时将亚克力书架设为显示样式，此后保留手动选择。移除阅读翻页动画及其设置，字体设置恢复紧凑面板，普通翻页继续使用单次灰阶刷新。阅读目录改为独立单行卡片，标题测量与绘制使用相同字体以避免重叠；EPUB 3 目录兼容更多标记格式，目录缺失的正文标题在关闭书籍时写回索引。
- 固件 0.3.3-rc21：设置页和联网状态栏改用 UX v17 预览图同源的 WiFi 灰阶像素图形，保留圆润的双段弧线与水滴形底部；状态栏的图标位置和电池比例同步对齐参考稿。
- 固件 0.3.3-rc20：优化大 EPUB 的 ZIP 目录与流式解压，单个无关大资源不再阻止整本书打开；目录缺项时按需读取章节正文标题。阅读排版支持书内外部 CSS 的有界子集，修正中文左标点折行后的对齐宽度；插图识别补充惰性加载、SVG 图片标签与单图 SVG 包装，并解码图片路径中的实体。
- 固件 0.3.3-rc19：移除微信读书入口和在线阅读模块；底栏第四项改为“设置”，点击后直接进入设置页。同时移除尚未实现的蓝牙设置与 Calibre 传输入口。首次启动新版固件时清除旧版保存的微信读书登录凭据，保留本地书籍。

## 2026-10-01 · Codex

- 固件 0.3.3-rc18：撤销整页 8 相短 DU；系统页面切换改为 30 相 8 灰阶差分 GL16，每 12 次同步做一次完整 GC16 清残影，降低残影且不做延迟二次刷新。

- 固件 0.3.3-rc17：文件首页改为真实 TF 根目录卡片列表并直接提供文件操作；系统页面切换改为 8 相差分 DU，累计 20 次同步做一次 GC16 清残影，取消每次导航的明显灰阶整页刷新。
- 固件 0.3.3-rc16：把参考稿 S03 文件操作面板、S05 传输方式选择与 S06 浏览器二维码传输页写入固件；提高阅读字体选择、统计、书签和最近阅读面板的文字、边框与选中状态对比度，并统一状态栏 WiFi 图标。
- 固件 0.3.3-rc15：连续 DU 翻页由 8 段 6 相位缩短为 6 段 5 相位，减少约四分之一动画扫描；定稿阶段对变化像素使用加强差分 GL16，压实新文字并继续擦白旧文字，改善翻页后的新旧页叠影。
- 固件 0.3.3-rc14：区分默认翻页与翻页动画。默认翻页保持单次 GL16；动画档复用示例工程连续 DU 状态机，以 8 段方向性边界同步擦除旧页、写入新页，下一页从右向左、上一页从左向右，最后只补抗锯齿与插图灰阶，避免再次整页刷新。
- 固件 0.3.3-rc13：阅读翻页改为单次 GL16 灰阶差分刷新。下一页一次提交，页眉、正文、插画和页脚在同一遍刷新中完成，不再横向分段擦入；普通缓存未命中也使用 GL16，仍每 14 页用 GC16 清理残影。
- 固件 0.3.3-rc12：降低首页、书架、文件管理和应用之间切换时的刷新存在感。普通切换保持差分 GL16；局部按钮、菜单、封面和弹窗刷新不再累计整屏清残影次数，系统页清理周期延长到 40 次整页切换，减少频繁黑白闪动。阅读页仍独立保持每 14 页清残影。
- 固件 0.3.3-rc11：参考 KOReader 软件擦入动画重做阅读翻页。下一页从右向左、上一页从左向右；中间帧使用灰阶窄条刷新，末帧以 GL16 定稿整页，清除原快速 DU 动画在文字和抗锯齿边缘留下的大量残影。
- 固件 0.3.3-rc10：阅读页底部章节与进度信息改用独立固定字号，开合阅读设置后不再随正文或系统字号变化；字号略微缩小，并显示“第几章 + 章节名”。同时加入阅读翻页动画选项、图像灰阶抖动、票根锁屏封面背景、WiFi 连接驻留、目录排版和 EPUB 插画兼容修复，并减少常规系统页面的整屏刷新。
- 固件 0.3.3-rc9：修复阅读字体滑块拖动时白块遮住滑轨；扩大滑块局部刷新范围并改用灰阶刷新。首页封面、书架分页和目录分页改为内容区局部更新，减少整屏刷新。目录页移除旧底部入口并重新排版。修复微信读书 HTTPS 登录链接被二维码校验错误拒绝。书籍名称统一取文件名，文件管理新增打开、复制、重命名和删除操作，连接 WiFi 时状态栏显示无线标识。
- 固件 0.3.3-rc8：修复 WiFi 扫描出现 `ESP_ERR_NO_MEM`。扫描前释放可重建的字形缓存，扫描结果移至外部内存并限制为界面实际可显示的 16 个网络；调低单用户传书不需要的无线缓冲预留，并增加失败阶段和剩余内存诊断。
- 固件 0.3.3-rc7：重写设备端 WiFi 扫描生命周期。扫描建立标准 STA 接口，兼容目标机的 remote/hosted WiFi 后端；一次性读取并去重结果，短暂状态错误自动重试，覆盖中国 2.4 GHz 信道 1–13。设置入口直接进入独立配网页，开放网络可直接连接，失败状态显示原因并提供重试。
- 固件 0.3.3-rc5：重构设备端 WiFi 配网页面并修复无线模块重复初始化后失效；设置入口直接进入配网，连接状态与已保存网络可正常更新。
- 首页固定使用六本书的基础网格，不再跟随个性书架；封面集中解码后一次刷新，避免每出现一本书就整屏闪烁。书架样式精简为深色书轨、亚克力挡板和半透明书袋三种。
- 系统设置新增全局对比度；字号、边距、行距支持端点逐级微调，修复最小行距反而增大的问题。
- 长按电源键显示双圆形按钮的重启／关机菜单；关机执行主机彻底断电，短按继续进入所选票根或壁纸锁屏。
- 首页恢复为六本书与继续阅读布局，书名保持单行且最多八个字，并同步展示所选书架效果；书架页取消书名，数量与页码移到底部中央。TF 目录补充文件夹、文件图形和容量信息，WiFi 传书页移除重复的 USB 入口，设置中的 WiFi 直接进入配网。应用图标加粗到适合墨水屏的清晰线宽，字体、书架与锁屏选项统一使用浅灰选中态。
- 底栏应用四宫格下移以统一视觉中心；应用页保留圆角外框，内部微信读书与设置改用和底栏一致的极简细线图标。
- 书架标题右侧新增「管理」并与「导入」并排，直接进入批量管理；应用页移除重复的图片、传输和图书管理入口，仅保留微信读书与设置。
- 系统二级页面共用的圆形返回键缩小，保留原触控热区，兼顾视觉比例和点击容错。
- 修复 TF 卡全目录浏览的分类计数越界；文件夹使用独立探测并优先排序，进入根目录后可以继续逐层浏览。
- 阅读设置补齐实际交互：字号、边距和行距可拖动并在松手后统一重排；阅读字体超过九个时可上下翻页；阅读统计的“阅读明细”和“最近30天”进入真实数据页面。
- 阅读页顶部恢复显示当前书名并移除圆形返回键及其隐藏热区；修正新版阅读工具栏仍误用旧按钮编号的问题，目录、书签、阅读统计和字体设置分别执行对应功能。
- 字体设置面板补齐可点击的字号、页边距、行距、阅读字体和晃动翻页控制，弹层使用各自命中区域，不再与底部工具栏重叠。
- 系统字体与阅读正文字体完全分离；首次进入产品界面前再次接管已挂载的 TF 系统字体，书名和系统控件不再跟随阅读字体。字体列表用浅灰底标记当前项。
- 首页与书架按内容版本、目录和 TF 挂载状态缓存扫描结果与封面，仅在导入、删除、换目录或存储状态变化后重扫。
- EPUB 插图改为读取文件签名识别 JPEG/PNG，兼容扩展名缺失或错误的图片；提高图片容量上限和浅色插画对比度。
- 阅读翻页取消延迟二次刷新；系统页面切换改用灰阶页刷，传输状态只刷新局部区域，减少整屏闪烁和等待。
- 状态栏使用圆角电池、左侧百分比、充电闪电和满电填充；应用页改为四列圆角图标，设置与各二级页统一返回、卡片间距及更清晰的系统字号。
- 文件管理显示 TF 目录并支持自选书籍/字体目录；USB 读卡直接进入挂载流程。票根与自选图片归入锁屏样式。
- 修复继续阅读书名尾部 `=This`、长书名截断、书架透明挡板辨识度及旧示例页面残留入口。

## 2026-09-30 · Codex

- 书架新增第四种半透明书袋样式，样式选择页使用四种实际书架首行的缩略图；票根锁屏在阅读中保留当前正文为背景，其他页面使用灰阶斜格纹。状态栏在充电时显示闪电标识。
- 应用页增加微信读书入口，目前支持已保存 WiFi 下的扫码登录、云端书架同步和退出登录；书籍缓存与本机阅读仍在开发中，尚未作为完整功能交付。
- 系统页面三颗触控键改为返回／首页／返回；阅读正文继续使用翻页与中键设置。系统小字约放大 1.2 倍，阅读正文大小不变。文字边缘、图标与分割线加深；书籍封面、插图、图片和票根壁纸提高灰阶对比度。
- 首页改为紧凑的继续阅读卡、近七天实测阅读时长与柱状图、书架摘要和四本最近阅读；书架提供默认网格、深色书轨、亚克力挡板三种样式，选项保存在设备上。
- 应用页改为浅色九宫格，框线加深并换用 Pico 灰阶图标；设置页改为卡片分组和较醒目的灰色图标，字号与间距集中在一个设置页。
- 亚克力书架挡板增加灰阶模糊和更明显的半透明覆盖，样式选择页共用同一绘制效果。状态栏使用圆角电池并在左侧显示百分比，系统界面字号略增。
- 书架切页后复用已扫描书目和封面，在传书、USB 退出或存储状态变化后重扫。重新生成内建字体子集，补齐当前界面文字；移除打开书籍时底部临时加载提示。
- 产品界面统一使用 Pico 名称。书架仅显示导入书籍，每页九本。底栏第四项改为应用，集中放置图片、传输、图书管理和设置。
- 本机书名编辑加入可点击定位、左右移动的插入光标，可在书名中间插入或删除文字。设置改为分组列表，加入 WiFi、蓝牙和日期时间入口。
- 手动设时与 WiFi 对时写入 PMU；开机读取 PMU 时钟恢复系统时间。WiFi 对时使用已保存网络，完成后关闭无线连接。

## 2026-09-29 · Codex

- 系统界面改用独立内建的 Noto Sans SC Regular 子集（OFL-1.1）；阅读正文仍可选择其他字体。修复书架设置入口的残留重绘与旧设置页代码。
- 设置页增加本机日期时间编辑（北京时间 UTC+8），保存后同步系统时间和 PMU；EPUB 章节题头使用目录标题，不再把正文前置引言当作章节名。
- 首页封面缓存在 PSRAM，返回首页时按路径、文件大小与修改时间复用，书籍顺序变化也可复用。
- 底部四栏图标改为细线风格：首页房屋、书架斜立书脊、文件夹与设置宫格。
- 首页阅读热力图改为最近 30 个自然日，使用实际阅读时长；设备时钟未同步时不显示虚构的日期数据。
- 加入首页、书架、文件管理、设置四栏导航；文件页集中书籍、图片、字体和 USB/Wi-Fi 传输入口。跨页面与进入阅读时执行整屏刷新。
- EPUB 章节第一页单独显示章节题头；书名改为本机拼音候选输入与保存，无需打开网页。阅读字体入口直接进入 TF 卡字体目录。
- 预览图使用苹方；固件保留可分发的无衬线后备字体，支持从 TF 卡加载兼容 TTF/OTF 文件，未随固件分发苹果字体。
- 固件 0.3.2：书架封面下只显示书名。首次打开书架或滑到未缓存的页面时先显示书架，再逐本补齐封面；生成的缩略图保存在 TF 卡隐藏目录，后续开机和翻页直接读取，换书后自动更新。
- 修正开机时误导性的「初始化内置存储」提示；既有内置分区仍按正常流程挂载，不会在每次开机格式化。字号和间距重排只测量字宽，减少额外的进度保存和字号二次刷屏；间距变化先快速刷新，闲置后整理灰阶。

## 2026-09-28 · Codex

- 固件 0.3.1：阅读页进度条改为圆角并移除外伸章节刻度；书架继续阅读区增加最近一本的封面、章节与页码信息。
- 票根锁屏上下使用裁纸缺口，背景由最近阅读的 EPUB 封面生成灰阶壁纸；无内嵌封面的书使用书名占位背景。

- 固件 0.3.0：书架移除底部翻页与菜单，改用左右滑动；顶部「设置」整合图书管理、图片和传输。
- 图书详情加入手动编辑书名入口；WiFi 网页可编辑 `book`、`books` 和内置图书的显示书名，独立保存而不改 EPUB 或进度。
- 票根锁屏显示最近阅读书籍、封面、进度和实测阅读统计；WiFi 网页同步设备时间以启用今日统计，时钟无效时明确提示。

- 主菜单收敛为书架、图片显示、传输；书架优先读取 EPUB 的书名、作者，显示封面和继续阅读入口，进入书架不再自动跳到正文。
- EPUB 正文支持 JPEG/PNG 图片独立成页；阅读设置增加字号、行距、段距和扩展字体。普通翻页改为快速差分，闲置后灰阶整理，周期整屏清残影。
- 传输页增加 USB 读卡模式，安全弹出后恢复 TF 卡本机挂载；USB 活跃时暂停本机卡访问和锁屏。
- 固件版本 0.2.0 已使用 DIO/80 MHz 写入 ESP32-S3，哈希验证通过，启动日志确认 SD 卡挂载与书架扫描。
- 增加图片对比页：内置灰阶测试图，读取 TF 卡 `pictures`/`images` 下的 JPG/PNG，并通过传书网页上传图片；图片切换时整屏灰阶刷新。
- 阅读中键单击进入页面设置，长按回书架；阅读页移除右下角返回图标；书架与字体列表忽略 macOS 的隐藏辅助文件。
- 书架同时扫描 TF 卡根目录的 `book` 与 `books` 文件夹，兼容已放入单数目录的书籍，传书仍保存到 `books`。
- 图书书架改为每页六本的封面网格；自动从 EPUB 2/3 的 OPF 声明提取 JPEG/PNG 封面，生成电子墨水屏灰阶缩略图，缺失时显示书名占位封面。
- 阅读工具栏加入宋体、黑体、楷体快捷切换，字体从 TF 卡 `fonts` 目录读取；附带三种开源中文字体。
- 提供合并固件镜像与基于 ESP Web Tools 的本地 Chrome 刷机页面。

## 2026-09-27 · UNSaWEN

- 图书阅读：支持 TF 卡或内置存储的 UTF-8/GBK TXT 与纯文本 EPUB，目录、逐书续读、字号、手势翻页及默认关闭的实验晃动翻页。
  Read UTF-8/GBK TXT and text-only EPUB from TF or internal storage, with a TOC, per-book resume, font sizes, gestures and optional experimental shake-to-turn.
- 图书管理：增加拼音/首字母/英文搜索、来源筛选、名称/最近阅读排序、详情，以及分别确认的单本/批量删除和清进度。
  Add pinyin/initials/English search, source filters, name/recent sorting, details and separately confirmed single/batch deletion or progress reset.
- WiFi 传书：支持设备热点和已有网络、触屏/网页配网、连接与网址二维码；网页管理当前存储，支持确认替换、取消和重试；停止后返回进入前的位置。
  Transfer over the device hotspot or an existing network, with touchscreen/web provisioning and connection/URL QR codes. Manage current storage in the browser, confirm replacements, cancel or retry uploads, and return to the entry location on stop.
- 可靠性：处理保存失败与部分成功重试、中文文件名和覆盖中断恢复；修复显示欠载后相位队列残留导致的死锁；检测 TF 失效后停止相关消费者并回退字体，插回后需显式重挂。
  Handle save failures, partial-operation retries, Chinese filenames and interrupted replacements. Fix a display queue deadlock after underrun; stop affected consumers and fall back fonts on TF loss, requiring explicit remount after reinsertion.
# S31 本地测试：在线字体（2026-10-24）

- 系统与阅读字体选择页新增共用在线字体列表，五款按行显示名称、文件大小与下载；分块写 TF，校验完整大小、SHA-256 与字体表，保存原版权及 OFL 文本。
- 下载不自动切换字体，取消、离页、锁屏和拔卡收尾沿用现有字体及 S31 生命周期；保留异常启动字体保护。本项仅本地构建，未发布。
