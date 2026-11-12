# 双板型构建

本仓库的 `main` 同时维护 Pico 与 Metalio S31 源码：共享 `main/` 阅读器和 UI，板级驱动与配置由 `boards/` 选择。正式二进制继续在统一 Release 归档，官网按机型选择。现有文件许可与第三方权利保持。

维护同一份 `main/` 阅读器和 UI 源码，通过 `KIIKO_BOARD` 选择设备。`boards/<设备>/` 保存该设备的配置、分区和硬件后端，`cmake/board.cmake` 选择参与编译的驱动。已有 `read_pico_*` 接口名继续作为内部兼容接口使用。

## 支持的设备

| 编译选项 | `pico` | `metalio47` |
| --- | --- | --- |
| 设备 | 原 kiikoread 墨水屏 | Metalio E-INK4 Plus 4.7 V1.5 |
| 芯片 / 编译器 | ESP32-S3 / Xtensa | ESP32-S31 / RISC-V |
| 固件项目名 | `Read_Pico` | `Kiikoread_Metalio_S31` |
| 触摸 | CST836U | FT6336 |
| 屏幕供电 | 原 CW32 / SY7636A 路径 | TPS65185 / TCA9555 |
| 波形 | 原 Pico 校准序列 | 原 GC48 / GL48 / DU20 |
| 应用分区 | 双 4 MiB | 双 5 MiB |
| bootloader 地址 | `0x0` | `0x2000` |
| OTA 数据地址 | `0xd10000` | `0xd000` |
| 设置命名空间 | `read_pico` | `metalio_s31` |
| 锁屏 / 启动记录空间 | `pico_lock` / `pico_startup` | `metalio_lock` / `metalio_start` |
| 联网 OTA | 沿用正式版 | 支持，GitHub Pages 优先、官网备用 |

Metalio 仅适用于已适配的 **V1.5 / E0470A14-AF 34Pin** 硬件，不自动推断其他硬件修订。两种设备的原始波形、供电时序、引脚、存储地址及默认配置分别保留；共享功能更新不代表把一块板的底层参数搬到另一块板。

两块板共用rc97的凭据读取和紧凑启动记录逻辑：正常可读的已设密码仍验证；读取失败或损坏时不拦启动、可重新读取且禁止覆盖旧凭据。启动记录兼容旧定长格式，空间不足只回收自身旧记录。S31异常启动字体回退、USB交接、前光与睡眠恢复由自身后端保留。

Metalio设置主列表及文件管理内容按黑白目标使用DU20，固定底栏保留已显示像素，手动全刷与故障恢复仍走完整路径；设置子页图片预览保留灰阶。首页继续阅读卡片纯白，七款时钟在两种板型共用可释放底图缓存，覆盖首页、样式预览及放大时间页。实时底图两槽最多231840字节；S31额外为七款选择页预览各保留一个有界槽，统一池最多1043280字节PSRAM，始终预留2MiB；IKEA/小屋数字使用当前系统字体，Cat眼睛和Kehai圆环/圆点仅重绘局部，日期/分钟/方向/黑白模式和尺寸变化按需失效；内存不足直接绘制，进入阅读前释放。S31样式选择页固定10:10:30并按需缓存完整表盘，停止秒针/猫眼更新；箭头切换不单独扫描按下态，未变的底部按钮排除在更新范围之外，仍使用完整灰阶。选择页显示完成且空闲600ms后每轮最多在临时PSRAM画布准备一个相邻表盘，不写front/back，保留2MiB余量，用后即释放；触摸、待推屏或缺内存时暂缓。离开字体设置后重新进入首页时失效完整预览，首页与时间详情的实时底图不包含活动字形。普通灰阶切页的备行等待仅在非传输且无欠载时缩减，欠载后本次开机回退保守余量。

Metalio普通主页使用定向黑白端点和完整中灰相位，周期整理与底栏横条共享扫描；阅读、水波纹、手动全刷与失败恢复保留各自路径。当前普通灰阶48相、每相至少12ms，仅相位节拍已需576ms，另有供电、排版与传输开销；不能用CPU或内存规格推断整页能在300ms完成。串口的`SWITCH`日志区分退出、字体、进页、推屏和总耗时，`main refresh`另记录供电及显示总耗时；这些是固件计时，最终光学稳定时间仍需实机测量。

S31的PIE只在Core1可用。较大的差分作业在扫描空闲时整帧提交给现有Core1供屏任务，前台同步等待像素、脏图与前导统计全部归还；小区域、无工作核或资源不足保留原路径，不迁移主任务。超时先请求取消并收回缓冲后回退，无法归还时停止而不留下后台写入。默认构建使用C算子，显式加`--s31-pie`才在Core1的16字节对齐完整块使用128位PIE，Core0、未对齐和尾部保持C。`METALIO_LOCAL_LATENCY=ON`仅供本地整帧oracle及开关对照，交付构建必须关闭；微基准不能作为整机倍数。

S31实际显示输出与SDK Flash操作共用递归锁，完整扫描和断轨结束后才允许Flash停顿；准备封面/绘制期间不持有此锁。主页面绘制可与TPS的50ms唤醒等待重叠；只提前WAKEUP，PWRUP/VCOM保持低，实际扫描仍执行原厂后续顺序，取消或失败立即下电，不保压等待用户。供屏在原1KiB查表内预置各像素位，空闲普通/黑白切页保留已验证的128行预填，传输、临时对时/扫描、BLE活动和欠载保持192行；扫描仍为20MHz、原GC48/GL48/DU20与至少12ms。S31内部DMA双缓冲各64行，以64字节边界分成不超过4032字节的描述符，每个缓冲只在末段通知EOF，保留约1ms补数余量且不改变面板行时序。DMA EOF提高中断优先级并过滤准备/停止期的旧VSYNC；按SDK启用RGB ISR缓存关闭安全选项，ISR读取的行队列指针表固定在内部RAM且只按指针大小分配。旧S31构建缓存由构建工具迁移这两项安全配置，其余配置保留。未完成扫描仍报错并走原恢复。

书架最终灰阶封面复用既有动态4MiB LRU，不增设整页常驻副本。键包含源像素校验、尺寸、旋转、对比度与4×4点阵相位；仅复制精确对齐的物理行，收藏、边框和文字实时画，未对齐或内存不足直接绘制。阅读前缩减与媒体失效清理沿用既有生命周期。首页分钟到点把状态栏和静态表盘合并为一次真实差分，输出失败不确认已显示。

## S31 JPEG混合解码

S31使用SDK原生JPEG硬解处理兼容的8位基线YCbCr/灰度图，按MCU填充计算输出步长，颜色转灰度后使用面积平均缩小或双线性放大。小封面的大图、超预算、非兼容采样、资源不足或硬解超时回退原流式软件路径。渐进式优先固定版Espressif libjpeg-turbo3.2.0~1完整扫描，按目标尺寸选择1/1～1/8 IDCT缩放，不再固定只取1/8 DC；超预算/失败时仍允许旧预览回退。

单次混合任务串行，工作预算最多12MiB并预留2MiB PSRAM；硬解200ms超时、渐进式进度检查2.5s，完成或失败立即释放引擎及临时图像，不新增常驻整帧。大文件可直接从FILE软解，硬解输入最大4MiB；PNG保留现有流式/透明度处理。最终显示灰阶、原厂波形、封面LRU和缓存生命周期保持，S31封面/壁纸缓存版本单独失效重建。硬解速度和实际颜色需实机验收，宿主硬件替身测试不构成硬件性能测量。

English: Native S31 JPEG decoding handles compatible8-bit baseline YCbCr/gray with MCU-padded stride, luminance conversion and area/bilinear resampling. Keep streaming fallback for large-source thumbnails, unsupported layouts, budget/allocation errors or timeouts. Pinned libjpeg-turbo3.2.0~1 fully decodes progressive scans at a target-appropriate IDCT scale, retaining the old small preview as the final low-memory fallback. Serialize hybrid work, cap its budget at12MiB with2MiB PSRAM reserved, use bounded200ms hardware /2.5s progressive checks, and release temporary buffers/engines on every exit. Keep PNG alpha, display grayscale, vendor waveforms and existing caches; version S31 cover/wallpaper products independently. Host fixtures do not measure native hardware speed. The official libjpeg component requires a build path without spaces; a dedicated/tmp build directory can be exported to the normal workspace afterward.

## S31关机与锁屏省电

S31挂载发布不等待全卡FAT空闲计数；文件管理先完成目录首帧，再在空闲时异步查询分区用量。浅睡唤醒先恢复显示与触摸，媒体最多短等500ms，之后由已有恢复服务继续重试。暂停期间不对字体路径做stat或卸载保留槽，恢复后补一次页面；不覆盖用户选择，也不改变异常启动使用内建字体的保护。500ms只限制挂载状态轮询，底层单次卡命令和字体/书籍文件重开仍受其自身I/O超时约束，不代表任何坏卡都能在500ms恢复。

挂卡配置最多八次：四线40/40/20/10/5/1MHz，单线5/1MHz。成功配置留在RAM，浅睡重挂优先复用；四字节有效范围校验的RTC提示仅在深睡启动读取，冷启动/异常复位回原高速。提示失败先试余下保守档，再有界绕回其他档，换卡不会被旧档永久限制；OOM/参数错误直接停止，无卡停止，不额外格式化、不写NVS。单线仍使用原slot0/CLK24/CMD25/D020，D1～D3不交给控制器，并在挂载前将其GPIO和专用PAD恢复高阻上拉，避免沿用睡眠低电平；引脚恢复失败停止，不当作文件系统坏卡。内置DMA缓冲、USB恢复和驱动失败清理由原所有者保留。慢卡/损坏卡的成功率与实际降速后读写稳定性需实机验证。

English: Try at most8 profiles: four-line40/40/20/10/5/1MHz, then single-line5/1MHz. Reuse successful settings from RAM on light wake; a range-checked4-byte RTC hint is accepted only after deep sleep. Cold/abnormal resets start high. If a hint fails, try safer settings and wrap once through other profiles, so changed media can recover. Stop on absent media, OOM or invalid arguments; never format automatically or write NVS. Single-line retains slot0/CLK24/CMD25/D020 while leavingD1–D3 unassigned. Keep DMA/USB ownership and failure cleanup. Physical weak-card success and sustained lower-speed IO still require device tests.

English: Publish a mounted filesystem before scanning FAT allocation counts. File management draws its directory first and queries partition space during idle. Light wake restores LCD/touch before a bounded500ms media poll; existing services continue recovery. Do not stat/unload retained font faces while media is suspended, and repaint once recovery completes. Saved font choices and abnormal-boot fallback remain intact. The poll bound does not replace individual SD-command/file-reopen timeouts or guarantee recovery of faulty media.

“关机睡眠”只保留“浅睡10分钟后关机”（默认）和“彻底断电”，不再单独选择锁屏省电方式。旧`lock_lp`值忽略，已明确保存的关机/睡眠选择仍保留；新选择在NVS提交成功后生效。短按及自动锁屏按此选项执行：默认浅睡十分钟后关机，或锁屏画面定稿后直接关机。十分钟到时保留最后成功显示的锁屏画面，TF/字体已关闭且供电停放时直接同步外挂PCF8563并发出原厂关机脉冲，不先恢复SD/LCD/前光；失败才完整回滚外设。滑动关机独立于该选项，先显示“kiikoread 已关机”再直接硬件断电。RTC仅保时，不设置闹钟或定时重新开机；无效系统时间不覆盖RTC。

浅睡保留RAM、字形与阅读索引，使用Metalio原厂SD、TPS、屏总线、P17/GPIO2和断轨顺序。每个维护切片不超过60秒，最后一段按十分钟截止时间缩短，不反复挂载TF。真正按键优先于到时交接，定时不能解锁或点前光。共享EXT1中断不能单独补造短按：恢复按键方向和慢挂卡前两次有效P17低电平才确认，未确认或I2C读失败仍保持锁屏。真实按键释放与双击继续由原采样器消费；有密码仍须验证。

恢复输入方向后，按键扫描也须相隔30ms两次有效P17低电平才承接已有按住，避免单次低电平在随后松开时伪造短按。启动日志的`reasons`位掩码用于定位字体恢复保护：`0x01`异常复位、`0x02`NVS读取/记录校验失败、`0x04`上次启动未完成、`0x08`开书中断、`0x10`检查点写入失败；原因可组合，保护策略与NVS格式不变。

English: After input directions are restored, require two valid P17-low samples30ms apart before inheriting a held key; a single low must not become a synthetic release click. Startup `reasons` is a diagnostic bitmask: `0x01` abnormal reset, `0x02` NVS read/record validation, `0x04` incomplete startup, `0x08` interrupted book opening, `0x10` checkpoint write failure. Causes may combine; policy and persisted format remain unchanged.

S31在开机门禁前保留已校验的锁定记录，防止恢复启动期间再次复位丢失锁定意图。异常启动仍禁止自动开书与字体恢复；真实按键/PIN门禁完成后才清理不能自动恢复的锁定标记，正常续读仍先持久化消费入口再开书，不改变记录格式。

English: S31 retains a validated durable lock record before the boot gate so another startup reset cannot discard lock intent. Abnormal starts still prohibit automatic book/font recovery. Clear non-resumable lock intent only after the real-key/PIN gate completes; normal resume is still persistently consumed before opening, with the record format unchanged.

充电/USB期间暂缓锁屏自动关机，拔线后重新计算所选等待时长，不执行充电期间已到期的截止时间。USB、忙传输、活动挂载和不安全唤醒线暂缓浅睡；无卡且没有待重开句柄时可以浅睡，不必一直空转。没有成功保存的检查点不贸然自动关机。浅睡失败按1/3/10/30秒退避，硬件关机失败保持锁定、30秒后重试；文件关闭/同步失败不发关机脉冲，按键事务拒绝时保留真实输入。关机不切换字体、不改设置；重新硬件开机且尽早两次采样确认P17按下时，单击模式才直接恢复，有密码/双击继续门禁，软件复位与未确认的深睡唤醒不能直接放行。超短按若未被采样确认可能需要再按，不能为避免漏按而把共享中断当真实按键。异常启动字体保护不变，另从校验通过的原NVS记录保留锁屏门禁，不能因禁用自动开书而直接进入首页；NVS格式不变。

S31壁纸锁屏先在内存中准备完整16灰画布，再从最后成功显示的真实参考帧直接执行一轮原厂GC48，省去独立白屏预刷；文件管理/设置的黑白输出策略不影响壁纸灰阶。仅成功推屏后提交壁纸与锁屏缓存，推屏失败重试和PIN门禁保持。票根、拼贴反馈及透明书封的既有清理流程保留。

S31浅睡SDK兼容模块来自[Metalio官方2.0.66](https://github.com/CloudZao/Metalio-E-INK4-Plus/commit/2c1e5efba74f9ffd6f7d72e348011308e53f39a1)，源固定为`ad8edf4150a688737c0d7c00cae4413079b262ef`中的`s31_sleep`，保留MIT许可。恢复MPLL模拟校准前置条件与12/12偏置、PSRAM/XTAL/MPLL时钟交接，保留看门狗和CPU/PSRAM睡眠配置，只编译工程内SDK兼容副本，不修改全局SDK。RTC RAM阶段记录与复位原因在启动后有界重放，区分真实唤醒和复位恢复；这里的RTC RAM诊断与外挂PCF8563保时是不同用途。此项只适用于S31，不能移植寄存器参数到Pico。

English: S31 offers default light sleep for ten minutes followed by PCF8563 sync and hardware power-off, or immediate complete power-off. Close/sync book/font/SD before the vendor power pulse, retaining saved selections and checkpoints; RTC preserves time without alarm wake. No-card/no-handle standby is allowed, while busy media/USB/unsafe IRQ defer sleep. Retain bounded slices, retries and PIN/double gates. Shared EXT1 alone cannot grant unlock: verify physical P17 twice before restoring inputs/media. Very short unobserved presses may require another press. Single-press hardware boots resume directly only with early verified P17 evidence; software resets and unconfirmed deep wakes remain gated, and abnormal-font protection stays intact. The MIT vendor2.0.66 sleep module backports MPLL calibration/bias and PSRAM clock handoff through project-local SDK sources, preserving watchdogs and board isolation. Delayed reset/RTC-RAM traces distinguish wake from reboot; external RTC timekeeping is separate. Current draw, actual ten-minute power-off and wake/media/USB behavior still require hardware validation.

S31同步Pico PR46–49的设置控件、密码白底与标题提示排版。密码页可点按切换“关闭/5分钟/1小时/8小时/24小时”宽限期，默认关闭，档位存入S31自身NVS命名空间；只在本次锁屏的真实电源键浅睡唤醒后检查当前有效时钟。共享中断、定时、触摸和USB不授予免密资格；到期、无效时间、时钟回拨及硬件重新开机仍需密码。冷启动不复用旧宽限，异常启动字体保护、十分钟RTC同步硬件关机与PIN门禁保持独立。密码层先在内存铺白供按钮采样，入场一次输出；等待及取消恢复原锁图，不额外全刷。

English: S31 adopts Pico PR46–49 control/PIN/notice presentation. Optional PIN grace cycles off/5min/1h/8h/24h, defaults off and uses the S31 NVS namespace. Recheck valid current time only after a real power-key light wake in this lock session; timers/shared IRQ/USB/touch cannot grant grace. Expiry, invalid/rollback time, hardware reboot and abnormal-start protection retain authentication. Sample PIN buttons on white memory, present once, and retain artwork while waiting/cancelling without extra full scans.

### S31本地刷机启动核对

本地POSIX刷机工具可调用`tools/s31_controlled_restart.py`，前提是已核对MAC、分区、实际启动槽、应用完整内容与保留数据，并已请求原生esptool下载复位。必须核对真实启动日志，不能仅凭工具显示“watchdog reset”推断固件的复位原因；本机该命令也实测报告过正常POWERON。若目标项目/版本/ELF匹配、保护原因全部为零且UI就绪，直接结束，不再额外重启。

只有此次受控更新的首次启动明确为WDT、且唯一保护原因是异常复位时，才在UI安全就绪后执行一次普通USB复位，再核对同一应用启动、保护归零及UI就绪。原有异常判定不变，不写恢复记录；启动/开书中断、NVS读写错误、重复启动、应用身份不符、诊断缺失或超时均停止，不自动重试复位。PIN/锁定门禁未通过时也不因等待而放行。原生刷机后必须以校验及启动记录为准，不把辅助脚本完成等同于实屏/睡眠验收。

English: The optional POSIX local updater verifies real startup evidence after a guarded native reset. A matching clean POWERON/USB boot needs no extra reset. Only the controlled update's sole WDT-reset protection cause may receive one ordinary USB reset after safe UI readiness, followed by build/cause/UI verification. Retain all firmware protections; missing diagnostics, dirty startup/book records, NVS failures, repeated boots, identity mismatch and timeouts stop without reset retries or record edits. A waiting lock/PIN gate is never bypassed.

### S31首次短按冷开机实验

显式`--s31-early-power-key`才启用，默认关闭，仅S31。引导程序在自身初始化前只读TCA9555的方向、反相及P1.7，要求两次有效低电平相隔约30ms；NACK、总线不空闲、时钟超时、方向/反相不符均不提供按键凭据。只操作GPIO0/1的有界开漏读事务，不使用共享GPIO2判断按键，不改扩展器输出/方向、前光、RTC时间或持久配置。

配套应用用SDK的`.rodata_custom_desc`声明自己的32字节RTC noinit记录区。项目包装`bootloader_load_image`，先完整执行原SDK校验和加载，成功且描述符/芯片/分区/地址范围均匹配后才传递记录；不得覆盖已加载的代码/数据或SDK顶部保留区。没有协议的旧镜像不接收记录，原OTA选择/回退不变，不增加或移动SDK RTC预留区。初始化前的采样保存在已初始化的引导程序data段，不依赖尚未清零的BSS。

应用开头即消费并清空记录，仅当前真实POWERON且校验有效的按键凭据可与后续真实P1.7采样合并。单击、双击、PIN和异常字体保护继续沿现有门禁；WDT/USB/定时/其他复位不继承旧按键凭据。此实验需要配套应用和引导程序，单写应用不能保证修复首次短按。完整编译及宿主测试不替代断USB十分钟后的实际开机测试，不能将ROM阶段之前已经松开的按键凭空补记。

English: Explicit S31-only early-key experiments sample real P17 before bootloader init, retain stable evidence in initialized data, and hand it only to a successfully validated/loaded image declaring an app-owned RTC noinit record. Legacy images receive no handoff and SDK RTC layout, slot selection and rollback remain unchanged. Consume once at app entry; retain single/double/PIN/crash guards and reject all non-POR/stale/fault evidence. Install the paired images for hardware testing; normal builds remain OFF.

### S31媒体恢复顺序

先挂卡并完成只读扇区及FAT根目录验证，才发布可读状态；未通过验证的档位不记忆，按既有四线降频/单线序列有界回退。TF可同时打开文件上限16，当前字体槽优先恢复；闲置字体失败保留待恢复状态，不阻塞当前字体与书架。健康检查不因字体恢复失败而停止。

浅睡重开连续三次失败后，先等待封面/头像任务释放并关闭页面书籍、全部字体槽（含部分已恢复句柄），再失效旧VFS会话，由主循环安排异步重挂。挂卡期间不重开字体、不抢USB模拟盘/OTA/字体下载/传书会话；验证新会话后重新尝试保存的字体路径，不改变设置。异常启动的内建字体保护继续独立生效。

前光的PWM为零后才保持GPIO6/7，恢复外设不会释放这两脚；只有真实解锁/通过启动门禁后，先设置期望PWM再解除保持。硬件初始化不提前恢复已保存亮度。极短开机按键未被采到时可能需再按一次，不能把上电复位或共享中断当作解锁证据。上述顺序的回归与完整编译不能替代实机挂卡成功率、充电兼容性和功耗测量。

English: S31 verifies actual sector IO and the FAT root before publishing a mount, retaining existing bounded clock/one-bit fallback and remembering only verified profiles. Sixteen open-file slots and active-face-first resume prevent idle faces from blocking media. Three failed wake resumes yield to worker quiescence and full consumer closure before asynchronous remount; fonts wait for verified media, with USB/OTA/transfer owners respected and saved paths/abnormal-boot protection retained. Explicit slider shutdown displays the powered-off message; timed lock shutdown preserves artwork and cuts from parked peripherals without a revive/remount cycle. Charging defers automatic off and unplug starts a fresh selected interval. Zero-duty GPIO6/7 holds survive restoration until authorized unlock; startup stays dark until its gate passes. Hardware acceptance, current and recovery rates remain to be measured.

## S31 AI快捷键

AI/BOOT按键沿用[官方GPIO61输入](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/main/main/boards/metalio-e-ink4-plus/config.h)，不改下载和唤醒配置。稳定按下500ms打开上方横向液态玻璃菜单，点选“全刷一次、书签开关、前光开关、返回首页、不设功能”后保存短按映射并收起；默认全刷。只改变映射，不当场执行所选动作。菜单已打开时再按AI或点外部关闭，长按后的抬手不触发短按；书签仅当前阅读正文有效，复用既有书签记录与提示。

菜单只保存648×252底图（81648字节临时PSRAM），展开一次模糊与一次有界灰阶差分，收起精确还原并一次差分；没有入场位移动画、额外清白或逐项推屏。选项共用绘制/命中矩形，输入零分配/零绘制，多指、拖动和触摸读错取消，保存失败保留旧映射并提示重试。低内存无法保存底图时保持原页面。玻璃保留本板现有完整灰阶，未宣称已实测达到300ms；串口AI compose/display分开记录耗时。

菜单覆盖时暂停底页轮询/字体重试/导航和蓝牙翻页；电源、锁屏、媒体变化会清理覆盖层；配置菜单不阻止原自动锁屏，进入锁屏前先还原实际底页，透明书封不会缓存菜单。锁屏/PIN期间AI不用于唤醒；跨休眠持有的按键须先释放。USB读卡、升级、传输写入和PMU专属页不接管AI。映射写入S31设置命名空间，仅成功提交才更改当前映射；旧跨板配置备份格式保留。

English: The vendorGPIO61 AI input keeps USB/wakeup configuration. Hold500ms for a glass chooser to persist tap refresh/bookmark/light/home/disabled; selection never executes the action. Default refresh; bookmarks remain reader-body-only. One blur and one bounded grayscale output per normal transition,81648B temporary underlay,exact restore and zero input allocation/painting. Preserve the calibrated grayscale path,modal/transfer/update/USB/PIN guards and sleep-release suppression; no measured300ms claim or portable-backup format change.

## S31覆盖层与电量

AI菜单五个圆形图标横向排列，文字在图标下方，选中项以圈内右上小点标记；标题和标签在玻璃处理之后直接绘制黑字，不加独立文字底色。下拉菜单保留左侧2×2开关和右侧前光双滑块，玻璃层提亮并模糊底字；输入继续恢复缓存并局刷。电源菜单仅在滑动条和重启/取消圆钮内部模糊提亮，控件外的页面像素保持原样，再画清晰文字与图标；输入不重复处理背景，缺内存保留原实心回退。

S31百分比直接使用BQ27220 `StateOfCharge(0x2C)` 的0～100值，不使用电压曲线估算或Pico的显示修正。电压仅验证读数范围；SOC读错或越界显示未知，不能用电压补算。充电检测继续结合BQ电流和充电芯片状态。计量精度取决于电池容量/化学参数、学习与老化状态，显示来源正确不等于已验证计量精度。

English: Paint crisp black labels after glass. Keep the S312×2 switches and dual light sliders; blur only on entry and retain cached local input. Power glass is confined to the slider and round buttons; all background pixels outside their shapes stay intact. AI labels have no separate backing. S31 percentages use BQ27220 SOC directly with no voltage estimate or Pico correction; failed/invalid SOC remains unknown. Gauge accuracy still depends on battery parameters,learning and aging.

## S31锁屏与侧键

“双击解锁”位于显示主列表的自动休眠锁屏下一行；新配置默认关闭，旧配置中的明确单击/双击选择不改变。S31继续使用自身单击/双击事件消费与PIN门禁，关机睡眠页统一为先浅睡十分钟后深睡或彻底断电。票根封面与背景仅经一次对比度灰阶映射，保持自然比例和旧封面缓存兼容。用户主动选择导入阅读字体连续三次真正回退内建后，可选择保存进度再重启或返回字体页；未保存成功不能重启，异常启动保护仍独立生效。

首页元数据、封面索引与头像使用PSRAM BSS，保留静态零初始化；DMA、ISR、板级状态和缓存预算不变。时钟预览待更新范围独立于按下反馈，在失败后合并后续范围，仅输出成功才清除。S31冷启动使用已有内建字形绘制Kiikoread与准备资源，不再链接全屏loading位图；USB接管、VCOM校准、深睡保图与PIN流程保留。

English: The Display root list owns the unlock toggle immediately after auto-lock; missing settings default to single press while explicit saved choices survive. Keep S31 wake-event consumption and PIN with the unified staged-sleep/power-off preference. Ticket artwork receives one grayscale contrast mapping. Three actual fallbacks from explicit imported-face selections offer a progress-safe restart or cancel; abnormal-boot protection remains independent. Cold Home metadata/cover indices/avatar use zero-initialized PSRAM BSS, never DMA/ISR storage. Clock pending bounds survive failed outputs and merge subsequent changes. The cold splash uses existing embedded glyphs without the loading bitmap, preserving USB/VCOM/deep-wake/PIN flows.

锁屏单击电源键后等待650ms双击窗口，再局部显示“双击电源键解锁”，约三秒后还原原图。只暂存448×72区域（16128字节PSRAM），不额外常驻整帧；缺内存跳过提示。提示不打开前光，触摸不解锁；双击及已有密码验证继续走S31原路径。提示待显示或已显示期间不启动新的硬件休眠事务，恢复后沿用原模式。锁屏图缓存先于提示保存，提示不会进入持久缓存。

上下音量键在四个根页面与屏幕左右键一致：上键对应左键、下键对应右键，循环切换主页面；书架分页控制开启时同样用于翻页。阅读和二级页面继续由该页面的按键映射处理。菜单、电源对话框、下拉层、USB独占、升级及待处理切页均保留输入保护。

English: A debounced locked short release schedules Pico's448×72 hint after650ms for3s, using16128 bytes of temporary PSRAM and restoring exact pixels. Keep the lamp off, double-click/PIN authentication and S31 sleep transactions; no touch unlock or persistent hint cache. Root volume edges share left/right navigation and shelf paging; reader/child handlers remain page-owned, with modal/USB/update/pending-navigation guards.

透明PNG锁屏书封缓存继续放在`/sdcard/.metalio_s31/locks/seal-layer.bin`，使用SEA2无损行压缩保留原gray/alpha字节和阴影。首次生成在空闲PSRAM大于两图层加1MiB时直接处理并写压缩层，否则沿用有界行流程；所有工作区用后释放，不缓存当前页面底图。旧格式/尺寸/文件变化或损坏触发重建，完整校验先于叠加；FAT不能覆盖重命名时备份原缓存、发布失败回滚。图层不减灰、不改完整刷新波形和S31睡眠/PIN/USB流程。

## 大图片EPUB打开

排版通过书源尺寸接口测量插图，不为每幅图完整提取资源。PNG先探测32字节；JPEG仅在必要时扩大到4KiB、64KiB，SVG包装与更晚的帧头沿用原有有界完整读取回退。ZIP前缀探测按512B/4KiB输入分块，完整提取仍使用32KiB及完整CRC校验；头部仅是尺寸提示，不能作为图片内容验证或完整下载成功的依据。当前章仍按原规则分页、恢复位置，实际显示只解码当前页图片，没有因此承诺任意长任务可打断或其余章节已全面后台化。

English: Layout uses advisory source dimensions instead of extracting every illustration in full. PNG starts with32 bytes; JPEG grows to4KiB/64KiB when needed, retaining bounded full fallback for SVG wrappers and later frames. Prefix input chunks are512B/4KiB; full extraction retains32KiB bulk reads and complete CRC verification. Dimensions do not establish payload integrity. Keep current-chapter pagination/progress and current-page decoding; this does not implement universal cancellation or background preparation of all remaining chapters.

## S31阅读面板响应

同一已显示纯文字页打开工具栏，或从工具栏进入字体/阅读设置与统计时，核对正文、章节、页码、排版代次、方向、全屏状态和前后缓冲一致后，只叠加面板，不重新绘制正文、准备插图或启动预绘。图形面板恢复原厂GL48、16级目标及一次区域定稿，不再把滑轮、开关借用文字22相量化。符合条件的完成绘制/字体重排及局部控件也通过同一完整灰阶面板出口；其余回退原路径。面板的绘制缓存优化仍保留，相位扫描时间恢复完整灰阶。
面板开合暂停未完成预绘，保留现有四槽里同代次的已完成正文；完整绘制需要时可复用这些正文，再画当前控件。覆盖面板、提示不写入正文缓存，字体/字重/抗锯齿/布局与媒体变化继续走既有完整失效。缓存未命中或内存不足直接绘制，不增加另一套常驻整帧。面板差分不计入整页自动清理次数，手动全刷和正文周期清理继续保留。

English: Overlay tools/primary settings only over verified unchanged text, retaining existing body-cache/prefetch reuse. Graphical panels now use complete vendorGL48 and16-level targets in one regional settle; neither text22-phase quantization nor FastBW may handle sliders/switches. Qualifying final renders/reflows and local controls share this full-gray outlet. No measured latency claim.

### 可选GL37阅读面板实验

仅显式加`tools/build_firmware.py --board metalio47 --s31-reader-panel37`启用，普通构建每次强制关闭，包清单记录`experimental_reader_panel37`。按Metalio官方`erase_max=11/sat_cut=5/white_sat_cut=0/hold=3`从本机GL48派生37相，保留16级输入和灰阶尾段，不量化控件；屏蔽所有等值迁移，包括白到白补推。原GC48/GL48/DU20数据与扫描时序不变。

只作用于已核对正文缓存、章节/页码/排版代次/方向/全屏和缓冲的面板叠加、同面板切换及局部控件反馈；字体或字号松手后重排的整页仍走完整48相。手动/周期清理和故障恢复优先，图文、书架/封面、首页/系统页面及锁屏沿各自原路径。构造仅尝试一次，波形缓冲最多3072B PSRAM；原厂裁剪器另有17408B静态工作区，S31放在外部RAM，不挤占联网内部堆，不能把这部分记为零开销。分配失败直接回到原48相；扫描失败保留原故障恢复并在本次启动停用37相。

37相相对于48相少11次扫描，不等于整次交互必然提速23%：渲染、差分、供电和像素实际变色均有耗时。裁掉擦除/饱和余量可能增加残影或降低墨色稳定性，必须按实屏比较；本地包不覆盖已发布同版本固件，不影响Pico。

English: Explicit `--s31-reader-panel37` alone enables vendor11/5/0/3 GL37 on verified same-body panels/sliders; regular builds force it OFF and manifests record the flag. Preserve sixteen input levels, gray tails,final holds,native tables and scan timing,holding every equal pair. Whole-body reflow,art/other pages and manual/periodic/fault cleaning retain previous owners. One3072B PSRAM LUT plus the vendor trimmer's17408B external static scratch are the added budgets,preserving internal network heap; allocation failure uses native48,while a scan fault retains recovery and disables37 until reboot. Eleven fewer scans do not establish23% total latency improvement or equivalent optical stability; compare on hardware and keep the image local.

### 两个主设置面板四灰快刷

用户在200毫秒提速讨论后最终选择四灰，取代此前讨论的纯黑白。仅显式`--s31-fast-reader-panels`构建启用，清单标记`experimental_fast_reader_panels`；普通构建强制OFF。仅字体设置、阅读设置使用0/5/10/15四级，UI原13/14近白底色映射浅灰10，避免控件消失；实底先覆盖面板矩形，区域外正文/插图和历史帧不量化。不改其他设置子面板、书架、封面、锁屏或正文刷新规则，不把前次图形22相扩大试验重新全局启用。

面板独立16相：黑白目标沿用官方短DU的7黑/8白预算；白起点中灰直接复用已有FOLLOW灰推；其他旧色先复用8白再推中灰，最长8白+5黑，末尾至少3相保持。没有截取GL48或正文22相，不增加白到白维护。此组合是本地实验，并非原厂提供的完整四灰标定表，墨色与残影必须实屏比较。LUT仅1024B外部RAM，无额外整帧；不受正文22相的开关/错误禁用影响。

面板扫描故障本次启动禁用短表，保留既有完整恢复，后续回退37/48相；37相自身的错误禁用不能被绕过。图文底页只在已成功显示且正文/章节/页码/排版代次/图片数/方向/全屏及前后帧一致时直接覆盖，不重复解码；新实底必须覆盖完整旧面板。露出正文/图片、重排或其他不匹配仍按原路径恢复。字体/字号等真正正文重排、手动/周期清理继续完整48相。入口不为按下态预刷DU20，抬起提交一次；取消不补刷。

按现有12毫秒相周期，16相名义约192毫秒，再加绘制/差分/供电；不能将此写为实测总响应已达200毫秒。此试验不加后台延迟全刷，旧37相本地包仍保留对照。

English: The final choice is four grays only on the Font/Reading primary sheets. Explicit `--s31-fast-reader-panels` enables it,regular builds force OFF,and manifests record the flag. UI targets are0/5/10/15,with13/14 fills retained as10; opaque sheets leave outside text/art/history untouched. The isolated16-phase experiment combines vendor shortDU seven-black/eight-white budgets with existing white-origin FOLLOW grays; nonwhite gray transitions whiten first. The longest eight-white/five-black sequence retains three final holds,with equal whites held. This is a local combination,not a vendor-calibrated four-gray table,and does not trim GL48/text22. One1024B external LUT,no extra frame,independent body settings/faults. Guarded overlays may reuse image underlays without decoding; uncovered body/art,reflow/manual/periodic/fault recovery retains previous paths and37/48 fallback. Drop the entry DU20 press pass; cancel without scanning.192ms is nominal scanning,not measured200ms total; compare ink and ghosting on hardware without delayed background cleaning.

## S31图形短灰阶实验撤回

406f7e8的22相图形扩展已撤回：书架亚克力浅灰被13/14→15量化成白色，原16级全刷与四灰页刷目标不一致；简化中灰迁移未经光学校准，用户实机反馈封面发黑、文字残影和设置控件异常。书架/首页封面、下拉、AI、电源、目录和解锁提示恢复原灰阶路径；普通/水波纹16级48相，原快刷仍黑白DU20，固定灰底栏与缓存预算保留。禁止仅把48截成24或改名兼容：原GL表白→10需23个有效驱动及25相跨度，5→10需38个有效驱动。正文22相不改，原厂表/电源/VCOM/扫描/USB/PIN保留，新构建显式关闭旧UI实验缓存。恢复图形正确性会增加扫描时间，后续独立短灰阶须实机标定。

English: Withdraw406f7e8's graphical22-phase extension after optical reports. Nearest-level mapping collapsed13/14 to white and differed from full16-gray redraws; uncalibrated midgray transitions produced dark covers/ghosts/control defects. Restore native UI/art paths and complete gray panel settling, retaining text22-phase,fastDU20,gray navigation,caches and hardware.24 phases are not an automatic compatibility fix: source15→10 has23active pulses over25phases,5→10 has38active pulses. New builds explicitly disable the old UI flag; optical validation remains required.

## S31普通与速读翻页

阅读设置的“翻页效果”在普通、水波纹、速读黑白间点按切换，原有0/1设置值保持兼容。所有文字路径保留安全192行备行；128行在实机普通阅读出现欠载，已撤回。TPS预唤醒与差分生成重叠，保留厂家等待和驱动顺序。

本地显式 `--s31-text-ripple` 包按用户最新要求让普通与水波纹共用四级目标（0/5/10/15）及22相。普通用一次整轮提交同步输出，不作条带动画；水波纹用8带、末两带共用启动时刻，共28拍。每个像素的有效/尾相保持，正文与进度合并，不追加第二轮页脚刷新。用户2026-10-22修订：自动全刷统一选择不全刷、5/10/15/20/30页，短灰阶与速读不再强制十页清理，面板显示所选间隔，保存/重启/备份恢复保持一致。用户反馈白底闪动后，短表的白→白与其他等值迁移全保持，取消每页三次白底尾部补推；变化的旧字仍保留擦白驱动。此项针对未变化白底闪动，尚不能据此宣称简化中灰迁移的残影已解决。

常规构建强制关闭实验；禁用实验、波形不可用或本次开机扫描失败后，普通回到完整GL48及16级字缘，水波纹使用对应完整序列。这里不把22相称为完整16级等效；灰阶及残影须按实屏验收。原厂波形原数据、电压、扫描时钟及相位下限保留。

速读黑白仅在前后均为无图无背景文字页时使用本板原厂DU20原数据，目标阈值化为黑白，实际旧灰阶保留作为迁移起点；字缘更硬，正文/进度合并，周期GC48遵循所选自动全刷间隔。所有模式的图片/背景、首次进入和手动全刷保留完整灰阶。三种效果随重启和配置备份保存；Pico仍为两种效果。

实验Bin只适用于已有kiikoread S31分区。CrossLink应用位于0x20000，与本项目0x10000不同，须走单独恢复流程；本地包不改NVS格式、不公开OTA。

English: S31 retains safe192-row prefill after a real128-row reader underrun, and overlaps TPS pre-wake with difference generation without changing vendor sequencing. The explicit local `--s31-text-ripple` build now shares one four-level22-phase table between Ordinary and Ripple: Ordinary batches one synchronous output; Ripple uses eight bands with a paired tail over28 scans. Preserve every active/tail phase and fuse progress without a follow-up scan. Per the2026-10-22 revision, periodicGC48 follows Off/5/10/15/20/30 turns for every effect, persisting across reload and backups without a ten-turn cap. After reported background flicker, every equal pair including white holds, removing three unchanged-white tail pulses per page; changed old glyphs still erase. This targets background flicker without claiming to fix the uncalibrated midgray transitions. Regular builds disable the experiment; unavailable/boot-disabled paths retain fullGL48/16-level targets. This is not an optically equivalent16-level replacement. FastBW uses the exact nativeDU20 for consecutive text-only turns, thresholding only targets while retaining real old-gray history, with harder edges and user-selected periodicGC48 cleanup. Images/backgrounds,initial entry and manual full remain grayscale. Settings/backup retain S31's three effects and Pico's two. Raw waveforms,power,clock and phase minima remain unchanged. Experimental images require the existing S31 layout; CrossLink needs its separate recovery flow.

## S31二级页与返回

2026-10-20用户最新修改：系统二级菜单恢复常驻灰底栏，首页组件/阅读目标、设置、文件目录及配网传输页面均保留。设置/文件内容止于1088，分页和系统键盘上移；设置PIN键盘采用紧凑布局，实际锁屏PIN保持全高。点击当前底栏项回到所属根页，其他项按下切页；升级写入、USB独占与上传等不可中断阶段保留保护。物理三键仍按根页/子页各自语义执行，不因为底栏可见就覆盖返回行为。

S31仅二值化内容，底栏保留固定0xe0灰底、固定图标和局部选中横条。2026-10-21用户修订：两板二级页右滑返回共用阅读识别器，内容区任意横向位置起手，水平位移至少120px且大于竖向位移，不再追加32px边缘和两倍角度限制；键盘起手仍留给输入控件。S31复用本板原八条带水波纹LTR返回，范围截止1120，避开底栏图标。主页间的水波纹仍按所选模式执行；二级页点按进入、左上角或物理返回和分页仍普通差分。多指、过短和竖向主导手势不触发返回；下拉玻璃、正文、锁屏与手动/故障恢复保留各自路径。常驻底栏不改变22相文字波形、192行备行或白底保持修复。

English: Per the latest2026-10-20 request, system children retain fixed0xe0 gray navigation, including home widgets/goals,Settings,Files and network/transfer pages. Content ends at1088 with paging/keyboards moved upward; settingsPIN is compact while the lockPIN retains its full-height layout. The current tab returns to its root,other tabs switch on press; update writes,USB ownership and uploads remain guarded. Physical-key root/child semantics stay independent. S31 binarizes content only and preserves fixed navigation icons plus local selection feedback. Accepted content right-swipes reuse reading recognition and this board's existing eight-band LTR ripple through1120,excluding navigation icons; taps,top-left/physical returns and paging keep ordinary differences. Main tabs honor the selected ripple policy. Reading22 phases,safe192-row prefill,white holding,quick-menu glass,lock,manual full and failure recovery retain their separate paths.

S31充电图标固定黑底白色闪电描边。检测复用CX25601N有效充电阶段，同时按官方BQ27220 `0x0C`有符号电流（大于5mA）补足阶段漏报；明确停止充电/OTG立即清除，通信未知最多保留已确认状态六秒。VBUS类型不作为插电依据，不修改电流、电压或终止参数。

S31 charging uses a fixed black well with a white bolt outline. Detection retains valid CX25601N charge stages and supplements missing stages with the vendor BQ27220 signed-current register0x0C above5mA. Explicit disable/OTG clears immediately; unknown communication can retain confirmed state for at most six seconds. Do not infer input from VBUS type or change electrical charge parameters.

## 准备 SDK

需要 Python 环境、ESP-IDF 6.1 及对应工具链。Pico 使用已有的 ESP32-S3 SDK；Metalio 使用已适配 ESP32-S31 的 SDK，必须包含 `components/soc/esp32s31`。普通只支持 S3 的 SDK 不能编译 S31。

将路径填入环境变量，Python 路径应指向已安装 ESP-IDF Python 依赖的虚拟环境入口：

```sh
export KIIKO_IDF_PICO=/path/to/esp-idf-s3
export KIIKO_IDF_METALIO47=/path/to/esp-idf-s31
export IDF_TOOLS_PATH=/path/to/idf-tools
```

## 一条命令生成两个版本

在源码根目录执行：

```sh
python3 tools/build_firmware.py --board all \
  --python /path/to/idf-python/bin/python \
  --tools "$IDF_TOOLS_PATH"
```

也可以单独选择：

```sh
python3 tools/build_firmware.py --board pico --python /path/to/idf-python/bin/python
python3 tools/build_firmware.py --board metalio47 --python /path/to/idf-python/bin/python
```

`--idf-pico`、`--idf-metalio47` 可代替 SDK 环境变量；`--output` 修改输出目录。离线构建可用 `--managed-pico`、`--managed-metalio47` 指定与各自锁文件匹配的 `managed_components` 缓存。`--dry-run` 显示计划，`--board all --list` 显示支持的设备。工具仅编译和打包，不打开串口。

构建器从同一维护源码生成独立的临时设备视图，分开保存配置、依赖、CMake 缓存、日志及产物：

```text
build/boards/
  pico/{source,build,sdkconfig,build.log}
  metalio47/{source,build,sdkconfig,build.log}
  packages/pico/
  packages/metalio47/
  packages/kiikoread-pico-<版本>-USB.zip
  packages/kiikoread-metalio47-<版本>-USB.zip
```

日常修改应改维护源码 `main/`、`components/` 或相应 `boards/<设备>/src/`，不要修改生成的 `build/boards/<设备>/source/`。共用功能不再分别维护两套应用目录；仅硬件、驱动和有明确平台差异的行为选择分支。

图标由同一个生成器维护：`python3 tools/gen_ui_icons.py --board all` 更新两块板的生成头文件；也可只指定一种设备。保留原图标枚举顺序和 Metalio 的前光图标，生成器不会覆盖共享的图标选择头。

## CMake 选择层

构建器将 `-DKIIKO_BOARD=pico|metalio47`、对应 `IDF_TARGET`、sdkconfig 和依赖锁文件交给各自的 ESP-IDF。生产源码中的条件分支由真实 C 编译器处理。请用上面的构建器隔离依赖锁和 SDK 缓存；不要在维护源码根目录轮换两个 SDK 并共用根 sdkconfig。板型和目标冲突、未知板型会在配置阶段报错。

CI 对两块板各跑一轮构建和宿主测试（`matrix.board: [pico, metalio47]`），`tools/board_layer_host_test.py` 校验 board.json、板级 sdkconfig、分区表、CI 矩阵与本文档互相对得上。板级配置、分区表和依赖锁只保留在 `boards/<设备>/`；仓库根不再有 `sdkconfig.defaults`、`sdkconfig.ci` 或 `partitions*.csv` 副本。

## 交付与验证

打包前校验应用内部芯片 ID、项目名、版本、分区表及两个 OTA 槽的容量，同时校验 bootloader 的芯片 ID 和 SDK 给出的烧录地址。完整 USB 包包含四个镜像、SHA-256、manifest、`flash_args`、`flash-command.txt` 及 `verification.json`；S31 命令保留 `--no-stub`。应用 Bin 只适用于已有正确底包的同板型升级，不能写到地址 0。固件内的升级校验也拒绝另一芯片/项目的应用。

本次整合为本地测试，沿用当前正式版本号区分板型，未覆盖官网、Release 或历史固定版本下载。两套完整构建和主机回归不等同于已在两块实机完成触摸、显示、TF 卡及低功耗验证。

本地同版本测试请使用对应 USB 包；设备的正常 TF/OTA 检查仍会拒绝与当前版本号相同的镜像。

## 回归命令

```sh
python3 tools/board_layer_host_test.py
python3 tools/board_selection_host_test.py
python3 tools/board_selection_host_test.py --build-root build/boards

python3 tools/board_source_view.py --board pico --tests --output build/host/pico
python3 tools/board_source_view.py --board metalio47 --tests --output build/host/metalio47
```

在相应生成的主机测试视图中运行现有测试。Pico 包括 `run_water_turn_host_test.sh`、`run_display_host_test.sh`、`reader_spacing_host_test.py`、`test_book_toc.py`、`lock_pin_host_test.py`；Metalio 还必须运行 `run_metalio_host_test.sh` 和 `run_metalio_ota_host_test.sh`，其专用夹具随测试视图自动选择。

子页布局按1216px可用高度定位：文件目录列表扩展，目录设置/移动按钮与分页贴底，系统输入键盘底部留26px；绘制、点击和局部刷新共用几何。字体列表与书架样式预览使用完整子页视口，传输底部按钮随视口下移。Pico保留原布局。

Child layouts use the1216px viewport: extend file lists, bottom-anchor directory/move actions and paging, and leave26px below system keyboards. Painting,hits and local refresh share geometry. Font lists and shelf-style previews use the child viewport; transfer footer controls move with it. Pico retains its existing layout.

## S31传输与深睡恢复

大于等于四个接收块的上传可使用最多四槽顺序写卡：HTTP只填空槽，单个后台任务按原顺序写入；接收块最大64KiB，最多增加三块PSRAM及4096字节内部任务栈，PSRAM不足回退双槽，再不足沿用单缓冲。任何接收/写入失败先等待已提交任务完成，再关闭、清理或替换文件。只在整个文件落盘关闭成功后确认提交。TCP接收窗口65535字节、接收邮箱64项，静态RX默认6/动态RX64/BA12；PSRAM传书独占会话按S31 SDK建议对照16槽静态RX/BA窗口（最大16槽），普通联网与临时对时保持原6槽。乱序队列最多48包以覆盖65535字节窗口并启用SACK，省电所有权保留；USB MSC块16KiB、TF最高20MHz并按兼容档回退，始终电脑/固件独占挂载。

深睡保留屏幕物理锁图，TF挂载尚未完成时不清白换图；有效快照恢复前后参考不额外扫描，缺失/损坏时仅对可独立生成的真实锁屏作一次完整输出。未知透明底图保留物理画面，禁用差分提示；实际解锁取消所有延迟恢复。PIN继续验证，异常启动字体保护和S31外设休眠顺序保持。入书/目录返回正文使用一次原厂GC48，后续正文22相及图形GL48各走既有路径。

English: Large S31 uploads use up to four ordered64KiB slots,three optional PSRAM blocks and a4096B internal worker stack; PSRAM pressure falls back to two slots,then synchronous IO. Join before close/cleanup/commit on every path. TCP receive65535/mailbox64,default staticRX6/dynamicRX64/BA12 and bounded48-packet SACK. PSRAM bulk sessions compare the SDK16-slot staticRX/BA minimum (capped at16),while ordinary connectivity/time sync retain6; MSC16KiB with a20MHz card-clock ceiling and compatibility fallback and exclusive ownership. Preserve physical deep-lock art while media is pending; trusted snapshots restore references without scanning,unknown transparent art never receives difference hints. Cancel deferred work before unlock,retain PIN/abnormal-font/board sleep guards,and clean reader entry once with vendorGC48 without changing subsequent body or graphical waveforms.

文件传输服务持有BLE网络租约，停止扫描/连接并阻止主循环重启蓝牙；关闭服务后按原设置及最近设备恢复，不清配对/学习。仅连接WiFi的常驻会话不持有该租约，仍可使用蓝牙翻页器。临时扫描/联网的失败必须回滚租约；成功启动后的任何退出都释放一次。`/info`提供固件ELF身份、实际接收预算、BLE运行状态和内部RAM余量，便于核对实机；这些诊断不提供任意命令或存储访问。

English: File-transfer sessions own a BLE radio lease,stop scanning/links and prevent foreground restart until release,retaining user settings and recent peers. Network-only keeps coexistence;temporary-network failures roll back ownership. `/info` reports firmware ELF identity,actual RX budget,BLE and internal heap without arbitrary command/storage access.

## 板级能力与跨平台入口

`boards/<设备>/kiiko_board_config.h` 集中维护身份、NVS 命名空间、硬件能力和缓存预算；`main/kiiko_board_config.h` 是兼容入口。沿用 Moku 的板级门面 `components/kiiko_board`，其初始化不得重新拉起已由 BSP 初始化的外设。`board.cmake` 读取对应 `board.json`，芯片冲突立即报错，不强制改写旧构建目录。

S31 稳定 ID 为 `metalio47`，兼容 Moku 的 `metalio_s31` 构建别名。Windows 加载对应 ESP-IDF 后可运行 `tools/build_board.ps1 -Board metalio_s31`，该入口共用 Python 构建器，分别生成源码视图、配置和依赖锁。`all` 需要分别配置两块板的 SDK 环境变量，参见上面的 Python 参数。

CI 同时编译两块板并验证芯片、项目名、分区、启动地址和应用大小。网页完整性回归使用 `tools/fixtures/flasher` 中的固定脚本及模拟清单，不依赖未提交的官网固件；官网文件和 Pages 部署由本仓库维护。

English: Moku's board manifest/facade design centralizes capabilities and unchanged budgets while keeping native backends and strict target/config guards. `metalio_s31` aliases stable `metalio47`. The PowerShell wrapper shares the Python materializer/build driver. CI builds both chips and validates package identities/layouts; web regressions use self-contained fixtures without publishing a site.

S31 SDMMC沿用Metalio官方内部DMA中转：固定4096字节内部对齐缓冲、最多8扇区，外部RAM不直接进入SD DMA；失败挂载与成功卸载回收，卸载失败保留缓冲和卡所有权。时钟降档、四线引脚、FAT锁及USB独占流程保持。传书每两秒只对文件名/字节/进度条做局部黑白差分，失败刷新重试，网页字节发送完成后继续计时并等待设备保存确认。

S31的标准TF文件访问统一计数，包括打开的文件/目录、路径操作以及传书和在线字体提交事务；卸载先阻止新访问并等待句柄归还，暂忙时保留原挂载。内部`/flash`不参加TF访问计数。VFS关闭失败仍按SDK已回收句柄处理，保留错误返回，避免拔卡后永久阻塞恢复。普通挂载和USB原始卡共用四线/单线配置、内部4096字节DMA及成功档提示；USB最高20MHz，失败继续按原兼容档降频和单线回退。TinyUSB完全停止后才回收原始卡、host和DMA，收尾失败保留所有权供重试。现有健康检查、自动恢复、休眠引脚和阅读波形保持。
English: S31 counts SD VFS files/directories, path operations and upload/font commit transactions. Unmount blocks new access and waits for admitted work; busy transitions retain the mount. Internal `/flash` is independent. Failed VFS closes release accounting because the SDK recycles handles, while errors remain visible. VFS/USB share bus configuration, internal4096B DMA and known-good profiles; USB caps at20MHz and retains lower-clock/single-bit fallback. Raw resources are released only after TinyUSB stops; failed cleanup retains ownership for retry. Health/recovery, sleep pins and reader waveforms retain their existing behavior.


显式本地`--s31-transfer-startup-test`仅用于无人值守对照，启动仍保留PIN/恢复门禁，随后进入已保存网络的传书页。正常构建强制关闭并在manifest/verification记录该开关；此测试包不得交付。

### Metalio 字体恢复与文件名

S31正常待机后继续恢复TF上的字体与书籍句柄；临时读卡/内存失败按1、3、10、30秒间隔重试，成功手动加载可清除旧拒绝记录。已判定无效的字体仍限制自动重试，异常启动仍使用内建字库，不覆盖保存的选择。字体列表最多128款，按实际路径区分文件，标签按完整UTF-8字符截短并区分重名；仅收录`.ttf`和`.otf`，扩展名不代表所有内部格式都支持。原本超过设置路径容量的文件名才会缩短并附校验标识，保留字体内容、避开已有文件。字体上传自动规范化不适合FAT的符号，其他上传类型的路径检查保持原样。

S31 resumes pending SD font/book handles after normal standby. Transient I/O/OOM failures retry after1/3/10/30seconds; a successful manual load clears old rejection. Invalid faces still limit automatic retries, and abnormal-start protection retains built-in fonts without changing saved choices. The128-entry catalogue identifies actual paths, keeps UTF-8 labels valid and distinguishes duplicates. Only `.ttf`/`.otf` are listed; extensions do not guarantee support for every outline format. Only names exceeding persisted-path capacity are shortened with a hash, retaining font bytes and avoiding existing files. Font uploads normalize FAT-incompatible symbols; other upload path checks remain unchanged.

### S31 DU14 local experiment / S31十四相本地实验

`tools/build_firmware.py --board metalio47 --s31-du14` opts into the user-supplied manufacturer DU14 table; it is OFF by default and rejected for Pico. The original header is preserved at `boards/metalio47/src/components/e0470_epaper_waveform/my_waveform_du14.h` (SHA256 `cac0efbcb2801114b36a4bee5eaa9c453dcd7d098210ef2739f853e86a0c39f4`). No redistribution license has been inferred from its delivery. Package metadata explicitly records `experimental_du14`.

仅替换速读黑白、主页黑白及黑白页刷访问器，复用原扫描时序和脏区。14相原表不加前清屏或额外相位；黑到黑末4相补黑、白到白保持。底栏灰阶组合显式取原20相；普通阅读、图片/封面、GL37/48、4灰面板、8相跟手和手动/周期/故障全刷不改变。灰阶目标不得直接用DU14替代。实际黑度、残影、温度及速度需要实屏比较。
Only fast binary reading and binary main/page accessors switch, using existing scan timing/dirty regions. DU14 receives no preclear or added phases; it reinforces black-to-black in the last4 phases and holds white-to-white. Gray footer synthesis explicitly retains reference DU20. Ordinary reading, images/covers, GL37/48, four-gray panels, eight-phase tracking and manual/periodic/fault cleanup remain unchanged. Do not substitute DU14 for grayscale targets; optical density, ghosting, temperature and latency require panel comparison.

Run `python3 tools/metalio_du14_host_test.py` in the S31 source/test view to verify raw transitions, accessor isolation and byte-identical unaffected profiles. This is a local experiment, with no firmware version bump or publication implied.

DU14 white maintenance: homepage Fast mode uses a derived14-phase profile only during real root-tab switches; the Fast BW reader shares its LUT through a GL16 mode alias. The last3 existing phases drive white-to-white toward white, and all other equal pairs hold. Changed transitions keep the supplied sequence; no raw tables, timing or total phase count change. Root maintenance covers unchanged rows/columns in one existing content-area batch, cropped above navigation icons. Same-page/child/ordinary/water/cleanup paths remain unchanged. The derived table costs896 bytes once, with no per-switch allocation; panel optics still need testing.
十四相白底维护仅用于首页快刷真实切页及速读黑白正文；末三相补推等白，其他等值保持，变化像素保留厂家序列；一轮内容区输出覆盖未变行列，保护底栏图标，仍14相。不增加每次切换分配。新增验证：`python3 tools/metalio_du14_white_host_test.py`。

## TXT分卷目录 / TXT volume navigation

两机共用TXT分卷识别和目录呈现。明确的卷/部/册及英文Part标题优先；没有卷名时，前章号至少5、当前回到1并在后续最多8个导航项内连续确认2/3，才推断新卷，不能按章号去重或排序。总结/感言/卷终保留为上一卷内容，缺少可信卷名显示自动编号。目录只展开一卷，进入时展开并定位当前章节；点击卷名仅展开/折叠，正文跳转仍使用原目录项与文件偏移。EPUB沿用自身目录，不从其正文推断TXT卷。

最多256组，仅实际分卷时分配2KiB定位元数据；已有最多16384项/128KiB章节定位页和八条按需标题缓存不变。卷表超限或分配失败回退完整平铺目录，不缓存不完整结果。TXT缓存版本5校验卷范围、数量及校验和，更新时可自动重建缓存，不改旧2048阅读分段、进度/书签编号或TXT文件。首次扫描耗时仍受文件/SD影响。

English: Both boards share TXT grouping and its single-open-volume directory. Prefer explicit volume/part headings. Infer a missing boundary only after a previous ordinal of at least5, a reset to1 and confirmed2/3 within eight following navigation items. Keep notes with the old volume and retain original order/positions. EPUB retains its own navigation. Allocate at most2KiB for256groups only when needed; retain existing paged chapter locations and lazy titles. On OOM/cap exhaustion show the flat directory and avoid incomplete caches. Cachev5 validates ranges/count/checksum and rebuilds without changing legacy progress/bookmarks or source files.

分卷目录底部“选择卷”进入仅显示卷名的一级目录，使用原分页/滑动，每页最多7卷；选择后展开对应卷并定位卷行，不直接加载正文。“返回章节”及返回键取消选择并恢复原目录页。再次进入目录恢复按当前阅读位置定位。选择器仅保留开关和返回页，无额外章节/标题数组，未分卷书不显示该入口。

English: The bottom volume button opens a root-only list using the existing seven-row paging/swipes. Choosing a volume expands and locates its header without loading text. Cancel/back restores the old directory page; normal entry still locates the reading position. The picker stores only a flag/return page, with no extra chapter/title arrays; flat books hide it.


### S31 主页面切换加强擦白 / Stronger S31 root-switch erasure

显式本地参数 `--s31-main-clean` 启用，默认关闭，Pico 单板构建拒绝此参数。清单记录 `experimental_main_clean`。只在首页、书架、文件管理、设置之间真正换页且画面改变时生效；同页控件、阅读正文、图片浏览、面板/菜单、锁屏、手动/周期/故障全刷保留原出口。

切换来源或目标为书架时增加八相变白，其余根页之间增加六相；追加两相全保持释放。保留S31原派生表的完整序列、灰阶、等值维护及DU14等白三相，不移植Pico波形。书架→首页原下半区加强先组合再派生；六/八相缓存按力度区分，已派生输入回到原源表，不重复叠加。底栏范围、横条同轮输出和水波纹方向保留，不增加前清屏或第二轮补刷。

DU14根页维护分别为22/24相；完整灰阶的既有50相根页分别为58/60相，书架→首页下半区最多62相；GL37书封的既有39相变为47/49相。均在本机64相引擎上限内。相较原“四相变白、两相释放”再增加2/4次扫描；总延迟和残影需实屏比较。阅读速读、普通/图文及水波纹保持原各自方案。

根页最多24份、每份不超过62×64字节，合计最多95232字节PSRAM及少量元数据；不新增整帧或逐次分配。缓存满、分配失败或源不兼容时保留原表，不缓存失败。原厂表、电压、时钟和供电顺序不变。

设置/蓝牙列表的边框、分隔线和图标改明确黑白，文字抗锯齿和头像保留灰阶。列表滚动独立采用本机完整48相灰阶加八相变白、两相释放，共58相；只刷新列表范围，题头/底栏不参与，松手和空闲不追加补刷。独立缓存一份3712字节PSRAM，失败回退本机原灰阶；增加扫描用于清残影，会比原DU列表慢。`grayscale_ui`是S31的只读当前子视图查询，避免列表或预览被系统二值化；进页不叠加列表尾段，根页补白仍由六/八相许可负责，手动/故障全刷优先。系统灰阶水波纹使用主页表，不进入阅读文字量化。

English: Explicit local S31 root scope chooses eight changed-white phases when either side is the shelf, six otherwise, then two release holds. Preserve all native actions, existing equal-white maintenance, lower-body reinforcement, navigation crops and ripple direction. Strength-keyed caches rebase derived inputs instead of stacking. DU14 totals22/24, full-gray roots58/60 and shelf-to-home lower62, within the native64-phase cap. Compared with the earlier four-white/two-release profile this adds two/four scans; optics/latency still need panel acceptance. Bound24 root LUTs to95232B external memory, retaining safe fallback without changing native tables, timing or power. Settings/BLE decoration is BW while glyph/avatar grays remain. Scrolling independently uses native48 plus eight changed-white/two release phases (58) in one cropped output, with no release/idle pass. Its single3712B external LUT retries after OOM. The S31-only pure `grayscale_ui` query admits gray list/preview rendering; entry retains root scope without stacking list tails, and full/fault recovery wins. Gray system ripple uses main LUTs outside reader quantization. Reading modes remain independent.

### 未配置时的外观默认值 / Unconfigured appearance defaults

两机共用产品默认：液态玻璃书架（样式6）、Moon Clock（组件4）、首页强刷开启。只有缺失或无效设置才使用默认；保留已保存的有效样式/组件，以及明确关闭首页强刷的选择。旧书架迁移标记仅补齐记录，不再强制把已保存的样式改为亚克力；已删除的3/4样式仍按既有规则回退2。刷机保留数据时不会清除已有选择；旧配置备份恢复继续按其记录执行。

English: Both boards default to liquid-glass shelf6, Moon Clock4 and Home full refresh ON only when unconfigured/invalid. Preserve valid saved choices and explicit OFF, including across retained-data flashing and backup restore. The legacy shelf marker no longer overwrites valid styles; retired3/4 still map to2.

### Pico 主页面擦白实验 / Pico root-whitening experiment

`--pico-main-clean` 单独启用，默认OFF，S31单板命令拒绝；与S31的`--s31-main-clean`分开配置。两机清单均记录各自的`experimental_main_clean`。只在四个可见根页之间真正切换时选择，首次进入、同页更新、阅读/子页/菜单/锁屏、首页强刷和手动/周期/失败全刷保留原出口。

Pico派生其本机DU20/书架出口24/GL37：切换来源或目标是书架时补八相，其余根页之间补六相。保留原非等值动作，将补白插在原末尾保持之前；没有末尾保持的源再补一相释放。灰阶目标不量化，等值像素全部保持。GL37分别合43/45相，DU20合26/28相；书架退出原24相加8相及1保持为33相。书架内翻页、列表滚动和正文保留各自出口，不继承根页切换许可。

条带引擎上限45相，8带紧凑切换分别50/52拍；阅读水波纹仍41相。扫描节拍、电压/供电、底栏裁剪和方向不变，不采用S31数据。普通/水波纹根页合并底部指示条，快刷灰阶带保留；补白在同轮输出，无新增全刷轮次。六个源表各缓存六/八两种力度，最多12×45×64=34560字节PSRAM加元数据，条带展开缓存最多45KiB；不新增整帧。OOM、缓存满或不兼容源回退原表，重复调用及力度切换都不叠加相数。设置列表独立45相（补八相），首页强刷和手动/周期/故障全刷优先，光学改善与实际延迟仍需实屏比较。

English: Explicit local `--pico-main-clean` remains OFF by default and scoped to changed visible root tabs. Either source or destination being the shelf selects eight changed-white phases; other root pairs select six. Retain native changed-pixel actions and settling, hold all equal pairs, and add one release only when a source has no settling. GL37 totals43/45, DU20 totals26/28, and the24-phase shelf exit totals33 with eight drives and one release. Shelf pagination, list scrolling and reading retain independent outlets. The band cap is45 (50/52 compact ticks over eight bands); reader ripple remains41. Preserve timing, voltage/power, navigation and direction; never import S31 LUTs. Two strengths per six sources use at most34560B external LUT data plus metadata, and the band expansion uses at most45KiB; no extra frame or clear pass. Cache keys include strength, derived inputs rebase instead of stacking, and OOM/capacity/incompatibility retain the source. Settings lists keep their own45-phase profile and full/manual/periodic/fault outputs retain priority. Hardware acceptance is still required.

## S31阅读差分更新（2026-10-29） / S31 reader differences

用户2026-11-12决定：S31 产品/默认构建不再启用`--s31-pie`、`--s31-text-ripple`、`--s31-reader-panel37`，恢复原生48相阅读翻页与C像素算子——rc111 起这些实验默认开启，实机反馈翻页效果明显变差；这些实验改为仅显式本地开关，且只影响 S31，Pico 保持不变。单独显式加`--s31-reader-panel37`时仍按 Moku PR6 的 GL37 加六相目标白，共43相、16级灰阶；六相包含原本已白的区域，黑/灰等值不重驱。水波纹继续原独立出口；手动及周期清理保留GC48。GL37关闭或分配失败时，完整GL48保留全部非等值迁移，追加六相目标白和三相保持，共57相，避免回到重驱等黑/灰的原表。

速读黑白独立量化目标，使用本板所选DU完整相序再加四相目标白；原表有尾部保持时将四相插在其前。默认DU20共24相，带DU14实验的本地构建共18相；不改首页DU14白底维护、原表数据、VCOM、供电或扫描时钟。实际光学速度、白底闪动和残影需实屏比较；相数并非总响应时间。

User decision 2026-11-12: S31 product/default builds no longer enable `--s31-pie`, `--s31-text-ripple` or `--s31-reader-panel37`; restore the native48-phase reader turn and C pixel kernels because these experiments defaulted on from rc111 and visibly degraded page turns on hardware. They remain explicit local opt-ins and affect S31 only; Pico is unchanged. Passing `--s31-reader-panel37` explicitly alone still enables Moku PR6's exact GL37 plus six white-target phases:43 phases,16 grays,including equal whites while equal black/grays hold. Ripple keeps its own outlet and manual/periodic cleanup keeps GC48. With GL37 disabled or allocation failure, retain every changed GL48 transition plus six whites and three release holds,57 phases,without re-driving equal black/grays.

FastBW independently quantizes targets and retains the selected native DU sequence plus four white-target phases,inserted before existing trailing holds. NativeDU20 totals24; a local DU14 experiment totals18. Root DU14 maintenance,vendor tables,VCOM,power and clocks stay separate. Optical timing/flash/ghosting still require hardware comparison; phase count is not total latency.


## 当前功能层 / Current feature layer

两板共用首次引导、系统中英切换、文件递归搜索/图标视图/批量操作、书架排序与独立全屏/沉浸状态。引导及文件选择使用自己的底部操作栏，`navigation_visible` 显式决定系统导航是否占位；引导只在完成/跳过成功持久化后退出。引导通过本板 U 盘或 Wi-Fi 服务导入字体，不提供在线下载；设置保留独立在线目录。
Both boards share setup, system localization, recursive file search/grid/batch actions, shelf sorting and independent fullscreen/immersive state. Setup and selection own their bottom action bars; `navigation_visible` controls system-navigation space. Setup exits only after a successful Finish/Skip commit and imports fonts through the board's USB/Wi-Fi service; Settings retains its separate online catalog.

阅读UI决定正文和信息栏范围；调用PR59已有差分/整块出口及原阅读波形，空闲后独立更新底部信息。不重新引入旧的d74b180驱动或本地缓存保护。`tools/pr59_driver_baseline_host_test.py`核验216个底层、配置及波形文件与用户指定PR提交一致。
Reader UI owns body/metadata bounds and calls PR59's existing difference/full-area APIs and reader waveforms, updating footer metadata after idle. Do not reintroduce the d74b180 drivers or the superseded local cache guard. The driver-baseline host test verifies216 low-level/config/waveform files against the selected PR commit.

阅读触屏规则（2026-11-02）：非全屏中间单击进入全屏；全屏后所有正文区域按所选左右/上下分区翻页。图片单击延后340ms以识别双击；非全屏中间图片的单击进入全屏，其他未放大图片单击按分区翻页。双击正文或整页图片放大/退出，已放大图片单击不翻页。底部上滑仍回到非全屏并共同显示标题操作行与工具栏；弹窗/工具栏优先于图片点按。
Reader taps2026-11-02: standard centre taps enter fullscreen; fullscreen body taps follow the selected horizontal/vertical regions. Image singles wait340ms for double-tap zoom; standard centre image singles enter fullscreen and other unzoomed singles turn. Zoomed singles do not turn. Bottom swipes restore standard header/tools together; modal/tool controls take priority over image taps.


## S31保持浅睡与字体恢复

关机睡眠提供“浅睡10分钟后关机”“彻底断电”“保持浅睡”三项。保持浅睡不设置锁屏自动断电截止；定时维护仍维持现有浅睡及TF/屏幕停放，真实按键与PIN规则保持。充电/拔线不会给保持浅睡补造断电截止，电源菜单手动关机仍直接关机。既有shutdown_mode键以0/1兼容旧选项，2表示保持浅睡；便携备份沿用已有sleep字段，无格式变更。设置写入失败保留当前策略。

唤醒IO未恢复时保留字体槽和原选择，不把“暂缓切换”当加载失败而卸载。已验证的唤醒/挂卡会话清除旧拒绝记录；解析、I/O或内存失败按1/3/10/30秒退避，多次持续失败后每5分钟复查，避免永久拒绝或频繁重载。异常启动保护和不覆盖已保存字体选择的规则保持。

English: S31 lock power offers timed10-minute off,immediate off and keep-light. Keep-light has no automatic power-off deadline,including after cable changes; hardware light-sleep maintenance,physical wake/PIN and manual power-off retain existing paths. The existing shutdown_mode key retains0/1 and adds2; portable backups use the existing sleep field without a format change. Failed saves retain live policy. Pending wake font slots survive deferred activation. Verified wake/mount sessions clear old rejection; all load failures retry after1/3/10/30seconds,then every5minutes for persistent faults. Abnormal-boot protection and saved choices remain intact. Transfer performance requires device/network measurements; diagnostic queue/receive/write times and selected SD profiles are available in the existing info endpoint.

Pico开机书架扫描保留原目录递归与首屏分组封面；每64本共用一次只读NVS进度会话，约2.5KiB PSRAM工作区不足或打开失败时回到逐本读取。目录已核验的普通文件大小/修改时间用于同版EPUB元数据缓存键，减少重复stat，大小/时间/版本失配仍拒绝；明确为普通非图书文件的目录项提前跳过stat，未知类型仍核验。加入时间索引采用有界16KiB stdio缓冲，不改记录、排序、封面缓存版本或硬件参数。Pico的shelf scan日志分列目录/进度/加入索引耗时，实际整机增幅须实测。
Pico cold shelves keep recursive discovery and grouped covers. Up to64 progress queries share a read-only NVS session; bounded scratch/OOM/session failures fall back per book. Reuse directory-validated size/mtime for unchanged EPUB metadata keys, retaining mismatch rejection; known regular non-books skip redundant stat while unknown types remain checked. The first-added journal uses bounded16KiB stdio buffering without format/order/cache-version/hardware changes. Pico scan logs separate directory, progress and journal costs; device throughput still requires measurement.
