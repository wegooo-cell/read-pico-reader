/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 图书书架、阅读、目录与页内进度；文件解析和排版由book模块负责。
 * Book shelf, reader, TOC and progress; book modules own parsing and pagination.
 *
 * 用户修订：阅读设置采用已确认的半屏双列排版；自动全刷与翻页效果点按轮换，保留按键控制二级页。
 * User revision: use the approved compact two-column reading sheet; tap cycles refresh/effect and key controls retain their secondary page.
 * 冻结：Phase4b统一手势入口并接管三键为上页/工具条/下页；工具条保留强刷。屏幕翻页在抬起提交，不画按下态。
 * 用户修订：图文混排原样采用 PR9 的 book_layout；调用侧只适配图片缓存与上下文生命周期。
 * 晃动默认关；横向左晃上一页、右晃下一页，触摸与回弹不触发；离页恢复加速度配置并休眠。render只绘图。
 * 预渲染与同步推屏重叠、按行可停；推屏返回前交还字体与章节资源。
 * 普通翻页只刷新正文与页脚；手动或周期清残影整屏全刷。
 * 用户修订：阅读手动与周期全刷恢复 rc90 的原表 GC16 目标页输出，不先擦白再补文字。
 * User revision: restore rc90's original-table GC16 target-page output for manual and periodic refresh, without a white pre-clear and text pass.
 * 用户2026-10-21修订：Pico进入阅读页单次完整GC16清理；普通文字翻页保留校准GL16前导擦除及等灰保持，沿用预读与单轮合并进度。
 * User revision 2026-10-21: Pico reader entry uses one complete GC16; ordinary text retains calibrated GL16 erasure and equal-gray holds, prefetch and one fused progress output.
 * 用户修订：正文中间双击固定切换全屏，单击左右各半或上1/3下2/3翻页，不受按键配置影响。
 * 三键单击及中键长按可自定义，提供翻页/工具栏及首页/全屏/工具栏两套推荐；中键长按默认返回主页，阅读页不画右下角菜单图标。
 * 用户授权基础管理：长按书架先看完整详情，清进度与删文件分别确认；失败保留待重试记录，不自动回收其他书进度。
 * 用户修订：单本管理为书架弹窗；管理页用于批量操作。分页和排序保留勾选，筛选/应用搜索及重扫清除勾选。
 * 失败进度仅按变更路径失效；删除后的清理重试保留到本次开机结束，不随切页释放。
 * 卡失效时先保存进度并关闭阅读资源，再由主循环回退字体；禁止自动续读失效挂载。
 * 用户最新修订：书架只保留深色书轨与亚克力，每页九本；收录导入及读过的书，移出仅隐藏，再读重新上架。
 * 底栏保留首页/书架/文件/设置，设置直接进入设置页。
 * 用户修订：长按图书可用本机拼音输入编辑书名；阅读时长与翻页真实记录，供票根锁屏使用。
 * 用户最新修订：书名编辑可点选插入位置并用左右键微调，支持在文字中间插入和删除。
 * 用户授权新输入法：书名和搜索共用九宫格/全键盘、中英文及离线词语候选，不改正文排版。
 * 用户修订：首页、书架、文件、设置四栏导航；切换界面采用 GL16，章节首页单独排标题。
 * 用户修订：EPUB 章节首页以书内目录标题为准，正文题头仅在相同时折叠，避免引言误判。
 * 用户2026-10-23确认：阅读统计内增加本章页码跳转，减/加逐页预选、数字键盘确认，跳转后短暂提供返回原位置。
 * User approval 2026-10-23: chapter page jumping in reading statistics, stepwise drafts and numeric confirmation, with a short-lived return anchor.
 * 用户修订：阅读进度条无外伸刻度并使用圆角；继续阅读区显示最近书籍封面。
 * 用户修订：书架整页读取成品缓存后一次显示；首次生成有界分批处理，但整页封面一起显现，不逐本推屏。
 * User revision: read the page's finished caches before its first frame; cold generation is bounded, but cover artwork is revealed together.
 * 用户修订：字号和间距重排只保存最终进度，字号使用现有闲置灰阶整理。
 * 用户修订：阅读设置滑杆可拖动并在松手后重排；字体选择可纵向翻页；统计入口显示真实明细与近30天数据。
 * 用户修复：间距滑杆点按和拖动共用完整档位，字间距每次1px，段距按显示档位递进。
 * User fix: spacing taps and drags share the full ladders; tracking steps by 1px and paragraph spacing follows the displayed stops.
 * 用户修订：前后均为纯文字的普通翻页采用柔和 GL16 差分，仅白底灰边提前少量回浅，不改字形或灰度目标；插图、混排、周期全刷及水波纹保留原规则。
 * User revision: soften ordinary pure-text GL16 turns by advancing a few white-to-gray settling pulses; preserve glyph targets, images, mixed pages, cleanup and ripple.
 * 用户修订：目录由独立模块整页绘制与命中；目录标题清理换行并限制为单行，翻页不再沿用书架的局部刷新。
 * 用户修订：书架封面抽出与取消仅驱动变化像素，保持灰阶，不在点按时强制清屏。
 * 用户修订：列表封面上弹8px，松手先回位再开书；只重画两位置的并集，复用缓存，不触碰文字/边框/分隔线。快刷用跟随DU，其他模式保留灰阶差分。
 * User revision: list covers bounce up 8px and settle before opening; repaint only the position union using cached artwork, preserving text, frames and separators. Fast uses FOLLOW DU; other modes retain gray differentials.
 * 用户确认：列表封面缩至120×156，保留原中心点与右侧文字位置，等比填充并居中裁切。
 * User approval: list covers use 120×156 boxes, retaining their center and right-side text positions with aspect fill and centered cropping.
 * 用户修订：快刷亚克力采用规则一像素棋盘格，与已缓存封面合并输出，不注册灰区或等待框架先显示；未缓存封面继续有界延后解码。普通及水波纹保留灰阶，切页/锁屏取消旧目标。
 * 用户修订：书架读取最多当前整页缓存；未缓存封面限时准备，整组显现后才确认待刷标记，文件间检查导航，不新增整页缓存。
 * 用户修订：阅读背景先列pictures图片、忽略隐藏文件，点开预览上下切换，返回目录；背景按整屏坐标固定，不随文字边距或全屏状态缩放。
 * User revision: fast acrylic uses a regular one-pixel checkerboard in the same output as cached covers, without gray registration or a furniture-first wait; uncached covers retain bounded lazy decoding. Ordinary/water retain grays; page/lock changes cancel stale targets.
 * 用户要求采用PR17的快速水波纹，并最新指定8条带、每拍11090us；每次显式设置，结束恢复先前节拍，避免影响主页动画。全刷、插图及失败恢复保留原优先级。
 * User requests PR17 fast ripple with the latest 8 bands at 11090us; explicitly select pacing per turn and restore it afterward so main animations remain independent. Retain cleanup, image and recovery priority.
 * 用户2026-10-22新增Pico可选速读黑白：仅连续纯文字、无背景用本板原厂DU；正文/页脚一次输出，原全刷设置优先。
 * User adds optional Pico Fast BW: native DU for consecutive text without wallpaper, fused body/footer and the existing cleanup preference taking priority.
 * 用户2026-10-20授权S31独立文字水波纹实验包：纯文字水波纹可选四级/22相/8带并合并尾带，合并页脚且最多十页GC48。
 * 用户最新指定本地普通文字也用与水波纹相同的四级22相，同步整页；速读黑白保留DU20。
 * Latest user request: local ordinary text shares ripple's four-level22 phases in one synchronous output; FastBW retainsDU20.
 * 两个短灰阶路径均最多十页GC48；未启用实验、缺相或本次开机扫描失败时回到完整GL48，图片/背景与手动全刷保持。
 * Both short-gray paths useGC48 within ten turns; builds without the experiment,missing phases or boot-local scan failures retain fullGL48, as do images/backgrounds and manual cleanup.
 * User authorizes a separate S31 text-ripple experiment on2026-10-20: four target levels/22 phases/eight bands with a paired tail and fused footer and GC48 within ten turns. Regular builds, main pages, images and manual cleanup retain their paths.
 * 用户随后要求借鉴原厂普通阅读的干净过渡：普通字缘仍保留16级，仅跳过本次所有像素都保持的前导相，不裁有效脉冲。
 * The user then requests clean vendor-like ordinary turns: retain16-level text targets, omitting only actual all-hold leading phases without trimming active pulses.
 * 用户增加管理/导入按压态：复用现有抬起确认流程，按下灰底加深并下沉2px，只推按钮区域。
 * User adds management/import feedback: retain release-confirmed actions, darken and depress controls 2px on press, presenting only their region.
 * 用户修订：快刷封面保留轻量减黑并按最新要求略增黑点密度，在PSRAM缓存最多九张1位缩略图（约40KiB）；进度未改变时不重复丢弃封面。缓存只画封面，收藏与界面文字实时绘制，开书或卡失效时释放。
 * Frozen: Phase4b uses the shared gesture entry and owns previous/tools/next keys; the toolbar keeps full refresh. Screen turns commit on release without pressed decoration.
 * User revision: adopt PR9's book_layout unchanged; callers only adapt bitmap caching and context lifetimes.
 * Authorized keyboard revision: titles and search share T9/QWERTY, bilingual offline phrase candidates; body layout stays intact.
 * 用户修订：输入不重画题头、封面或正文，只刷新输入变化区域，退出后释放查字缓存。
 * 用户修订：新开书默认全屏；双击中间切换，翻页关闭工具栏并回全屏；重排以已选目标页的文字位置为锚，不跳过未读文字。
 * User revision: new books open full screen; center double taps toggle it, turns dismiss tools and return to full screen; reflow anchors the selected destination before changing geometry so unread text is not skipped.
 * 用户修订：字重用横向减/加胶囊，每次微调1/16像素覆盖率；保持字形比例/字宽，不加载额外整份字库。
 * User revision: a horizontal minus/plus pill adjusts coverage by 1/16 pixel per tap, preserving advances/proportions without another font.
 * 用户确认：字体/字重与书内字体/排版分别并排，移除阅读背景入口；字号和阅读线继续可调。
 * User approval: pair face/weight and embedded-face/typography, removing the background entry while keeping size and reading rules adjustable.
 * 用户增加阅读抗锯齿开关，默认开；书架整页封面合并显示；正文底部上滑固定呼出工具栏，不受翻页/按键设置影响。
 * User adds default-on reading AA and grouped shelf artwork; upward swipes from the body bottom always open tools independently of tap/key modes.
 * 用户修订：底部上滑固定呼出工具栏，不随按键及翻页模式变化；取消短按或长按必须保留工具栏的限制。
 * User revision: bottom-edge swipes always open tools independently of key/tap modes; short and hold actions no longer require a toolbar mapping.
 * User revision: input refreshes changed regions without repainting headers, covers or body; release lookup caches on exit.
 * Shake is off by default: left/right impulses turn back/forward, suppressing touch and rebound; exit restores the sensor and sleeps it. Render only paints.
 * Prefetch overlaps synchronous output and stops between rows; no font/chapter resources survive presentation.
 * Ordinary turns refresh only body and footer; manual and periodic ghost cleanup refresh the entire screen.
 * User revision: center body double taps always toggle full screen; single taps use halves or upper 1/3/lower 2/3 independently of key mappings.
 * Short key actions are configurable with two presets; middle holds are configurable and default to home. The reader has no bottom-right menu icon.
 * User-authorized management shows full details before separate clear/delete confirmations; retain failed saves for retry without pruning other books.
 * User revision: single-book actions use a shelf dialog; full management is for batches. Paging/sorting preserve selection; filtering/applied search and rescanning clear it.
 * Invalidate failed progress only for changed paths; retain deletion cleanup retries across page exits for this boot.
 * Lost media saves progress and closes reader resources before global font fallback; never auto-resume an invalid mount.
 * Latest user revision: the two retained shelf styles hold nine imported or read books; retired styles restore as acrylic, and removal hides until reread.
 * The fourth tab opens Settings directly.
 * User revision: book details lead to an on-device Pinyin title editor; measured reading time and turns feed the ticket lock face.
 * Latest user revision: the title editor can place and move an insertion caret for edits in the middle of text.
 * 用户统一修订：书名与搜索采用共用常亮光标；点文字或箭头定位，按系统字体测量。
 * User-wide revision: title/search share a steady caret, text/arrow positioning and system-font measurement.
 * User revision: home, shelf, files and settings have four-tab navigation; view changes use GL16 and chapter starts have a title lead.
 * User revision: EPUB chapter leads use navigation titles; body headings are folded only when matching, preventing front matter from being mislabeled.
 * User revision: the reader bar is rounded without protruding ticks; continue reading displays the latest cover.
 * User revision: the shelf retains its title layout, reads a complete cached page before painting, and reveals cold artwork only after the page is prepared.
 * User revision: size and spacing reflow saves final progress only; size uses the existing idle grayscale settle.
 * User revision: reader sliders drag and reflow on release; font selection pages vertically; statistics entries show real details and recent-30-day data.
 * User revision: pure-text turns use softened gray-edge differential GL16; images/mixed pages, periodic cleanup and water turns retain their policies.
 * User revision: a standalone module owns full-page TOC rendering and hit testing; normalized single-line titles cannot leak into another row.
 * User revision: cover lift and cancellation drive changed pixels in grayscale, without forced cleanup during a tap.
 * 用户修订：快刷仅常驻黑白缩略图；灰阶解码缓冲转好即释放，灰阶模式按需重建，收藏白边单独记录。
 * User revision: fast mode retains only packed monochrome thumbnails, frees gray decoding buffers immediately after conversion, rebuilds gray on demand, and stores favorite-edge contrast separately.
 * User revision: fast covers retain a light white lift with slightly denser blacks per the latest request and cache at most nine 1-bit thumbnails in PSRAM (about 40 KiB); unchanged progress no longer discards covers. Cache artwork only, draw favorites and UI text live, and release on book opening or media loss.
 * 用户最新修订：阅读书名与章节标题复用当前阅读字体，以原32/48px直接光栅化；首页另用其活动系统字体，不同时加载两份字库。
 * Latest user revision: book/chapter titles reuse the current reader face at the original 32/48px; Home uses its active system face, never retaining both fonts.
 * 用户确认：题头下拉选择 books 子目录，各类别/布局记住页码；去掉题头横线，主体在确认位置基础上下移8px。
 * User approval: choose books subfolders in the title menu, retain per-category/layout pages, remove the header rule and lower the approved body by 8px.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "app.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "app_registry.h"
#include "ble_page_turner.h"
#include "book_layout.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "book_lock_collage.h"
#include "book_epub.h"
#include "book_policy.h"
#include "book_file_path.h"
#include "book_progress.h"
#include "book_font_size.h"
#include "book_title.h"
#include "book_ticket.h"
#include "sleep.h"
#include "book_excerpt.h"
#include "book_toc.h"
#include "app_font_context.h"
#include "book_source.h"
#include "book_store.h"
#include "book_shelf_categories.h"
#include "display.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "read_pico_search.h"
#include "settings.h"
#include "book/book_reader_input.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_image_dither.h"
#include "ui_reader_background.h"
#include "ui_gesture.h"
#include "continuous_du.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_click_feedback.h"
#include "app_transfer_mode.h"

// 阅读工具栏图标盒边长；行内图标在 1096..1216 的工具条里以 1138 为中心。
// Reader toolbar icon box; the row centres on y=1138 inside the 1096..1216 bar.
#define READER_TOOL_ICON_PX 40
// 二级面板返回键里的箭头边长；圆底半径 24，原来的箭头是 20 像素高。
// Chevron box inside a secondary panel's back control; the old arrow was 20 px tall.
#define READER_SHEET_BACK_PX 52

#define BOOK_ROWS 13
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
#define BOOK_PX_STEP 1
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define BOOK_MARGIN_CHOICES 9
#define BOOK_TOOL_COUNT 5
#define BOOK_FONT_PAGE 9
#define BOOKMARK_MAX 24
#define BOOKMARK_ROWS 6

typedef enum { SHELF, READING, TOC, MANAGE, BULK, IMPORT, SEARCH, EDIT } book_view_t;
typedef enum {
    READER_PANEL_NONE,
    READER_PANEL_TOOLS,
    READER_PANEL_FONT_SETTINGS,
    READER_PANEL_LAYOUT_SETTINGS,
    READER_PANEL_WEIGHT_SETTINGS,
    READER_PANEL_RULE_SETTINGS,
    READER_PANEL_FONT_PICKER,
    READER_PANEL_BACKGROUND,
    READER_PANEL_BACKGROUND_PREVIEW,
    READER_PANEL_STATS,
    READER_PANEL_CHAPTER_JUMP,
    READER_PANEL_BOOKMARKS,
    READER_PANEL_STATS_RECENT,
    READER_PANEL_REFRESH_SETTINGS,
    READER_PANEL_TURN_SETTINGS,
    READER_PANEL_KEY_SETTINGS,
    READER_PANEL_KEY_PICKER,
    READER_PANEL_FONT_RECOVERY,
} reader_panel_t;
typedef struct {
    char name[256];
    char author[128];
    char path[BOOK_STORE_PATH_MAX];
    uint32_t size;
    uint16_t chapter;
    bool is_flash;
    bool has_progress;
    uint8_t pct;
    uint32_t recent;
    bool selected, removed, search_match, favorite;
} shelf_entry_t;

static const char* TAG = "book";
static book_view_t s_view;
static int s_presented_view = -1;
static bool s_reader_image_refresh_pending;
static bool s_reader_turn_pending;
// 只记录成功推屏的纯正文基准，避免跨章或退出插图时误用文字波形。
// Track only successfully presented text-only bodies, protecting chapter and image transitions.
static bool s_reader_text_frame;
static char s_requested_open[BOOK_STORE_PATH_MAX];
static bool s_requested_open_home;
static bool s_requested_fullscreen = true;
static bool s_reader_return_home;
static bool s_requested_manage;
static shelf_entry_t* s_shelf;
static struct {
    int index;
    uint8_t *gray, *fast_bits;
    unsigned width, height;
    uint8_t fast_contrast, fast_phase;
    bool fast_white_edge;
} s_covers[BOOK_ROWS];
static unsigned s_cover_pending_mask, s_cover_decode_mask, s_cover_warm_mask;
static unsigned s_cover_dirty_mask, s_cover_present_mask;
static int64_t s_cover_warm_after_ms;
static size_t s_shelf_capacity;
static int s_count, s_visible_count, s_filter;
static bool s_recent_sort;
// 目录记录有界增长；同一开机周期按完整路径保存两种布局的页码。
// Bounded folder records retain pages by full path for the two layouts during this boot.
#define SHELF_CATEGORY_LIMIT 128
#define SHELF_CATEGORY_ROWS 7
typedef struct { char path[BOOK_STORE_PATH_MAX]; int pages[2], count; bool available; } shelf_category_t;
static shelf_category_t *s_categories;
static int s_category_count, s_category_capacity, s_category_selected, s_category_menu_page;
static bool s_category_menu;
static char s_category_root[BOOK_STORE_PATH_MAX];
static void shelf_category_init(void);
static void shelf_category_remember(app_ctx_t *ctx);
static void shelf_category_restore(app_ctx_t *ctx);
static void shelf_categories_recount(void);
static void draw_category_menu(uint8_t *fb);
static const char *shelf_category_name(int index);
static bool shelf_category_matches(const char *path);
static int shelf_category_add(const char *path);
static app_redraw_t shelf_category_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev);
static bool s_reader_favorite;
static shelf_entry_t s_managed;
static bool s_delete_confirm, s_file_removed;
static char s_shelf_warning[128], s_manage_message[128];
static char s_query[65], s_search_draft[65], s_batch_message[128];
static ui_text_edit_t s_search_input;
static book_view_t s_search_parent;
// 批量操作需要二次确认的三类：删文件、清进度、从书架移除。前两类动数据，后一类只动书架。
// Three batch actions need confirmation: delete files, clear progress, remove from the shelf.
// The first two touch stored data; the last one only touches shelf membership.
typedef enum {
    BATCH_DELETE,
    BATCH_CLEAR,
    BATCH_UNSHELF,
} batch_kind_t;
static bool s_batch_confirm;
static batch_kind_t s_batch_kind;
typedef struct pending_progress {
    char path[BOOK_STORE_PATH_MAX];
    book_progress_t value;
    bool dirty, progress_saved;
    book_progress_watch_t* watch;
    struct pending_progress* next;
} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {
    shelf_entry_t entry;
    struct delete_retry* next;
} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[BOOK_STORE_PATH_MAX];
static char s_editor_title[121], s_editor_notice[96];
static ui_text_edit_t s_editor_input;
static uint8_t* s_editor_cover;
static bool s_save_failed;
static bool s_pending_invalidated;
static int64_t s_save_retry_ms;
static unsigned s_store_revision;
static uint32_t s_progress_revision=UINT32_MAX;
static bool s_shelf_cache_valid, s_cache_sd_present, s_cache_sd_mounted;
static uint8_t s_cache_shelf_style;
static bool s_scan_pending, s_clear_confirm;
static reader_panel_t s_reader_panel;
// 本章页码草稿不改变阅读位置；撤销保存文字锚点，重排后仍能回到原文。
// Draft chapter pages never move the reader; undo keeps a text anchor across reflow.
static size_t s_chapter_jump_page;
static char s_chapter_jump_digits[24];
static bool s_chapter_jump_replace, s_chapter_jump_error;
static struct {
    bool valid;
    size_t chapter, offset;
    int64_t until_ms;
} s_chapter_jump_undo;
// 微调长按只更新预览，松手后统一重排和保存；离开控件即取消。
// Held indent adjustments update the preview only; release reflows/saves once and leaving the control cancels.
static struct {
    bool active, cancelled, repeated;
    int control, value;
    int64_t next_ms, last_ms;
} s_indent_hold;
static bool s_indent_feedback_pending;
static void reader_indent_hold_cancel(void);
static bool s_reader_fullscreen;
static bool s_shelf_key_mode,s_shelf_key_paint_pending;
static int s_shelf_key_pressed,s_shelf_key_delta;
static int64_t s_shelf_key_until;
static int64_t s_reader_clock_minute = -1, s_reader_clock_painted_minute = -1;
static bool s_reader_clock_pending;
static char s_message[128], s_storage[128], s_path[BOOK_STORE_PATH_MAX], s_title[128];
static char s_book_title[128];
static char s_chapter_heading_title[128], s_chapter_heading_label[32];
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height;
// 横排题头该占的行高，与当前模式无关地记着：首块是插图时它是 0，
// 切回横排要用它，而不是拿竖排留下的 0 去算。
// The row height a horizontal heading should take, remembered independently of the current mode:
// it is zero when the chapter opens with an illustration, and switching back must use it rather
// than the zero a vertical heading left behind.
static unsigned s_chapter_lead_row_height;
// 竖排首页为题头预留的列宽；横排为 0。/ Columns reserved for a vertical heading; 0 horizontally.
static unsigned s_chapter_lead_width;
static char s_font_path[192];
static char* s_text;
bool app_book_reader_body_visible(void) {
    return s_view == READING && s_text && s_reader_panel == READER_PANEL_NONE && !s_clear_confirm;
}
static blk_t* s_blocks;
// 书内字体的 run 表与块表同生共死：都来自同一次 html_text 解析，排版直接借用。
// The face run table lives and dies with the block table: both come from the same html_text
// parse, and the layout borrows them.
static html_run_t* s_runs;
static size_t s_run_count;
// 上一次排版用的书内字体开关：字体页改过之后，回阅读页要重排当前章才看得出来。
// The embedded-face setting the current layout was built with: after the font page changes
// it, the reader must re-typeset the chapter before the change is visible.
static bool s_book_fonts_applied;
// 字体开关改了之后要重排当前章，但那次重排不能在触摸回调里做：栈太深。
// The font switch needs the current chapter re-typeset, but not from the touch callback: the
// stack there is too deep.
static bool s_fonts_reload_pending;
// 只计用户主动换字体后的实际回退，每次选择最多记一次；预览和后台重试不计数。
// Count actual fallbacks after explicit choices once each, excluding preview loads and background retries.
static unsigned s_font_fallbacks;
static bool s_font_choice_pending, s_font_recovery_pending, s_font_restart_pending;
static bool s_font_recovery_save_failed;
static void reader_font_choice_begin(const char *path) {
    if (ttf_font_path_is_builtin(path)) {
        s_font_fallbacks = 0;
        s_font_choice_pending = s_font_recovery_pending = false;
    } else s_font_choice_pending = true;
}
static void reader_font_choice_finish(void) {
    if (!s_font_choice_pending) return;
    s_font_choice_pending = false;
    if (ttf_font_is_builtin()) {
        if (s_font_fallbacks < 3) ++s_font_fallbacks;
        if (s_font_fallbacks == 3) s_font_recovery_pending = true;
    } else if (ttf_font_ready()) s_font_fallbacks = 0;
}
static void reader_font_recovery_reset(void) {
    s_font_fallbacks = 0;
    s_font_choice_pending = s_font_recovery_pending = s_font_restart_pending = false;
    s_font_recovery_save_failed = false;
}
static char** s_images;
static size_t s_image_count;
// PR #7 的当前页图片集合：只保留显示页的灰阶，不限制为八张。
// PR #7's current-page image set retains visible grayscale only, without an eight-image limit.
typedef struct { uint8_t *gray; int width, height; } reader_image_t;
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_page_images_for = SIZE_MAX;
static uint32_t s_page_images_generation;
static size_t s_block_count;
static size_t s_text_len, s_chapter, s_page;
// 整页插图的双指缩放：1000 为原始大小；缩放与位置只属于当时那一页，换页自动还原。
// 图像中心用屏幕坐标保存，双指中点既是缩放锚点也是平移把手；单指拖动同样平移。
// Two-finger zoom for a whole-page illustration: 1000 is original size, and the scale plus
// position belong to the page they were made on. The image centre is kept in screen
// coordinates; the contact midpoint anchors the scale and doubles as the pan handle, and a
// one-finger drag pans as well.
#define READER_IMAGE_ZOOM_MAX 4000
#define READER_IMAGE_ZOOM_MIN 1000
static int s_image_zoom = READER_IMAGE_ZOOM_MIN;
static size_t s_image_zoom_page = SIZE_MAX;
// 正文插图缩放模式：双击正文里的一张图后，单独按整页方式缩放它。
// Inline image zoom mode: after double-tapping an illustration inside the text, that image is
// zoomed as if it were a whole-page sheet.
static bool s_zoom_active;
static int s_zoom_slot;
static int s_image_zoom_cx, s_image_zoom_cy;
static uint32_t s_image_zoom_start_distance;
static int s_image_zoom_start_scale = READER_IMAGE_ZOOM_MIN;
static int s_image_zoom_start_cx, s_image_zoom_start_cy;
static int s_image_zoom_anchor_dx, s_image_zoom_anchor_dy;
static int64_t s_image_zoom_draw_ms;
static bool s_image_zoom_bw;
static bool s_image_pan_active;
static uint16_t s_image_pan_x, s_image_pan_y;
static size_t s_selected_toc = SIZE_MAX;
static bool s_toc_jump_open, s_toc_jump_drag;
static int s_toc_jump_percent;
static size_t s_jump_offset = SIZE_MAX, s_jump_page;
static uint32_t s_file_size;
static int s_px, s_margin, s_line_spacing, s_turns, s_unsaved;
static int64_t s_poll_ms, s_last_turn_ms;
static int64_t s_stats_last_ms, s_stats_activity_ms;
static uint32_t s_stats_pending_ms, s_stats_pending_turns;
static uint32_t s_session_read_ms, s_session_turns;
static int s_font_page;
static int s_reader_slider = -1;
static bool s_reader_slider_endpoint;
static int s_reader_preview_px, s_reader_preview_margin, s_reader_preview_line, s_reader_preview_para;
static int s_reader_preview_tracking;
static uint32_t s_recent_days[30];
static bool s_recent_days_valid;
static char s_reader_notice[64];
static int64_t s_reader_notice_until;
static int s_bookmark_page;
static bool s_bookmark_edit, s_bookmark_delete_confirm, s_bookmark_delete_error;
static uint32_t s_bookmark_selected;
typedef struct { char path[288]; } reader_background_item_t;
static reader_background_item_t *s_backgrounds;
static int s_background_count, s_background_selected, s_background_page;
#define READER_BACKGROUND_ROWS 7
static bool s_background_ready;
static char s_background_notice[96];
static char s_background_preview_notice[96];
static bool s_background_prepare_pending;
static EpdRect s_area;
static enum EpdDrawMode s_mode = MODE_GL16;
static bool s_input_settle;
static bool s_reader_cleanup;
static bool s_reader_footer_pending;
static bool s_water_turn_pending;
static e0470_turn_dir_t s_water_turn_dir;
static int s_pressed_control = -1;
static bool s_shelf_feedback_pending;
static bool s_cover_batch_present_pending;
static bool s_shelf_page_pending;
static book_reader_tap_t s_reader_tap;
static unsigned s_reader_key_pick;
static char s_reader_key_error[64];
static int64_t s_du_ms;
static unsigned s_du_count;
static EpdRect s_du_area;
static SemaphoreHandle_t s_draw_lock, s_prep_done;
static TaskHandle_t s_prep_task;
static atomic_bool s_prep_stop;
static book_layout_draw_cursor_t s_prep_cursor;
static uint32_t s_next_generation;
static uint8_t* s_next_fb;
static int s_next_page = -1, s_prep_page = -1;

#define s_toolbar (s_reader_panel != READER_PANEL_NONE)

static void render(app_ctx_t* ctx, uint8_t* fb);
static void draw_reader_panel(uint8_t* fb);
static EpdRect reader_slider_rect(int slider);
static void scan_shelf(app_ctx_t* ctx);
static void copy_text(char* dst, size_t cap, const char* src);
static void fit_text(char* text, int px, int width);
static void free_book(void);
static void save_progress(void);
static void before_lock(app_ctx_t *ctx);
static void invalidate_prep(void);
static void prepare_inline_image(void);
static void release_page_images(void);
static void sort_shelf(app_ctx_t* ctx);
static void prepare_covers(app_ctx_t* ctx);
static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx);
bool app_book_request_open(const char* path) {
    if (!path || !path[0] || strnlen(path, sizeof(s_requested_open)) >= sizeof(s_requested_open)) return false;
    if (!book_file_path_supported(path)) return false;
    copy_text(s_requested_open, sizeof(s_requested_open), path);
    s_requested_open_home = false;
    s_requested_fullscreen = true;
    return true;
}
bool app_book_request_open_from_home(const char* path) {
    if (!app_book_request_open(path)) return false;
    s_requested_open_home = true;
    return true;
}
bool app_book_request_resume(const char *path, bool fullscreen) {
    if (!app_book_request_open_from_home(path)) return false;
    s_requested_fullscreen = fullscreen;
    return true;
}
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen) {
    if (!s_text || !s_path[0] || s_view == SHELF || s_view == MANAGE || s_view == BULK ||
        !path || !fullscreen || strlen(s_path) >= capacity || s_save_failed) return false;
    copy_text(path, capacity, s_path);
    *fullscreen = s_reader_fullscreen;
    return true;
}
void app_book_request_manage(void) { s_requested_manage = true; }
static void draw_control(uint8_t* fb, EpdRect rect, const char* label, int id) {
    if (id == 112 && s_query[0]) {
        ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
        ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2,
                   UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_pressed_control == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, label, false);
}
static void lock_draw(void) { if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY); }
static void unlock_draw(void) { if (s_draw_lock) xSemaphoreGive(s_draw_lock); }
static size_t fb_bytes(void) { return (size_t)epd_width() * epd_height() / 2; }
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
// 正文上方留出一致的呼吸空间，下沿靠近进度区；两种模式共用排版与刷新边界。
// Give the body a consistent top inset and extend it toward the progress strip in both modes.
static EpdRect reader_area(void) {
    const int top = s_reader_fullscreen ? (app_settings_reader_immersive() ? 0 : 80) : 160;
    // 正文排版不占进度线；文字翻页合并差分时仅驱动实际变化的进度像素。
    // Keep text layout outside the progress strip; a fused text turn drives only changed progress pixels.
    const int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP : UI_BAR_TOP + 8;
    return (EpdRect){0, top, UI_LOCK_WIDTH, bottom - top};
}
static EpdRect reader_fullscreen_progress_area(void) {
    return (EpdRect){0, READER_FULLSCREEN_PROGRESS_TOP,
                     UI_LOCK_WIDTH, UI_LOCK_HEIGHT - READER_FULLSCREEN_PROGRESS_TOP};
}
static EpdRect body_rect_for_tracking(int tracking_index) {
    int top = s_reader_fullscreen ? (app_settings_reader_immersive() ? 24 : 96) : 188;
    int margin = s_margin;
    // Leave only a narrow strip for the full-book progress line in full screen.
    int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP - 2 : UI_BAR_TOP + 8;
    (void)tracking_index;
    // 竖排边距作用于上下，横排作用于左右；保留所有中间像素档位。
    // Vertical margins affect top/bottom and horizontal margins left/right; retain all intermediate pixel stops.
    if (app_settings_book_vertical()) {
        return (EpdRect){24, top + margin, UI_LOCK_WIDTH - 48, bottom - top - 2 * margin};
    }
    EpdRect outer = {margin, top, UI_LOCK_WIDTH - 2 * margin, bottom - top};
    // 保留每个边距档位的真实像素；两端对齐由排版器分配字间距。
    // Preserve each margin in pixels; justification distributes the remaining line width.
    return outer;
}
static EpdRect body_rect(void) {
    return body_rect_for_tracking(app_settings_book_tracking());
}
static int reader_margin_width(int margin, int px, int tracking_px) {
    (void)px; (void)tracking_px;
    return UI_LOCK_WIDTH - 2 * margin;
}
static int reader_margin_levels(int px, int tracking_px,
                                int widths[BOOK_MARGIN_CHOICES], int values[BOOK_MARGIN_CHOICES]) {
    int count = 0;
    for (int i = 0; i < BOOK_MARGIN_CHOICES; ++i) {
        int value = BOOK_MARGIN_MIN + i * 6;
        int width = reader_margin_width(value, px, tracking_px);
        if (!count || widths[count-1] != width) {
            widths[count] = width;
            values[count++] = value;
        } else if (count > 1 && abs(value - 24) < abs(values[count-1] - 24)) {
            values[count-1] = value;
        }
    }
    // 同字格宽度只显示一个档位；极值仍到0/48，已有边距不因重画滑杆改写。
    // Show one stop per distinct column width, retaining the 0/48 extremes and existing saved margins.
    values[0] = BOOK_MARGIN_MIN;
    if (count > 1) values[count-1] = BOOK_MARGIN_MAX;
    return count;
}
static int reader_margin_level_for(int margin, int px, int tracking_px) {
    if (margin < BOOK_MARGIN_MIN) margin = BOOK_MARGIN_MIN;
    if (margin > BOOK_MARGIN_MAX) margin = BOOK_MARGIN_MAX;
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    int width = reader_margin_width(margin, px, tracking_px);
    for (int i = 0; i < count; ++i) if (width >= widths[i]) return i;
    return count - 1;
}
static int reader_margin_for_level(int level, int current, int px, int tracking_px) {
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    if (level < 0) level = 0;
    if (level >= count) level = count - 1;
    return level == reader_margin_level_for(current, px, tracking_px) ? current : values[level];
}
static EpdRect progress_rect(void) {
    // 阅读页没有右侧菜单按钮；进度信息应和页眉分隔线一样铺满内容宽度。
    // The reader has no right-hand menu button; the footer spans the full content width.
    return (EpdRect){36, UI_BAR_TOP, UI_LOCK_WIDTH - 72, UI_BAR_H};
}
static int shelf_rows(void) { return s_view == SHELF && app_settings_shelf_style() == 5 ? 4 : BOOK_GRID_ROWS; }
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_HEIGHT 214
#define SHELF_FAST_COVER_STRIDE ((SHELF_FAST_COVER_WIDTH + 7) / 8)
#define SHELF_FAST_COVER_BYTES (SHELF_FAST_COVER_STRIDE * SHELF_FAST_COVER_HEIGHT)
#define SHELF_FAST_CACHE_RESERVE (512u * 1024u)
#define SHELF_GUARD_CACHE_BYTES (644u * 90u / 2u)
typedef struct { EpdRect rect; int rotation, contrast; bool binary, valid; } shelf_guard_key_t;
static uint8_t *s_guard_cache;
static shelf_guard_key_t s_guard_keys[3];
static bool s_guard_cache_attempted;
static void release_guard_cache(void) {
    free(s_guard_cache); s_guard_cache=NULL; s_guard_cache_attempted=false;
    memset(s_guard_keys,0,sizeof(s_guard_keys));
}
// 缓存精确的输入和成品；逐行比对底图，任何封面、位置或模式变化均重绘。
// Cache exact inputs and finished pixels; compare every input row so art, position and mode changes redraw.
static void draw_shelf_guard(uint8_t *fb,EpdRect rect,int slot) {
    const bool binary=s_view==SHELF&&app_settings_main_fast_refresh();
    EpdRect p=ui_rotate_rect_to_fb(rect);
    const bool fits=slot>=0&&slot<3&&!(p.x&1)&&!(p.width&1)&&p.x>=0&&p.y>=0&&
        p.x+p.width<=epd_width()&&p.y+p.height<=epd_height()&&
        (size_t)p.width*p.height/2<=SHELF_GUARD_CACHE_BYTES;
    if(fits&&!s_guard_cache&&!s_guard_cache_attempted) {
        s_guard_cache_attempted=true;
        size_t bytes=6u*SHELF_GUARD_CACHE_BYTES;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>=bytes+2u*1024u*1024u)
            s_guard_cache=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    }
    if(!fits||!s_guard_cache){ui_draw_acrylic_guard(fb,rect);if(binary)ui_acrylic_bw_rect(fb,rect);return;}
    shelf_guard_key_t *key=&s_guard_keys[slot];
    size_t stride=(size_t)epd_width()/2,span=(size_t)p.width/2;
    uint8_t *input=s_guard_cache+2u*slot*SHELF_GUARD_CACHE_BYTES,*output=input+SHELF_GUARD_CACHE_BYTES;
    bool same=key->valid&&key->rotation==epd_get_rotation()&&key->contrast==app_settings_system_contrast()&&
        key->binary==binary&&!memcmp(&key->rect,&rect,sizeof(rect));
    for(int y=0;same&&y<p.height;++y)
        same=!memcmp(fb+(size_t)(p.y+y)*stride+p.x/2,input+(size_t)y*span,span);
    if(!same) {
        key->valid=false;
        for(int y=0;y<p.height;++y)memcpy(input+(size_t)y*span,fb+(size_t)(p.y+y)*stride+p.x/2,span);
        ui_draw_acrylic_guard(fb,rect);if(binary)ui_acrylic_bw_rect(fb,rect);
        for(int y=0;y<p.height;++y)memcpy(output+(size_t)y*span,fb+(size_t)(p.y+y)*stride+p.x/2,span);
        *key=(shelf_guard_key_t){.rect=rect,.rotation=epd_get_rotation(),.contrast=app_settings_system_contrast(),.binary=binary,.valid=true};
    } else for(int y=0;y<p.height;++y)memcpy(fb+(size_t)(p.y+y)*stride+p.x/2,output+(size_t)y*span,span);
}
static EpdRect row_rect(int row) {
    if (s_view == SHELF && app_settings_shelf_style() == 5)
        return (EpdRect){36, 184 + row * 197, 612, 197};
    if (s_view != BULK) {
        int col = row % 3, line = row / 3;
        int cell = (ui_content_width() - 2 * UI_GAP) / 3;
        return (EpdRect){UI_MARGIN + col * (cell + UI_GAP), (s_view == SHELF ? 192 : 224) + line * 282, cell, 260};
    }
    return (EpdRect){UI_MARGIN, 308 + row * (UI_BTN_H + UI_GAP), ui_content_width(), UI_BTN_H};
}
static EpdRect shelf_cover_image(int row) {
    EpdRect card = row_rect(row);
    if (s_view != SHELF || app_settings_shelf_style() != 5)
        return (EpdRect){card.x + (card.width - 164) / 2, card.y, 164, 214};
    // 列表所有封面统一尺寸；原图按比例铺满并居中裁切。/ Uniform list boxes use aspect fill and center crop.
    return (EpdRect){71, card.y + (197-156)/2, 120, 156};
}
static unsigned s_cover_generation;
static void invalidate_covers(void) {
    book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;
    release_guard_cache();
    s_cover_pending_mask = s_cover_decode_mask = s_cover_warm_mask = 0;
    s_cover_dirty_mask = s_cover_present_mask = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) {
        free(s_covers[i].gray);
        free(s_covers[i].fast_bits);
        s_covers[i].gray = NULL;
        s_covers[i].fast_bits = NULL;
        s_covers[i].index = -1;
    }
}
static void release_fast_covers(void) {
    for (int i=0;i<BOOK_ROWS;++i) {
        free(s_covers[i].fast_bits);
        s_covers[i].fast_bits=NULL;
    }
}
static bool fast_cover_matches(int row, EpdRect image) {
    return s_covers[row].fast_bits && s_covers[row].fast_contrast==app_settings_system_contrast() &&
        s_covers[row].fast_phase==((image.x&3)|((image.y&3)<<2));
}
static bool cover_favorite_needs_white_edge(const uint8_t *gray, EpdRect image, unsigned sw, unsigned sh);
static void prepare_fast_covers(void) {
    // 转换成功即释放灰阶；不释放仍有效的黑白缓存，避免重复切页重新解码。
    // Release gray immediately after conversion, retaining valid packed artwork across tab changes.
    if (s_view != SHELF || !app_settings_main_fast_refresh()) { release_fast_covers(); return; }
    for (int row = 0; row < BOOK_ROWS; ++row) {
        if (!s_covers[row].gray) continue;
        EpdRect image = shelf_cover_image(row);
        if (row < shelf_rows() && row < BOOK_GRID_ROWS && !fast_cover_matches(row, image)) {
            free(s_covers[row].fast_bits); s_covers[row].fast_bits = NULL;
            // 转换后回收的灰阶空间也计入保留量；内存不足时显示标题，不长期保留灰阶副本。
            // Count reclaimed gray toward the reserve; on OOM show a title rather than retain a gray duplicate.
            size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) + BOOK_COVER_W * BOOK_COVER_H;
            uint8_t *bits = free_after >= SHELF_FAST_CACHE_RESERVE + SHELF_FAST_COVER_BYTES
                ? heap_caps_calloc(1, SHELF_FAST_COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
            if (bits) {
                unsigned sw = app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W;
                unsigned sh = app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H;
                book_crop_t crop = book_cover_crop(sw, sh, image.width, image.height);
                unsigned sample_x[SHELF_FAST_COVER_WIDTH];
                uint8_t tones[256];
                for(int x=0;x<image.width;++x)sample_x[x]=crop.x+(uint64_t)(unsigned)x*crop.width/(unsigned)image.width;
                for(unsigned v=0;v<256;++v)tones[v]=(ui_contrast_gray((uint8_t)v)>>4)*17u;
                for (int y = 0; y < image.height; ++y) {
                    const unsigned sy = crop.y + (uint64_t)(unsigned)y * crop.height / (unsigned)image.height;
                    for (int x = 0; x < image.width; ++x) {
                        const unsigned sx = sample_x[x];
                        uint8_t tone = tones[s_covers[row].gray[sy * sw + sx]];
                        if (ui_image_dither_cover_bw(tone, image.x + x, image.y + y))
                            bits[y * SHELF_FAST_COVER_STRIDE + x / 8] |= (uint8_t)(1u << (x & 7));
                    }
                }
                s_covers[row].fast_white_edge = cover_favorite_needs_white_edge(s_covers[row].gray, image, sw, sh);
                s_covers[row].fast_bits = bits;
                s_covers[row].fast_contrast = app_settings_system_contrast();
                s_covers[row].fast_phase = (image.x & 3) | ((image.y & 3) << 2);
            }
        }
        free(s_covers[row].gray); s_covers[row].gray = NULL;
    }
}
void app_book_cover_mode_changed(void) { invalidate_covers(); }
static uint8_t* load_cover_gray(const char* source, const char* title, const char* author,
                                bool decode, bool* pending) {
    *pending = false;
    if (!pico_boot_asset_allowed(source)) return NULL;
    uint8_t* gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) return NULL;
    if (!book_cover_load_gray(source, title, author, gray, decode, pending)) {
        free(gray); return NULL;
    }
    return gray;
}
static uint8_t *load_shelf_cover_gray(int row, const char *source, const char *title,
                                      const char *author, bool decode, bool *pending) {
    if (s_view != SHELF || app_settings_shelf_style() != 5)
        return load_cover_gray(source, title, author, decode, pending);
    *pending = false;
    if (!pico_boot_asset_allowed(source)) return NULL;
    s_covers[row].width = BOOK_COVER_W; s_covers[row].height = BOOK_COVER_H;
    uint8_t *gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray && !book_cover_load_list_gray(source, title, author, gray, decode, pending,
                                          &s_covers[row].width, &s_covers[row].height)) {
        free(gray); gray = NULL;
    }
    return gray;
}

// 直接读最终点阵：不分配灰阶工作区，也不在每次书架切页重新转换。
// Read the finished raster: no gray workspace or repeated conversion on shelf paging.
static bool load_shelf_cover_fast(int row, int index) {
    if (s_view != SHELF || !app_settings_main_fast_refresh() || row >= BOOK_GRID_ROWS ||
        !pico_boot_asset_allowed(s_shelf[index].path)) return false;
    EpdRect image = shelf_cover_image(row);
    size_t bytes = (size_t)SHELF_FAST_COVER_STRIDE * image.height;
    if (bytes > SHELF_FAST_COVER_BYTES || heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        < SHELF_FAST_CACHE_RESERVE + SHELF_FAST_COVER_BYTES) return false;
    uint8_t *bits = heap_caps_calloc(1, SHELF_FAST_COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bits) return false;
    book_fast_cover_key_t key = {(unsigned)image.width, (unsigned)image.height, SHELF_FAST_COVER_STRIDE,
        app_settings_system_contrast(), (image.x&3)|((image.y&3)<<2)};
    bool edge;
    if (!book_cover_fast_cache_read(s_shelf[index].path, s_shelf[index].name, s_shelf[index].author,
                                   key, bits, &edge)) { free(bits); return false; }
    s_covers[row].fast_bits = bits; s_covers[row].fast_white_edge = edge;
    s_covers[row].fast_contrast = key.contrast; s_covers[row].fast_phase = key.phase;
    return true;
}
static void save_shelf_cover_fast(int row, int index) {
    if (!s_covers[row].fast_bits) return;
    EpdRect image = shelf_cover_image(row);
    book_fast_cover_key_t key = {(unsigned)image.width, (unsigned)image.height, SHELF_FAST_COVER_STRIDE,
        s_covers[row].fast_contrast, s_covers[row].fast_phase};
    book_cover_fast_cache_write(s_shelf[index].path, s_shelf[index].name, s_shelf[index].author,
                                key, s_covers[row].fast_bits, s_covers[row].fast_white_edge);
}

static unsigned prepare_cover_batch(app_ctx_t *ctx,bool cache_only) {
    if((s_view!=SHELF&&s_view!=MANAGE)||(!cache_only&&ctx->touch&&ctx->touch->touched))return 0;
    unsigned changed=0;
    for(int row=0;row<shelf_rows();++row){
        unsigned bit=1u<<row;if(!(s_cover_pending_mask&bit))continue;
        if(s_view==SHELF&&app_poll_main_navigation(ctx))return changed;
        int index=ctx->leaf*shelf_rows()+row;
        if(index<0||index>=s_visible_count||s_covers[row].index!=index||s_shelf[index].removed){s_cover_pending_mask&=~bit;continue;}
        EpdRect image=shelf_cover_image(row);
        book_cover_job_t request={.list=s_view==SHELF&&app_settings_shelf_style()==5,
            .fast=s_view==SHELF&&app_settings_main_fast_refresh(),.width=image.width,.height=image.height,
            .stride=SHELF_FAST_COVER_STRIDE,.contrast=app_settings_system_contrast(),
            .phase=(image.x&3)|((image.y&3)<<2),.x=image.x,.y=image.y};
        copy_text(request.path,sizeof(request.path),s_shelf[index].path);
        copy_text(request.title,sizeof(request.title),s_shelf[index].name);
        copy_text(request.author,sizeof(request.author),s_shelf[index].author);
        if(!s_cover_generation)s_cover_generation=book_cover_jobs_begin();
        if(!book_cover_jobs_submit(s_cover_generation,row,&request)){
            s_cover_generation=book_cover_jobs_begin();
            if(!book_cover_jobs_submit(s_cover_generation,row,&request)){s_cover_pending_mask&=~bit;continue;}
        }
        book_cover_result_t result={0};int state=book_cover_jobs_take(s_cover_generation,row,&result);
        if(!state)continue;
        s_cover_pending_mask&=~bit;s_cover_decode_mask&=~bit;
        if(state<0)continue;
        free(s_covers[row].gray);free(s_covers[row].fast_bits);
        s_covers[row].gray=result.fast?NULL:result.pixels;
        s_covers[row].fast_bits=result.fast?result.pixels:NULL;
        s_covers[row].width=result.width;s_covers[row].height=result.height;
        s_covers[row].fast_white_edge=result.white_edge;
        s_covers[row].fast_contrast=request.contrast;s_covers[row].fast_phase=request.phase;
        changed|=bit;s_cover_dirty_mask|=bit;
    }
    return changed;
}

// 分批只用于准备，不能让几张已解码封面先出现在屏幕或点击反馈中。
// Chunk preparation only: neither page painting nor press feedback reveals an incomplete cover group.
static bool shelf_cover_frame_ready(void) {
    return s_view != SHELF || s_cover_pending_mask == 0;
}
static unsigned shelf_cover_reveal_mask(void) {
    return shelf_cover_frame_ready() ? s_cover_dirty_mask : 0;
}

static void prepare_covers(app_ctx_t* ctx) {
    if (s_view != SHELF && s_view != MANAGE) return;
    int rows = shelf_rows();
    if (s_covers[0].index != ctx->leaf * rows) {
        book_cover_jobs_cancel(s_cover_generation);s_cover_generation=book_cover_jobs_begin();
        s_cover_pending_mask = s_cover_decode_mask = s_cover_warm_mask = 0;
        s_cover_dirty_mask = s_cover_present_mask = 0;
    }
    for (int row = 0; row < rows; ++row) {
        int index = ctx->leaf * rows + row;
        EpdRect image = shelf_cover_image(row);
        bool packed = s_view == SHELF && app_settings_main_fast_refresh();
        if (s_covers[row].index == index &&
            (packed ? fast_cover_matches(row, image) : s_covers[row].gray != NULL)) continue;
        if (s_covers[row].index == index && !s_covers[row].gray && !s_covers[row].fast_bits) continue;
        free(s_covers[row].gray); free(s_covers[row].fast_bits);
        s_covers[row].gray = s_covers[row].fast_bits = NULL;
        s_covers[row].index = index;
        s_cover_dirty_mask &= ~(1u<<row);
        if (index >= s_visible_count || index < 0 || s_shelf[index].removed) continue;
        // Queue each slot, then gather cheap disk-cache hits before painting; decode misses later.
        s_cover_pending_mask |= 1u << row;
        s_cover_warm_mask |= 1u << row;
        s_cover_warm_after_ms = ctx->now_ms + 1500;
    }
    prepare_fast_covers();
    (void)prepare_cover_batch(ctx, true);
}

// 首帧短暂汇合TF缓存命中，让题头、书架和封面一起显示；冷解码不无限阻塞。
// Briefly gather warm TF hits for a complete first frame; never wait indefinitely for cold decoding.
static void gather_cover_entry(app_ctx_t *ctx) {
    if(s_view!=SHELF||!s_cover_pending_mask||ctx->request_app||
       (s_presented_view==(int)SHELF&&!s_shelf_page_pending))return;
    const int64_t deadline=esp_timer_get_time()+60000;
    while(s_cover_pending_mask&&esp_timer_get_time()<deadline){
        if(app_poll_main_navigation(ctx))return;
        vTaskDelay(1);
        (void)prepare_cover_batch(ctx,true);
        if(ctx->request_app)return;
    }
}

// 画面和操作优先，预生成只在封面加载完、无触摸的空闲书架运行一次一本。
// Present and input take priority; warm one cover only on an idle shelf after visible loading completes.
static void warm_shelf_lock_cover(app_ctx_t *ctx) {
    if (s_view != SHELF || app_settings_lock_style() != 2 || s_clear_confirm || s_batch_confirm ||
        s_cover_pending_mask || !s_cover_warm_mask || ctx->now_ms < s_cover_warm_after_ms ||
        (ctx->touch && ctx->touch->touched) || ctx->request_app || ctx->request_return || ctx->request_menu) return;
    if (app_poll_main_navigation(ctx)) return;
    for (int row = 0; row < shelf_rows(); ++row) {
        const unsigned bit = 1u << row;
        if (!(s_cover_warm_mask & bit)) continue;
        s_cover_warm_mask &= ~bit; s_cover_warm_after_ms = ctx->now_ms + 1000;
        int index = ctx->leaf * shelf_rows() + row;
        if (index >= 0 && index < s_visible_count && s_covers[row].index == index && !s_shelf[index].removed)
            (void)book_lock_collage_warm_cover(s_shelf[index].path);
        (void)app_poll_main_navigation(ctx);
        break;
    }
}

static void draw_favorite_icon(uint8_t *fb, int x, int y, int w, int h, bool filled, uint8_t ink) {
    // 只绘制标记本身，封面上不加矩形衬底。/ Draw only the glyph, without a rectangular cover underlay.
    const int notch_y = y + h - 9;
    if (filled) {
        // 把 V 形尾部也填满，避免选中后只剩矩形黑块、尾部仍是空心。
        // Fill the V-shaped tail as well so the selected glyph is solid throughout.
        for (int py = y; py <= y + h; ++py) {
            int arm = py <= notch_y ? w / 2 : (w / 2) * (y + h - py) / 9;
            ui_fill_rect_fast((EpdRect){x, py, arm + 1, 1}, ink, fb);
            ui_fill_rect_fast((EpdRect){x + w - arm, py, arm + 1, 1}, ink, fb);
        }
        return;
    }
    // 空心态使用与阅读工具栏相近的两像素线宽。
    // Keep the outline close to the other reader toolbar icons in visual weight.
    ui_fill_rect_fast((EpdRect){x, y, w + 1, 2}, ink, fb);
    ui_fill_rect_fast((EpdRect){x, y, 2, h - 1}, ink, fb);
    ui_fill_rect_fast((EpdRect){x + w - 1, y, 2, h - 1}, ink, fb);
    epd_draw_line(x, y + h, x + w / 2, notch_y, ink, fb);
    epd_draw_line(x + 1, y + h - 1, x + w / 2, notch_y + 1, ink, fb);
    epd_draw_line(x + w / 2, notch_y, x + w, y + h, ink, fb);
    epd_draw_line(x + w / 2, notch_y + 1, x + w - 1, y + h - 1, ink, fb);
}
static void draw_shelf_favorite_icon(uint8_t *fb, int x, int y, int w, int h, bool white_edge) {
    if (white_edge) {
        // 仅沿书签轮廓扩两像素白边，深封面上也不出现白色矩形底块。
        // Dilate only the bookmark silhouette by two pixels, never a white backing box.
        const int notch_y = y + h - 9;
        for (int py = y; py <= y + h; ++py) {
            int arm = py <= notch_y ? w / 2 : (w / 2) * (y + h - py) / 9;
            ui_fill_rect_fast((EpdRect){x - 2, py - 2, arm + 5, 5}, UI_GRAY_WHITE, fb);
            ui_fill_rect_fast((EpdRect){x + w - arm - 2, py - 2, arm + 5, 5}, UI_GRAY_WHITE, fb);
        }
    }
    draw_favorite_icon(fb, x, y, w, h, true, UI_GRAY_BLACK);
}
static bool cover_favorite_needs_white_edge(const uint8_t *gray, EpdRect image, unsigned sw, unsigned sh) {
    if (!gray || !sw || !sh || image.width <= 0 || image.height <= 0) return false;
    unsigned sum = 0, count = 0;
    const book_crop_t crop = book_cover_crop(sw, sh, image.width, image.height);
    // 只在标记周围采样；图标始终为黑色，深封面才加白边。
    // Sample the badge area; keep the glyph black and outline it only on dark covers.
    for (int y = 9; y < 39; y += 5) {
        for (int x = 11; x < 35; x += 4) {
            unsigned sx = crop.x + (uint64_t)(unsigned)x * crop.width / (unsigned)image.width;
            unsigned sy = crop.y + (uint64_t)(unsigned)y * crop.height / (unsigned)image.height;
            if (sx >= sw) sx = sw - 1;
            if (sy >= sh) sy = sh - 1;
            sum += ui_contrast_gray(gray[sy * sw + sx]);
            ++count;
        }
    }
    return sum < count * 145;
}
static void draw_shelf_cover(uint8_t* fb, EpdRect card, int row, const char* name, bool favorite) {
    EpdRect image = shelf_cover_image(row);
    image.y += card.y - row_rect(row).y;
    const bool artwork = shelf_cover_frame_ready();
    ui_fill_rect_fast(image, UI_GRAY_LIGHT, fb);
    if (artwork && app_settings_main_fast_refresh() && s_view==SHELF && fast_cover_matches(row,image)) {
        ui_blit_mono_bitmap(fb,image,s_covers[row].fast_bits,SHELF_FAST_COVER_STRIDE);
    } else if (artwork && s_covers[row].gray) {

        const uint8_t *gray = s_covers[row].gray;
        // 封面缓冲是 176×240、这一格是 164×214；按长边铺满 + 居中裁剪，避免被压扁。
        // The cover buffer is 176x240 and this frame is 164x214; fill by the longer side and
        // centre-crop so the artwork is not squashed.
        const unsigned frame_width = (unsigned)image.width, frame_height = (unsigned)image.height;
        unsigned sw = s_view == SHELF && app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W;
        unsigned sh = s_view == SHELF && app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H;
        const book_crop_t crop = book_cover_crop(sw, sh, frame_width, frame_height);
        unsigned sample_x[SHELF_FAST_COVER_WIDTH];
        uint8_t tones[256];
        const bool sampled=image.width<=SHELF_FAST_COVER_WIDTH;
        const bool binary=app_settings_main_fast_refresh()&&s_view==SHELF;
        if(sampled)for(int x=0;x<image.width;++x)sample_x[x]=crop.x+(uint64_t)(unsigned)x*crop.width/frame_width;
        for(unsigned v=0;v<256;++v)tones[v]=ui_contrast_gray((uint8_t)v);
        for (int y = 0; y < image.height; ++y) {
            const int sy = (int)(crop.y + (uint64_t)(unsigned)y * crop.height / frame_height);
            for (int x = 0; x < image.width; ++x) {
                const unsigned sx = sampled?sample_x[x]:crop.x+(uint64_t)(unsigned)x*crop.width/frame_width;
                const uint8_t tone = tones[gray[sy * sw + sx]];
                epd_draw_pixel(image.x + x, image.y + y,
                               binary
                                   ? ui_image_dither_cover_bw((tone>>4)*17u, image.x + x, image.y + y)
                                   : ui_image_dither_gray(tone, image.x + x, image.y + y), fb);
            }
        }
    } else {
        char title[80]; copy_text(title, sizeof(title), name);
        char *dot = strrchr(title, '.'); if (dot) *dot = 0;
        fit_text(title, 26, image.width - 22);
        ui_text_vc(fb, image.x + image.width / 2, image.y + image.height / 2, 26,
                   title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, image, 0, UI_GRAY_BLACK);
    if (favorite && image.width >= 28 && image.height >= 42) {
        draw_shelf_favorite_icon(fb, image.x + 10, image.y + 8, 24, 31,
                                 artwork && ((app_settings_main_fast_refresh() && s_view == SHELF && fast_cover_matches(row, image))
                                     ? s_covers[row].fast_white_edge : cover_favorite_needs_white_edge(s_covers[row].gray, image,
                                         app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W,
                                         app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H)));
    }
}
static EpdRect paint_list_cover_feedback(app_ctx_t *ctx, int row) {
    EpdRect image = shelf_cover_image(row);
    EpdRect area = image;
    area.y -= SHELF_LIST_BOUNCE_PX;
    area.height += SHELF_LIST_BOUNCE_PX;
    // 两位置均位于白色列表卡片内；不清整行，也不绘制额外反馈框。
    // Both positions stay within the white list card; never clear a whole row or add a feedback frame.
    ui_fill_rect_fast(area, UI_GRAY_WHITE, ctx->fb);
    EpdRect card = row_rect(row);
    if (s_pressed_control == row) card.y -= SHELF_LIST_BOUNCE_PX;
    int index = ctx->leaf * shelf_rows() + row;
    if (index >= 0 && index < s_visible_count)
        draw_shelf_cover(ctx->fb, card, row, s_shelf[index].name, s_shelf[index].favorite);
    return area;
}
static void draw_list_title(uint8_t *fb, const char *name, int top) {
    char lines[2][128] = {{0}};
    int px = ttf_em_height_px(27);
    const unsigned char *at = (const unsigned char *)name;
    for (int line = 0; line < 2 && *at; ++line) {
        size_t used = 0;
        while (*at && used < sizeof(lines[line])-5) {
            unsigned n = *at < 128 ? 1 : (*at & 0xe0)==0xc0 ? 2 : (*at & 0xf0)==0xe0 ? 3 : 4;
            if (strlen((const char *)at) < n) break;
            memcpy(lines[line]+used, at, n); lines[line][used+n] = 0;
            if (ui_text_fixed_width_px(px, lines[line]) > 378) { lines[line][used] = 0; break; }
            used += n; at += n;
        }
        if (line == 1 && *at) fit_text(lines[line], px, 352);
        ui_text_fixed_vc(fb, 244, top + line*35, px, lines[line], EPD_DRAW_ALIGN_LEFT, false);
    }
}
static void draw_list_row(uint8_t *fb, int row, const shelf_entry_t *item) {
    EpdRect card = row_rect(row);
    draw_list_title(fb, item->name, card.y + 42);
    char author[128]; copy_text(author, sizeof(author), item->author[0] ? item->author : "作者未知");
    int author_px = ttf_em_height_px(21), progress_px = ttf_em_height_px(19);
    fit_text(author, author_px, 378);
    int author_y = ui_text_fixed_width_px(ttf_em_height_px(27), item->name) > 378 ? 106 : 83;
    ui_text_fixed_ink_vc(fb, 244, card.y + author_y, author_px, author, EPD_DRAW_ALIGN_LEFT, 0x60);
    unsigned progress = item->has_progress ? item->pct : 0;
    if (progress > 100) progress = 100;
    char label[48]; snprintf(label, sizeof(label), progress == 100 ? "已读完 · %u%%" :
                              progress ? "已读 %u%%" : "未开始 · %u%%", progress);
    ui_text_fixed_vc(fb, 244, card.y + 143, progress_px, label, EPD_DRAW_ALIGN_LEFT, false);
    EpdRect bar = {244, card.y + 163, 378, 11};
    ui_fill_round_rect(fb, bar, 5, 0xd0);
    if (progress) {
        EpdRect fill = bar; fill.width = bar.width * progress / 100;
        if (fill.width < 1) fill.width = 1;
        ui_fill_round_rect(fb, fill, 5, 0x40);
    }
    ui_draw_control_frame(fb, bar, 5, 0x50);
    if (row < 3) ui_draw_separator(fb, card.y + 196, 50, 584, 0x60);
}
static void draw_shelf_furniture(uint8_t* fb) {
    uint8_t style = app_settings_shelf_style();
    if (!style || style == 5) return;
    for (int row = 0; row < 3; ++row) {
        int top = (s_view == SHELF ? 192 : 224) + row * 282;
        if (style == 1) {
            ui_fill_rect_fast((EpdRect){36, top + 214, 612, 12}, 0x30, fb);
            ui_hairline(fb, top + 214, 36, 612, 0x58);
            ui_hairline(fb, top + 225, 36, 612, 0x10);
        } else if (style == 2) {
            EpdRect acrylic = {20, top + 136, 644, 90};
            draw_shelf_guard(fb, acrylic, row);

        }
    }
}
static EpdRect tool_rect(int i) {
    int x0 = i * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    int x1 = (i + 1) * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    return (EpdRect){x0, 1096, x1 - x0, 120};
}
static int leaves(void) {
    if (s_view == TOC) return book_toc_pages(book_navigation_count());
    int count = s_visible_count;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    return count ? 1 + (count - 1) / rows : 1;
}
static EpdRect shelf_manage_rect(void) { return (EpdRect){442, 94, 97, 54}; }
static EpdRect shelf_import_rect(void) { return (EpdRect){551, 94, 97, 54}; }
static void draw_shelf_header_button(uint8_t *fb, EpdRect r, bool importing) {
    bool pressed = s_pressed_control == (importing ? 115 : 114);
    ui_fill_round_rect(fb, r, 18, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, r, 18, 0x70);
    if (pressed) ui_draw_pressed_round_rect(fb, r, 18);
    ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2 + (pressed ? 2 : 0), 20,
               importing ? "+ 导入" : "管理", EPD_DRAW_ALIGN_CENTER, false);
}
static EpdRect shelf_page_arrow_rect(int direction) {
    return ui_pager_rect(direction<0?UI_PAGER_PREVIOUS:UI_PAGER_NEXT);
}
static void draw_shelf_pager(uint8_t *fb, int count, int page, int pages) {
    char label[64];
    snprintf(label, sizeof(label), "%d本书 · %02d/%02d", count, page + 1, pages);
    int pressed=s_shelf_key_mode&&s_shelf_key_pressed?
        (s_shelf_key_pressed<0?UI_PAGER_PREVIOUS:UI_PAGER_NEXT):UI_PAGER_NONE;
    if(s_pressed_control>=116&&s_pressed_control<=119)pressed=s_pressed_control-116;
    ui_pager_draw(fb,label,page,pages,s_shelf_key_mode,pressed);
}
// 只推分页小区；按压不重绘封面，翻页在下一轮输入后执行，可被切页打断。
// Update only the pager; presses do not redraw covers. Page work follows on the next input tick and is cancellable by navigation.
static app_redraw_t shelf_key_paint(app_ctx_t *ctx) {
    s_area=ui_pager_area();
    ui_fill_rect_fast(s_area,UI_GRAY_WHITE,ctx->fb);
    draw_shelf_pager(ctx->fb,s_visible_count,ctx->leaf,leaves());
    s_shelf_key_paint_pending=true;
    return APP_REDRAW_AREA;
}
static bool shelf_main_key(app_ctx_t *ctx,int key,app_redraw_t *redraw) {
    if(s_view!=SHELF || s_clear_confirm || s_batch_confirm)return false;
    if(key==UI_KEY_2){
        s_shelf_key_mode=!s_shelf_key_mode;s_shelf_key_delta=s_shelf_key_pressed=0;
        s_category_menu=false;*redraw=shelf_key_paint(ctx);return true;
    }
    if(!s_shelf_key_mode || (key!=UI_KEY_1 && key!=UI_KEY_3))return false;
    int dir=key==UI_KEY_1?-1:1;
    s_shelf_key_pressed=dir;s_shelf_key_delta+=dir;
    if(s_shelf_key_delta>8)s_shelf_key_delta=8;
    if(s_shelf_key_delta< -8)s_shelf_key_delta= -8;
    s_shelf_key_until=ctx->now_ms+35;
    *redraw=shelf_key_paint(ctx);return true;
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction);
static EpdRect import_rect(int index) { return (EpdRect){36, 214 + index * 150, 612, 126}; }
static void clean_filename(char *dst, size_t cap, const char *filename) {
    copy_text(dst, cap, filename);
    char *ext = strrchr(dst, '.');
    if (ext) *ext = 0;
    char *start = dst;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != dst) memmove(dst, start, strlen(start) + 1);
    size_t len = strlen(dst);
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = 0;
}

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void fit_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void fit_fixed_text(char* text, int px, int width) {
    while (*text && ui_text_fixed_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void reader_footer_strip_number(char *dst, size_t cap, const char *source) {
    char title[128];
    copy_text(title, sizeof(title), source ? source : "");
// name 只被读取，但下面会把字符串字面量赋给它。CI 的 sdkconfig.ci 开了
// CONFIG_COMPILER_WARN_WRITE_STRINGS，字面量因此是 const char[]，赋给 char* 会丢 const 并在
// -Werror 下报错。声明成 const 指针即可。
// name is only read from, but it is assigned a string literal below. CI's sdkconfig.ci enables
// CONFIG_COMPILER_WARN_WRITE_STRINGS, which makes literals const char[], so assigning one to a
// char* discards the qualifier and fails under -Werror. A const pointer is enough.
    const char *name = title;
    while (*name == ' ' || *name == '\t') ++name;
    char *chapter_mark = !strncmp(name, "第", strlen("第")) ? strstr(name, "章") : NULL;
    if (chapter_mark && chapter_mark - name < 24) {
        char *after = chapter_mark + strlen("章");
        while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
               !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、")) ||
               !strncmp(after, "·", strlen("·"))) {
            if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
            else after += strlen("：");
        }
        name = after;
    }
    // 无名页不给占位名：底栏只留页码与全书进度。
    // Unnamed pages get no placeholder label; the footer keeps only page and book progress.
    if (!*name || !strcmp(name, "本章") || !strcmp(name, "章节")) name = "";
    copy_text(dst, cap, name);
}
static void reader_footer_chapter_name(char *dst, size_t cap) {
    char source[128] = {0};
    bool selected = s_selected_toc < book_navigation_count() &&
        book_navigation_chapter(s_selected_toc) == s_chapter &&
        book_navigation_title(s_selected_toc, source, sizeof(source)) == ESP_OK && source[0];
    if (!selected) {
        if (s_chapter_heading_title[0]) copy_text(source, sizeof(source), s_chapter_heading_title);
        else if (book_chapter_title(s_chapter, source, sizeof(source)) != ESP_OK) source[0] = 0;
    }
    reader_footer_strip_number(dst, cap, source);
}
static uint32_t chapter_end(void) {
    return s_chapter + 1 < book_chapter_count()
        ? book_chapter_byte_offset(s_chapter + 1) : book_total_bytes();
}
static size_t reader_page_offset(size_t page) {
    size_t off = book_layout_page_start_offset(page);
    if (s_jump_offset != SIZE_MAX && page == s_jump_page && s_jump_offset > off)
        off = s_jump_offset;
    return off;
}
static unsigned percent(size_t page) {
    uint32_t total = book_total_bytes();
    if (!total) return 0;
    if (s_chapter + 1 == book_chapter_count() && page + 1 == book_layout_page_count()) return 100;
    uint32_t off = book_position_bytes(book_chapter_byte_offset(s_chapter), chapter_end(),
                                       reader_page_offset(page), s_text_len);
    return (unsigned)((uint64_t)off * 100 / total);
}
static pending_progress_t* pending_find(const char* path) {
    for (pending_progress_t* p = s_pending; p; p = p->next) if (!strcmp(p->path, path)) return p;
    return NULL;
}
static int layout_name(uint8_t* fb, const char* name, int y, bool draw);
static EpdRect manage_panel(void) {
    int height = layout_name(NULL, s_managed.name, 0, false) + 495;
    return (EpdRect){UI_MARGIN - 16, 190 + (876 - height) / 2, ui_content_width() + 32, height};
}
static EpdRect manage_rect(int index, int count) {
    EpdRect panel = manage_panel();
    return ui_row_rect(index, count, panel.y + panel.height - 94, 76);
}
// 六个批量按钮排成两行各三格：上排选择与重扫，下排三个需要二次确认的操作。
// Six batch controls in two rows of three: selection and rescan on top, the three actions that
// ask for confirmation below.
static EpdRect batch_rect(int id) {
    return (EpdRect){36 + (id % 3) * 208, id < 3 ? 918 : 988, 196, 58};
}
static EpdRect manage_back_rect(void) { return (EpdRect){36, 92, 44, 44}; }
static EpdRect bulk_filter_rect(int id) { return (EpdRect){36 + id * 208, 216, 196, 70}; }
static EpdRect search_rect(int id) {
    if (id < 40) {
        int width = (ui_content_width() - 54) / 10;
        return (EpdRect){UI_MARGIN + id % 10 * (width + 6), 388 + id / 10 * 100, width, 88};
    }
    if (id < 43) return ui_row_rect(id - 40, 3, 808, 80);
    return ui_bar_rect(id - 43, 2);
}
static size_t selected_count(void) {
    size_t selected = 0;
    for (int i = 0; i < s_count; ++i) if (s_shelf[i].selected) ++selected;
    return selected;
}
static void clear_selection(void) {
    for (int i = 0; i < s_count; ++i) s_shelf[i].selected = false;
}
static void toggle_selection(int index) {
    if (index >= 0 && index < s_visible_count) s_shelf[index].selected = !s_shelf[index].selected;
}
static void select_page(int page) {
    for (int i = page * BOOK_BULK_ROWS; i < s_visible_count && i < (page + 1) * BOOK_BULK_ROWS; ++i) s_shelf[i].selected = true;
}

static void search_begin(void) {
    s_search_parent = s_view;
    memcpy(s_search_draft, s_query, sizeof(s_query));
    ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft));
    ui_keyboard_begin(&s_search_input, false);
    s_view = SEARCH;
}
static void refresh_search_matches(void) {
    for (int i = 0; i < s_count; ++i)
        s_shelf[i].search_match = read_pico_search_match(s_shelf[i].name, s_query);
}
static void search_finish(app_ctx_t* ctx, bool apply) {
    ui_keyboard_end();
    s_view = s_search_parent;
    if (apply) {
        memcpy(s_query, s_search_draft, sizeof(s_query));
        refresh_search_matches();
        clear_selection();
        sort_shelf(ctx);
        s_batch_message[0] = 0;
    }
    memset(s_search_draft, 0, sizeof(s_search_draft));
    ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft));
}

static bool pending_reserve(const char* path) {
    if (pending_find(path)) return true;
    pending_progress_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    copy_text(p->path, sizeof(p->path), path);
    // 阅读页内注册；传书 HTTP 尚未启动，退出后也不注销失败项。
    // Register in reading before transfer HTTP starts; failed records survive page exit.
    p->watch = book_progress_watch_create(path);
    if (!p->watch) { free(p); return false; }
    pending_progress_t** tail = &s_pending;
    while (*tail) tail = &(*tail)->next;
    *tail = p;
    return true;
}
static bool pending_restore(const char* path, uint32_t size, book_progress_t* out) {
    pending_progress_t* pending = pending_find(path);
    if (!pending || !pending->dirty || pending->value.file_size != size) return false;
    *out = pending->value;
    return true;
}
static void pending_discard(const char* path) {
    pending_progress_t** p = &s_pending;
    while (*p) {
        if (!strcmp((*p)->path, path)) {
            pending_progress_t* old = *p;
            *p = old->next;
            book_progress_watch_destroy(old->watch);
            free(old);
            break;
        }
        p = &(*p)->next;
    }
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}
static void pending_mark_latest(const char* path) {
    pending_progress_t** item = &s_pending;
    while (*item && strcmp((*item)->path, path)) item = &(*item)->next;
    if (*item) {
        pending_progress_t* current = *item;
        *item = current->next;
        current->next = NULL;
        pending_progress_t** tail = &s_pending;
        while (*tail) tail = &(*tail)->next;
        *tail = current;
    }
    copy_text(s_latest_path, sizeof(s_latest_path), path);
}
static void pending_drop_invalidated(void) {
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        if (book_progress_watch_invalidated(p->watch)) {
            if (p->dirty) s_pending_invalidated = true;
            pending_discard(p->path);
        }
        p = next;
    }
}
static delete_retry_t* delete_retry_find(const char* path) {
    for (delete_retry_t* p = s_delete_retries; p; p = p->next)
        if (!strcmp(p->entry.path, path)) return p;
    return NULL;
}
static bool pending_flush(pending_progress_t* p) {
    if (!p->dirty) return true;
    if (!p->progress_saved) {
        if (book_progress_save(p->path, &p->value) != ESP_OK) return false;
        p->progress_saved = true;
    }
    if (!strcmp(p->path, s_latest_path) && book_progress_set_last_path(p->path) != ESP_OK) return false;
    p->dirty = false;
    return true;
}
static void retry_progress(void) {
    s_save_failed = false;
    bool had_dirty = false;
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        bool dirty = p->dirty;
        had_dirty |= dirty;
        if (s_text && !strcmp(p->path, s_path)) { p = next; continue; }
        if (!pending_flush(p)) s_save_failed = true;
        else if (strcmp(p->path, s_path)) pending_discard(p->path);
        else if (dirty) s_unsaved = 0;
        p = next;
    }
    if (s_text && (s_unsaved || had_dirty)) save_progress();
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}

/* ---- 管理详情 / Management details ---- */
static int layout_name(uint8_t* fb, const char* name, int y, bool draw) {
    const char* at = name;
    while (*at) {
        char line[256];
        size_t used = 0;
        while (at[used]) {
            unsigned char first = (unsigned char)at[used];
            size_t n = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            size_t remain = strlen(at + used);
            if (n > remain) n = 1;
            if (used + n >= sizeof(line)) break;
            memcpy(line + used, at + used, n);
            line[used + n] = 0;
            if (ttf_text_width_px(UI_PX_CAPTION, line) > ui_content_width() && used) break;
            used += n;
        }
        if (!used) break;
        line[used] = 0;
        if (draw) ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
        y += 36;
        at += used;
    }
    return y;
}
static int draw_wrapped_name(uint8_t* fb, const char* name, int y) {
    return layout_name(fb, name, y, true);
}
static void draw_manage(uint8_t* fb) {
    EpdRect panel = manage_panel();
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text(fb, UI_MARGIN, panel.y + 18, UI_PX_BODY,
            s_clear_confirm ? (s_delete_confirm ? "确认删除文件？" : "确认清除进度？") : "图书详情", EPD_DRAW_ALIGN_LEFT, false);
    int y = draw_wrapped_name(fb, s_managed.name, panel.y + 74) + 12;
    char info[96];
    snprintf(info, sizeof(info), "%s · %s · %.2f MB", s_managed.is_flash ? "内置存储" : "TF 卡",
             strrchr(s_managed.name, '.') ? strrchr(s_managed.name, '.') + 1 : "", s_managed.size / 1048576.0);
    ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(info, sizeof(info), s_managed.has_progress ? "阅读进度 %u%%" : "尚无阅读进度", s_managed.pct);
    ui_text(fb, UI_MARGIN, y + 44, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, y + 88, UI_PX_CAPTION,
            s_managed.is_flash ? "/flash/books" : !strncmp(s_managed.path, "/sdcard/book/", 13) ? "/sdcard/book" : app_settings_books_dir(),
            EPD_DRAW_ALIGN_LEFT, false);
    if (s_manage_message[0]) ui_text(fb, UI_MARGIN, panel.y + panel.height - 270, UI_PX_CAPTION, s_manage_message, EPD_DRAW_ALIGN_LEFT, false);
    if (s_clear_confirm) {
        ui_text(fb, UI_MARGIN, panel.y + panel.height - 142, UI_PX_CAPTION, s_delete_confirm ? "删除后文件无法恢复" : "仅清阅读进度，保留图书文件", EPD_DRAW_ALIGN_LEFT, false);
        draw_control(fb, manage_rect(0, 2), "取消", 300);
        draw_control(fb, manage_rect(1, 2), s_delete_confirm ? "确认删除" : "确认清除", 301);
    } else if (s_file_removed) {
        draw_control(fb, manage_rect(0, 2), "关闭", 400);
        draw_control(fb, manage_rect(1, 2), "重试清理", 403);
    } else {
        draw_control(fb, (EpdRect){UI_MARGIN, panel.y + panel.height - 190, ui_content_width(), 68}, "前往文件管理重命名", 404);
        draw_control(fb, manage_rect(0, 3), "关闭", 400);
        draw_control(fb, manage_rect(1, 3), "清进度", 401);
        draw_control(fb, manage_rect(2, 3), "删除文件", 402);
    }
}

static void editor_start(void) {
    free(s_editor_cover);
    bool pending = false;
    s_editor_cover = load_cover_gray(s_managed.path, s_managed.name, "", false, &pending);
    copy_text(s_editor_title, sizeof(s_editor_title), s_managed.name);
    ui_text_edit_init(&s_editor_input, s_editor_title, sizeof(s_editor_title));
    s_editor_notice[0] = 0;
    ui_keyboard_begin(&s_editor_input, false);
    s_view = EDIT;
}



static void draw_editor(uint8_t* fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, UI_LOCK_WIDTH / 2, 107, 31, "编辑书籍", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 646, 107, 26, "完成", EPD_DRAW_ALIGN_RIGHT, false);
    ui_hairline(fb, 133, 36, 612, UI_GRAY_LIGHT);
    EpdRect cover = {36, 175, 112, 152};
    ui_fill_rect_fast(cover, UI_GRAY_LIGHT, fb);
    if (s_editor_cover) {

        for (int y = 0; y < cover.height; ++y) {
            for (int x = 0; x < cover.width; ++x) {
                const uint8_t tone = ui_contrast_gray(
                    s_editor_cover[(y * BOOK_COVER_H / cover.height) * BOOK_COVER_W +
                                   x * BOOK_COVER_W / cover.width]);
                epd_draw_pixel(cover.x + x, cover.y + y,
                    ui_image_dither_gray(tone, cover.x + x, cover.y + y), fb);
            }
        }
    } else {
        char title[80]; copy_text(title, sizeof(title), s_managed.name);
        fit_text(title, 22, cover.width - 16);
        ui_text_vc(fb, cover.x + cover.width / 2, cover.y + cover.height / 2, 22, title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, cover, 0, UI_GRAY_BLACK);
    char title[128]; copy_text(title, sizeof(title), s_managed.name);
    fit_text(title, 30, 460);
    ui_text(fb, 174, 192, 30, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 174, 244, 22, "只修改书架显示名称", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 356, 36, 612, UI_GRAY_LIGHT);
    ui_text(fb, 36, 378, 24, "书名", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect field = {36, 424, 612, 83};
    ui_text_input_draw(fb, &s_editor_input, field, 31, false, NULL);
    if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 530, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
    ui_keyboard_draw(fb, 560);
}
static void draw_search(uint8_t* fb) {
    ui_clear_page(fb); ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "搜索图书", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 646, 107, 25, "应用", EPD_DRAW_ALIGN_RIGHT, false);
    ui_text_input_draw(fb, &s_search_input, (EpdRect){UI_MARGIN, 200, ui_content_width(), 88}, UI_PX_BODY, false, NULL);
    ui_text_fixed_vc(fb, 36, 337, 23, "可输入中文、完整拼音、首字母或英文", EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_button(fb, (EpdRect){36, 392, 180, 66}, "清空", false);
    ui_keyboard_draw(fb, 560);
}
static void draw_import(uint8_t* fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "导入图书", EPD_DRAW_ALIGN_CENTER, false);
    ui_text(fb, 36, 166, 20, "选择导入方式", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 207, 36, 612, UI_GRAY_LIGHT);
    static const char *titles[] = {"浏览 TF 卡", "WiFi 传书", "热点传书", "USB 读卡", "云同步"};
    static const char *details[] = {
        "查看目录，打开或管理已有图书", "手机与墨水屏连接同一网络",
        "连接 kiikoread 热点后上传图书", "连接电脑，把图书放入 books 文件夹",
        "扫码绑定后拉取星曈云端的图书"
    };
    for (int i = 0; i < 5; ++i) {
        EpdRect r = import_rect(i);
        ui_fill_round_rect(fb, r, 22, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 22, 0xb0);
        ui_text(fb, r.x + 24, r.y + 25, 27, titles[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, r.x + 24, r.y + 78, 19, details[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, r.x + r.width - 24, r.y + r.height / 2, 26, "›", EPD_DRAW_ALIGN_RIGHT, false);
    }
    ui_text(fb, 36, 985, 19, "图书放入 TF 卡的 books 文件夹后会自动出现在书架。",
            EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 1);
}
static void draw_batch_confirmation(uint8_t* fb) {
    if (!s_batch_confirm) return;
    EpdRect panel = {54, 406, 576, 330};
    ui_fill_round_rect(fb, panel, 25, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, panel, 25, 0x48);
    char title[96];
    const char* verb = s_batch_kind == BATCH_DELETE ? "删除" :
                       s_batch_kind == BATCH_CLEAR ? "清除进度：" : "从书架移除";
    const char* note = s_batch_kind == BATCH_DELETE ? "删除文件不可撤销，失败项可重试" :
                       s_batch_kind == BATCH_CLEAR ? "仅清阅读进度，所有文件保留" :
                       "只移出书架，文件与阅读进度都保留";
    snprintf(title, sizeof(title), "%s %u 本图书？", verb, (unsigned)selected_count());
    ui_text_vc(fb, 342, 477, 28, title, EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 342, 541, 19, note, EPD_DRAW_ALIGN_CENTER, false);
    draw_control(fb, ui_row_rect(0, 2, 620, UI_BTN_H), "取消", 600);
    draw_control(fb, ui_row_rect(1, 2, 620, UI_BTN_H), "确认", 601);
}

static EpdRect bulk_nav_rect(int i) { return (EpdRect){36 + i * 208, UI_BAR_TOP, 196, UI_BAR_H}; }
static int bulk_nav_hit(int x, int y) {
    for (int i=0;i<3;++i) if(ui_rect_hit(bulk_nav_rect(i),x,y)) return i;
    return -1;
}
static void draw_bulk(uint8_t* fb, int leaf) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "管理书架", EPD_DRAW_ALIGN_CENTER, false);
    char summary[96];
    snprintf(summary, sizeof(summary), "%d 本图书 · 已选 %u 本", s_visible_count, (unsigned)selected_count());
    ui_text(fb, 36, 166, 20, s_batch_message[0] ? s_batch_message : summary, EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 200, 36, 612, 0x70);
    const char *filters[] = {s_filter == 0 ? "全部来源" : s_filter == 1 ? "TF 卡" : "内置",
                             s_recent_sort ? "按最近" : "按名称", s_query[0] ? "搜索中" : "搜索"};
    for (int i = 0; i < 3; ++i) {
        EpdRect r = bulk_filter_rect(i);
        ui_fill_round_rect(fb, r, 20, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 20, 0x70);
        ui_text_vc(fb, r.x + r.width / 2, r.y + 35, 21, filters[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int row = 0; row < BOOK_BULK_ROWS; ++row) {
        int index = leaf * BOOK_BULK_ROWS + row;
        if (index >= s_visible_count) break;
        EpdRect r = row_rect(row);
        bool selected = s_shelf[index].selected;
        ui_fill_round_rect(fb, r, 18, selected ? 0xd0 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 18, selected ? 0x60 : 0x70);
        char title[128]; copy_text(title, sizeof(title), s_shelf[index].name);
        fit_text(title, 26, 495);
        ui_text_vc(fb, r.x + 22, r.y + 31, 26, title, EPD_DRAW_ALIGN_LEFT, false);
        char detail[48];
        snprintf(detail, sizeof(detail), "%s · %s", s_shelf[index].is_flash ? "内置" : "TF 卡",
                 s_shelf[index].has_progress ? "已有阅读进度" : "未开始阅读");
        ui_text_vc(fb, r.x + 22, r.y + 62, 20, detail, EPD_DRAW_ALIGN_LEFT, false);
        epd_draw_circle(r.x + r.width - 37, r.y + 42, 12, 0x58, fb);
        if (selected) epd_fill_circle(r.x + r.width - 37, r.y + 42, 7, 0x38, fb);
    }
    if (!s_visible_count)
        ui_text_vc(fb, 342, 570, 26, s_message[0] ? s_message : "当前筛选没有图书", EPD_DRAW_ALIGN_CENTER, false);
    // 三列格宽只有 193 像素，标签控制在四字以内，放大系统字号也不会顶出按钮。
    // A three-column cell is 193 px wide, so labels stay within four characters and still fit
    // once the system font scale is turned up.
    static const char *labels[] = {"本页全选", "清除勾选", "重新扫描",
                                   "删除所选", "清进度", "移出书架"};
    for (int i = 0; i < 6; ++i) {
        EpdRect r = batch_rect(i);
        ui_fill_round_rect(fb, r, 18, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 18, 0x70);
        ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2, 20, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    const char *nav[] = {"上一页", "完成", "下一页"};
    char page[48];
    snprintf(page, sizeof(page), "%d / %d 页", leaf + 1, leaves());
    ui_text_vc(fb, 342, 1072, 20, page, EPD_DRAW_ALIGN_CENTER, false);
    for (int i = 0; i < 3; ++i) {
        EpdRect r = bulk_nav_rect(i);
        bool available = i == 1 || (i == 0 ? leaf > 0 : leaf + 1 < leaves());
        ui_fill_round_rect(fb, r, 20, i == 1 ? 0x20 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 20, available ? 0x70 : 0xb0);
        ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2, 25, nav[i], EPD_DRAW_ALIGN_CENTER, i == 1);
    }
    draw_batch_confirmation(fb);
}
static void save_progress(void) {
    if (!s_text || !s_path[0] || !book_layout_page_count()) return;
    bool was_failed = s_save_failed;
    pending_progress_t* pending = pending_find(s_path);
    if (!pending) { s_save_failed = true; return; }
    book_progress_t p = {
        .file_size = s_file_size, .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)reader_page_offset(s_page),
        .px = (uint8_t)s_px, .pct = (uint8_t)percent(s_page),
        .last_open_s = 0,
    };
    if (!pending->dirty || pending->value.file_size != p.file_size || pending->value.chapter != p.chapter ||
        pending->value.byte_off != p.byte_off || pending->value.px != p.px || pending->value.pct != p.pct) {
        pending->value = p;
        pending->progress_saved = false;
    }
    pending->dirty = true;
    if (pending_flush(pending)) {
        s_unsaved = 0;
        s_save_failed = false;
        for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
    }
    else { if (!s_unsaved) s_unsaved = 1; s_save_failed = true; }
    if (was_failed != s_save_failed) invalidate_prep();
}
static void invalidate_prep(void) { s_next_page = s_prep_page = -1; s_next_generation=0; memset(&s_prep_cursor,0,sizeof(s_prep_cursor)); }

// 目录使用系统字体；切换前等待预渲染结束，回正文时恢复阅读字体与字形缓存。
// The directory uses the system face; join painting before switching and restore the reader face on return.
static void set_reader_view(book_view_t view) {
    reader_indent_hold_cancel();
    s_reader_tap.pending = false;
    lock_draw();
    invalidate_prep();
    if (view == READING) app_font_activate_reading();
    else app_font_activate_system();
    s_view = view;
    unlock_draw();
}

/* ---- 阅读工具层 / Reader overlays ---- */
typedef struct {
    uint32_t magic, file_size, byte_off;
    uint16_t chapter;
    char path[BOOK_STORE_PATH_MAX];
} legacy_reader_bookmark_t;

typedef struct {
    uint16_t chapter;
    uint16_t reserved;
    uint32_t byte_off;
    uint32_t saved_s;
} reader_bookmark_entry_t;

typedef struct {
    uint32_t magic, file_size;
    uint16_t count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[BOOKMARK_MAX];
} reader_bookmarks_t;

static void bookmark_key(const char* path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "m_%08lx", (unsigned long)hash);
}
// 收藏按完整路径持久化，与章节书签分别存放。/ Persist whole-book favorites separately from page bookmarks.
static void favorite_key(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "f_%08lx", (unsigned long)hash);
}
static bool favorite_read_handle(nvs_handle_t h, const char *path) {
    uint8_t value = 0;
    char key[11]; favorite_key(path, key);
    return nvs_get_u8(h, key, &value) == ESP_OK && value == 1;
}
static bool favorite_load(const char *path) {
    nvs_handle_t h;
    if (nvs_open("rp_favs", NVS_READONLY, &h) != ESP_OK) return false;
    bool value = favorite_read_handle(h, path);
    nvs_close(h);
    return value;
}
static bool favorite_save(const char *path, bool value) {
    nvs_handle_t h;
    if (nvs_open("rp_favs", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; favorite_key(path, key);
    esp_err_t err = value ? nvs_set_u8(h, key, 1) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !value) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// 从书架移除只隐藏条目，不动文件也不清进度；再次打开这本书会重新上书架。
// Removing from the shelf hides the entry only: no file or progress is touched, and opening the
// book again puts it back. 值为完整路径，命中的同键记录属于别的书时拒绝覆盖。
// The value is the full path, so a colliding key owned by a different book is never overwritten.
#define SHELF_HIDDEN_NS "rp_shelf"
static void shelf_hidden_key(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "h_%08lx", (unsigned long)hash);
}
static bool shelf_hidden_read_handle(nvs_handle_t h, const char *path) {
    char stored[BOOK_STORE_PATH_MAX];
    char key[11];
    shelf_hidden_key(path, key);
    size_t len = sizeof(stored);
    return nvs_get_str(h, key, stored, &len) == ESP_OK && strcmp(stored, path) == 0;
}
static bool shelf_hidden_load(const char *path) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool hidden = shelf_hidden_read_handle(h, path);
    nvs_close(h);
    return hidden;
}
static bool shelf_hidden_save(const char *path, bool hidden) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11];
    shelf_hidden_key(path, key);
    char stored[BOOK_STORE_PATH_MAX];
    size_t len = sizeof(stored);
    esp_err_t existing = nvs_get_str(h, key, stored, &len);
    esp_err_t err = existing == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : existing;
    if (existing == ESP_OK && strcmp(stored, path) != 0) err = ESP_ERR_INVALID_STATE;
    // 恢复上架也校验路径，避免擦掉碰撞键属于另一书的隐藏记录。
    // Check ownership when unhiding too, so a collision cannot erase a different book removal.
    if (err == ESP_OK && hidden) err = nvs_set_str(h, key, path);
    else if (err == ESP_OK) {
        err = nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK)book_lock_collage_invalidate();
    return err == ESP_OK;
}
static bool bookmark_read_handle(nvs_handle_t h, reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    memset(marks, 0, sizeof(*marks));
    char key[11]; bookmark_key(s_path, key);
    size_t size = 0;
    if (nvs_get_blob(h, key, NULL, &size) != ESP_OK) return false;
    if (size == sizeof(legacy_reader_bookmark_t)) {
        legacy_reader_bookmark_t old;
        size = sizeof(old);
        if (nvs_get_blob(h, key, &old, &size) != ESP_OK ||
            old.magic != UINT32_C(0x52504d31) || old.file_size != s_file_size ||
            strcmp(old.path, s_path)) return false;
        marks->magic = UINT32_C(0x52504d32);
        marks->file_size = old.file_size;
        marks->count = 1;
        copy_text(marks->path, sizeof(marks->path), old.path);
        marks->entries[0] = (reader_bookmark_entry_t){
            .chapter = old.chapter, .byte_off = old.byte_off,
        };
        return true;
    }
    if (size != sizeof(*marks) || nvs_get_blob(h, key, marks, &size) != ESP_OK) return false;
    return marks->magic == UINT32_C(0x52504d32) && marks->file_size == s_file_size &&
        marks->count <= BOOKMARK_MAX && !strcmp(marks->path, s_path);
}

static bool bookmark_load(reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = bookmark_read_handle(h, marks);
    nvs_close(h);
    return ok;
}
static bool bookmark_is_current(void) {
    reader_bookmarks_t marks;
    if (!bookmark_load(&marks)) return false;
    size_t offset = book_layout_page_start_offset(s_page);
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) return true;
    return false;
}
static size_t bookmark_count(void) {
    reader_bookmarks_t marks;
    return bookmark_load(&marks) ? marks.count : 0;
}
static int bookmark_row_step(void) {
    int scale=app_settings_system_font_size();
    if(scale<100)scale=100;
    if(scale>200)scale=200;
    return 82+(scale-100)/2;
}
static int bookmark_rows(void) { return (s_bookmark_edit?424:492)/bookmark_row_step(); }
static EpdRect bookmark_row_rect(int row) {
    int step=bookmark_row_step();
    return (EpdRect){36,656+row*step,612,step-10};
}
static int bookmark_pages(size_t count) {
    return count ? ((int)count + bookmark_rows() - 1) / bookmark_rows() : 1;
}
static unsigned bookmark_selected_count(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < BOOKMARK_MAX; ++i) if (s_bookmark_selected & (UINT32_C(1) << i)) ++count;
    return count;
}
static void bookmark_compact(reader_bookmarks_t* marks, uint32_t selected) {
    size_t write = 0;
    for (size_t i = 0; i < marks->count; ++i)
        if (!(selected & (UINT32_C(1) << i))) marks->entries[write++] = marks->entries[i];
    marks->count = (uint16_t)write;
}
static bool bookmark_delete_selected(void) {
    if (!s_bookmark_selected || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    esp_err_t err = valid ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (valid) {
        bookmark_compact(&marks, s_bookmark_selected);
        char key[11]; bookmark_key(s_path, key);
        err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) return false;
    s_bookmark_selected = 0;
    s_bookmark_page = 0;
    s_bookmark_edit = marks.count != 0;
    return true;
}
static bool bookmark_toggle(void) {
    if (!s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; bookmark_key(s_path, key);
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    if (!valid) {
        memset(&marks, 0, sizeof(marks));
        marks.magic = UINT32_C(0x52504d32);
        marks.file_size = s_file_size;
        copy_text(marks.path, sizeof(marks.path), s_path);
    }
    uint32_t offset = (uint32_t)book_layout_page_start_offset(s_page);
    size_t found = marks.count;
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) { found = i; break; }
    bool remove = found < marks.count;
    if (remove) {
        memmove(&marks.entries[found], &marks.entries[found + 1],
                (marks.count - found - 1) * sizeof(marks.entries[0]));
        --marks.count;
    } else {
        if (marks.count == BOOKMARK_MAX) {
            memmove(&marks.entries[0], &marks.entries[1],
                    (BOOKMARK_MAX - 1) * sizeof(marks.entries[0]));
            --marks.count;
        }
        time_t now = time(NULL);
        marks.entries[marks.count++] = (reader_bookmark_entry_t){
            .chapter = (uint16_t)s_chapter, .byte_off = offset,
            .saved_s = now >= 1704067200 ? (uint32_t)now : 0,
        };
    }
    esp_err_t err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !marks.count) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        copy_text(s_reader_notice, sizeof(s_reader_notice), remove ? "书签已移除" : "书签已添加");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1600;
    } else {
        copy_text(s_reader_notice, sizeof(s_reader_notice), "书签保存失败");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
    }
    return err == ESP_OK;
}

static void draw_reader_tool_icon(uint8_t* fb, int cx, int cy, int kind, bool selected) {
    // 阅读工具栏图标全部来自 Lucide；已保存的书签用带勾的一档区分未保存。
    // Reader toolbar icons all come from Lucide; a saved bookmark uses the check variant.
    static const ui_icon_t tools[] = {
        UI_ICON_LIST,          // 0 目录
        UI_ICON_BOOKMARK,      // 1 书签
        UI_ICON_CHART_COLUMN,  // 2 阅读统计
        UI_ICON_BOOK_OPEN,     // 3 阅读设置 / Reading settings
        UI_ICON_TYPE,          // 4 字体设置
    };
    if (kind < 0 || kind >= (int)(sizeof(tools) / sizeof(tools[0]))) return;
    const ui_icon_t icon = (kind == 1 && selected) ? UI_ICON_BOOKMARK_CHECK : tools[kind];
    ui_draw_icon(fb, cx, cy, READER_TOOL_ICON_PX, icon, 0x38);
}

// 单行辅助文字保持在当前控件内；本机只使用当前活动字库，按原轮廓重新光栅化。
// Keep one-line UI text inside its slot, rasterizing native outlines in the existing active font.
static void reader_panel_text_vc(uint8_t *fb,int x,int cy,int px,const char *text,
                                  enum EpdFontFlags align,bool inverted) {
    int width=align==EPD_DRAW_ALIGN_LEFT?642-x:align==EPD_DRAW_ALIGN_RIGHT?x-42:
              2*(x<342?x-42:642-x);
    int left=align==EPD_DRAW_ALIGN_LEFT?x:align==EPD_DRAW_ALIGN_RIGHT?x-width:x-width/2;
    ui_text_box(fb,(EpdRect){left,cy-22,width,44},px,text,align,inverted);
}
static void reader_panel_text(uint8_t *fb,int x,int y,int px,const char *text,
                               enum EpdFontFlags align,bool inverted) {
    int height=ui_text_effective_px(px);if(height>44)height=44;
    reader_panel_text_vc(fb,x,y+height/2,px,text,align,inverted);
}

static void draw_sheet(uint8_t* fb, int top, const char* title) {
    EpdRect sheet = {0, top, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - top + 28};
    ui_fill_round_rect(fb, sheet, 28, UI_GRAY_WHITE);
    ui_fill_rect_fast((EpdRect){0, top + 28, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - top - 28}, UI_GRAY_WHITE, fb);
    ui_draw_control_frame(fb, sheet, 28, 0x58);
    ui_fill_round_rect(fb, (EpdRect){292, top + 14, 100, 7}, 3, 0x48);
    ui_text_box(fb,(EpdRect){110,top+26,464,66},30,title,EPD_DRAW_ALIGN_CENTER,false);
}

// 二级面板共用 AnimateIcons circle-chevron-left，含圆环与箭头，避免叠加手画框线。
// Secondary panels share the complete AnimateIcons circle-chevron-left without an extra hand-drawn ring.
static void draw_sheet_back(uint8_t* fb, int top) {
    ui_click_feedback_register(fb,(EpdRect){32,top+25,64,64},UI_CLICK_BACK,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT);
    ui_draw_icon(fb,64,top+57,READER_SHEET_BACK_PX,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT,0x30);
}

static void draw_pill_slider(uint8_t* fb, EpdRect r, const char* left, const char* right,
                             const char* value, int index, int count, int left_px, int right_px) {
    ui_fill_round_rect(fb, r, r.height / 2, 0xb8);
    ui_draw_control_frame(fb, r, r.height / 2, 0x50);
    int cy = r.y + r.height / 2;
    ui_text_box(fb,(EpdRect){r.x+12,cy-26,40,52},left_px,left,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){r.x+r.width-52,cy-26,40,52},right_px,right,EPD_DRAW_ALIGN_CENTER,false);
    int span = r.width - 128;
    int cx = r.x + 64 + (count > 1 ? span * index / (count - 1) : 0);
    EpdRect track = {r.x + 59, cy - 3, r.width - 118, 6};
    ui_fill_round_rect(fb, track, 3, 0x78);
    track.width = cx - track.x;
    if (track.width > 0) ui_fill_round_rect(fb, track, 3, 0x40);
    epd_fill_circle(cx, cy, 37, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, cy, 37, 0x48, fb);
    ui_text_box(fb,(EpdRect){cx-31,cy-28,62,56},22,value,EPD_DRAW_ALIGN_CENTER,false);
}

static EpdRect reader_slider_refresh_rect(int slider) {
    EpdRect r = reader_slider_rect(slider);
    // The knob extends four pixels outside the pill. Include that margin so
    // moving it restores the old white footprint and the rail beneath it.
    r.x -= 8; r.y -= 8; r.width += 16; r.height += 16;
    return r;
}

static const char* font_friendly_name(const ttf_font_item_t* item) {
    return item ? ttf_font_localized_name(item->name) : "";
}

static void draw_font_name_with_current_face(uint8_t* fb, int x, int y, const char* name, int width) {
    int above = 0, below = 0;
    int px=ui_text_effective_px(25);
    while (px>1 && ttf_text_width_px(px,name)>width) --px;
    ttf_measure_line_px(px, name, &above, &below);
    ttf_draw_text_px(fb, x, y + (above - below) / 2, px, name,
                     EPD_DRAW_ALIGN_CENTER, UI_INK_BLACK, UI_INK_WHITE);
}

// 用户确认双列：字体/字重在第一行，书内字体/排版在第二行；绘制与命中共用矩形。
// Approved paired rows: face/weight, then embedded faces/layout; paint and touch share rectangles.
// 放大文字时向上展开面板，保持双列与底部完成键的位置；命中也使用这些矩形。
// Expand sheets upward for larger UI text, retaining paired columns and the Done button; hits share these rectangles.
static int reader_ui_extra(int maximum) {
    int scale = app_settings_system_font_size();
    if (scale < 100) scale = 100;
    if (scale > 200) scale = 200;
    return (scale-100)*maximum/100;
}
static int reader_font_top(void) { return 580-reader_ui_extra(112); }
static EpdRect reader_font_card(int index) {
    int extra=reader_ui_extra(48), footer=reader_ui_extra(16);
    if (index>=4) return (EpdRect){index==4?36:350,1054-footer,298,72+footer};
    int row=index/2;
    return (EpdRect){index%2?350:36,796+row*132-(2-row)*extra-footer,
                     298,(row?110:116)+extra};
}
#define s_font_card reader_font_card(0)
#define s_weight_card reader_font_card(1)
#define s_bookfont_card reader_font_card(2)
#define s_layout_card reader_font_card(3)
#define s_rule_card reader_font_card(4)
#define s_antialias_card reader_font_card(5)
static void draw_reading_switch(uint8_t *fb, EpdRect track, bool on);
static const EpdRect s_rule_offset_up = {36, 980, 190, 88};
static const EpdRect s_rule_offset_reset = {246, 980, 192, 88};
static const EpdRect s_rule_offset_down = {458, 980, 190, 88};

static int background_compare(const void *a, const void *b) {
    return strcasecmp(((const reader_background_item_t *)a)->path, ((const reader_background_item_t *)b)->path);
}
// 字体页入口已按用户要求移除；保留内部旧面板处理，便于安全结束正在运行的预览。
// The requested font-sheet entry is removed; retain the internal legacy panel to end an active preview safely.
static void reader_background_scan(void) __attribute__((unused));
static void reader_background_scan(void) {
    free(s_backgrounds); s_backgrounds = NULL; s_background_count = s_background_selected = s_background_page = 0;
    s_background_notice[0] = 0; s_background_ready = false;
    s_background_preview_notice[0] = 0; s_background_prepare_pending = false;
    DIR *dir = opendir("/sdcard/pictures");
    if (!dir) { copy_text(s_background_notice, sizeof(s_background_notice), "请在 pictures 中放入 JPG 或 PNG 图片"); return; }
    // A bounded catalogue, independent of book count. No image data in the catalogue.
    s_backgrounds = heap_caps_calloc(128, sizeof(*s_backgrounds), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_backgrounds) copy_text(s_background_notice, sizeof(s_background_notice), "内存不足，请关闭图书后重试");
    struct dirent *ent;
    while (s_backgrounds && (ent = readdir(dir))) {
        // AppleDouble ._ files and other hidden metadata are not images.
        if (ent->d_name[0] == '.') continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
        char path[288]; struct stat st;
        int n = snprintf(path, sizeof(path), "/sdcard/pictures/%s", ent->d_name);
        if (n < 0 || n >= (int)sizeof(path) || stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;
        if (s_background_count == 128) { copy_text(s_background_notice, sizeof(s_background_notice), "仅显示前 128 张图片"); break; }
        strcpy(s_backgrounds[s_background_count++].path, path);
    }
    closedir(dir);
    if (s_background_count) qsort(s_backgrounds, s_background_count, sizeof(*s_backgrounds), background_compare);
    for (int i = 0; i < s_background_count; ++i)
        if (!strcmp(s_backgrounds[i].path, app_settings_book_background_path())) s_background_selected = i;
    s_background_page = s_background_selected / READER_BACKGROUND_ROWS;
    if (!s_background_count && !s_background_notice[0])
        copy_text(s_background_notice, sizeof(s_background_notice), "pictures 中暂无 JPG 或 PNG 图片");
}
static int reader_background_pages(void) {
    return s_background_count ? (s_background_count + READER_BACKGROUND_ROWS - 1) / READER_BACKGROUND_ROWS : 1;
}
static EpdRect reader_background_row(int row) { return (EpdRect){36, 344 + row * 96, 612, 96}; }
static void draw_background_directory(uint8_t *fb) {
    draw_sheet(fb, 200, "阅读背景"); draw_sheet_back(fb, 200);
    reader_panel_text(fb, 42, 303, 23, "pictures", EPD_DRAW_ALIGN_LEFT, false);
    char count[32]; snprintf(count, sizeof(count), "%d 张图片", s_background_count);
    reader_panel_text(fb, 642, 303, 22, count, EPD_DRAW_ALIGN_RIGHT, false);
    ui_fill_round_rect(fb, (EpdRect){36,344,612,672}, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, (EpdRect){36,344,612,672}, 20, 0x58);
    for (int row = 0; row < READER_BACKGROUND_ROWS; ++row) {
        int index = s_background_page * READER_BACKGROUND_ROWS + row;
        if (index >= s_background_count) break;
        EpdRect r = reader_background_row(row);
        bool active = !strcmp(s_backgrounds[index].path, app_settings_book_background_path());
        if (active) ui_fill_round_rect(fb, (EpdRect){r.x+6,r.y+6,r.width-12,r.height-12}, 16, 0xe0);
        if (row) ui_hairline(fb, r.y, r.x+20, r.width-40, 0x78);
        ui_draw_icon(fb, 78, r.y+48, 30, UI_ICON_FILE, 0x38);
        const char *base = strrchr(s_backgrounds[index].path, '/');
        char name[128]; copy_text(name, sizeof(name), base ? base+1 : s_backgrounds[index].path);
        fit_text(name, 25, 470);
        reader_panel_text_vc(fb, 112, r.y+48, 25, name, EPD_DRAW_ALIGN_LEFT, false);
        if (active) epd_fill_circle(595, r.y+48, 4, UI_GRAY_BLACK, fb);
        ui_draw_icon(fb, 623, r.y+48, 23, UI_ICON_CHEVRON_RIGHT, 0x38);
    }
    if (s_background_notice[0]) {
        char notice[96]; copy_text(notice, sizeof(notice), s_background_notice);
        fit_text(notice, 21, 572);
        reader_panel_text_vc(fb, 342, s_background_count ? 1098 : 655, 21, notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    char page[32]; snprintf(page, sizeof(page), "%d / %d", s_background_page+1, reader_background_pages());
    ui_draw_button(fb, (EpdRect){36,1040,190,52}, "上一页", false);
    reader_panel_text_vc(fb, 342, 1066, 22, page, EPD_DRAW_ALIGN_CENTER, false);
    ui_draw_button(fb, (EpdRect){458,1040,190,52}, "下一页", false);
    ui_draw_button(fb, (EpdRect){36,1120,294,72}, "无背景", false);
    ui_draw_button(fb, (EpdRect){354,1120,294,72}, "返回", false);
}
static void draw_background_picker(uint8_t *fb) {
    draw_sheet(fb, 200, "背景预览"); draw_sheet_back(fb, 200);
    EpdRect preview = {157, 316, 369, 656}; // 9:16, matching the complete wallpaper canvas.
    ui_fill_rect_fast(preview, UI_GRAY_WHITE, fb);
    if (s_background_ready) ui_reader_background_paint(fb, preview, false);
    ui_draw_control_frame(fb, preview, 0, UI_GRAY_BLACK);
    char name[128] = "暂无图片", position[48];
    if (s_background_count) {
        const char *base = strrchr(s_backgrounds[s_background_selected].path, '/');
        copy_text(name, sizeof(name), base ? base + 1 : "背景");
    }
    fit_text(name, 24, 584);
    reader_panel_text_vc(fb, 342, 1002, 24, name, EPD_DRAW_ALIGN_CENTER, false);
    snprintf(position, sizeof(position), "%d/%d · 上下滑动预览", s_background_count ? s_background_selected + 1 : 0, s_background_count);
    reader_panel_text_vc(fb, 342, 1046, 21, s_background_preview_notice[0] ? s_background_preview_notice : position, EPD_DRAW_ALIGN_CENTER, false);
    ui_draw_button(fb, (EpdRect){36, 1100, 190, 76}, "无背景", false);
    ui_draw_button(fb, (EpdRect){246, 1100, 192, 76}, "返回", false);
    ui_draw_button(fb, (EpdRect){458, 1100, 190, 76}, "使用背景", true);
}
static void reader_background_preview(int index) {
    if (index < 0 || index >= s_background_count) return;
    s_background_selected = index; s_background_page = index / READER_BACKGROUND_ROWS;
    s_reader_panel = READER_PANEL_BACKGROUND_PREVIEW;
    s_background_ready = false; s_background_prepare_pending = true;
    s_background_preview_notice[0] = 0;
}
static app_redraw_t reader_background_step(int dir) {
    if (s_reader_panel == READER_PANEL_BACKGROUND) {
        int next = s_background_page + dir;
        if (next < 0 || next >= reader_background_pages()) return APP_REDRAW_NONE;
        s_background_page = next; return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW)
        reader_background_preview(s_background_selected + dir);
    return APP_REDRAW_NONE; // Preview decoding is deferred to the shallow tick stack.
}
static app_redraw_t reader_background_action(uint16_t x, uint16_t y) {
    const EpdRect back = {32,225,64,64};
    if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) {
        bool close = y < 200 || ui_rect_hit(back, x, y);
        if (y >= 1100 && y < 1176) {
            if (x >= 36 && x < 226) {
                if (!app_settings_set_book_background_path("")) return APP_REDRAW_NONE;
                close = true;
            } else if (x >= 246 && x < 438) close = true;
            else if (x >= 458 && x < 648) {
                if (!s_background_count || !s_background_ready ||
                    !app_settings_set_book_background_path(s_backgrounds[s_background_selected].path)) return APP_REDRAW_NONE;
                close = true;
            }
        }
        if (!close) return APP_REDRAW_NONE;
        s_background_prepare_pending = false; s_background_ready = false;
        s_reader_panel = READER_PANEL_BACKGROUND;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (y < 200 || ui_rect_hit(back, x, y) || ui_rect_hit((EpdRect){354,1120,294,72}, x, y)) {
        s_reader_panel = READER_PANEL_FONT_SETTINGS;
        s_background_prepare_pending = false;
        free(s_backgrounds); s_backgrounds = NULL; s_background_count = s_background_page = 0;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){36,1120,294,72}, x, y)) {
        if (!app_settings_set_book_background_path("")) return APP_REDRAW_NONE;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){36,1040,190,52}, x, y)) return reader_background_step(-1);
    if (ui_rect_hit((EpdRect){458,1040,190,52}, x, y)) return reader_background_step(1);
    for (int row = 0; row < READER_BACKGROUND_ROWS; ++row) {
        int index = s_background_page * READER_BACKGROUND_ROWS + row;
        if (index < s_background_count && ui_rect_hit(reader_background_row(row), x, y)) {
            reader_background_preview(index); invalidate_prep(); return APP_REDRAW_NONE;
        }
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t reader_background_tick(void) {
    if (s_view != READING || s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW || !s_background_prepare_pending)
        return APP_REDRAW_NONE;
    s_background_prepare_pending = false;
    s_background_ready = s_background_count && s_background_selected >= 0 &&
        s_background_selected < s_background_count && ui_reader_background_prepare(s_backgrounds[s_background_selected].path);
    if (!s_background_ready) copy_text(s_background_preview_notice, sizeof(s_background_preview_notice), "图片无法解码或内存不足，请换一张");
    invalidate_prep(); return APP_REDRAW_PAGE;
}
// 字体名称复用已加载的阅读字库；不让UI字体选择规则把它换成内建界面字形。
// Preview the name in the loaded reader face, bypassing the built-in UI-face preference.
static void draw_reading_font_name(uint8_t *fb,EpdRect box,const char *name) {
    char shown[TTF_FONT_NAME_MAX];copy_text(shown,sizeof(shown),name);
    int prior_slot=ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    bool prior_aa=ttf_get_antialias();ttf_set_antialias(true);
    int px=ui_text_effective_px(27),above=0,below=0;
    while(px>1){
        ttf_measure_line_px(px,shown,&above,&below);
        if(above+below+2<=box.height)break;
        --px;
    }
    while(*shown && ttf_text_width_px(px,shown)>box.width){
        size_t n=strlen(shown)-1;
        while(n && ((unsigned char)shown[n]&0xc0)==0x80)--n;
        shown[n]=0;
    }
    ttf_measure_line_px(px,shown,&above,&below);
    ttf_draw_text_px(fb,box.x,box.y+box.height/2+(above-below)/2,px,shown,
                     EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
    ttf_set_antialias(prior_aa);ttf_font_select(prior_slot);
}
static void draw_font_pair(uint8_t *fb, EpdRect card, const char *title, const char *value) {
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 20, 0x70);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[64]; copy_text(shown, sizeof(shown), value);
    EpdRect value_box={card.x+21,card.y+card.height/2,card.width-76,card.height/2-8};
    if (!strcmp(title,"阅读字体"))draw_reading_font_name(fb,value_box,shown);
    else ui_text_box(fb,value_box,27,shown,EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,card.x+card.width-23,card.y+3*card.height/4,26,UI_ICON_CHEVRON_RIGHT,0x38);
}
static void reader_weight_value(char *out,size_t size,int steps){
    if(!steps){snprintf(out,size,"标准");return;}
    unsigned amount=steps<0?-steps:steps,whole=amount/16,fraction=(amount%16)*625;
    snprintf(out,size,"%c%u.%04u",steps<0?'-':'+',whole,fraction);
    size_t n=strlen(out);while(n&&out[n-1]=='0')out[--n]=0;if(n&&out[n-1]=='.')out[--n]=0;
}
static void draw_font_settings(uint8_t* fb) {
    // 界面标签独立于正文AA开关，退出后恢复正文设置。/ UI labels stay smooth independently of body AA; restore it afterward.
    bool prior_aa=ttf_get_antialias();ttf_set_antialias(true);
    draw_sheet(fb, reader_font_top(), "字体设置");
    int shown_px = s_reader_slider >= 0 ? s_reader_preview_px : s_px;
    char value[16]; snprintf(value, sizeof(value), "%d", shown_px);
    draw_pill_slider(fb, reader_slider_rect(0), "A", "A", value,
                     shown_px - BOOK_PX_MIN, BOOK_PX_MAX - BOOK_PX_MIN + 1, 20, 31);
    draw_font_pair(fb, s_font_card, "阅读字体", ttf_font_display_name());
    char weight_value[20];reader_weight_value(weight_value,sizeof(weight_value),app_settings_book_weight_fine());
    draw_font_pair(fb, s_weight_card, "字重", weight_value);
    draw_font_pair(fb, s_layout_card, "排版设置", "边距 · 行距");
    const bool book_fonts = app_settings_book_fonts();
    EpdRect card = s_bookfont_card;
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, card, 20, 0x70);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,"书内自带字体",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,card.width-122,card.height/2-8},27,book_fonts?"开启":"关闭",EPD_DRAW_ALIGN_LEFT,false);
    EpdRect toggle = {card.x+card.width-89,card.y+3*card.height/4-19,69,39};
    ui_fill_round_rect(fb, toggle, 19, book_fonts ? 0x38 : 0xd0);
    int knob = toggle.x + (book_fonts ? 49 : 20);
    epd_fill_circle(knob, toggle.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, toggle.y + 19, 16, 0x78, fb);
    EpdRect rule = s_rule_card;
    ui_fill_round_rect(fb, rule, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, rule, 20, 0x70);
    static const char *const rule_names[] = {"无", "虚线", "点线"};
    int selected_rule = app_settings_book_reading_line();
    ui_text_box(fb,(EpdRect){rule.x+22,rule.y+10,142,rule.height-20},25,"阅读线",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){rule.x+172,rule.y+10,77,rule.height-20},25,rule_names[selected_rule],EPD_DRAW_ALIGN_RIGHT,false);
    ui_draw_icon(fb,rule.x+rule.width-24,rule.y+rule.height/2,27,UI_ICON_CHEVRON_RIGHT,0x38);
    EpdRect aa = s_antialias_card;
    ui_fill_round_rect(fb, aa, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, aa, 20, 0x70);
    ui_text_box(fb,(EpdRect){aa.x+21,aa.y+10,aa.width-122,aa.height-20},25,"抗锯齿",EPD_DRAW_ALIGN_LEFT,false);
    draw_reading_switch(fb,(EpdRect){aa.x+aa.width-89,aa.y+aa.height/2-19,69,39},app_settings_book_antialias());
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 28, "完成", EPD_DRAW_ALIGN_CENTER, true);
    ttf_set_antialias(prior_aa);
}

static EpdRect reader_weight_rect(int index) { return (EpdRect){index?542:56,909,86,86}; }
static int reader_weight_step(int x,int y) {
    if(ui_rect_hit(reader_weight_rect(0),x,y))return -1;
    if(ui_rect_hit(reader_weight_rect(1),x,y))return 1;
    return 0;
}
static void draw_weight_settings(uint8_t *fb) {
    draw_sheet(fb, 580, "字重");
    draw_sheet_back(fb, 580);
    reader_panel_text_vc(fb, 342, 730, 22, "调整阅读文字粗细", EPD_DRAW_ALIGN_CENTER, false);
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    int weight=app_settings_book_weight_fine(),prior_weight=ttf_get_stroke_adjust_fine();
    ttf_set_stroke_adjust_fine(weight);
    ttf_draw_text_px(fb, 342, 833, 48, "阅读文字 Aa 123", EPD_DRAW_ALIGN_CENTER, 0, 15);
    ttf_set_stroke_adjust_fine(prior_weight);
    ui_fill_round_rect(fb,(EpdRect){36,895,612,114},57,0xe0);
    ui_draw_control_frame(fb,(EpdRect){36,895,612,114},57,0x30);
    for(int i=0;i<2;++i){EpdRect control=reader_weight_rect(i);epd_fill_circle(control.x+43,952,43,UI_GRAY_WHITE,fb);ui_draw_control_frame(fb,control,43,0x30);}
    ui_fill_rect_fast((EpdRect){87,951,24,3},weight<=-APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    ui_fill_rect_fast((EpdRect){573,951,24,3},weight>=APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    ui_fill_rect_fast((EpdRect){584,941,3,24},weight>=APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    char value[20];reader_weight_value(value,sizeof(value),weight);
    reader_panel_text_vc(fb,342,936,44,value,EPD_DRAW_ALIGN_CENTER,false);
    reader_panel_text_vc(fb,342,975,20,"像素 · 每次 1/16",EPD_DRAW_ALIGN_CENTER,false);
    reader_panel_text_vc(fb, 342, 1030, 20, "标准保留字体原始笔画", EPD_DRAW_ALIGN_CENTER, false);
    reader_panel_text_vc(fb, 342, 1070, 20, "同时适用于导入字体和书内字体", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static EpdRect rule_style_rect(int index) {
    return (EpdRect){36 + index * 208, 788, 196, 120};
}

static void draw_rule_settings(uint8_t *fb) {
    const int top = 580;
    draw_sheet(fb, top, "阅读线");
    draw_sheet_back(fb, top);
    reader_panel_text(fb, 42, 741, 21, "选择样式", EPD_DRAW_ALIGN_LEFT, false);
    static const char *const names[] = {"无", "虚线", "点线"};
    int selected = app_settings_book_reading_line();
    for (int i = 0; i < 3; ++i) {
        EpdRect card = rule_style_rect(i);
        ui_fill_round_rect(fb, card, 20, selected == i ? 0xc8 : 0xe8);
        ui_draw_control_frame(fb, card, 20, selected == i ? 0x40 : 0x78);
        reader_panel_text_vc(fb, card.x + card.width / 2, card.y + 37, 24, names[i], EPD_DRAW_ALIGN_CENTER, false);
        if (i) {
            int span = i == 1 ? 14 : 3, step = i == 1 ? 24 : 13;
            int start = card.x + 25, end = card.x + card.width - 25;
            for (int x = start; x < end; x += step)
                ui_fill_rect_fast((EpdRect){x, card.y + 83, x + span <= end ? span : end - x, 2}, 0x48, fb);
        } else reader_panel_text_vc(fb, card.x + card.width / 2, card.y + 83, 17, "不显示", EPD_DRAW_ALIGN_CENTER, false);
    }
    char offset[36];
    snprintf(offset, sizeof(offset), "上下位置  %+d", app_settings_book_reading_line_offset());
    reader_panel_text(fb, 42, 936, 21, offset, EPD_DRAW_ALIGN_LEFT, false);
    const EpdRect controls[] = {s_rule_offset_up, s_rule_offset_reset, s_rule_offset_down};
    const char *const labels[] = {"上移 2px", "恢复居中", "下移 2px"};
    for (int i = 0; i < 3; ++i) {
        ui_fill_round_rect(fb, controls[i], 20, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, controls[i], 20, selected ? 0x60 : 0xa0);
        reader_panel_text_vc(fb, controls[i].x + controls[i].width / 2,
                   controls[i].y + controls[i].height / 2, 22, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    if (!selected) reader_panel_text(fb, 42, 1080, 18, "选择虚线或点线后可调整位置", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect done = {36, 1113, 612, 72};
    ui_fill_round_rect(fb, done, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1149, 23, "返回字体设置", EPD_DRAW_ALIGN_CENTER, true);
}

static EpdRect indent_adjust_rect(int index) {
    const EpdRect controls[] = {{214,1044,100,72}, {322,1044,208,72}, {538,1044,100,72}};
    return controls[index];
}
static EpdRect reader_layout_back_rect(void) { return (EpdRect){36,1132,612,72}; }
static int indent_adjust_hit(int x, int y) {
    for(int i=0;i<3;++i){
        EpdRect r=indent_adjust_rect(i);
        r.x-=4;r.width+=8;r.y-=6;r.height+=16;
        if(ui_rect_hit(r,x,y))return i;
    }
    return -1;
}
static void reader_indent_hold_cancel(void) {
    memset(&s_indent_hold,0,sizeof(s_indent_hold));
    s_indent_feedback_pending=false;
}
static void draw_indent_adjustments(uint8_t *fb) {
    bool preview=s_indent_hold.active&&!s_indent_hold.cancelled;
    int value=preview?s_indent_hold.value:app_settings_book_indent_adjust();
    char adjustment[28];snprintf(adjustment,sizeof(adjustment),"%+d px · 复位",value);
    const char *labels[]={"−",adjustment,"+"};
    for(int i=0;i<3;++i){
        EpdRect control=indent_adjust_rect(i);
        bool pressed=preview&&s_indent_hold.control==i;
        ui_fill_round_rect(fb,control,16,pressed?UI_GRAY_BLACK:0xf0);
        ui_draw_control_frame(fb,control,16,0x50);
        ui_text_box(fb,(EpdRect){control.x+8,control.y+5,control.width-16,control.height-10},i==1?22:31,labels[i],EPD_DRAW_ALIGN_CENTER,pressed);
    }
}
static app_redraw_t reader_indent_feedback(app_ctx_t *ctx) {
    ui_text_set_system_scale(true);draw_indent_adjustments(ctx->fb);ui_text_set_system_scale(false);
    s_area=(EpdRect){214,1044,424,72};s_mode=MODE_DU;s_indent_feedback_pending=true;
    return APP_REDRAW_AREA;
}
static bool reader_indent_present(app_ctx_t *ctx,app_redraw_t redraw) {
    bool pending=s_indent_feedback_pending;s_indent_feedback_pending=false;
    if(!pending||redraw!=APP_REDRAW_AREA||s_view!=READING||s_reader_panel!=READER_PANEL_LAYOUT_SETTINGS||ctx->request_app)return false;
    guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,s_area));
    return true;
}
static void draw_layout_settings(uint8_t* fb) {
    const int top = 500;
    draw_sheet(fb, top, "排版设置");
    draw_sheet_back(fb, top);
    int shown_margin = s_reader_slider >= 0 ? s_reader_preview_margin : s_margin;
    int shown_line = s_reader_slider >= 0 ? s_reader_preview_line : app_settings_book_line_spacing();
    int shown_para = s_reader_slider >= 0 ? s_reader_preview_para : app_settings_book_paragraph_spacing();
    int shown_tracking = s_reader_slider >= 0 ? s_reader_preview_tracking : app_settings_book_tracking();
    int margin_widths[BOOK_MARGIN_CHOICES], margin_values[BOOK_MARGIN_CHOICES];
    int margin_count = reader_margin_levels(s_px, (shown_tracking + APP_BOOK_TRACK_MIN),
                                             margin_widths, margin_values);
    draw_pill_slider(fb, reader_slider_rect(1), "小", "大", "边距",
                     reader_margin_level_for(shown_margin, s_px, (shown_tracking + APP_BOOK_TRACK_MIN)),
                     margin_count, 21, 21);
    int line_i = shown_line - 110;
    if (line_i < 0) line_i = 0;
    if (line_i > 40) line_i = 40;
    draw_pill_slider(fb, reader_slider_rect(2), "紧", "松", "行距", line_i, 41, 21, 21);
    reader_panel_text(fb, 42, 715, 21, "段距", EPD_DRAW_ALIGN_LEFT, false);
    char value[16]; snprintf(value, sizeof(value), "%d%%", shown_para);
    draw_pill_slider(fb, reader_slider_rect(3), "紧", "松", value, app_settings_book_paragraph_level((uint8_t)shown_para), APP_BOOK_PARAGRAPH_COUNT, 21, 21);
    reader_panel_text(fb, 42, 835, 21, "字间距", EPD_DRAW_ALIGN_LEFT, false);
    char track_name[24];snprintf(track_name,sizeof(track_name),"%+d px",shown_tracking+APP_BOOK_TRACK_MIN);
    draw_pill_slider(fb, reader_slider_rect(4), "紧", "松", track_name, shown_tracking, APP_BOOK_TRACK_COUNT, 21, 21);
    ui_text_box(fb,(EpdRect){42,970,154,64},24,"首行缩进",EPD_DRAW_ALIGN_LEFT,false);
    static const char* indent_names[] = {"无", "1字", "2字", "3字"};
    int indent = app_settings_book_indent();
    for (int i = 0; i < 4; ++i) {
        EpdRect choice = {214 + i * 108, 970, 100, 64};
        bool selected = i == indent;
        ui_fill_round_rect(fb, choice, 16, selected ? UI_GRAY_BLACK : 0xe4);
        ui_draw_control_frame(fb, choice, 16, selected ? UI_GRAY_BLACK : 0x98);
        ui_text_box(fb,(EpdRect){choice.x+8,choice.y+5,choice.width-16,choice.height-10},23,indent_names[i],EPD_DRAW_ALIGN_CENTER,selected);
    }
    ui_text_box(fb,(EpdRect){42,1044,154,72},24,"缩进微调",EPD_DRAW_ALIGN_LEFT,false);
    draw_indent_adjustments(fb);
    EpdRect back = reader_layout_back_rect();
    ui_fill_round_rect(fb, back, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1168, 24, "返回字体设置", EPD_DRAW_ALIGN_CENTER, true);
}

// 用户确认半屏双列布局；绘制与点击共用同一矩形，间隙不穿透。/ Approved two-column half sheet; paint and hit testing share rectangles with inert gaps.
static EpdRect reading_setting_rect(int index) {
    int extra=reader_ui_extra(56), footer=reader_ui_extra(36);
    if (index < 0 || index >= 8) return (EpdRect){0, 0, 0, 0};
    int row=index/2;
    // 四行双列：竖排占第四行右格，与「关闭书内图片」并排；行距收到 104，
    // 四行底部才仍然留在「完成」之上。
    // Four two-column rows: the vertical switch takes the fourth row's right cell beside "hide
    // in-book images", and a 104 pitch keeps every row clear of Done.
    return (EpdRect){36+(index%2)*314,702+row*104-(3-row)*extra-footer,298,98+extra};
}
static int reading_settings_top(void) { return 580-reader_ui_extra(204); }
static EpdRect reading_toggle_rect(int index) {
    return reading_setting_rect(index == 0 ? 3 : index == 1 ? 5 : index == 2 ? 6 : 7);
}
static void draw_reading_switch(uint8_t *fb, EpdRect track, bool on) {
    ui_fill_round_rect(fb, track, 19, on ? 0x38 : 0xc4);
    int cx = track.x + (on ? 49 : 20);
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, 0x78, fb);
}
static void draw_reading_card(uint8_t *fb, int index, const char *title,
                              const char *value, bool cycle) {
    EpdRect card = reading_setting_rect(index);
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 20, 0x68);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[48]; copy_text(shown,sizeof(shown),value);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,222,card.height/2-8},26,shown,EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,card.x+272,card.y+3*card.height/4,26,
                 cycle ? UI_ICON_ROTATE_CW : UI_ICON_CHEVRON_RIGHT, 0x38);
}
static void draw_reading_toggle(uint8_t *fb, int index, const char *title,
                                const char *detail, bool on) {
    EpdRect row = reading_toggle_rect(index);
    ui_fill_round_rect(fb, row, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, row, 20, 0x68);
    if (index == 2) {
        // 半宽格放两行：标题与说明各降一号字，开关仍留在右端。
        // A half-width cell carries two lines: title and detail step down a size, the switch stays right.
        ui_text_box(fb,(EpdRect){row.x+20,row.y+10,row.width-40,row.height/2-16},21,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){row.x+20,row.y+row.height/2,row.width-40,row.height/2-10},17,detail,EPD_DRAW_ALIGN_LEFT,false);
        draw_reading_switch(fb,(EpdRect){row.x+row.width-90,row.y+3*row.height/4-19,69,39},on);
    } else {
        ui_text_box(fb,(EpdRect){row.x+21,row.y+6,row.width-42,row.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){row.x+21,row.y+row.height/2,166,row.height/2-8},26,on?"开启":"关闭",EPD_DRAW_ALIGN_LEFT,false);
        draw_reading_switch(fb,(EpdRect){row.x+209,row.y+3*row.height/4-19,69,39},on);
    }
}
static void draw_reading_settings(uint8_t* fb) {
    draw_sheet(fb, reading_settings_top(), "阅读设置");
    char interval[24];
    unsigned pages = app_settings_reader_full_pages();
    if (pages) snprintf(interval, sizeof(interval), "每 %u 页", pages);
    else copy_text(interval, sizeof(interval), "不强制");
    draw_reading_card(fb, 0, "自动全刷", interval, true);
    draw_reading_card(fb, 1, "翻页效果", app_settings_reader_turn_effect()==2?"速读黑白":
        app_settings_reader_turn_effect()?"水波纹":"普通",true);
    draw_reading_card(fb, 2, "翻页模式", app_settings_reader_vertical_turn() ? "上下翻页" : "左右翻页", false);
    draw_reading_toggle(fb, 0, "电源键翻页", NULL, app_settings_reader_power_turn());
    draw_reading_card(fb, 4, "按键控制", "自定义三键", false);
    draw_reading_toggle(fb, 1, "全屏沉浸", NULL, app_settings_reader_immersive());
    draw_reading_toggle(fb, 2, "关闭书内图片", "跳过正文插图，保留原书文件", app_settings_reader_hide_images());
    draw_reading_toggle(fb, 3, "竖排（从右到左）", NULL, app_settings_book_vertical());
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static void draw_turn_settings(uint8_t *fb) {
    draw_sheet(fb, 270, "翻页模式");
    draw_sheet_back(fb, 270);
    bool vertical = app_settings_reader_vertical_turn();
    for (int i = 0; i < 2; ++i) {
        int y = 370 + i * 322;
        bool selected = vertical == (i != 0);
        EpdRect card = {36, y, 612, 292};
        ui_fill_round_rect(fb, card, 22, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, card, 22, 0x50);
        ui_draw_control_frame(fb, (EpdRect){37, y + 1, 610, 290}, 21, 0x50);
        EpdRect diagram = {64, y + 37, 182, 220};
        ui_fill_round_rect(fb, diagram, 12, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, diagram, 12, 0x58);
        if (i) {
            ui_hairline(fb, diagram.y + 73, 65, 180, 0x58);
            reader_panel_text_vc(fb, 155, diagram.y + 36, 21, "上一页", EPD_DRAW_ALIGN_CENTER, false);
            reader_panel_text_vc(fb, 155, diagram.y + 147, 21, "下一页", EPD_DRAW_ALIGN_CENTER, false);
        } else {
            ui_fill_rect_fast((EpdRect){155, diagram.y + 1, 1, 218}, 0x58, fb);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+94-15,78,30},21,"上一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+94-15,78,30},21,"下一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
        }
        ui_text_box(fb,(EpdRect){274,y+43,350,44},32,i ? "上下翻页" : "左右翻页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+112,350,44},23,i ? "上 1/3：上一页" : "左 1/2：上一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+158,350,44},23,i ? "下 2/3：下一页" : "右 1/2：下一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+213,350,44},21,"左右滑动均可翻页",EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(610, y + 61, 12, 0x38, fb);
        if (selected) epd_fill_circle(610, y + 61, 7, UI_GRAY_BLACK, fb);
    }
    reader_panel_text_vc(fb, 342, 1051, 21, "中间区域双击切换全屏 · 始终可用", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static const char *reader_key_label(app_reader_key_action_t action) {
    static const char *const labels[] = {"上一页", "下一页", "阅读工具栏", "切换全屏", "返回主页", "手动全刷", "不操作"};
    return labels[action >= 0 && action < APP_READER_KEY_COUNT ? action : APP_READER_KEY_NONE];
}
static EpdRect reader_key_card(unsigned key) { return (EpdRect){36 + (int)key * 208, 506, 196, 114}; }
static EpdRect reader_key_preset_card(unsigned preset) { return (EpdRect){36, 810 + (int)(preset - 1) * 148, 612, 136}; }
static bool reader_key_preset_selected(unsigned preset) {
    static const app_reader_key_action_t actions[2][3] = {
        {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT},
        {APP_READER_KEY_HOME, APP_READER_KEY_FULLSCREEN, APP_READER_KEY_TOOLS}
    };
    for (unsigned i = 0; i < 3; ++i) if (app_settings_reader_key_action(i) != actions[preset - 1][i]) return false;
    return app_settings_reader_hold_action() == (preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH);
}
static void draw_key_settings(uint8_t *fb) {
    draw_sheet(fb, 270, "按键控制");
    draw_sheet_back(fb, 270);
    static const char *const names[] = {"左键", "中键", "右键"};
    for (unsigned i = 0; i < 3; ++i) {
        int cx = 134 + (int)i * 208;
        if (i == 1) {
            epd_draw_circle(cx, 418, 25, UI_GRAY_BLACK, fb);
            epd_draw_circle(cx, 418, 24, UI_GRAY_BLACK, fb);
        } else ui_fill_round_rect(fb, (EpdRect){cx - 26, 415, 52, 6}, 3, UI_GRAY_BLACK);
        reader_panel_text_vc(fb, cx, 473, 22, names[i], EPD_DRAW_ALIGN_CENTER, false);
        EpdRect card = reader_key_card(i);
        ui_fill_round_rect(fb, card, 20, 0xf0);
        ui_draw_control_frame(fb, card, 20, 0x58);
        ui_text_box(fb,(EpdRect){card.x+17,card.y+7,card.width-57,36},20,"单击",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){card.x+12,card.y+52,card.width-24,52},24,reader_key_label(app_settings_reader_key_action(i)),EPD_DRAW_ALIGN_CENTER,false);
        ui_draw_icon(fb, card.x + card.width - 23, card.y + 24, 20, UI_ICON_CHEVRON_DOWN, 0x38);
    }
    EpdRect hold = {36, 646, 612, 96};
    ui_fill_round_rect(fb, hold, 20, 0xf0); ui_draw_control_frame(fb, hold, 20, 0x58);
    ui_text_box(fb,(EpdRect){58,652,238,48},26,"中键长按",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){322,652,270,48},25,reader_key_label(app_settings_reader_hold_action()),EPD_DRAW_ALIGN_RIGHT,false);
    ui_draw_icon(fb, 625, 679, 20, UI_ICON_CHEVRON_DOWN, 0x38);
    reader_panel_text(fb, 58, 710, 19, "点击选择长按功能，例如返回主页", EPD_DRAW_ALIGN_LEFT, false);
    reader_panel_text_vc(fb, 342, 775, 20, "屏幕中间双击切换全屏，不受按键设置影响", EPD_DRAW_ALIGN_CENTER, false);
    for (unsigned preset = 1; preset <= 2; ++preset) {
        EpdRect card = reader_key_preset_card(preset);
        bool selected = reader_key_preset_selected(preset);
        ui_fill_round_rect(fb, card, 20, selected ? 0xd8 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, card, 20, 0x58);
        char title[32]; snprintf(title, sizeof(title), "推荐设置 %u", preset);
        ui_text_box(fb,(EpdRect){58,card.y+7,344,49},26,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){435,card.y+7,186,49},20,selected?"已应用":"点击应用",EPD_DRAW_ALIGN_RIGHT,false);
        ui_text_box(fb,(EpdRect){58,card.y+59,564,40},21,preset==1?"左上一页 · 中工具栏 · 右下一页":"左回首页 · 中全屏 · 右工具栏",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){58,card.y+103,564,34},19,preset==1?"中键长按：返回主页":"中键长按：手动全刷",EPD_DRAW_ALIGN_LEFT,false);
    }
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}
static void draw_key_picker(uint8_t *fb) {
    static const char *const titles[] = {"左键功能", "中键功能", "右键功能", "中键长按功能"};
    draw_sheet(fb, 270, titles[s_reader_key_pick < 4 ? s_reader_key_pick : 0]);
    draw_sheet_back(fb, 270);
    for (int i = 0; i < APP_READER_KEY_COUNT; ++i) {
        EpdRect card = {36, 390 + i * 94, 612, 78};
        bool selected = (s_reader_key_pick == 3 ? app_settings_reader_hold_action() : app_settings_reader_key_action(s_reader_key_pick)) == i;
        ui_fill_round_rect(fb, card, 20, selected ? 0xd8 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, card, 20, 0x58);
        reader_panel_text_vc(fb, 58, card.y + 39, 27, reader_key_label(i), EPD_DRAW_ALIGN_LEFT, false);
        epd_draw_circle(608, card.y + 39, 11, 0x38, fb);
        if (selected) epd_fill_circle(608, card.y + 39, 6, UI_GRAY_BLACK, fb);
    }
    reader_panel_text_vc(fb, 342, 1112, 20, s_reader_key_error[0] ? s_reader_key_error : "从底部上滑可随时打开阅读工具栏", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "返回", EPD_DRAW_ALIGN_CENTER, true);
}

static int reader_font_picker_top(void) { return app_settings_system_font_size()>=150?270:584; }
static EpdRect reader_font_pick_rect(int slot) {
    bool large=app_settings_system_font_size()>=150;
    int columns=large?2:3;
    return (EpdRect){36+(slot%columns)*(large?314:204),
                     reader_font_picker_top()+(large?150:131)+(slot/columns)*146,
                     large?298:180,126};
}
static void draw_font_picker(uint8_t* fb) {
    const int top = reader_font_picker_top();
    draw_sheet(fb, top, "字体");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    int count = ttf_font_count();
    int pages = count > 0 ? (count + BOOK_FONT_PAGE - 1) / BOOK_FONT_PAGE : 1;
    char current[96];
    snprintf(current, sizeof(current), "当前选择 · %s  %d/%d", ttf_font_display_name(), s_font_page + 1, pages);
    fit_text(current, 20, 540);
    ui_text_box(fb,(EpdRect){90,top+94,504,32},20,current,EPD_DRAW_ALIGN_CENTER,false);
    int start = s_font_page * BOOK_FONT_PAGE;
    char active[TTF_FONT_PATH_MAX]; copy_text(active, sizeof(active), ttf_font_path());
    bool active_builtin = ttf_font_is_builtin();
    for (int slot = 0; slot < BOOK_FONT_PAGE && start + slot < count; ++slot) {
        const ttf_font_item_t* item = ttf_font_item(start + slot);
        EpdRect card = reader_font_pick_rect(slot);
        bool selected = !strcmp(active, item->path);
        ui_fill_round_rect(fb, card, 18, selected ? 0xc2 : 0xe4);
        ui_draw_control_frame(fb, card, 18, selected ? 0x48 : 0x98);
        char label[TTF_FONT_NAME_MAX];
        copy_text(label, sizeof(label), font_friendly_name(item));
        if (ttf_font_open(item->path) == ESP_OK) {
            fit_text(label,25,card.width-32);
            draw_font_name_with_current_face(fb, card.x + card.width/2, card.y + 63, label,card.width-32);
        } else {
            fit_fixed_text(label, ui_text_effective_px(25), card.width - 32);
            ui_text_box(fb,ui_inset_rect(card,16),25,label,EPD_DRAW_ALIGN_CENTER,false);
        }
        if (selected) epd_fill_circle(card.x+card.width-22,card.y+22,7,UI_GRAY_BLACK,fb);
    }
    if (active_builtin) (void)ttf_font_open_builtin();
    else if (active[0]) (void)ttf_font_open(active);
    // 字体名称预览归还真实阅读字库后，再确认本次选择是否回退。
    // Observe the actual reader face after name previews have restored it.
    reader_font_choice_finish();
    if (!count) reader_panel_text_vc(fb, 342, 780, 24, "所选字体目录中没有可用字体", EPD_DRAW_ALIGN_CENTER, false);
}

static EpdRect reader_font_recovery_button(int index) {
    return (EpdRect){66 + index * 286, 764, 266, 82};
}
static void draw_font_recovery(uint8_t *fb) {
    ui_text_set_system_scale(true);
    EpdRect card = {42, 410, 600, 464};
    ui_fill_round_rect(fb, card, 26, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 26, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){66,438,552,80}, 30, "阅读字体加载异常", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){66,538,552,66}, 24, "已多次回退到内建字体", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){66,610,552,66}, 24, "可尝试重启后重新选择", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){66,688,552,58}, 21,
        s_font_recovery_save_failed ? "进度保存失败，请重试" : "重启前将保存阅读进度", EPD_DRAW_ALIGN_CENTER, false);
    for (int i = 0; i < 2; ++i) {
        EpdRect button = reader_font_recovery_button(i);
        ui_fill_round_rect(fb, button, 22, i ? UI_GRAY_BLACK : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, button, 22, UI_GRAY_BLACK);
        ui_text_box(fb, ui_inset_rect(button, 12), 25,
            i ? "立即重启" : "暂不重启", EPD_DRAW_ALIGN_CENTER, i != 0);
    }
    ui_text_set_system_scale(false);
}

// 绘制与命中共用矩形，留出键盘、按钮与底部安全间距。
// Drawing and hit testing share geometry with space around keys, actions and the bottom edge.
#define READER_STATS_TOP 586
#define READER_JUMP_TOP 518
static EpdRect reader_stats_control_rect(int control) {
    if (control == 1) return (EpdRect){32, READER_STATS_TOP + 25, 64, 64};
    if (control == 2) return (EpdRect){54, READER_STATS_TOP + 348, 74, 72};
    if (control == 3) return (EpdRect){128, READER_STATS_TOP + 348, 254, 72};
    if (control == 4) return (EpdRect){382, READER_STATS_TOP + 348, 74, 72};
    if (control == 5) return (EpdRect){476, READER_STATS_TOP + 348, 154, 72};
    if (control == 6) return (EpdRect){36, READER_STATS_TOP + 480, 294, 78};
    if (control == 7) return (EpdRect){354, READER_STATS_TOP + 480, 294, 78};
    return (EpdRect){0};
}
static EpdRect reader_chapter_key_rect(int key) {
    if (key >= 0 && key < 12)
        return (EpdRect){54 + key % 3 * 196, READER_JUMP_TOP + 269 + key / 3 * 75, 184, 65};
    if (key == 12) return (EpdRect){54, READER_JUMP_TOP + 582, 278, 72};
    if (key == 13) return (EpdRect){352, READER_JUMP_TOP + 582, 278, 72};
    return (EpdRect){0};
}
static EpdRect reader_chapter_undo_rect(void) { return (EpdRect){178, 224, 328, 46}; }
static bool reader_chapter_undo_visible(void) {
    return s_chapter_jump_undo.valid && s_text && s_view == READING &&
        s_chapter_jump_undo.chapter == s_chapter && s_reader_panel == READER_PANEL_NONE;
}
static int reader_chapter_jump_hit(uint16_t x, uint16_t y) {
    if (s_reader_panel == READER_PANEL_STATS) {
        if (y < READER_STATS_TOP) return 0;
        for (int i = 1; i <= 7; ++i) if (ui_rect_hit(reader_stats_control_rect(i), x, y)) return i;
    } else if (s_reader_panel == READER_PANEL_CHAPTER_JUMP) {
        if (y < READER_JUMP_TOP) return 32;
        for (int i = 0; i < 14; ++i) if (ui_rect_hit(reader_chapter_key_rect(i), x, y)) return 20 + i;
    }
    return -1;
}
static void draw_reader_stats(uint8_t* fb) {
    const int top = READER_STATS_TOP;
    draw_sheet(fb, top, "阅读统计");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    char values[3][32];
    snprintf(values[0], sizeof(values[0]), "%u%%", percent(s_page));
    snprintf(values[1], sizeof(values[1]), "%lu 分钟", (unsigned long)(s_session_read_ms / 60000));
    snprintf(values[2], sizeof(values[2]), "%lu 次", (unsigned long)s_session_turns);
    const char* labels[] = {"阅读进度", "本次阅读", "翻页"};
    for (int i = 0; i < 3; ++i) {
        int cx = 114 + i * 228;
        ui_text_box(fb,(EpdRect){cx-100,top+100,200,48},33,values[i],EPD_DRAW_ALIGN_CENTER,false);
        ui_text_box(fb,(EpdRect){cx-100,top+154,200,36},20,labels[i],EPD_DRAW_ALIGN_CENTER,false);
        if (i < 2) ui_fill_rect_fast((EpdRect){228 + i * 228, top + 99, 1, 74}, 0x98, fb);
    }
    ui_text_box(fb,(EpdRect){54,top+194,450,36},21,"本书进度",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){514,top+194,116,36},19,values[0],EPD_DRAW_ALIGN_RIGHT,false);
    EpdRect track = {54, top + 240, 576, 12};
    ui_fill_round_rect(fb, track, 6, 0xd0);
    EpdRect fill = track; fill.width = fill.width * (int)percent(s_page) / 100;
    if (fill.width) ui_fill_round_rect(fb, fill, 6, 0x40);
    ui_hairline(fb, top + 276, 36, 612, 0xb0);
    ui_text_box(fb,(EpdRect){54,top+292,190,38},23,"本章跳转",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){258,top+292,372,38},19,s_title,EPD_DRAW_ALIGN_RIGHT,false);
    size_t count = book_layout_page_count();
    size_t draft = count ? (s_chapter_jump_page < count ? s_chapter_jump_page : count - 1) : 0;
    EpdRect stepper = {54, top + 348, 402, 72};
    ui_fill_round_rect(fb, stepper, 36, 0xe8); ui_draw_control_frame(fb, stepper, 36, 0x78);
    ui_fill_rect_fast((EpdRect){128,top+360,1,48},0xb0,fb);
    ui_fill_rect_fast((EpdRect){382,top+360,1,48},0xb0,fb);
    ui_text_box(fb,reader_stats_control_rect(2),32,"−",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,reader_stats_control_rect(4),32,"+",EPD_DRAW_ALIGN_CENTER,false);
    char page[64];
    snprintf(page,sizeof(page),"%u / %u 页",count ? (unsigned)draft + 1 : 0,(unsigned)count);
    ui_text_box(fb,reader_stats_control_rect(3),26,page,EPD_DRAW_ALIGN_CENTER,false);
    EpdRect jump = reader_stats_control_rect(5);
    ui_fill_round_rect(fb,jump,36,UI_GRAY_BLACK);
    ui_text_box(fb,jump,24,"跳转",EPD_DRAW_ALIGN_CENTER,true);
    char hint[96];
    snprintf(hint,sizeof(hint),"当前第 %u 页，点按数字可输入页码",count ? (unsigned)s_page + 1 : 0);
    ui_text_box(fb,(EpdRect){54,top+432,576,34},18,hint,EPD_DRAW_ALIGN_LEFT,false);
    for (int i = 6; i <= 7; ++i) {
        EpdRect rect=reader_stats_control_rect(i);
        ui_fill_round_rect(fb,rect,20,0xd8);ui_draw_control_frame(fb,rect,20,0x68);
        char label[48];
        if(i==6)snprintf(label,sizeof(label),"查看书签 · %u",(unsigned)bookmark_count());
        else copy_text(label,sizeof(label),"最近 30 天");
        ui_text_box(fb,rect,24,label,EPD_DRAW_ALIGN_CENTER,false);
    }
}
static void draw_reader_chapter_jump(uint8_t *fb) {
    const int top=READER_JUMP_TOP;
    draw_sheet(fb,top,"跳转到本章");
    char caption[160];
    snprintf(caption,sizeof(caption),"%s · 共 %u 页",s_title,(unsigned)book_layout_page_count());
    ui_text_box(fb,(EpdRect){54,top+85,576,36},20,caption,EPD_DRAW_ALIGN_CENTER,false);
    EpdRect input={54,top+138,576,78};
    ui_fill_round_rect(fb,input,18,UI_GRAY_WHITE);ui_draw_control_frame(fb,input,18,0x58);
    ui_text_box(fb,(EpdRect){74,top+138,60,78},23,"第",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){550,top+138,60,78},23,"页",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){142,top+138,400,78},36,s_chapter_jump_digits,EPD_DRAW_ALIGN_CENTER,false);
    char error[80]={0};
    if(s_chapter_jump_error)snprintf(error,sizeof(error),"请输入 1—%u 之间的页码",(unsigned)book_layout_page_count());
    ui_text_box(fb,(EpdRect){54,top+224,576,34},18,error,EPD_DRAW_ALIGN_CENTER,false);
    const char *keys[]={"1","2","3","4","5","6","7","8","9","清空","0","退格","取消","跳转"};
    for(int i=0;i<14;++i){
        EpdRect rect=reader_chapter_key_rect(i);
        ui_fill_round_rect(fb,rect,i<12?14:36,i==13?UI_GRAY_BLACK:i==12?UI_GRAY_WHITE:0xe8);
        ui_draw_control_frame(fb,rect,i<12?14:36,i>=12?0x58:0x98);
        ui_text_box(fb,rect,i<9||i==10?29:23,keys[i],EPD_DRAW_ALIGN_CENTER,i==13);
    }
}

static void draw_reader_bookmarks(uint8_t* fb) {
    const int top = 560;
    draw_sheet(fb, top, "书签");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    reader_bookmarks_t marks = {0};
    bool valid = bookmark_load(&marks);
    int pages = bookmark_pages(valid ? marks.count : 0);
    if (s_bookmark_page >= pages) s_bookmark_page = pages - 1;
    if (valid && marks.count) {
        EpdRect manage = {544, top + 30, 104, 54};
        ui_fill_round_rect(fb, manage, 20, s_bookmark_edit ? 0xd8 : 0xe8);
        ui_draw_control_frame(fb, manage, 20, 0x78);
        reader_panel_text_vc(fb, 596, top + 57, 20, s_bookmark_edit ? "完成" : "管理", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (!valid || !marks.count) {
        reader_panel_text_vc(fb, 342, top + 260, 26, "还没有保存书签", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 309, 18, "阅读时点按“书签”即可保存当前位置", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    int first = s_bookmark_page * bookmark_rows();
    for (int row = 0; row < bookmark_rows() && first + row < marks.count; ++row) {
        int newest = (int)marks.count - 1 - (first + row);
        const reader_bookmark_entry_t* mark = &marks.entries[newest];
        EpdRect item = bookmark_row_rect(row);
        bool current = mark->chapter == s_chapter && mark->byte_off == book_layout_page_start_offset(s_page);
        bool selected = s_bookmark_edit && (s_bookmark_selected & (UINT32_C(1) << newest));
        ui_fill_round_rect(fb, item, 14, selected ? 0xc0 : current ? 0xc8 : 0xe8);
        ui_draw_control_frame(fb, item, 14, current ? 0x50 : 0x98);
        char chapter[96];
        if (book_chapter_title(mark->chapter, chapter, sizeof(chapter)) != ESP_OK) chapter[0] = 0;
        char where[52];
        if (mark->chapter == s_chapter)
            snprintf(where, sizeof(where), "第 %u 页", (unsigned)book_layout_page_for_offset(mark->byte_off) + 1);
        else snprintf(where, sizeof(where), "章节 %u", (unsigned)mark->chapter + 1);
        int end=item.x+item.width-(s_bookmark_edit?70:20);
        int where_width=ui_text_fixed_width_px(ui_text_effective_px(20),where);
        if(where_width>220)where_width=220;
        int chapter_width=end-where_width-item.x-34;
        fit_text(chapter,24,chapter_width);
        ui_text_box(fb,(EpdRect){item.x+18,item.y+5,chapter_width,item.height/2-10},24,chapter,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){end-where_width,item.y+5,where_width,item.height/2-10},20,where,EPD_DRAW_ALIGN_RIGHT,false);
        ui_text_box(fb,(EpdRect){item.x+18,item.y+item.height/2,item.width-90,item.height/2-5},18,
                    s_bookmark_edit?"点按选择":current?"当前阅读位置":"点按跳转到此处",EPD_DRAW_ALIGN_LEFT,false);
        if (s_bookmark_edit) {
            epd_draw_circle(item.x + item.width - 35, item.y + 36, 12, 0x48, fb);
            if (selected) epd_fill_circle(item.x + item.width - 35, item.y + 36, 7, 0x38, fb);
        }
    }
    if (s_bookmark_edit) {
        EpdRect all = {36, top + 520, 258, 64}, remove = {312, top + 520, 336, 64};
        ui_draw_button(fb, all, "全选本页", false);
        char label[40]; snprintf(label, sizeof(label), "删除已选 · %u", bookmark_selected_count());
        ui_draw_button(fb, remove, label, bookmark_selected_count() > 0);
    }
    char page[32]; snprintf(page, sizeof(page), "‹   %d / %d   ›", s_bookmark_page + 1, pages);
    reader_panel_text_vc(fb, 342, top + 606, 22, page, EPD_DRAW_ALIGN_CENTER, false);
    if (s_bookmark_delete_confirm) {
        EpdRect panel = {54, top + 196, 576, 240};
        ui_fill_round_rect(fb, panel, 24, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, panel, 24, 0x48);
        reader_panel_text_vc(fb, 342, top + 252, 27,
                   s_bookmark_delete_error ? "删除失败，请重试" : "删除选中的书签？", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 298, 18, "只删除书签，不影响阅读进度", EPD_DRAW_ALIGN_CENTER, false);
        ui_draw_button(fb, (EpdRect){76, top + 348, 244, 64}, "取消", false);
        ui_draw_button(fb, (EpdRect){364, top + 348, 244, 64}, "确认删除", true);
    }
}

static void draw_reader_stats_recent(uint8_t* fb) {
    const int top = 584;
    draw_sheet(fb, top, "最近 30 天");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    if (!s_recent_days_valid) {
        reader_panel_text_vc(fb, 342, top + 280, 23, "请先完成日期与时间设置", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 326, 18, "完成对时后开始记录每天的阅读时长", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    uint64_t sum = 0;
    uint32_t maximum = 0;
    for (int i = 0; i < 30; ++i) {
        sum += s_recent_days[i];
        if (s_recent_days[i] > maximum) maximum = s_recent_days[i];
    }
    char total[64];
    snprintf(total, sizeof(total), "累计 %llu 小时 %llu 分钟",
             (unsigned long long)(sum / 3600), (unsigned long long)((sum / 60) % 60));
    reader_panel_text_vc(fb, 342, top + 112, 28, total, EPD_DRAW_ALIGN_CENTER, false);
    const int base_y = top + 438, max_h = 250;
    ui_hairline(fb, base_y, 42, 600, 0xb0);
    for (int i = 0; i < 30; ++i) {
        int h = maximum ? (int)((uint64_t)s_recent_days[i] * max_h / maximum) : 0;
        if (s_recent_days[i] && h < 5) h = 5;
        if (h) ui_fill_round_rect(fb, (EpdRect){47 + i * 20, base_y - h, 10, h}, 5, 0x38);
    }
    reader_panel_text(fb, 42, base_y + 22, 19, "30 天前", EPD_DRAW_ALIGN_LEFT, false);
    reader_panel_text(fb, 642, base_y + 22, 19, "今天", EPD_DRAW_ALIGN_RIGHT, false);
}

static void draw_reader_panel(uint8_t* fb) {
    if (s_reader_panel==READER_PANEL_NONE) return;
    ui_text_set_system_scale(true);
    if (s_reader_panel == READER_PANEL_TOOLS) {
        ui_fill_rect_fast((EpdRect){0, 1096, UI_LOCK_WIDTH, 120}, UI_GRAY_WHITE, fb);
        ui_hairline(fb, 1096, 0, UI_LOCK_WIDTH, 0x98);
        const char* labels[] = {"目录", "书签", "阅读统计", "阅读设置", "字体设置"};
        for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            EpdRect rect = tool_rect(i);
            int cx = rect.x + rect.width / 2;
            bool bookmarked = i == 1 && bookmark_is_current();
            draw_reader_tool_icon(fb, cx, 1138, i, bookmarked);
            ui_text_box(fb,(EpdRect){rect.x+6,1159,rect.width-12,40},20,bookmarked?"已书签":labels[i],EPD_DRAW_ALIGN_CENTER,false);
        }
    } else if (s_reader_panel == READER_PANEL_FONT_SETTINGS) draw_font_settings(fb);
    else if (s_reader_panel == READER_PANEL_WEIGHT_SETTINGS) draw_weight_settings(fb);
    else if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) draw_layout_settings(fb);
    else if (s_reader_panel == READER_PANEL_RULE_SETTINGS) draw_rule_settings(fb);
    else if (s_reader_panel == READER_PANEL_BACKGROUND) draw_background_directory(fb);
    else if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) draw_background_picker(fb);
    else if (s_reader_panel == READER_PANEL_FONT_PICKER) draw_font_picker(fb);
    else if (s_reader_panel == READER_PANEL_STATS) draw_reader_stats(fb);
    else if (s_reader_panel == READER_PANEL_CHAPTER_JUMP) draw_reader_chapter_jump(fb);
    else if (s_reader_panel == READER_PANEL_BOOKMARKS) draw_reader_bookmarks(fb);
    else if (s_reader_panel == READER_PANEL_STATS_RECENT) draw_reader_stats_recent(fb);
    else if (s_reader_panel == READER_PANEL_REFRESH_SETTINGS) draw_reading_settings(fb);
    else if (s_reader_panel == READER_PANEL_TURN_SETTINGS) draw_turn_settings(fb);
    else if (s_reader_panel == READER_PANEL_KEY_SETTINGS) draw_key_settings(fb);
    else if (s_reader_panel == READER_PANEL_KEY_PICKER) draw_key_picker(fb);
    ui_text_set_system_scale(false);
}

/* ---- 绘制与预渲染 / Drawing and preparation ---- */
static uint8_t inline_ink_gray(uint8_t gray) {
    // 解码器已提供灰阶；再次加深并叠加系统对比度会压死抖动前的中间色阶。
    // The decoder already supplies grayscale; extra darkening and UI contrast clip midtones.
    return gray;
}

static void draw_reader_header(uint8_t *fb) {
    ui_nav_back(fb, 36, 79);
    char header[128];
    const char *sample = s_book_title[0] ? s_book_title : s_title;
    copy_text(header, sizeof(header), sample);
    int title_px = ui_text_title_fit(header, 32, 450, sample);
    // 返回键、书名与收藏使用同一视觉中心线；字体切换不改变对齐。
    // Back, title and favorite share one visual centerline across font changes.
    ui_text_title_vc(fb, UI_LOCK_WIDTH / 2, 114, title_px, header, sample, EPD_DRAW_ALIGN_CENTER);
    ui_draw_icon(fb, 620, 114, 48,
                 s_reader_favorite ? UI_ICON_ANIMATE_BOOKMARK_PLUS : UI_ICON_BOOKMARK, 0x38);
    ui_hairline(fb, 151, 36, 612, UI_GRAY_LIGHT);
}
// 竖排章节题头：右列是编号（第三章），左列是章名，读序依旧自右向左；两列整体留出列宽，
// 排版用同一个宽度在首页右边缘预留，绘制与预留因此不会错位。
// A vertical chapter heading: the number column (第三章) on the right and the title on the left,
// still read right to left. Both columns reserve their width, and the layout reserves the same
// width at the right edge of the first page, so drawing and reservation cannot drift apart.
typedef struct {
    int width;       ///< 首页右起需要预留的列宽 / columns to reserve at the first page's right
    int title_px;    ///< 章名字号 / title type size
    int title_x;     ///< 章名列左边缘 / title column's left edge
    int label_px;    ///< 编号字号，无编号时为 0 / number size, 0 without a label
    int label_x;     ///< 编号列左边缘 / label column's left edge
    int text_height; ///< 较长一列的高度，用来垂直居中 / taller column, used to center
} reader_heading_t;

static reader_heading_t reader_heading_metrics(const char *title, const char *label, EpdRect body) {
    reader_heading_t metrics = {0};
    if (title == NULL || title[0] == 0 || body.height <= 0 || body.width <= 0) return metrics;
    // 题头跟横排的题头一样走系统字体：正文换卡上字体不改变章节标题。
    // The heading uses the system face exactly like the horizontal one, so swapping the body face
    // never changes chapter titles.
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    const int avail = body.height - 48;
    int px = 48;
    int need = ttf_vertical_text_height_px(px, title, 0);
    // 一列放不下就缩字号；竖排的题头不折列，缩到 24 为止。
    // Shrink the size when one column cannot hold the title; a vertical heading does not break
    // into more columns and stops at 24.
    if (need > avail && avail > 0) {
        px = px * avail / need;
        px &= ~3;
        if (px < 24) px = 24;
    }
    int label_px = label != NULL && label[0] ? 24 : 0;
    const int gap = 12;
    metrics.title_px = px;
    metrics.label_px = label_px;
    metrics.width = px + (label_px ? label_px + gap : 0);
    // 题头最多占正文带的三分之一，留够列给正文。
    // A heading takes at most a third of the body band, leaving the rest for body columns.
    if (metrics.width > body.width / 3) {
        int scale = body.width / 3 * 100 / metrics.width;
        metrics.title_px = metrics.title_px * scale / 100;
        if (metrics.title_px < 24) metrics.title_px = 24;
        metrics.label_px = label_px ? label_px * scale / 100 : 0;
        if (metrics.label_px && metrics.label_px < 18) metrics.label_px = 18;
        metrics.width = metrics.title_px + (metrics.label_px ? metrics.label_px + gap : 0);
    }
    const int right = body.x + body.width;
    metrics.title_x = right - metrics.width;
    metrics.label_x = right - metrics.label_px;
    int title_h = ttf_vertical_text_height_px(metrics.title_px, title, 0);
    int label_h = metrics.label_px ? ttf_vertical_text_height_px(metrics.label_px, label, 0) : 0;
    metrics.text_height = title_h > label_h ? title_h : label_h;
    return metrics;
}

static void draw_vertical_heading(uint8_t *fb, EpdRect body) {
    reader_heading_t metrics = reader_heading_metrics(s_chapter_heading_title, s_chapter_heading_label, body);
    if (!metrics.width) return;
    int y = body.y + (body.height - metrics.text_height) / 2;
    if (y < body.y + 8) y = body.y + 8;
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    if (metrics.label_px)
        ttf_draw_text_px_vertical(fb, metrics.label_x, y, metrics.label_px, s_chapter_heading_label, 0, 0, 15);
    ttf_draw_text_px_vertical(fb, metrics.title_x, y, metrics.title_px, s_chapter_heading_title, 0, 0, 15);
}

// 重排前把章节题头的几何重新说给排版：竖排留右侧列宽、横排留顶部行高，
// 绘制侧读的是同一组 s_chapter_lead_*。切模式、切全屏、改字号都必须走这里，
// 否则排版按旧模式预留、题头按新模式绘制，题头就会压到正文上。
// Tell the layout the chapter heading geometry before every re-layout: columns to the right in
// vertical setting, rows at the top in horizontal. Drawing reads the same s_chapter_lead_*
// values. Switching mode or full screen or changing the type size must all come through here;
// otherwise the layout reserves for the old mode while the heading draws in the new one and lands
// on top of the body.
// 题头几何按当前模式重算：竖排留右侧列宽、横排留顶部行高。
// height 由调用方按原有规矩算好（首块是插图、或没有可用标题时为 0），这里不再自己推导——
// 无条件按 214 算会把不该出现的题头带出来，正文里那份标题就与它并排成了两个标题。
// Heading geometry for the current mode: columns to the right vertically, rows at the top
// horizontally. `height` arrives already decided by the caller (zero when the first block is an
// illustration or no title is usable); deriving it here unconditionally brought a heading onto
// sheets that should have none, leaving it side by side with the body's own copy.
static void apply_chapter_lead(const char *title, const char *label, size_t skip, unsigned height) {
    s_chapter_lead_skip = skip;
    if (book_kind() != BOOK_KIND_EPUB || title == NULL || title[0] == 0 || !height) {
        s_chapter_lead_height = s_chapter_lead_width = 0;
        book_layout_set_chapter_lead(0, 0);
        book_layout_set_chapter_lead_width(0);
        return;
    }
    if (app_settings_book_vertical()) {
        // 量竖排题头会光栅化字形，调用方须已持有绘制锁。
        // Measuring the vertical heading rasterizes glyphs; the caller must hold the draw lock.
        s_chapter_lead_height = 0;
        s_chapter_lead_width = (unsigned)reader_heading_metrics(title, label, body_rect()).width;
        book_layout_set_chapter_lead(skip, 0);
        book_layout_set_chapter_lead_width(s_chapter_lead_width);
    } else {
        s_chapter_lead_width = 0;
        s_chapter_lead_height = height;
        book_layout_set_chapter_lead(skip, s_chapter_lead_height);
        book_layout_set_chapter_lead_width(0);
    }
}

// 重排前把题头几何交给排版；调用方须持有绘制锁。所有正文重排都要走这一句，
// 否则换了模式或字号之后，排版按上一次的题头尺寸预留，题头就会压住正文。
// Hand the heading geometry to the layout before a re-layout; the caller holds the draw lock.
// Every body re-layout needs this line, otherwise a mode or size change reserves room for the
// previous heading and the heading lands on the body.
static void refresh_chapter_lead(void) {
    apply_chapter_lead(s_chapter_heading_title, s_chapter_heading_label, s_chapter_lead_skip,
                       s_chapter_lead_row_height);
}

// PR9 未知尺寸整页图没有混排槽；在调用侧适配原有位图缓存。/ PR9's unknown-size page has no inline slot; adapt the bitmap cache here.
static int reader_image_slots(size_t page) {
    int count = book_layout_page_image_count(page);
    return count ? count : book_layout_page_image(page) >= 0 ? 1 : 0;
}
static bool reader_image_slot(size_t page, int slot, int *index, int *y, int *width, int *height) {
    if (book_layout_page_image_count(page))
        return book_layout_page_image_at(page, slot, index, y, width, height);
    int image = book_layout_page_image(page);
    if (slot != 0 || image < 0) return false;
    // 未知尺寸的整页图同样通栏，不能拿正文栏宽当上限。
    // An unknown-size whole-page image bleeds as well, so the body column is not its ceiling.
    EpdRect band = book_layout_image_rect();
    if (index) *index = image;
    if (y) *y = 0;
    if (width) *width = band.width;
    if (height) *height = band.height;
    return true;
}
// 整数平方根：双指距离只用来比比例，不必引入浮点。
// Integer square root: pinch distance only feeds a ratio, so no floating point is needed.
static uint32_t reader_sqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// 缓存必须属于当前阅读页和布局；解码失败或换章时不能引用旧槽。
// Require the active page and layout: failed decoding or chapter changes cannot borrow stale slots.
static bool reader_image_cache_ready(void) {
    return s_view == READING && s_text && !app_settings_reader_hide_images() &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar &&
        s_page_images && s_page_image_count > 0 && s_page_images_for == s_page &&
        s_page_images_generation == book_layout_generation();
}

// 这一页是不是整屏只有一张插图，且它的灰阶位图已经就绪。
// Whether this page is one illustration and nothing else, with its gray bitmap decoded.
static bool reader_single_image_page(void) {
    return reader_image_cache_ready() &&
           book_layout_page_image(s_page) >= 0 && reader_image_slots(s_page) == 1 &&
           s_page_image_count == 1 && s_page_images[0].gray &&
           s_page_images[0].width > 0 && s_page_images[0].height > 0;
}

// 当前缩放目标：整页插图的唯一一张，或正文里双击进入缩放的那一张。
// Zoom target: the sole illustration of a whole-page sheet, or the inline image a double tap
// opened in zoom mode.
static const reader_image_t *reader_zoom_image(void) {
    if (!reader_image_cache_ready()) return NULL;
    if (s_zoom_active)
        return s_zoom_slot >= 0 && s_zoom_slot < s_page_image_count &&
            s_page_images[s_zoom_slot].gray && s_page_images[s_zoom_slot].width > 0 &&
            s_page_images[s_zoom_slot].height > 0 ? &s_page_images[s_zoom_slot] : NULL;
    return reader_single_image_page() ? &s_page_images[0] : NULL;
}
// 有可缩放目标（整页插图，或已进入正文插图缩放模式）。
// Whether a zoom surface is available: a whole-page illustration, or an inline image already
// opened in zoom mode.
static bool reader_zoom_mode(void) { return reader_zoom_image() != NULL; }

// 1:1 时缩放目标在插图区域里的显示尺寸：等比缩小到能放下，不放大。
// The display size of the zoom target at 1:1: aspect-fit into the illustration region, never
// upscaled.
static void reader_image_base_size(int *out_w, int *out_h) {
    const reader_image_t *image = reader_zoom_image();
    EpdRect band = book_layout_image_rect();
    int64_t w = image ? image->width : 1, h = image ? image->height : 1;
    if (w > band.width) { h = h * band.width / w; w = band.width; }
    if (h > band.height) { w = w * band.height / h; h = band.height; }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    *out_w = (int)w;
    *out_h = (int)h;
}

// 正文里按点选中的插图槽；不在任何插图矩形里返回 false。
// The inline illustration slot under a tap, or false when the tap misses every image rectangle.
static bool reader_inline_image_at(uint16_t x, uint16_t y, int *slot) {
    if (!slot || !reader_image_cache_ready()) return false;
    EpdRect band = book_layout_image_rect();
    for (int i = 0; i < reader_image_slots(s_page) && i < s_page_image_count; ++i) {
        int iy = 0, width = 0, height = 0;
        if (!reader_image_slot(s_page, i, NULL, &iy, &width, &height)) continue;
        const reader_image_t *image = &s_page_images[i];
        if (!image->gray || image->width <= 0 || image->height <= 0) continue;
        int left = band.x + (band.width - image->width) / 2;
        int top = band.y + iy + (height - image->height) / 2;
        if (iy == 0) top = reader_area().y;
        if (x >= left && x < left + image->width && y >= top && y < top + image->height) {
            *slot = i;
            return true;
        }
    }
    return false;
}

// 这一页上点到的是不是一张插图（正文插图或整页插图）。
// Whether a tap landed on an illustration of the current page.
static bool reader_page_image_hit(uint16_t x, uint16_t y) {
    int slot = 0;
    return reader_inline_image_at(x, y, &slot);
}

// 图片中心活动范围：图比区域大时图像边缘不能缩进区域里，比区域小时固定在中心。
// Where the image centre may sit: while the image is larger than the region its edges must not
// pull inside it, and while it is smaller the centre stays put.
static void reader_image_clamp_center(int *cx, int *cy, int w, int h, EpdRect band) {
    if (w <= band.width) *cx = band.x + band.width / 2;
    else {
        int low = band.x + band.width - w / 2, high = band.x + w / 2;
        if (*cx < low) *cx = low;
        if (*cx > high) *cx = high;
    }
    if (h <= band.height) *cy = band.y + band.height / 2;
    else {
        int low = band.y + band.height - h / 2, high = band.y + h / 2;
        if (*cy < low) *cy = low;
        if (*cy > high) *cy = high;
    }
}

// 4x4 有序抖动点阵：连续 DU 只认纯黑/纯白，靠点的密度表现灰阶。
// 4x4 ordered-dither matrix: continuous DU only drives pure black and white, so dot density
// carries the gray level.
static const uint8_t s_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// 把 0=黑…255=白 的一档灰度摊成纯黑/白点阵。/ Turn a 0=black..255=white tone into black/white dots.
static uint8_t reader_bw_tone(uint8_t gray, int x, int y) {
    unsigned threshold = (unsigned)s_bayer4[((y & 3) << 2) | (x & 3)] * 16u + 8u;    return gray >= threshold ? 0xff : 0x00;
}

// 双线性取样：放大时比最近邻平滑，缩小时也不会出现整片跳变。
// Bilinear sampling: smoother than nearest neighbour when enlarging, without whole-pixel jumps
// when shrinking.
static uint8_t reader_sample_bilinear(const reader_image_t *image, int sx_fp, int sy_fp) {
    int x0 = sx_fp >> 16, y0 = sy_fp >> 16;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 >= image->width) x0 = image->width - 1;
    if (y0 >= image->height) y0 = image->height - 1;
    int x1 = x0 + 1 < image->width ? x0 + 1 : x0;
    int y1 = y0 + 1 < image->height ? y0 + 1 : y0;
    unsigned fx = ((unsigned)sx_fp >> 8) & 0xffu, fy = ((unsigned)sy_fp >> 8) & 0xffu;
    const uint8_t *p = image->gray;
    unsigned top = (unsigned)p[(size_t)y0 * image->width + x0] * (256u - fx) +
                   (unsigned)p[(size_t)y0 * image->width + x1] * fx;
    unsigned bottom = (unsigned)p[(size_t)y1 * image->width + x0] * (256u - fx) +
                      (unsigned)p[(size_t)y1 * image->width + x1] * fx;
    return (uint8_t)((top * (256u - fy) + bottom * fy) >> 16);
}

// 缩放并平移到位的插图，采样用双线性；bw=true 时用点阵出纯黑白，专供跟手快刷。
// The zoom target at the current scale and centre, sampled bilinearly. With bw=true it renders
// pure black/white dots for the interactive fast frames.
static void draw_zoomed_image(uint8_t *fb, EpdRect band, int scale, int cx, int cy, bool bw) {
    const reader_image_t *image = reader_zoom_image();
    if (!image) return;
    int base_w = 0, base_h = 0;
    reader_image_base_size(&base_w, &base_h);
    int w = (int)((int64_t)base_w * scale / READER_IMAGE_ZOOM_MIN);
    int h = (int)((int64_t)base_h * scale / READER_IMAGE_ZOOM_MIN);
    if (w < 1 || h < 1) return;
    int left = cx - w / 2, top = cy - h / 2;
    int64_t step_x = ((int64_t)image->width << 16) / w;
    int64_t step_y = ((int64_t)image->height << 16) / h;
    int64_t max_x = (int64_t)(image->width - 1) << 16;
    int64_t max_y = (int64_t)(image->height - 1) << 16;
    // 只走屏幕与图片的交集，避免每像素再做越界判断。
    // Walk only the intersection of the sheet and the image, so no per-pixel bounds test is needed.
    int x0 = band.x > left ? band.x : left;
    int y0 = band.y > top ? band.y : top;
    int x1 = band.x + band.width < left + w ? band.x + band.width : left + w;
    int y1 = band.y + band.height < top + h ? band.y + band.height : top + h;
    if (x0 < band.x) x0 = band.x;
    if (y0 < band.y) y0 = band.y;
    if (x1 > band.x + band.width) x1 = band.x + band.width;
    if (y1 > band.y + band.height) y1 = band.y + band.height;
    for (int py = y0; py < y1; ++py) {
        int64_t sy_fp = (int64_t)(py - top) * step_y;
        if (sy_fp > max_y) continue;
        for (int px = x0; px < x1; ++px) {
            int64_t sx_fp = (int64_t)(px - left) * step_x;
            if (sx_fp > max_x) continue;
            uint8_t gray = reader_sample_bilinear(image, (int)sx_fp, (int)sy_fp);
            epd_draw_pixel(px, py, bw ? reader_bw_tone(gray, px, py)
                                      : ui_image_dither_gray(inline_ink_gray(gray), px, py), fb);
        }
    }
}
/* ---- 跟手帧：1bit 目标帧 / Gesture frames from a 1-bit target ---- */
// 物理像素反解成逻辑坐标，与 epdiy 的 _rotate 一一对应。
// Map a physical pixel back to logical coordinates, mirroring epdiy's _rotate().
static void reader_phys_to_logical(int px, int py, int *lx, int *ly) {
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: *lx = py; *ly = epd_width() - 1 - px; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *lx = epd_width() - 1 - px; *ly = epd_height() - 1 - py; break;
        case EPD_ROT_INVERTED_PORTRAIT: *lx = epd_height() - 1 - py; *ly = px; break;
        default: *lx = px; *ly = py; break;
    }
}

// 旋转把两个轴整体对调或翻转，所以各建一张轴映射表，逐像素只剩数组取值；点阵图按物理行
// 顺序写，顺序内存访问比逐像素 nibble 读写快得多。灰度双线性只在松手后做。
// Rotation swaps or flips whole axes, so one map per axis turns per-pixel work into a lookup.
// The bit rows are written in physical order; sequential memory beats per-pixel nibble access.
// Bilinear grayscale runs only after the fingers leave.
static uint8_t *s_zoom_bits;
static size_t s_zoom_bits_bytes;

static void reader_zoom_reset(void) {
    s_zoom_active = false; s_zoom_slot = -1;
    s_image_zoom = READER_IMAGE_ZOOM_MIN; s_image_zoom_page = SIZE_MAX;
    s_image_zoom_start_distance = 0; s_image_pan_active = false;
    s_image_zoom_bw = false; s_reader_tap.pending = false;
    free(s_zoom_bits); s_zoom_bits = NULL; s_zoom_bits_bytes = 0;
}



// 按区域大小准备两块 1bit 帧缓冲：一块是这一帧的目标，一块是上一帧，用来算差分。
// Prepare two 1-bit frame buffers sized to the region: this frame's target and the previous one
// used for the difference.
static bool zoom_bits_ready(EpdRect phys) {
    size_t bytes = (size_t)((phys.width + 7) / 8) * (size_t)phys.height;
    if (bytes == s_zoom_bits_bytes && s_zoom_bits) return true;
    free(s_zoom_bits);
    s_zoom_bits = NULL;
    s_zoom_bits_bytes = 0;
    uint8_t *a = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a) return false;
    memset(a, 0, bytes);
    s_zoom_bits = a;
    s_zoom_bits_bytes = bytes;
    return true;
}

// 逻辑坐标正解成物理坐标，与 epdiy 的 _rotate 一致。
// Map a logical pixel to physical coordinates, matching epdiy's _rotate().
static void reader_logical_to_phys(int lx, int ly, int *px, int *py) {
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: *px = epd_width() - 1 - ly; *py = lx; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *px = epd_width() - 1 - lx; *py = epd_height() - 1 - ly; break;
        case EPD_ROT_INVERTED_PORTRAIT: *px = ly; *py = epd_height() - 1 - lx; break;
        default: *px = lx; *py = ly; break;
    }
}

// 生成一帧：按逻辑坐标遍历，源图行顺序读，写出的点落在物理内存里的小块中；
// 逻辑→物理是仿射映射（系数只有 0/±1），所以每次取样只做两次乘法。
// 逐像素取样再按有序抖动摊成黑白：跟手帧用细抖动保持画面连续，灰度定稿另走双线性。
// Build one frame by walking logical coordinates: source rows read sequentially and the dots land
// in a small physical tile. Logical-to-physical is affine with 0/±1 coefficients, so a sample
// costs two multiplies. Every pixel is sampled and ordered-dithered, so the motion frame keeps a
// continuous look instead of 4x4 blocks; the grayscale settle still uses bilinear.
#define ZOOM_BITS_STEP 1
static void zoom_bits_build(uint8_t *bits, EpdRect phys, EpdRect band, int iw, int ih,
                            int w, int h, int left, int top) {
    const int width_bytes = (phys.width + 7) / 8;
    memset(bits, 0, (size_t)width_bytes * (size_t)phys.height);
    // 只遍历"图片 ∩ 插图区域"：放大后图片比屏幕大得多，多算的部分全在屏幕外。
    // Walk only the image ∩ region: a zoomed image reaches far past the sheet, and anything
    // outside it would be sampled and discarded.
    int lx0 = left > band.x ? left : band.x;
    int ly0 = top > band.y ? top : band.y;
    int lx1 = left + w < band.x + band.width ? left + w : band.x + band.width;
    int ly1 = top + h < band.y + band.height ? top + h : band.y + band.height;
    if (lx1 > left + w) lx1 = left + w;
    if (ly1 > top + h) ly1 = top + h;
    if (lx1 <= lx0 || ly1 <= ly0) return;
    // 仿射系数：px = ax*lx + ay*ly + px0，py = dx*lx + dy*ly + py0。
    // Affine coefficients: px = ax*lx + ay*ly + px0 and py = dx*lx + dy*ly + py0.
    int px0 = 0, py0 = 0, e1x = 0, e1y = 0, e2x = 0, e2y = 0;
    reader_logical_to_phys(0, 0, &px0, &py0);
    reader_logical_to_phys(1, 0, &e1x, &e1y);
    reader_logical_to_phys(0, 1, &e2x, &e2y);
    const int ax = e1x - px0, ay = e2x - px0, ddx = e1y - py0, ddy = e2y - py0;
    // 采样步长用定点：每像素 64 位除法太贵，这里只乘一次再移位。
    // Fixed-point sampling steps: a 64-bit division per pixel is far too costly, so multiply once
    // and shift.
    const int64_t step_x = ((int64_t)iw << 16) / w;
    const int64_t step_y = ((int64_t)ih << 16) / h;
    const int step = ZOOM_BITS_STEP;
    const reader_image_t *zoom_image = reader_zoom_image();
    if (!zoom_image) return;
    for (int ly = ly0; ly < ly1; ly += step) {
        const int sy = (int)(((int64_t)(ly - top) * step_y) >> 16);
        if (sy < 0 || sy >= ih) continue;
        const uint8_t *src = zoom_image->gray + (size_t)sy * iw;
        for (int lx = lx0; lx < lx1; lx += step) {
            const int sx = (int)(((int64_t)(lx - left) * step_x) >> 16);
            if (sx < 0 || sx >= iw) continue;
            const uint8_t gray = src[sx];
            const int lxe = lx + step < lx1 ? lx + step : lx1;
            const int lye = ly + step < ly1 ? ly + step : ly1;
            for (int lv = ly; lv < lye; ++lv) {
                const int py = py0 + ddx * lx + ddy * lv;
                if (py < phys.y || py >= phys.y + phys.height) continue;
                uint8_t *row = bits + (size_t)(py - phys.y) * (size_t)width_bytes;
                for (int lv2 = lx; lv2 < lxe; ++lv2) {
                    const int px = px0 + ax * lv2 + ay * lv;
                    if (px < phys.x || px >= phys.x + phys.width) continue;
                    const int xx = px - phys.x;
                    if (gray < (unsigned)s_bayer4[((py & 3) << 2) | (px & 3)] * 16u + 8u)
                        row[xx >> 3] |= (uint8_t)(0x80u >> (xx & 7));
                }
            }
        }
    }
}

static void draw_reader_images(uint8_t *fb, size_t page, EpdRect body) {
    // 位图和布局代次必须匹配；预渲染另一页不能复用当前页插图。
    // Match page and layout generation; a prefetched page must never borrow current-page bitmaps.
    if (!app_settings_reader_hide_images() && page == s_page &&
        s_page_images_for == page && s_page_images_generation == book_layout_generation()) {
        // 缩放只属于缩放时的那一页；换页后回到原始大小与居中位置。
        // The scale belongs to the page it was made on; a page change returns to 1:1 and centre.
        if (s_image_zoom_page != page) {
            // 换页时丢掉还没扫完的相位，别把上一页的点阵带到新页。
            // Drop phases still in flight on a page change so no dots carry over.
            if (s_image_zoom_bw) continuous_du_reset();
            s_image_zoom = READER_IMAGE_ZOOM_MIN;
            s_image_zoom_bw = false;
            s_image_pan_active = false;
            s_zoom_active = false;
            EpdRect reset_band = book_layout_image_rect();
            s_image_zoom_cx = reset_band.x + reset_band.width / 2;
            s_image_zoom_cy = reset_band.y + reset_band.height / 2;
        }
        // 正文插图缩放模式：整块插图区域只画那一张，其他内容不参与。
        // Inline zoom mode: the illustration region shows that one image and nothing else.
        if (s_zoom_active) {
            draw_zoomed_image(fb, book_layout_image_rect(), s_image_zoom,
                              s_image_zoom_cx, s_image_zoom_cy, s_image_zoom_bw);
            return;
        }
        if (s_image_zoom > READER_IMAGE_ZOOM_MIN && reader_single_image_page()) {
            draw_zoomed_image(fb, book_layout_image_rect(), s_image_zoom,
                              s_image_zoom_cx, s_image_zoom_cy, s_image_zoom_bw);
            return;
        }
        // 横向按插图区域居中：通栏时图片铺满整屏，不再留正文边距。
        // Center horizontally in the illustration region: with full bleed the image spans the
        // panel and no longer carries the body margins.
        EpdRect band = book_layout_image_rect();
        for (int i = 0; i < reader_image_slots(page); ++i) {
            int y, width, height;
            if (!reader_image_slot(page, i, NULL, &y, &width, &height)) continue;
            const reader_image_t *image = s_page_images && i < s_page_image_count ? &s_page_images[i] : NULL;
            if (image && image->gray) {
                int left = band.x + (band.width - image->width) / 2;
                int top = band.y + y + (height - image->height) / 2;
                // 页首插图贴到阅读区顶端：正文上方那点呼吸空间只属于正文，不该把插图压下来。
                // A page-start illustration sits at the top of the reading area: the inset above
                // body text belongs to text and must not push the illustration down.
                if (y == 0) top = reader_area().y;

                for (int iy = 0; iy < image->height; ++iy)
                    for (int ix = 0; ix < image->width; ++ix)
                        epd_draw_pixel(left + ix, top + iy,
                            ui_image_dither_gray(inline_ink_gray(image->gray[(size_t)iy * image->width + ix]),
                                                 left + ix, top + iy), fb);
            } else if (height >= UI_PX_CAPTION + 4 && width >= 70) {
                char notice[64]; copy_text(notice, sizeof(notice), "此插图暂无法显示");
                fit_text(notice, UI_PX_CAPTION, width - 8);
                ui_text_vc(fb, body.x + body.width / 2, body.y + y + height / 2,
                           UI_PX_CAPTION, notice, EPD_DRAW_ALIGN_CENTER, false);
            }
        }
    }
}
static void draw_reader_outer(uint8_t *fb,size_t page) {
    ui_text_set_system_scale(false);
    if (!(s_reader_fullscreen && app_settings_reader_immersive())) ui_nav_status(fb);
    if (!s_reader_fullscreen) draw_reader_header(fb);
    EpdRect body=body_rect();
    if (page == 0 && s_chapter_heading_title[0] &&
        (s_chapter_lead_height || s_chapter_lead_width)) {
        if (app_settings_book_vertical()) {
            // 竖排题头站在首页预留的那条列里，正文从它左边接着排。
            // The vertical heading stands in the columns the first page reserved; the body
            // continues to its left.
            draw_vertical_heading(fb, body);
        } else {
            char heading[128]; copy_text(heading, sizeof(heading), s_chapter_heading_title);
            int heading_px = ui_text_title_fit(heading, 48, ui_content_width(), s_chapter_heading_title);
            char label[sizeof(s_chapter_heading_label)]; copy_text(label, sizeof(label), s_chapter_heading_label);
            int label_px = ui_text_title_fit(label, 24, ui_content_width(), s_chapter_heading_label);
            ui_text_title_vc(fb, UI_LOCK_WIDTH / 2, body.y + 29, label_px, label,
                              s_chapter_heading_label, EPD_DRAW_ALIGN_CENTER);
            ui_text_title_vc(fb, UI_LOCK_WIDTH / 2, body.y + 112, heading_px, heading,
                              s_chapter_heading_title, EPD_DRAW_ALIGN_CENTER);
            ui_hairline(fb, body.y + 174, 210, 264, UI_GRAY_LIGHT);
        }
    }
    if (s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW)
        ui_reader_background_paint(fb, (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT}, true);
    draw_reader_images(fb, page, body);
    if (s_reader_fullscreen) {
        // Full-book progress uses the last few screen rows and no footer text.
        EpdRect track = {36, READER_FULLSCREEN_PROGRESS_TOP + 3, UI_LOCK_WIDTH - 72, 3};
        ui_fill_round_rect(fb, track, 1, 0x90);
        track.width = track.width * (int)percent(page) / 100;
        if (track.width) ui_fill_round_rect(fb, track, 1, UI_GRAY_BLACK);
    } else {
        EpdRect track = progress_rect();
        EpdRect bar = {track.x, track.y + 16, track.width, 12};
        ui_fill_round_rect(fb, bar, 5, UI_GRAY_LIGHT);
        bar.width = bar.width * (int)percent(page) / 100;
        if (bar.width) ui_fill_round_rect(fb, bar, 5, UI_GRAY_BLACK);
        char chapter_name[72] = {0};
        reader_footer_chapter_name(chapter_name, sizeof(chapter_name));
        const int footer_px = 24;
        char progress[48];
        snprintf(progress, sizeof(progress), "%u/%u · 全书 %u%%",
                 (unsigned)page + 1, (unsigned)book_layout_page_count(), percent(page));
        char chapter_line[192];
        snprintf(chapter_line, sizeof(chapter_line), "%s%s",
                 s_save_failed ? "未保存 · " : "", chapter_name);
        fit_fixed_text(chapter_line, footer_px,
                       track.width - ui_text_fixed_width_px(footer_px, progress) - 16);
        // 页码和全书进度靠右固定，长章节名仅截短左侧；字号不跟随正文设置。
        // Keep page and book progress right-aligned; trim only long chapter titles and ignore body font settings.
        ui_text_fixed(fb, track.x, track.y + 35, footer_px, chapter_line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, track.x + track.width, track.y + 35, footer_px,
                      progress, EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (s_reader_notice[0]) {
        EpdRect notice = {218, 160, 248, 48};
        ui_fill_round_rect(fb, notice, 24, 0xd0);
        ui_text_vc(fb, 342, 184, 19, s_reader_notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    if (reader_chapter_undo_visible()) {
        EpdRect notice={104,160,476,126};
        ui_fill_round_rect(fb,notice,20,UI_GRAY_WHITE);ui_draw_control_frame(fb,notice,20,0x68);
        ui_text_box(fb,(EpdRect){122,174,440,36},22,"已跳转到本章指定页",EPD_DRAW_ALIGN_CENTER,false);
        EpdRect back=reader_chapter_undo_rect();
        ui_fill_round_rect(fb,back,23,0xe0);
        ui_text_box(fb,back,21,"返回跳转前",EPD_DRAW_ALIGN_CENTER,false);
    }
    draw_reader_panel(fb);
}
static void draw_reader(uint8_t *fb,size_t page) {
    ui_text_set_system_scale(false);ui_clear_page(fb);
    if (!(s_zoom_active && page == s_page && reader_zoom_mode()))
        book_layout_draw_page(fb,page,body_rect(),s_px);
    draw_reader_outer(fb,page);
}
// 预绘只在同步推屏期间借用字体；结束前逐行停止并交还，输入回调不等待整页预绘。
// Prefetch borrows fonts only during synchronous output; stop between rows before returning, never joining a whole page for input.
#define BOOK_PREP_RESERVE (1024u * 1024u)
#define BOOK_PREP_IDLE_US 4000
static int reader_prefetch_step(void) {
    if(!s_next_fb||s_prep_page<0)return -1;
    if(!s_prep_cursor.generation)memset(s_next_fb,0xff,fb_bytes());
    int result=book_layout_draw_slice(s_next_fb,(size_t)s_prep_page,body_rect(),s_px,&s_prep_cursor);
    if(result<0)invalidate_prep();
    else if(result>0){s_next_page=s_prep_page;s_next_generation=book_layout_generation();}
    return result;
}
static bool reader_prefetch_buffer(void) {
    size_t need=fb_bytes();
    if(!s_next_fb&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>need+BOOK_PREP_RESERVE)
        s_next_fb=heap_caps_aligned_alloc(16,need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return s_next_fb!=NULL;
}
static void prep_task(void *arg) {
    (void)arg;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        lock_draw();
        while(!atomic_load_explicit(&s_prep_stop,memory_order_acquire)){
            if(reader_prefetch_step()!=0)break;
        }
        unlock_draw();
        xSemaphoreGive(s_prep_done);
    }
}
static bool kick_prep(void) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||
       s_page+1>=book_layout_page_count()||book_layout_page_image_count(s_page)||
       book_layout_page_image_count(s_page+1)||app_settings_book_background_path()[0])return false;
    int target=(int)s_page+1;
    if(s_next_page==target&&s_next_generation==book_layout_generation())return false;
    if(s_prep_page!=target||s_prep_cursor.generation!=book_layout_generation()){
        s_next_page=-1;s_next_generation=0;s_prep_page=target;memset(&s_prep_cursor,0,sizeof(s_prep_cursor));
    }
    if(!reader_prefetch_buffer())return false;
    if(!s_draw_lock)s_draw_lock=xSemaphoreCreateMutex();
    if(!s_prep_done)s_prep_done=xSemaphoreCreateBinary();
    if(!s_prep_task&&s_draw_lock&&s_prep_done&&
       xTaskCreatePinnedToCore(prep_task,"book_prep",12*1024,NULL,3,&s_prep_task,1)!=pdPASS)s_prep_task=NULL;
    if(!s_prep_task)return false;
    xSemaphoreTake(s_prep_done,0);
    atomic_store_explicit(&s_prep_stop,false,memory_order_release);
    xTaskNotifyGive(s_prep_task);
    return true;
}
static void reader_prefetch_end(bool started) {
    if(!started)return;
    atomic_store_explicit(&s_prep_stop,true,memory_order_release);
    // 最多交还当前一行，之后主循环才可改字体、章节或离页，避免竞态。
    // Hand back at the current row boundary before the loop may change fonts, chapters or pages.
    xSemaphoreTake(s_prep_done,portMAX_DELAY);
}
static bool reader_prefetch_resume(size_t page) {
    if(!s_next_fb||s_prep_page!=(int)page||!s_prep_cursor.started||
       s_prep_cursor.generation!=book_layout_generation())return false;
    // 连按到尚未完成的目标时接着画，保留已画文字，不清空重画整页。
    // A rapid turn completes the remaining rows of its target, retaining work already drawn.
    while(reader_prefetch_step()==0){}
    return s_next_page==(int)page&&s_next_generation==book_layout_generation();
}
static void reader_prefetch_idle(app_ctx_t *ctx) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||
       ctx->consumed||ctx->request_app||ctx->request_menu||ctx->request_return||
       (ctx->touch&&ctx->touch->touched)||s_prep_page!=(int)s_page+1)return;
    if(app_settings_book_background_path()[0]||book_layout_page_image_count(s_page)||
       book_layout_page_image_count(s_page+1)||s_page+1>=book_layout_page_count())return;
    if(s_next_page==s_prep_page&&s_next_generation==book_layout_generation())return;
    if(!reader_prefetch_buffer())return;
    const int64_t deadline=esp_timer_get_time()+BOOK_PREP_IDLE_US;
    for(unsigned row=0;row<8;++row){
        if(reader_prefetch_step()!=0||esp_timer_get_time()>=deadline)break;
    }
}
static size_t current_toc_position(void) {
    size_t count = book_navigation_count();
    if (s_selected_toc < count && book_navigation_chapter(s_selected_toc) == s_chapter)
        return s_selected_toc;
    size_t selected = SIZE_MAX;
    size_t best_chapter = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t chapter = book_navigation_chapter(i);
        if (chapter > s_chapter) break;
        if (selected == SIZE_MAX || chapter > best_chapter) { selected = i; best_chapter = chapter; }
    }
    return selected;
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    lock_draw();
    ui_text_set_system_scale(s_view != READING);
    if (s_view == EDIT) { draw_editor(fb); unlock_draw(); return; }
    if (s_view == SEARCH) { draw_search(fb); unlock_draw(); return; }
    if (s_view == IMPORT) { draw_import(fb); unlock_draw(); return; }
    if (s_view == BULK) { draw_bulk(fb, ctx->leaf); unlock_draw(); return; }
    if (s_view == MANAGE) {
        ui_clear_page(fb);
        ui_nav_status(fb);
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "图书详情", EPD_DRAW_ALIGN_CENTER, false);
        ui_hairline(fb, 176, 36, 612, 0x70);
        draw_manage(fb);
        unlock_draw();
        return;
    }
    if (s_view == TOC) {
        book_toc_render(fb, s_book_title[0] ? s_book_title : s_title,
                        book_navigation_count(), current_toc_position(), ctx->leaf, s_message);
        if (s_toc_jump_open)
            book_toc_render_jump(fb, book_navigation_count(), s_toc_jump_percent);
        unlock_draw();
        return;
    }
    if (s_view == READING && s_text) {
        if (s_reader_panel == READER_PANEL_FONT_RECOVERY) {
            ui_clear_page(fb);
            draw_font_recovery(fb);
            unlock_draw();
            return;
        }
        draw_reader(fb, s_page);
        unlock_draw();
        return;
    }
    ui_clear_page(fb);
    // 留白保持纯白，灰阶只由封面和书架材质生成，避免整块背景被当成灰色驱动。
    // Keep whitespace white; only covers and shelf materials create grays, avoiding gray drive over the whole background.
    ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_TOP},UI_GRAY_WHITE,fb);
    // 题头和分页使用白底，避免整块灰底在进入书架时产生不必要的深色过渡。
    // Keep title and pagination white so entering the shelf does not drive an unnecessary gray-background transition.
    ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,192},UI_GRAY_WHITE,fb);
    ui_fill_rect_fast((EpdRect){0,992,UI_LOCK_WIDTH,UI_NAV_TOP-992},UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    char category_title[BOOK_STORE_PATH_MAX];
    copy_text(category_title,sizeof(category_title),s_category_selected ? shelf_category_name(s_category_selected) : "书架");
    fit_text(category_title,52,326);
    ui_text(fb, 36, 90, 52, category_title, EPD_DRAW_ALIGN_LEFT, false);
    int title_width=ui_text_fixed_width_px(ui_text_effective_px(52),category_title);
    ui_draw_icon(fb,36+title_width+27,120,26,
                 s_category_menu ? UI_ICON_CHEVRON_UP : UI_ICON_CHEVRON_DOWN,UI_GRAY_BLACK);
    draw_shelf_header_button(fb, shelf_manage_rect(), false);
    draw_shelf_header_button(fb, shelf_import_rect(), true);
    if (s_message[0] || s_shelf_warning[0])
        ui_text(fb, UI_MARGIN, 991, 17, s_message[0] ? s_message : s_shelf_warning,
                EPD_DRAW_ALIGN_LEFT, false);
    if (app_settings_shelf_style() == 5) {
        ui_fill_round_rect(fb, (EpdRect){36,184,612,788}, 20, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, (EpdRect){36,184,612,788}, 20, 0x50);
    }
    for (int row = 0; row < shelf_rows(); ++row) {
        int i = ctx->leaf * shelf_rows() + row;
        if (i >= s_visible_count) break;
        EpdRect r = row_rect(row);
        char name[128]; copy_text(name, sizeof(name), s_shelf[i].name);
        // 封面抽出只改变书本位置，层板及触摸目标保持原位。
        // Lifting changes the book position only; shelf furniture and touch targets stay put.
        if (s_pressed_control == row)
            r.y -= app_settings_shelf_style() == 5 ? SHELF_LIST_BOUNCE_PX : SHELF_BOOK_LIFT_PX;
        draw_shelf_cover(fb, r, row, name, s_shelf[i].favorite);
        if (app_settings_shelf_style() == 5) draw_list_row(fb, row, &s_shelf[i]);
    }
    draw_shelf_furniture(fb);
    if(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL){
        // 32列对齐后的封面/书架区；顶部状态栏、题头和下方分页不做黑色强整理。
        // Aligned cover/furniture area; status, title and lower pagination never enter the dark settling pass.
        const EpdRect artwork={0,192,UI_LOCK_WIDTH,800};
        display_main_transition_gray_bands(&artwork,1);
    }
    draw_shelf_pager(fb, s_visible_count, ctx->leaf, leaves());
    ui_nav_draw(fb, 1);
    if (s_category_menu) draw_category_menu(fb);
    unlock_draw();
}
static EpdRect reader_status_area(void) { return (EpdRect){0,0,UI_LOCK_WIDTH,80}; }
static bool reader_status_visible(void) {
    return s_view==READING && s_text && !(s_reader_fullscreen && app_settings_reader_immersive());
}
static void reader_status_paint(uint8_t *fb) {
    EpdRect area=reader_status_area();
    ui_fill_rect_fast(area,UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    if (s_reader_panel!=READER_PANEL_BACKGROUND_PREVIEW && app_settings_book_background_path()[0])
        ui_reader_background_paint(fb,area,true);
}
static app_redraw_t reader_clock_tick(app_ctx_t *ctx) {
    if (!reader_status_visible() || (ctx->touch && ctx->touch->touched)) return APP_REDRAW_NONE;
    if (ui_nav_clock_minute()==s_reader_clock_minute) return APP_REDRAW_NONE;
    s_reader_clock_painted_minute=ui_nav_clock_minute();
    lock_draw(); reader_status_paint(ctx->fb); unlock_draw();
    s_reader_clock_pending=true;
    return APP_REDRAW_AREA;
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if(reader_indent_present(ctx,redraw))return true;
    if(s_shelf_key_paint_pending && redraw==APP_REDRAW_AREA && s_view==SHELF && !ctx->request_app){
        s_shelf_key_paint_pending=false;
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,s_area));
        return true;
    }
    if (s_reader_clock_pending) {
        s_reader_clock_pending=false;
        if (redraw==APP_REDRAW_AREA && reader_status_visible()) {
            // 时钟单独短刷，不准备插图、不预排正文，也不累计正文全刷次数。
            // Update status alone, without image preparation, pagination, or reader cleanup counters.
            enum EpdDrawError err=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,reader_status_area());
            if (err==EPD_DRAW_SUCCESS) s_reader_clock_minute=s_reader_clock_painted_minute;
            guard_draw_result(ctx->hl,err);
            return true;
        }
    }
    // 输入不准备封面或正文；候选停顿只整理词栏，不累计整页清屏次数。
    // Input skips cover/body preparation; idle candidate settling affects only its strip and not whole-page cleanup counting.
    if (redraw == APP_REDRAW_AREA && (s_view == EDIT || s_view == SEARCH)) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_area));
            s_input_settle = false;
            s_du_count = 0;
            return true;
        }
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_mode == MODE_DU ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, s_mode, s_area));
        s_du_count = 0;
        return true;
    }
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE || ctx->request_app) return true;
    if (s_presented_view != (int)s_view && redraw == APP_REDRAW_AREA) redraw = APP_REDRAW_PAGE;
    if ((s_view == SHELF || s_view == MANAGE) &&
        !(redraw == APP_REDRAW_AREA && (s_shelf_feedback_pending || s_cover_batch_present_pending))) {
        prepare_covers(ctx);
        gather_cover_entry(ctx);
    }
    if (ctx->request_app) return true;
    if (s_view == READING && s_text) {
        if (s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW)
            (void)ui_reader_background_prepare(app_settings_book_background_path());
        prepare_inline_image();
    }
    int64_t start = esp_timer_get_time();
    const int64_t frame_minute=ui_nav_clock_minute();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) render(ctx, ctx->fb);
    bool prep = kick_prep();
    int64_t drawn = esp_timer_get_time();
    // 本地细点阵挡板已是黑白目标，不注册灰区，省去暂存和第二轮GL16。
    // Local fine-dot guards are already BW targets; omit gray bands to skip staging and the second GL16 pass.
    if (s_view == SHELF && s_shelf_page_pending) display_main_transition_shelf_page();
    s_shelf_page_pending = false;
    enum EpdDrawError err;
    bool image_gray_refresh = s_reader_image_refresh_pending && s_view == READING &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm;
    const bool reader_entry=s_view==READING&&s_text&&s_presented_view!=(int)READING;
    // 入书和从目录返回正文时先用目标页一次完整清理，覆盖图片/背景，不追加擦白或补刷。
    // Clean reader entry and TOC returns in one complete target-page output, including images/backgrounds, without white prepasses or replay.
    if(reader_entry)
        err=update_display_area_full_with(ctx->hl,&E0470_FULL_WAVEFORM,MODE_GC16,
                                         (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT});
    else
    if (image_gray_refresh && redraw == APP_REDRAW_AREA && !s_reader_cleanup && s_presented_view == (int)READING) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = app_settings_reader_turn_effect() == 1
            ? update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, reader_area())
            : update_display_area_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16, reader_area());
        if (err == EPD_DRAW_SUCCESS && s_reader_footer_pending)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, progress_rect());
        if (err == EPD_DRAW_SUCCESS && s_reader_fullscreen)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
    }
    else if (image_gray_refresh) err = app_settings_reader_turn_effect() == 1 &&
        redraw != APP_REDRAW_FULL && !s_reader_cleanup
        ? update_display_mode_diff(ctx->hl, MODE_GL16)
        : update_display_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16);
    else if (s_reader_cleanup && redraw != APP_REDRAW_FULL)
        // 周期清理使用完整原厂灰阶定稿，保留饱和驱动；一次输出，不叠加擦白或文字补刷。
        // Periodic cleanup retains the complete vendor settling/saturation phases in one output, without erase or text replay.
        err = update_display_area_full_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16,
                                            (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT});
    else if (redraw == APP_REDRAW_FULL) err = update_display_full(ctx->hl);
    else if (redraw == APP_REDRAW_AREA && s_view == SHELF && s_shelf_feedback_pending) {
        // 抽出与复位只驱动发生变化的像素，避免整张封面被反复压黑。
        // Lift and restore only changed pixels, avoiding a dark pulse over the whole cover.
        err = update_display_area_diff_with(ctx->hl,
            s_mode == MODE_DU ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, s_mode, s_area);
    }
    else if (redraw == APP_REDRAW_AREA) {
        // 快档每次显式设置并在推屏后恢复，不能把阅读节拍带入主页水波纹。
        // Explicitly select fast pacing per turn and restore afterward, without leaking reader timing into main-page ripple.
        const int prior_tick = s_water_turn_pending ? e0470_page_turn_tick_us() : 0;
        if (s_water_turn_pending) e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        // 翻页动画与页脚刷新独立：全屏没有页脚，仍使用用户选择的水波纹。
        // The turn effect is independent of the footer: full-screen turns still use the selected water effect.
        EpdRect turn_area=s_area;
        // 普通文字保留校准GL16完整擦除，正文与进度共用一次输出；图片、水波纹和清屏保留独立规则。
        // Retain calibrated GL16 erasure for ordinary text and present body/progress once; images, ripple and cleanup retain their own paths.
        const bool fused_footer=s_reader_turn_pending&&!s_water_turn_pending&&s_mode==MODE_GL16;
        if(fused_footer) {
            EpdRect footer=s_reader_fullscreen?reader_fullscreen_progress_area():progress_rect();
            turn_area=ui_rect_union(turn_area,footer);
        }
        err = s_water_turn_pending
            ? update_display_water_turn(ctx->hl, s_area, s_water_turn_dir)
            : s_reader_turn_pending && s_mode == MODE_GL16 && app_settings_reader_turn_effect() == 2 &&
              s_view == READING && s_presented_view == (int)READING &&
              s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm &&
              !book_layout_page_image_count(s_page) && !app_settings_book_background_path()[0]
                ? update_display_text_turn(ctx->hl, turn_area, E0470_TURN_RTL)
            // 普通翻页继续原灰阶差分；速读仅进入已验证的纯文字出口。
            // Ordinary keeps its gray differences; Fast BW enters only the qualified pure-text outlet.
            // 普通翻页与全屏阅读都只驱动变化像素；设置面板等局部绘制保留原路径。
            // Ordinary turns and full-screen reading drive changed pixels only; other local UI keeps its path.
            : (s_reader_fullscreen || s_reader_turn_pending) && s_mode == MODE_GL16
                ? update_display_area_diff_with(ctx->hl,
                    s_reader_turn_pending ? display_main_gray_waveform() : &E0470_WAVEFORM, s_mode, turn_area)
                : update_display_area_with(ctx->hl, &E0470_WAVEFORM, s_mode, s_area);
        if (s_water_turn_pending) e0470_page_turn_set_tick_us(prior_tick);
        if (s_reader_fullscreen && !fused_footer && err == EPD_DRAW_SUCCESS) {
            // 百分比不变时差分直接跳过；变化时只驱动最底部的少量像素。
            // Skip unchanged percentages; otherwise update only the small bottom strip.
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
        }
        if (s_reader_footer_pending && !fused_footer) {
            // 普通翻页页脚只驱动变化像素；全刷由上方整屏分支一次完成。
            // Ordinary turns drive only changed footer pixels; the full-screen branch handles cleanup at once.
            err = (enum EpdDrawError)(err | update_display_area_with(ctx->hl, &E0470_FOLLOW_WAVEFORM,
                                                                     MODE_DU, progress_rect()));
            ESP_LOGI(TAG, "reader regions body_h=%d footer=diff pct=%u", s_area.height, percent(s_page));
        }
    }
    else err = s_view == READING
        // 面板切换只推最终灰阶目标，不将首次选择水波纹算作周期全刷。
        // Panel transitions present their final grays without counting first ripple selection as periodic cleanup.
        ? update_display_area_diff_with(ctx->hl,display_main_normal_waveform(),MODE_GL16,
              (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT})
        : update_display_fast_page(ctx->hl);
    int64_t displayed = esp_timer_get_time();
    reader_prefetch_end(prep);
    if (redraw == APP_REDRAW_AREA && s_mode == MODE_DU &&
        !s_reader_cleanup && !image_gray_refresh && !s_shelf_feedback_pending) {
        s_du_area = s_du_count ? ui_rect_union(s_du_area, s_area) : s_area;
        ++s_du_count;
        s_du_ms = esp_timer_get_time() / 1000;
    } else s_du_count = 0;
    ESP_LOGI(TAG, "present draw=%lld display=%lld join=%lld ms", (drawn - start) / 1000,
             (displayed - drawn) / 1000, (esp_timer_get_time() - displayed) / 1000);
    guard_draw_result(ctx->hl, err);
    if ((s_view==SHELF || s_view==MANAGE) && err==EPD_DRAW_SUCCESS) {
        if ((redraw==APP_REDRAW_PAGE || redraw==APP_REDRAW_FULL) && shelf_cover_frame_ready()) s_cover_dirty_mask=0;
        else if (s_cover_batch_present_pending) s_cover_dirty_mask &= ~s_cover_present_mask;
    }
    s_cover_present_mask=0;
    if (err==EPD_DRAW_SUCCESS && reader_status_visible() &&
        (redraw==APP_REDRAW_PAGE || redraw==APP_REDRAW_FULL || s_reader_cleanup))
        s_reader_clock_minute=frame_minute;
    s_reader_text_frame = err == EPD_DRAW_SUCCESS && s_view == READING && s_text &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm &&
        book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_reader_cleanup = false;
    s_reader_image_refresh_pending = false;
    if(err==EPD_DRAW_SUCCESS) {
        s_presented_view = (int)s_view;
        if(reader_entry)s_turns=0;
    }
    s_reader_footer_pending = false;
    s_water_turn_pending = false;
    s_shelf_feedback_pending = false;
    s_cover_batch_present_pending = false;
    s_reader_turn_pending = false;
    s_mode = MODE_GL16;
    return true;
}
static app_redraw_t paint_reading(app_ctx_t* ctx, enum EpdDrawMode mode) {
    s_water_turn_pending = false;
    s_reader_turn_pending = false;
    int64_t started = esp_timer_get_time();
    (void)ui_reader_background_prepare(app_settings_book_background_path());
    prepare_inline_image();
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() &&
        book_layout_page_image_count(s_page) > 0 && s_page_images;
    lock_draw();
    bool cached = !s_toolbar && !s_clear_confirm && s_next_fb &&
                  ((s_next_page == (int)s_page && s_next_generation == book_layout_generation()) ||
                   reader_prefetch_resume(s_page));
    if (cached) {
        memcpy(ctx->fb, s_next_fb, fb_bytes());
        draw_reader_outer(ctx->fb,s_page);
    }
    else draw_reader(ctx->fb, s_page);
    // 下一页可能预渲染于数分钟前，复制后以当前时间重绘状态栏。
    // A prefetched page can be minutes old; replace its status with the current time.
    if (cached && reader_status_visible()) reader_status_paint(ctx->fb);
    unlock_draw();
    ESP_LOGI(TAG, "paint cached=%d ms=%lld", cached, (esp_timer_get_time() - started) / 1000);
    s_area = reader_area();
    s_reader_footer_pending = !s_reader_fullscreen;
    s_mode = mode;
    return APP_REDRAW_AREA;
}

/* ---- 文件与进度 / Files and progress ---- */
typedef struct {
    size_t chapter;
    char** images;
    size_t count;
} reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;

// 排版只探测尺寸；SVG或较晚JPEG帧头由书源保留完整读取回退。
// Probe dimensions for layout; the source retains full extraction for SVG wrappers and late JPEG headers.
static bool reader_image_dims(void* ctx, int image, int* width, int* height) {
    const reader_dims_ctx_t* c = (const reader_dims_ctx_t*)ctx;
    if (!c || image < 0 || (size_t)image >= c->count) return false;
    unsigned w = 0, h = 0;
    if (book_chapter_image_dimensions(c->chapter, c->images[image], &w, &h) != ESP_OK ||
        !w || !h || w > INT_MAX || h > INT_MAX) return false;
    if (width) *width = (int)w;
    if (height) *height = (int)h;
    return true;
}
static void flush_ticket_stats(void) {
    uint32_t seconds = s_stats_pending_ms / 1000;
    if (!seconds && !s_stats_pending_turns) return;
    if (book_ticket_record(seconds, s_stats_pending_turns) == ESP_OK) {
        s_stats_pending_ms -= seconds * 1000;
        s_stats_pending_turns = 0;
    }
}
static void track_ticket_stats(app_ctx_t* ctx) {
    if (s_view != READING || !s_text) { s_stats_last_ms = ctx->now_ms; return; }
    if (s_stats_last_ms > 0 && ctx->now_ms >= s_stats_last_ms &&
        ctx->now_ms - s_stats_last_ms <= 5000 &&
        ctx->now_ms - s_stats_activity_ms <= 300000 && !s_toolbar) {
        uint32_t elapsed = (uint32_t)(ctx->now_ms - s_stats_last_ms);
        s_stats_pending_ms += elapsed;
        s_session_read_ms += elapsed;
    }
    s_stats_last_ms = ctx->now_ms;
    if (s_stats_pending_ms >= 30000) flush_ticket_stats();
}
static void free_book(void) {
    reader_font_recovery_reset();
    s_chapter_jump_undo.valid = false;
    s_chapter_jump_digits[0] = 0;
    flush_ticket_stats();
    e0470_page_turn_release();
    s_water_turn_pending = false;
    s_reader_turn_pending = s_reader_text_frame = false;
    pending_progress_t* pending = pending_find(s_path);
    if (pending && !pending->dirty) pending_discard(s_path);
    invalidate_prep();
    book_layout_set_image_dims(NULL, NULL);
    s_reader_dims_ctx = (reader_dims_ctx_t){0};
    book_layout_free();
    book_layout_set_chapter_lead(0, 0);
    book_layout_set_chapter_lead_width(0);
    s_chapter_lead_skip = s_chapter_lead_height = 0;
    s_chapter_lead_width = 0;
    s_chapter_heading_title[0] = s_chapter_heading_label[0] = 0;
    free(s_text);
    free(s_blocks);
    free(s_runs);
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = NULL; s_image_count = 0;
    release_page_images();
    s_reader_image_refresh_pending = false;
    s_blocks = NULL;
    s_block_count = 0;
    s_runs = NULL;
    s_run_count = 0;
    book_layout_set_runs(NULL, 0);
    s_text = NULL;
    s_text_len = 0;
    s_selected_toc = SIZE_MAX;
    s_jump_offset = SIZE_MAX;
    book_close();
    s_path[0] = 0;
    s_reader_favorite = false;
    s_book_title[0] = 0;
    app_font_activate_system();
}
// 通栏之后一页位图更大：装不下就按最大空闲块等比缩一档——图小一圈也胜过整页消失。
// A full-bleed page bitmap is larger: when it does not fit, step the aspect-preserving size down
// into the largest free block. A slightly smaller illustration still beats a missing one.
static uint8_t *alloc_page_bitmap(int *width, int *height, size_t *pixels) {
    uint8_t *gray = heap_caps_malloc(*pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray) return gray;
    size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (room == 0) return NULL;
    for (int guard = 0; guard < 24 && *pixels > room; ++guard) {
        *width = *width > 1 ? *width * 7 / 8 : 1;
        *height = *height > 1 ? *height * 7 / 8 : 1;
        *pixels = (size_t)*width * (size_t)*height;
    }
    if (*pixels > room) return NULL;
    gray = heap_caps_malloc(*pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray)
        ESP_LOGW(TAG, "inline illustration shrunk to %dx%d, free block %u KB",
                 *width, *height, (unsigned)(room / 1024));
    return gray;
}
static void release_page_images(void) {
    reader_zoom_reset();
    for (int i = 0; i < s_page_image_count; ++i) free(s_page_images[i].gray);
    free(s_page_images); s_page_images = NULL; s_page_image_count = 0;
    s_page_images_for = SIZE_MAX;
}
static void prepare_inline_image(void) {
    if (app_settings_reader_hide_images()) { release_page_images(); return; }
    uint32_t generation = book_layout_generation();
    if (s_page_images_for == s_page && s_page_images_generation == generation) return;
    // 纯文字跨页不应作废已排好的下一页；布局代次改变或任一侧有插图时才失效。
    // Preserve the prefetched next text page; invalidate only across layout generations or image-bearing pages.
    const bool changed_layout_or_image = s_page_images_generation != generation || s_page_image_count > 0;
    release_page_images();
    s_page_images_for = s_page; s_page_images_generation = generation;
    // 插图不显示没有别的症状，每条失败路径以前都是静默 continue；这里把结局记下来，
    // 免得继续靠猜。
    // A missing illustration has no other symptom and every failure path used to be a silent
    // continue; record the outcome instead of guessing.
    int count = reader_image_slots(s_page);
    if (changed_layout_or_image || count > 0) invalidate_prep();
    size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!count || count > (int)HTML_TEXT_MAX_BLOCKS) {
        ESP_LOGI(TAG, "inline page %u: %d image slots", (unsigned)s_page, count);
        return;
    }
    s_page_images = heap_caps_calloc((size_t)count, sizeof(*s_page_images), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_page_images) {
        // 分配失败时计数必须跟着归零：release_page_images 会按计数遍历这张表。
        // The count has to follow the table to zero: release_page_images walks it by count.
        s_page_image_count = 0;
        ESP_LOGW(TAG, "inline page %u: no PSRAM for %d slots", (unsigned)s_page, count);
        return;
    }
    s_page_image_count = count;
    // 逐张解码并立即释放压缩数据；全页灰阶位图的总面积不超过插图区域。
    // Decode serially and release each encoded buffer; the page's bitmap area stays within its
    // illustration region.
    EpdRect band = book_layout_image_rect();
    size_t pixels_left = (size_t)band.width * band.height;
    int decoded = 0;
    for (int i = 0; i < count; ++i) {
        int index = -1, width = 0, height = 0;
        if (!reader_image_slot(s_page, i, &index, NULL, &width, &height) ||
            index < 0 || (size_t)index >= s_image_count || width <= 0 || height <= 0) {
            ESP_LOGW(TAG, "inline %d: slot i=%d width=%d height=%d", i, index, width, height);
            continue;
        }
        uint8_t *encoded = NULL; size_t size = 0; bool png = false;
        esp_err_t err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "inline %d %s: read %s", i, s_images[index], esp_err_to_name(err));
            free(encoded);
            continue;
        }
        unsigned source_w = 0, source_h = 0;
        if (book_image_dimensions(encoded, size, png, &source_w, &source_h) && source_w && source_h) {
            // 回退槽也使用真实宽高等比缩小，避免尺寸探测失败时拉伸图片。
            // Aspect-fit even fallback slots from decoded dimensions, avoiding stretched unknown-size images.
            uint64_t w = source_w, h = source_h;
            if (w > (unsigned)width) { h = h * width / w; w = width; }
            if (h > (unsigned)height) { w = w * height / h; h = height; }
            width = w ? (int)w : 1; height = h ? (int)h : 1;
            size_t pixels = (size_t)width * height;
            if (pixels > pixels_left) {
                ESP_LOGW(TAG, "inline %d: %ux%u over %u px budget",
                         i, (unsigned)width, (unsigned)height, (unsigned)pixels_left);
                free(encoded);
                continue;
            }
            uint8_t *gray = alloc_page_bitmap(&width, &height, &pixels);
            if (gray == NULL) {
                ESP_LOGW(TAG, "inline %d: no PSRAM for %u px (free %u)",
                         i, (unsigned)pixels,
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                free(encoded);
                continue;
            }
            if (book_image_grayscale(encoded, size, png, width, height, gray)) {
                s_page_images[i] = (reader_image_t){gray, width, height};
                pixels_left -= pixels;
                ++decoded;
            } else {
                ESP_LOGW(TAG, "inline %d: grayscale decode failed", i);
                free(gray);
            }
        } else {
            ESP_LOGW(TAG, "inline %d: unknown dimensions (%u bytes, png=%d)", i, (unsigned)size, (int)png);
        }
        free(encoded);
        vTaskDelay(1);
    }
    // 只在有图没画出来时汇总：插图缺失没有别的症状，而每次都打会把日志淹掉。
    // Summary only when something did not make it: a missing illustration has no other symptom,
    // and logging every page would drown the console.
    if (decoded < count) {
        ESP_LOGW(TAG, "inline page %u: %d/%d decoded, psram %u->%u KB", (unsigned)s_page, decoded, count,
                 (unsigned)(psram_before / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
    }
}
// 标题匹配要宽松：书内导航与正文的写法常常不同（"第一章 出发" 对 "出发"、"1. Prologue" 对
// "序章"，竖排书尤其如此）。逐字比较失败时正文标题块不会被跳过，题头与正文就各显示一次。
// Heading matching has to be forgiving: an authored navigation title rarely matches the body text
// verbatim ("Chapter 1 Departure" against "Departure", a numbered English title against its
// Japanese one), and a strict compare leaves the body heading on the page, so it shows up twice.
static bool heading_text_matches(const char *text, const char *toc, const char *heading,
                                 const char *label) {
    if (text == NULL || text[0] == 0) return false;
    const char *candidates[3] = {toc, heading, label};
    for (int i = 0; i < 3; ++i) {
        const char *cand = candidates[i];
        if (cand == NULL || cand[0] == 0) continue;
        // 互相包含即视为同一标题；短于三个字节的候选不作为依据，免得误吞正文。
        // Either containing the other counts as the same heading; candidates shorter than three
        // bytes are ignored so ordinary body text cannot be swallowed.
        if (strlen(cand) < 3) continue;
        if (strstr(text, cand) != NULL || strstr(cand, text) != NULL) return true;
    }
    return false;
}

static bool load_chapter_at(app_ctx_t* ctx, size_t chapter, size_t offset,
                            bool last_page, const char *anchor, size_t source_offset) {
    bool clear_toc = chapter != s_chapter || (!anchor && source_offset == SIZE_MAX);
    html_text_t loaded = {0};
    size_t anchor_offset = 0;
    esp_err_t err = book_chapter_load_blocks_target(chapter, anchor, source_offset,
                                                    &anchor_offset, &loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter load failed: %s", esp_err_to_name(err));
        copy_text(s_message, sizeof(s_message), "加载失败，请检查存储卡后重试");
        return false;
    }
    char heading[128] = {0}, label[32] = {0}, toc_title[128] = {0};
    size_t lead_skip = 0;
    unsigned lead_height = 0;
    if (book_kind() == BOOK_KIND_EPUB && loaded.len) {
        // 已识别的正文编号题头优先；没有就不画题头，不用「第 N 节」占位。
        // Prefer a recognized numbered body heading; without one no heading is drawn at all.
        if (book_chapter_title(chapter, toc_title, sizeof(toc_title)) != ESP_OK) toc_title[0] = 0;
        copy_text(heading, sizeof(heading), toc_title);
        char *chapter_mark = strstr(heading, "章");
        if (chapter_mark && chapter_mark - heading < 24) {
            char *after = chapter_mark + strlen("章");
            while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
                   !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、"))) {
                if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
                else after += strlen("：");
            }
            if (*after && (size_t)(chapter_mark + strlen("章") - heading) < sizeof(label)) {
                size_t label_len = (size_t)(chapter_mark + strlen("章") - heading);
                memcpy(label, heading, label_len); label[label_len] = 0;
                memmove(heading, after, strlen(after) + 1);
            }
        }
        size_t skip_blocks = 0;
        // 最多看四个块：标题可能被拆成"第一章"、"出发"、"——副标题"这样几块。
        // Up to four blocks: a heading can arrive split across several.
        while (skip_blocks < loaded.count && skip_blocks < 4 &&
               loaded.blocks[skip_blocks].heading && loaded.blocks[skip_blocks].image < 0) {
            char body_heading[128] = {0};
            size_t len = loaded.blocks[skip_blocks].len;
            if (len >= sizeof(body_heading)) break;
            memcpy(body_heading, loaded.utf8 + loaded.blocks[skip_blocks].offset, len);
            while (len && (body_heading[len - 1] == ' ' || body_heading[len - 1] == '\t')) body_heading[--len] = 0;
            const char *trimmed = body_heading;
            while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
            if (!heading_text_matches(trimmed, toc_title, heading, label)) break;
            ++skip_blocks;
        }
        if (skip_blocks) lead_skip = skip_blocks < loaded.count ? loaded.blocks[skip_blocks].offset : loaded.len;
        // 首块是插图时不预留题头：那页整页留给插图，标题在它后面才排。
        // 这个条件一直是题头该不该出现的判据，横排竖排都一样。
        // An illustration at the head of the chapter reserves nothing: it owns that page and the
        // heading follows. This condition has always decided whether a sheet gets a heading at all,
        // in either setting.
        if (skip_blocks < loaded.count && loaded.blocks[skip_blocks].image >= 0) {
            lead_skip = 0;
            lead_height = 0;
        } else {
            lead_height = body_rect().height > 214 ? 214 : 0;
        }
    }
    size_t old_lead_skip = s_chapter_lead_skip;
    unsigned old_lead_height = s_chapter_lead_height;
    unsigned old_lead_width = s_chapter_lead_width;
    reader_dims_ctx_t old_dims_ctx = s_reader_dims_ctx;
    lock_draw();
    invalidate_prep();
    // 题头几何按当前模式重算，横排留顶部行高、竖排留右侧列宽，绘制与预留不会错开。
    // Recompute the heading geometry for the current mode -- rows at the top horizontally,
    // columns at the right vertically -- so drawing and reservation cannot drift apart.
    apply_chapter_lead(heading, label, lead_skip, lead_height);
    s_reader_dims_ctx = (reader_dims_ctx_t){chapter, loaded.images, loaded.image_count};
    book_layout_set_image_dims(reader_image_dims, &s_reader_dims_ctx);
    book_layout_set_runs(loaded.runs, loaded.run_count);
    bool ok = book_layout_build_blocks(loaded.utf8, loaded.len, loaded.blocks, loaded.count, body_rect(), s_px);
    if (!ok) {
        // 回滚前恢复旧章上下文，避免尺寸回调引用已释放的新章图片。/ Restore the previous context before rollback to avoid a freed image list.
        s_reader_dims_ctx = old_dims_ctx;
        html_text_free(&loaded);
        // run 表随 loaded 一起没了，先把排版还回旧章的那份，否则重排会读到已释放的内存。
        // The run table dies with `loaded`, so put the previous chapter's back before the
        // rollback layout reads freed memory.
        book_layout_set_runs(s_runs, s_run_count);
        // 题头几何也跟着回滚，否则这一章的题头参数会留在失败的那次上。
        // Roll the heading geometry back too, or the failed chapter's parameters would stick.
        s_chapter_lead_skip = old_lead_skip;
        s_chapter_lead_height = old_lead_height;
        s_chapter_lead_width = old_lead_width;
        book_layout_set_chapter_lead(old_lead_skip, old_lead_height);
        book_layout_set_chapter_lead_width(old_lead_width);
        bool restored = s_text && book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            ctx->leaf = 0;
        }
        copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
        return false;
    }
    book_excerpt_offer(s_path, chapter, &loaded);
    free(s_text);
    free(s_blocks);
    free(s_runs);
    s_text = loaded.utf8;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    s_runs = loaded.runs;
    s_run_count = loaded.run_count;
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = loaded.images; s_image_count = loaded.image_count;
    release_page_images();
    s_chapter = chapter;
    s_chapter_jump_undo.valid = false;
    if (clear_toc) s_selected_toc = SIZE_MAX;
    s_chapter_lead_skip = lead_skip;
    s_chapter_lead_row_height = lead_height;
    copy_text(s_chapter_heading_title, sizeof(s_chapter_heading_title), heading);
    copy_text(s_chapter_heading_label, sizeof(s_chapter_heading_label), label);
    s_page = last_page ? book_layout_page_count() - 1 :
             book_layout_page_for_offset(anchor || source_offset != SIZE_MAX ? anchor_offset : offset);
    s_jump_offset = anchor || source_offset != SIZE_MAX ? anchor_offset : SIZE_MAX;
    s_jump_page = s_page;
    if (book_chapter_title(chapter, s_title, sizeof(s_title)) != ESP_OK) s_title[0] = 0;
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    s_message[0] = 0;
    unlock_draw();
    prepare_inline_image();
    // 开书这条链上有解析、解压和字体装载，栈曾经在这里溢出过；留一个可观测的水位。
    // 顺带量一下章节排完后的 PSRAM 余量：书内字体的装载预算要靠这个数来定，不能靠估。
    // Parsing, inflate and face loading all ride this chain, and it overflowed once before;
    // keep the headroom observable. The PSRAM left after a chapter is laid out is also the
    // number the face budget should be derived from rather than guessed.
    ESP_LOGI(TAG, "chapter %u stack free %u psram free %u largest %u", (unsigned)chapter,
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return true;
}
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page) {
    return load_chapter_at(ctx, chapter, offset, last_page, NULL, SIZE_MAX);
}
// 关闭图片时跳过纯插图页，避免出现空白翻页。/ Skip illustration-only pages when images are disabled.
static void skip_hidden_image_pages(app_ctx_t *ctx, int direction) {
    if (!app_settings_reader_hide_images()) return;
    for (int guard = 0; guard < 128 && book_layout_page_image(s_page) >= 0; ++guard) {
        if (direction > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
        else if (direction < 0 && s_page > 0) --s_page;
        else if (direction > 0 && s_chapter + 1 < book_chapter_count()) {
            if (!load_chapter(ctx, s_chapter + 1, 0, false)) break;
        } else if (direction < 0 && s_chapter > 0) {
            if (!load_chapter(ctx, s_chapter - 1, 0, true)) break;
        } else break;
    }
}
static bool open_book_impl(app_ctx_t* ctx, const char* path) {
    if (!book_file_path_supported(path)) {
        copy_text(s_message, sizeof(s_message), "此版本仅支持 TXT 和 EPUB 书籍");
        return false;
    }
    s_reader_return_home = false;
    if (!strncmp(path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0};
        read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) {
            copy_text(s_message, sizeof(s_message), "TF 卡不可用，请重新挂载后打开");
            return false;
        }
    }
    // 自动续读也不能绕过同路径旧文件的清理重试。
    // Automatic resume must also finish cleanup of the old file at this path.
    if (delete_retry_find(path)) {
        copy_text(s_message, sizeof(s_message), "文件已删除，进度清理失败，请重试");
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_message, sizeof(s_message), "文件不可读，请检查 TF 卡和书籍文件");
        return false;
    }
    if (strncmp(path, "/flash/", 7) == 0 && st.st_size > BOOK_STORE_FLASH_FILE_MAX) {
        copy_text(s_message, sizeof(s_message), "内置存储单本限 1 MB");
        return false;
    }
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
    // 书架封面和上一本书的预渲染缓冲可以重建；先释放，再解析 EPUB。
    // Cover and previous-page caches are rebuildable; release them before EPUB parsing.
    invalidate_covers();
    app_home_release_clock_cache();
    lock_draw();
    invalidate_prep();
    free(s_next_fb);
    s_next_fb = NULL;
    unlock_draw();
    if (network.network_ready) ttf_font_cache_clear();
    // 开书前把字形缓存全还回去并量一下能腾多少：书内字体要的正是这块 PSRAM，而系统字体
    // 的缓存是里面最容易回收的。字体本身保持装载，翻页时按需重新缓存。
    // Hand every glyph cache back before opening and measure what that was worth: embedded
    // faces need exactly this PSRAM, and the system face's cache is the easiest part to
    // reclaim. The faces stay loaded and re-cache on demand.
    size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t reclaimed = ttf_font_cache_clear_all();
    size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "open start wifi=%d internal=%u/%u psram=%u/%u cache=%uK freed=%uK",
             network.network_ready,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)psram_after,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)(reclaimed / 1024),
             (unsigned)((psram_after - psram_before) / 1024));
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法保留待保存进度");
        return false;
    }
    save_progress();
    free_book();
    s_reader_fullscreen = s_requested_fullscreen;
    app_font_activate_reading();
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法打开图书");
        app_font_activate_system();
        return false;
    }
    esp_err_t err = book_open(path);
    // 书源打开后再把书内字体开关接上：EPUB 才有效，TXT 是空操作。
    // Wire the embedded-face switch only after the source is open: it only applies to EPUB,
    // and is a no-op for TXT.
    if (err == ESP_OK) {
        s_book_fonts_applied = app_settings_book_fonts();
        book_set_embedded_fonts(s_book_fonts_applied);
    }
    if (err != ESP_OK) {
        pending_progress_t* pending = pending_find(path);
        if (pending && !pending->dirty) pending_discard(path);
        copy_text(s_message, sizeof(s_message),
                  err == ESP_ERR_NOT_SUPPORTED ? "文件格式或压缩方式暂不支持" :
                  err == ESP_ERR_NO_MEM ? "内存不足，请重启后重试" :
                  err == ESP_ERR_INVALID_SIZE ? "图书资源过大或章节过多" :
                  "无法打开图书，请检查文件");
        ESP_LOGW(TAG, "open failed: %s internal=%u/%u psram=%u/%u", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        app_font_activate_system();
        return false;
    }
    copy_text(s_path, sizeof(s_path), path);
    s_reader_favorite = favorite_load(path);
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    if (!book_title_from_path(path, s_book_title, sizeof(s_book_title)))
        clean_filename(s_book_title, sizeof(s_book_title), filename);
    s_file_size = (uint32_t)st.st_size;
    book_progress_t p = {0};
    bool resume = pending_restore(path, s_file_size, &p) || book_progress_load(path, s_file_size, &p);
    // 排版字号以全局阅读设置为准；旧书进度只决定阅读位置。
    // Use the shared reading size for every book; per-book progress restores position only.
    s_px = app_settings_book_px();
    if (s_px < BOOK_PX_MIN || s_px > BOOK_PX_MAX) s_px = 48;
    size_t chapter = resume && p.chapter < book_chapter_count() ? p.chapter : 0;
    if (!load_chapter(ctx, chapter, resume && p.chapter == chapter ? p.byte_off : 0, false)) {
        free_book();
        return false;
    }
    skip_hidden_image_pages(ctx, 1);
    if (!s_text) { free_book(); return false; }
    s_view = READING;
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() &&
        book_layout_page_image_count(s_page) > 0 && s_page_images;
    s_stats_last_ms = s_stats_activity_ms = ctx->now_ms;
    s_reader_panel = READER_PANEL_NONE;
    s_clear_confirm = s_batch_confirm = false;
    s_session_read_ms = s_session_turns = 0;
    s_reader_notice[0] = 0;
    s_turns = s_unsaved = 0;
    pending_mark_latest(s_path);
    save_progress();
    // 打开即重新上书架：移出书架只隐藏条目，重新读这本书就是它该在架上的信号。
    // Opening puts the book back on the shelf: removal only hides the entry, and reading it
    // again is the signal that it belongs there.
    if (shelf_hidden_load(s_path)) (void)shelf_hidden_save(s_path, false);
    ESP_LOGI(TAG, "opened kind=%d chapters=%u pages=%u px=%d", book_kind(), (unsigned)book_chapter_count(), (unsigned)book_layout_page_count(), s_px);
    return true;
}
static bool open_book(app_ctx_t *ctx, const char *path) {
    if (!book_cover_jobs_quiesce(2000)) {
        copy_text(s_message,sizeof(s_message),"封面任务正在收尾，请稍后再试");return false;
    }
    esp_err_t checkpoint_error=pico_boot_book_begin(path);
    if (checkpoint_error != ESP_OK) {
        ESP_LOGW("book", "open checkpoint failed: %s", esp_err_to_name(checkpoint_error));
        copy_text(s_message, sizeof(s_message), "启动记录保存失败，请重试");
        return false;
    }
    bool ok = open_book_impl(ctx, path);
    pico_boot_book_end(ok);
    return ok;
}
static bool open_requested_book(app_ctx_t* ctx) {
    if (!s_requested_open[0]) return false;
    char path[BOOK_STORE_PATH_MAX];
    copy_text(path, sizeof(path), s_requested_open);
    bool from_home = s_requested_open_home;
    s_requested_open[0] = 0;
    s_requested_open_home = false;
    bool opened = open_book(ctx, path);
    s_requested_fullscreen = true;
    if (!opened) return false;
    s_reader_return_home = from_home;
    return true;
}
/* ---- 书架数据 / Shelf data ---- */
static int shelf_category_add(const char *path) {
    for (int i=2;i<s_category_count;++i) {
        if (!strcmp(s_categories[i].path,path)) { s_categories[i].available=true; return i; }
    }
    if (s_category_count==SHELF_CATEGORY_LIMIT) return -1;
    if (s_category_count==s_category_capacity) {
        int capacity=s_category_capacity ? s_category_capacity*2 : 16;
        if (capacity>SHELF_CATEGORY_LIMIT) capacity=SHELF_CATEGORY_LIMIT;
        shelf_category_t *next=heap_caps_realloc(s_categories,(size_t)capacity*sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!next) return -1;
        s_categories=next; s_category_capacity=capacity;
    }
    int index=s_category_count++;
    memset(&s_categories[index],0,sizeof(s_categories[index]));
    copy_text(s_categories[index].path,sizeof(s_categories[index].path),path);
    s_categories[index].available=true;
    return index;
}
static void shelf_category_init(void) {
    const char *root=app_settings_books_dir();
    if (strcmp(s_category_root,root)) {
        free(s_categories); s_categories=NULL;
        s_category_count=s_category_capacity=s_category_selected=s_category_menu_page=0;
        copy_text(s_category_root,sizeof(s_category_root),root);
        s_shelf_cache_valid=false;
    }
    if (s_categories) return;
    s_categories=heap_caps_calloc(16,sizeof(*s_categories),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_categories) return;
    s_category_capacity=16; s_category_count=2;
    s_categories[0].available=s_categories[1].available=true;
}
static const char *shelf_category_name(int index) {
    if (index==1) return "未分类";
    if (!s_categories || index<2 || index>=s_category_count) return "全部书籍";
    const char *slash=strrchr(s_categories[index].path,'/');
    return slash ? slash+1 : s_categories[index].path;
}
static bool shelf_category_matches(const char *path) {
    if (!s_category_selected || !s_categories) return true;
    if (s_category_selected==1) {
        char child[BOOK_STORE_PATH_MAX];
        return !book_shelf_child(s_category_root,path,child,sizeof(child));
    }
    size_t size=strlen(s_categories[s_category_selected].path);
    return !strncmp(path,s_categories[s_category_selected].path,size) && path[size]=='/';
}
static void shelf_category_remember(app_ctx_t *ctx) {
    if (s_categories && s_view==SHELF && !s_scan_pending)
        s_categories[s_category_selected].pages[app_settings_shelf_style()==5]=ctx->leaf;
}
static void shelf_category_restore(app_ctx_t *ctx) {
    int layout=app_settings_shelf_style()==5;
    int saved=s_categories ? s_categories[s_category_selected].pages[layout] : 0;
    ctx->leaf=book_shelf_saved_page(saved,s_visible_count,shelf_rows());
    if (s_categories) s_categories[s_category_selected].pages[layout]=ctx->leaf;
}
static void shelf_categories_recount(void) {
    if (!s_categories) return;
    for (int i=0;i<s_category_count;++i) s_categories[i].count=0;
    for (int i=0;i<s_count;++i) {
        if (s_shelf[i].removed) continue;
        ++s_categories[0].count;
        char child[BOOK_STORE_PATH_MAX],path[BOOK_STORE_PATH_MAX];
        if (!book_shelf_child(s_category_root,s_shelf[i].path,child,sizeof(child))) {
            ++s_categories[1].count; continue;
        }
        int written=snprintf(path,sizeof(path),"%s/%s",s_category_root,child);
        if (written<0 || (size_t)written>=sizeof(path)) continue;
        int index=shelf_category_add(path);
        if (index>=2) ++s_categories[index].count;
    }
    if (s_category_selected>=s_category_count || !s_categories[s_category_selected].available)
        s_category_selected=0;
}
static int shelf_category_at(int ordinal) {
    for (int i=0;i<s_category_count;++i) {
        if (s_categories[i].available && ordinal--==0) return i;
    }
    return -1;
}
static int shelf_category_available(void) {
    int count=0;
    for (int i=0;i<s_category_count;++i) if (s_categories[i].available) ++count;
    return count;
}
static EpdRect shelf_category_box(void) {
    int count=shelf_category_available()-s_category_menu_page*SHELF_CATEGORY_ROWS;
    if (count>SHELF_CATEGORY_ROWS) count=SHELF_CATEGORY_ROWS;
    if (count<1) count=1;
    return (EpdRect){36,158,400,16+count*83+(shelf_category_available()>SHELF_CATEGORY_ROWS ? 56 : 0)};
}
static void draw_category_menu(uint8_t *fb) {
    EpdRect box=shelf_category_box();
    ui_fill_round_rect(fb,box,23,UI_GRAY_WHITE);
    ui_draw_control_frame(fb,box,23,0x50);
    for (int row=0;row<SHELF_CATEGORY_ROWS;++row) {
        int index=shelf_category_at(s_category_menu_page*SHELF_CATEGORY_ROWS+row);
        if (index<0) break;
        int y=box.y+8+row*83;
        bool selected=index==s_category_selected;
        if (selected) ui_fill_round_rect(fb,(EpdRect){box.x+8,y,box.width-16,79},16,0xd0);
        ui_draw_icon(fb,box.x+35,y+40,27,index<2 ? UI_ICON_LIBRARY_BIG : UI_ICON_FOLDER_OPEN,UI_GRAY_BLACK);
        char name[BOOK_STORE_PATH_MAX],count[24];
        snprintf(count,sizeof(count),"%d",s_categories[index].count);
        int name_width=290-ui_text_fixed_width_px(ui_text_effective_px(22),count);
        if (name_width>228) name_width=228;
        if (name_width<60) name_width=60;
        copy_text(name,sizeof(name),shelf_category_name(index)); fit_text(name,28,name_width);
        ui_text_vc(fb,box.x+62,y+40,28,name,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,box.x+box.width-38,y+40,22,count,EPD_DRAW_ALIGN_RIGHT,false);
        if (selected) epd_fill_circle(box.x+box.width-20,y+40,4,UI_GRAY_BLACK,fb);
    }
    if (shelf_category_available()>SHELF_CATEGORY_ROWS) {
        int y=box.y+box.height-28;
        ui_draw_icon(fb,box.x+48,y,28,UI_ICON_CHEVRON_LEFT,s_category_menu_page ? 0 : 0x90);
        char pages[24]; snprintf(pages,sizeof(pages),"%d / %d",s_category_menu_page+1,
            (shelf_category_available()+SHELF_CATEGORY_ROWS-1)/SHELF_CATEGORY_ROWS);
        ui_text_vc(fb,box.x+box.width/2,y,22,pages,EPD_DRAW_ALIGN_CENTER,false);
        ui_draw_icon(fb,box.x+box.width-48,y,28,UI_ICON_CHEVRON_RIGHT,0);
    }
}
static bool shelf_matches(const shelf_entry_t* item) {
    return shelf_category_matches(item->path) && item->search_match && (s_filter == 0 || (s_filter == 1 && !item->is_flash) || (s_filter == 2 && item->is_flash));
}
static int compare_books(const void* a, const void* b) {
    const shelf_entry_t* x = a;
    const shelf_entry_t* y = b;
    if (shelf_matches(x) != shelf_matches(y)) return shelf_matches(x) ? -1 : 1;
    if (x->favorite != y->favorite)
        return x->favorite ? -1 : 1;
    if (s_recent_sort && x->recent != y->recent) return x->recent > y->recent ? -1 : 1;
    int name = strcasecmp(x->name, y->name);
    return name ? name : strcmp(x->path, y->path);
}
static void sort_shelf(app_ctx_t* ctx) {
    invalidate_covers();
    if (s_count > 1) qsort(s_shelf, s_count, sizeof(*s_shelf), compare_books);
    s_visible_count = 0;
    while (s_visible_count < s_count && shelf_matches(&s_shelf[s_visible_count])) ++s_visible_count;
    ctx->leaf = 0;
}
static app_redraw_t shelf_category_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type!=UI_GESTURE_TAP && ev->type!=UI_GESTURE_SWIPE_U && ev->type!=UI_GESTURE_SWIPE_D)
        return APP_REDRAW_NONE;
    EpdRect previous=shelf_category_box();
    if (ev->type==UI_GESTURE_TAP && ui_nav_hit(ev->x0,ev->y0)>=0) {
        s_category_menu=false; ui_nav_request(ctx,ui_nav_hit(ev->x0,ev->y0)); return APP_REDRAW_NONE;
    }
    if (!s_category_menu) {
        if (ev->type!=UI_GESTURE_TAP || !s_categories) return APP_REDRAW_NONE;
        s_category_menu=true; s_category_menu_page=0;
        int ordinal=0;
        for (int i=0;i<s_category_selected;++i) if (s_categories[i].available) ++ordinal;
        s_category_menu_page=ordinal/SHELF_CATEGORY_ROWS;
    } else {
        int pages=(shelf_category_available()+SHELF_CATEGORY_ROWS-1)/SHELF_CATEGORY_ROWS;
        int direction=ev->type==UI_GESTURE_SWIPE_U ? 1 : ev->type==UI_GESTURE_SWIPE_D ? -1 : 0;
        if (!direction && ui_rect_hit(previous,ev->x0,ev->y0)) {
            int row=((int)ev->y0-previous.y-8)/83;
            bool footer=pages>1 && ev->y0>=previous.y+previous.height-56;
            if (footer) direction=ev->x0<previous.x+previous.width/2 ? -1 : 1;
            else {
                int index=ev->y0>=previous.y+8 && row<SHELF_CATEGORY_ROWS
                    ? shelf_category_at(s_category_menu_page*SHELF_CATEGORY_ROWS+row) : -1;
                if (index>=0) {
                    shelf_category_remember(ctx); s_category_selected=index;
                    s_category_menu=false; clear_selection(); sort_shelf(ctx); shelf_category_restore(ctx);
                    return APP_REDRAW_PAGE;
                }
            }
        } else if (!direction) s_category_menu=false;
        if (direction) {
            int next=s_category_menu_page+direction;
            if (next<0 || next>=pages) return APP_REDRAW_NONE;
            s_category_menu_page=next;
        }
    }
    EpdRect next=shelf_category_box();
    int bottom=previous.y+previous.height>next.y+next.height ? previous.y+previous.height : next.y+next.height;
    s_area=(EpdRect){24,79,424,bottom-79+4}; s_mode=MODE_GL16;
    render(ctx,ctx->fb);
    return APP_REDRAW_AREA;
}
static bool shelf_reserve(void) {
    if ((size_t)s_count < s_shelf_capacity) return true;
    if (s_count == INT_MAX) return false;
    size_t cap = s_shelf_capacity ? s_shelf_capacity * 2 : 32;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(*s_shelf)) return false;
    shelf_entry_t* entries = heap_caps_realloc(s_shelf, cap * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    s_shelf = entries;
    s_shelf_capacity = cap;
    return true;
}

// 在删除文件前预留记录，避免删除成功后才遇到内存不足。
// Reserve before unlink so allocation failure never loses a completed deletion's retry.
static delete_retry_t* delete_retry_reserve(const shelf_entry_t* entry) {
    delete_retry_t* existing = delete_retry_find(entry->path);
    if (existing) return existing;
    delete_retry_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->entry = *entry;
    p->next = s_delete_retries;
    s_delete_retries = p;
    return p;
}
static void delete_retry_discard(const char* path) {
    delete_retry_t** p = &s_delete_retries;
    while (*p) {
        if (!strcmp((*p)->entry.path, path)) {
            delete_retry_t* old = *p;
            *p = old->next;
            free(old);
            return;
        }
        p = &(*p)->next;
    }
}
// 显式目录栈放在 PSRAM；主任务只有 8 KB 栈，递归进多级 books 会在封面出现前溢出。
// Keep directory frames in PSRAM: recursive books scanning can overflow the 8 KB main-task stack before covers appear.
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;
static void scan_shelf_dir(app_ctx_t *ctx, const char *root, bool is_flash, nvs_handle_t favorites,
                           nvs_handle_t hidden, bool *truncated, bool *unreadable,
                           bool *skipped) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    size_t root_len = strnlen(root, sizeof(frames[0].path));
    if (root_len >= sizeof(frames[0].path)) { *skipped = true; free(frames); return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    memcpy(frames[0].path, root, root_len + 1);
    int depth = 0;
    while (depth >= 0 && !*truncated) {
        if (s_view == SHELF && app_poll_main_navigation(ctx)) break;
        errno = 0;
        struct dirent *ent = readdir(frames[depth].dir);
        if (!ent) {
            if (errno) *unreadable = true;
            closedir(frames[depth].dir);
            frames[depth].dir = NULL;
            --depth;
            continue;
        }
        if (ent->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int len = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) { *skipped = true; continue; }
        struct stat st;
        if (stat(path, &st) != 0) { *unreadable = true; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (!is_flash && depth==0 && !strcmp(root,app_settings_books_dir())) shelf_category_add(path);
            if (depth == BOOK_SCAN_DEPTH_LIMIT) { *skipped = true; continue; }
            DIR *child = opendir(path);
            if (!child) { *unreadable = true; continue; }
            ++depth;
            frames[depth].dir = child;
            memcpy(frames[depth].path, path, (size_t)len + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!book_file_path_supported(path)) continue;
        // 显式移出书架的书不再列入；文件仍在存储卡上，随时可以重新读回书架。
        // Books the user removed stay off the shelf; the file stays on the card and reading it
        // again puts it back.
        if (hidden && shelf_hidden_read_handle(hidden, path)) continue;
        if (strlen(ent->d_name) >= sizeof(((shelf_entry_t *)0)->name) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { *skipped = true; continue; }
        if (!shelf_reserve()) { *truncated = true; break; }
        shelf_entry_t candidate = {0};
        shelf_entry_t *item = &candidate;
        copy_text(item->path, sizeof(item->path), path);
        if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
            clean_filename(item->name, sizeof(item->name), ent->d_name);
        if (!strcasecmp(ext, ".epub")) {
            char title[sizeof(item->name)] = {0};
            char author[sizeof(item->author)] = {0};
            // Listing must stay cheap and bounded: never parse every EPUB before
            // the shelf is first painted. Opening a book populates this cache.
            // 首屏列书只读已有元数据缓存，不在封面出现前逐本解析 EPUB。
            if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
                copy_text(item->author, sizeof(item->author), author);
        }
        item->search_match = read_pico_search_match(item->name, s_query);
        item->size = st.st_size;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = book_progress_load(item->path, item->size, &p);
        item->pct = item->has_progress ? p.pct : 0;
        item->chapter = item->has_progress ? p.chapter : 0;
        item->recent = item->has_progress ? p.last_open_s : 0;
        item->favorite = favorites && favorite_read_handle(favorites, item->path);
        s_shelf[s_count++] = candidate;
    }
    for (int i = 0; i <= depth; ++i)
        if (frames[i].dir) closedir(frames[i].dir);
    free(frames);
}

// 阅读过的书即使不在书根目录内也要上书架：文件管理可以直接打开存储卡任意位置的 TXT/EPUB，
// 这些书以前读完就消失了。文件已删除或内容已被替换的记录不上架，与进度不可恢复的口径一致。
// A book that was read belongs on the shelf even outside the book roots, because the file
// manager can open a TXT or EPUB anywhere on the card. Records whose file is gone, or whose
// contents were replaced, stay off the shelf, matching the rule that changed sizes never resume.
typedef struct {
    nvs_handle_t favorites;
    nvs_handle_t hidden;
    bool truncated;
} shelf_backfill_t;

static bool shelf_backfill_visit(const char* path, const book_progress_t* progress, void* ctx) {
    shelf_backfill_t* scan = ctx;
    if (scan->truncated) return false;
    // 历史可能来自其他固件；只补入当前阅读器支持的真实文件格式。
    // History may come from another firmware; admit only file formats this reader supports.
    if (!book_file_path_supported(path)) return true;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, path)) return true;   // 书根目录已经收录
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX || (uint32_t)st.st_size != progress->file_size)
        return true;
    if (scan->hidden && shelf_hidden_read_handle(scan->hidden, path)) return true;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strlen(base) >= sizeof(((shelf_entry_t*)0)->name)) return true;
    if (!shelf_reserve()) { scan->truncated = true; return false; }
    shelf_entry_t candidate = {0};
    shelf_entry_t* item = &candidate;
    copy_text(item->path, sizeof(item->path), path);
    if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
        clean_filename(item->name, sizeof(item->name), base);
    if (!item->name[0]) return true;
    const char* ext = strrchr(base, '.');
    if (ext && !strcasecmp(ext, ".epub") && pico_boot_asset_allowed(item->path)) {
        char title[sizeof(item->name)] = {0};
        char author[sizeof(item->author)] = {0};
        if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
            copy_text(item->author, sizeof(item->author), author);
    }
    item->size = (uint32_t)st.st_size;
    // 内置书根固定在 /flash 下，与 book_store.c 和 open_book 的前缀判断一致。
    // The internal root lives under /flash, matching the prefix test in book_store.c and open_book.
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = read_pico_search_match(item->name, s_query);
    item->favorite = scan->favorites && favorite_read_handle(scan->favorites, item->path);
    s_shelf[s_count++] = candidate;
    return true;
}

// 把书根目录之外读过的书补进书架，返回是否新增了条目。
// 阅读只写进度、不动存储版本，所以缓存复用的路径也必须调用它，否则刚读完的书要等手动重扫。
// Backfill books read outside the roots and report whether anything was added. Reading only
// writes progress and never bumps the store revision, so the cached path has to call this too
// or a freshly read book stays invisible until a manual rescan.
static bool shelf_backfill_read_books(bool* truncated) {
    nvs_handle_t favorites = 0;
    nvs_handle_t hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    const int before = s_count;
    shelf_backfill_t backfill = {.favorites = favorites, .hidden = hidden};
    (void)book_progress_list(shelf_backfill_visit, &backfill);
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    return s_count != before;
}

static void scan_shelf(app_ctx_t* ctx) {
    const uint32_t progress_revision=book_progress_revision();
    shelf_category_init();
    for (int i=2;i<s_category_count;++i) s_categories[i].available=false;
    s_count = 0;
    s_visible_count = 0;
    s_message[0] = 0;
    s_shelf_warning[0] = 0;
    // 内置分区每次启动都必须重新挂载；已有文件不会因此格式化。
    // Flash mounts on each boot; existing files are not formatted by that mount.
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int n = 0;
    esp_err_t root_err = book_store_roots(roots, &n);
    bool truncated = false, unreadable = root_err != ESP_OK || book_store_roots_degraded(), skipped = false;
    nvs_handle_t favorites = 0;
    nvs_handle_t hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    s_storage[0] = 0;
    for (int i = 0; i < n; ++i) {
        if (i == 0 || roots[i].is_flash || roots[i - 1].is_flash) {
            char capacity[64];
            snprintf(capacity, sizeof(capacity), "%s%s %.1f MB", s_storage[0] ? " · " : "",
                     roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
            strncat(s_storage, capacity, sizeof(s_storage) - strlen(s_storage) - 1);
        }
        if (s_view == SHELF && app_poll_main_navigation(ctx)) break;
        scan_shelf_dir(ctx, roots[i].path, roots[i].is_flash, favorites, hidden,
                       &truncated, &unreadable, &skipped);
    }
    if (ctx->request_app) {
        if (favorites) nvs_close(favorites);
        if (hidden) nvs_close(hidden);
        s_shelf_cache_valid = false; s_scan_pending = true;
        return;
    }
    // 同路径重新出现也先完成旧清理，避免新阅读进度被后续重试擦掉。
    // Finish old cleanup even if the path reappears, before new reading can create progress.
    for (delete_retry_t* p = s_delete_retries; p; p = p->next) {
        int i = 0;
        while (i < s_count && strcmp(s_shelf[i].path, p->entry.path)) ++i;
        if (i == s_count) {
            if (!shelf_reserve()) { truncated = true; continue; }
            ++s_count;
        }
        s_shelf[i] = p->entry;
        s_shelf[i].selected = false;
        s_shelf[i].search_match = read_pico_search_match(p->entry.name, s_query);
    }
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    // 补齐书根目录之外读过的书；显式移出书架的除外。
    // Backfill books read outside the roots, minus the ones removed from the shelf.
    (void)shelf_backfill_read_books(&truncated);
    shelf_categories_recount();
    sort_shelf(ctx);
    if (s_view==SHELF) shelf_category_restore(ctx);
    s_latest_path[0] = 0;
    char last_path[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(last_path, sizeof(last_path))) {
        for (int i = 0; i < s_count; ++i) {
            if (!strcmp(s_shelf[i].path, last_path) && !s_shelf[i].removed) {
                copy_text(s_latest_path, sizeof(s_latest_path), last_path);
                break;
            }
        }
    }
    if (!n) copy_text(s_storage, sizeof(s_storage), "存储不可用，请检查 TF 卡");
    if (truncated) snprintf(s_shelf_warning, sizeof(s_shelf_warning), "内存不足，仅列 %d 本；释放后重扫", s_count);
    else if (unreadable) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分目录或文件不可读，请检查后重扫");
    else if (skipped) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分文件过大或名称过长，未列入");
    else if (s_pending_invalidated) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "图书已更新，旧待保存进度已作废");
    if (!s_count && !unreadable && !truncated) copy_text(s_message, sizeof(s_message), "暂无图书，请传入 TXT 或 EPUB");
    read_pico_sd_info_t cache_sd = {0};
    read_pico_sd_get_info(&cache_sd);
    s_cache_sd_present = cache_sd.present;
    s_cache_sd_mounted = cache_sd.mounted;
    s_cache_shelf_style = app_settings_shelf_style();
    s_store_revision = book_store_revision();
    s_progress_revision = progress_revision;
    s_shelf_cache_valid = !unreadable && !truncated;
    ESP_LOGI(TAG, "shelf books=%d roots=%d", s_count, n);
}

static void refresh_cached_progress(app_ctx_t* ctx) {
    const uint32_t revision=book_progress_revision();
    if(s_progress_revision==revision){if(s_view==SHELF)shelf_category_restore(ctx);return;}
    s_progress_revision=revision;
    // 目录没变也要补一次：阅读只写进度，不改存储版本，否则刚读完的书不会出现。
    // Backfill even when the listing is unchanged: reading writes progress and never bumps the
    // store revision, so a freshly read book would otherwise stay invisible until a manual rescan.
    const bool added = shelf_backfill_read_books(NULL);
    bool recent_changed = false;
    char latest[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(latest, sizeof(latest))) {
        for (int i = 0; i < s_count; ++i) {
            shelf_entry_t *item = &s_shelf[i];
            if (strcmp(item->path, latest) || item->removed) continue;
            book_progress_t progress;
            item->has_progress = book_progress_load(item->path, item->size, &progress);
            uint32_t next_recent=item->has_progress?progress.last_open_s:0;
            recent_changed=item->recent!=next_recent;
            item->pct = item->has_progress ? progress.pct : 0;
            item->chapter = item->has_progress ? progress.chapter : 0;
            item->recent = next_recent;
            copy_text(s_latest_path, sizeof(s_latest_path), latest);
            break;
        }
    }
    // 新增条目必须重排才能重算可见列表；只刷新进度时沿用原有顺序。
    // Added entries need a re-sort to recompute the visible list; a progress refresh alone
    // keeps the current order.
    if (added || (s_recent_sort && recent_changed)) { shelf_categories_recount(); sort_shelf(ctx); }
    if (s_view==SHELF) shelf_category_restore(ctx);
}
static void return_to_cached_shelf(app_ctx_t* ctx) {
    if (s_shelf_cache_valid && s_store_revision == book_store_revision()) refresh_cached_progress(ctx);
    else scan_shelf(ctx);
}

static void refresh_capacity(void) {
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int count = 0;
    s_storage[0] = 0;
    book_store_roots(roots, &count);
    for (int i = 0; i < count; ++i) {
        if (i && !roots[i].is_flash && !roots[i - 1].is_flash) continue;
        char line[64];
        snprintf(line, sizeof(line), "%s%s %.1f MB", s_storage[0] ? " · " : "", roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
        strncat(s_storage, line, sizeof(s_storage) - strlen(s_storage) - 1);
    }
}
static void manage_apply(app_ctx_t* ctx) {
    if (!strcmp(s_path, s_managed.path)) free_book();
    esp_err_t err;
    if (s_delete_confirm && !s_file_removed) {
        delete_retry_t* retry = delete_retry_reserve(&s_managed);
        if (!retry) {
            copy_text(s_manage_message, sizeof(s_manage_message), "内存不足，无法保留删除重试记录");
            s_clear_confirm = false;
            return;
        }
        bool removed = false;
        err = book_store_delete(s_managed.path, &removed);
        if (removed) {
            retry->entry.removed = true;
            s_file_removed = true;
            pending_discard(s_managed.path);
            book_store_notify_changed();
            s_store_revision = book_store_revision();
            for (int i = 0; i < s_count; ++i) if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].removed = true;
            refresh_capacity();
        } else delete_retry_discard(s_managed.path);
    } else {
        pending_discard(s_managed.path);
        err = book_progress_forget(s_managed.path);
    }
    s_clear_confirm = false;
    if (err != ESP_OK) {
        copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ?
                  "文件已删除，进度清理失败，请重试" : s_delete_confirm ?
                  "文件删除失败，请检查存储后重试" : "进度清理失败，请重新尝试");
        return;
    }
    delete_retry_discard(s_managed.path);
    s_view = SHELF;
    int write = 0, leaf = ctx->leaf;
    for (int i = 0; i < s_count; ++i) {
        if (!strcmp(s_shelf[i].path, s_managed.path)) {
            if (s_file_removed) continue;
            s_shelf[i].has_progress = false; s_shelf[i].pct = 0; s_shelf[i].chapter = 0; s_shelf[i].recent = 0;
        }
        s_shelf[write++] = s_shelf[i];
    }
    s_count = write;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
}

/* ---- 输入与生命周期 / Input and lifecycle ---- */

static bool reader_turn_chrome(app_ctx_t *ctx) {
    bool changed = !s_reader_fullscreen || s_toolbar;
    if (!s_reader_fullscreen) (void)toggle_reader_fullscreen(ctx);
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    if (changed) { invalidate_prep(); s_reader_text_frame = false; }
    return changed;
}
static app_redraw_t turn_page(app_ctx_t* ctx, int dir) {
    if (!s_text || s_clear_confirm) return APP_REDRAW_NONE;
    reader_zoom_reset();
    // 页面图片表可能随跨章加载替换，旧页资格以实际成功显示的画面为准。
    // Chapter loads may replace the image table; the old-page eligibility comes from the displayed frame.
    bool from_text_frame = s_reader_text_frame;
    bool changed = true;
    if (dir > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
    else if (dir < 0 && s_page) --s_page;
    else if (dir > 0 && s_chapter + 1 < book_chapter_count()) { save_progress(); changed = load_chapter(ctx, s_chapter + 1, 0, false); }
    else if (dir < 0 && s_chapter) { save_progress(); changed = load_chapter(ctx, s_chapter - 1, 0, true); }
    else return reader_turn_chrome(ctx) ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    if (!changed) { set_reader_view(s_text ? TOC : SHELF); ctx->leaf = s_text ? s_chapter / BOOK_TOC_ROWS : 0; return APP_REDRAW_PAGE; }
    skip_hidden_image_pages(ctx, dir);
    if (!s_text) { s_view = SHELF; return APP_REDRAW_PAGE; }
    // 先选择旧布局的目标页，再切全屏，保留目标位置而不是跨过未读段落。
    // Choose the destination in the old layout first, then reflow to retain its reading position.
    bool chrome_changed = reader_turn_chrome(ctx);
    if (!s_text) return APP_REDRAW_PAGE;
    s_jump_offset = SIZE_MAX;
    s_last_turn_ms = s_stats_activity_ms = ctx->now_ms;
    ++s_stats_pending_turns;
    ++s_session_turns;
    uint8_t full_pages = app_settings_reader_full_pages();
    if (full_pages) ++s_turns;
    else s_turns = 0;
    s_reader_cleanup = full_pages > 0 && s_turns >= full_pages;
    if (s_reader_cleanup) s_turns = 0;
    if (s_unsaved < 8) ++s_unsaved;
    if (s_unsaved >= 8 && !s_save_failed) save_progress();
    ESP_LOGI(TAG, "turn chapter=%u page=%u/%u pct=%u", (unsigned)s_chapter, (unsigned)s_page + 1, (unsigned)book_layout_page_count(), percent(s_page));
    // 预渲染未命中时也使用 GL16；到达设定页数后在本次翻页整屏全刷。
    // A cache miss still uses GL16; the selected turn count triggers a full-screen refresh on this turn.
    app_redraw_t redraw = paint_reading(ctx, MODE_GL16);
    s_reader_turn_pending = !chrome_changed && redraw == APP_REDRAW_AREA && from_text_frame &&
        book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_water_turn_pending = !chrome_changed && redraw == APP_REDRAW_AREA && !s_reader_cleanup &&
                           app_settings_reader_turn_effect() == 1 && from_text_frame &&
                           book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_water_turn_dir = dir > 0 ? E0470_TURN_RTL : E0470_TURN_LTR;
    return chrome_changed ? APP_REDRAW_PAGE : redraw;
}
static app_redraw_t resize_text(app_ctx_t* ctx, int dir) {
    int next = s_px + dir * BOOK_PX_STEP;
    if (next < BOOK_PX_MIN || next > BOOK_PX_MAX) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    int old = s_px;
    if (!book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), next)) {
        bool restored = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), old);
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    s_px = next;
    s_page = book_layout_page_for_offset(off);
    unlock_draw();
    app_settings_set_book_px(s_px);
    save_progress();
    return paint_reading(ctx, MODE_DU);
}

static app_redraw_t apply_reader_layout(app_ctx_t* ctx, int next_px, int next_margin,
                                        uint8_t next_line, uint8_t next_para) {
    (void)ctx;
    if (next_px < BOOK_PX_MIN || next_px > BOOK_PX_MAX ||
        next_margin < BOOK_MARGIN_MIN || next_margin > BOOK_MARGIN_MAX ||
        next_line < 110 || next_line > 150 ||
        app_settings_book_paragraph_value(app_settings_book_paragraph_level(next_para)) != next_para) return APP_REDRAW_NONE;
    if (next_px == s_px && next_margin == s_margin && next_line == app_settings_book_line_spacing() &&
        next_para == app_settings_book_paragraph_spacing()) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    int old_px = s_px, old_margin = s_margin;
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_para = app_settings_book_paragraph_spacing();
    lock_draw();
    invalidate_prep();
    s_px = next_px;
    s_margin = next_margin;
    s_line_spacing = next_line;
    book_layout_set_spacing(next_line, next_para);
    // 正文带随字号与边距变了，题头的预留跟着重算。
    // The body band moved with the size and margin, so the heading's reservation follows.
    refresh_chapter_lead();
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        s_px = old_px;
        s_margin = old_margin;
        s_line_spacing = old_line;
        book_layout_set_spacing(old_line, old_para);
        refresh_chapter_lead();
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_px((uint8_t)s_px);
    app_settings_set_book_margin((uint8_t)s_margin);
    app_settings_set_book_line_spacing(next_line);
    app_settings_set_book_paragraph_spacing(next_para);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_typography(app_ctx_t* ctx, int tracking_index) {
    (void)ctx;
    if (!s_text || tracking_index < 0 || tracking_index >= APP_BOOK_TRACK_COUNT)
        return APP_REDRAW_NONE;
    int old_index = app_settings_book_tracking();
    if (tracking_index == old_index) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_typography((tracking_index + APP_BOOK_TRACK_MIN));
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count,
                                       body_rect_for_tracking(tracking_index), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_typography(old_index + APP_BOOK_TRACK_MIN);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_tracking((uint8_t)tracking_index);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_indent(app_ctx_t* ctx, int em, int adjustment) {
    (void)ctx;
    if (!s_text || em < 0 || em > 3 || adjustment < -100 || adjustment > 100 ||
        (em == app_settings_book_indent() && adjustment == app_settings_book_indent_adjust()))
        return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    uint8_t old_em = app_settings_book_indent();
    int old_adjustment = app_settings_book_indent_adjust();
    lock_draw();
    invalidate_prep();
    book_layout_set_first_line_indent((unsigned)em);
    book_layout_set_first_line_indent_adjust(adjustment);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_first_line_indent(old_em);
        book_layout_set_first_line_indent_adjust(old_adjustment);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_indent((uint8_t)em);
    app_settings_set_book_indent_adjust((int8_t)adjustment);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_indent_finish(app_ctx_t *ctx,bool commit) {
    int value=s_indent_hold.value;
    bool valid=commit&&!s_indent_hold.cancelled;
    reader_indent_hold_cancel();
    if(valid){
        app_redraw_t redraw=apply_reader_indent(ctx,app_settings_book_indent(),value);
        if(redraw!=APP_REDRAW_NONE)return redraw;
    }
    return reader_indent_feedback(ctx);
}
static app_redraw_t reader_indent_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,bool *handled) {
    *handled=false;
    if(s_view!=READING||s_reader_panel!=READER_PANEL_LAYOUT_SETTINGS||s_clear_confirm){reader_indent_hold_cancel();return APP_REDRAW_NONE;}
    if(ev->type==UI_GESTURE_PRESS){
        reader_indent_hold_cancel();int control=indent_adjust_hit(ev->x0,ev->y0);
        if(control<0||ctx->consumed||ctx->now_ms<0)return APP_REDRAW_NONE;
        s_indent_hold.active=true;s_indent_hold.control=control;s_indent_hold.value=app_settings_book_indent_adjust();
        s_indent_hold.last_ms=ctx->now_ms;s_indent_hold.next_ms=ctx->now_ms+UI_LONG_PRESS_MS;
        s_reader_tap.pending=false;s_pressed_control=-1;*handled=true;
        return reader_indent_feedback(ctx);
    }
    if(!s_indent_hold.active)return APP_REDRAW_NONE;
    *handled=true;
    bool inside=indent_adjust_hit(ev->x,ev->y)==s_indent_hold.control;
    if(ctx->consumed||!inside||ctx->now_ms<s_indent_hold.last_ms){
        s_indent_hold.cancelled=true;
        if(ev->type==UI_GESTURE_TAP||ev->type==UI_GESTURE_CANCEL||ctx->released)return reader_indent_finish(ctx,false);
        return reader_indent_feedback(ctx);
    }
    if(ev->type==UI_GESTURE_TAP){
        if(!s_indent_hold.repeated){
            int next=s_indent_hold.control==1?0:s_indent_hold.value+(s_indent_hold.control==0?-1:1);
            s_indent_hold.value=next<-100?-100:next>100?100:next;
        }
        return reader_indent_finish(ctx,true);
    }
    if(ev->type==UI_GESTURE_CANCEL){
        bool released=ctx->released&&ctx->touch&&!ctx->touch->touched;
        return reader_indent_finish(ctx,released);
    }
    if(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D)return reader_indent_finish(ctx,false);
    return APP_REDRAW_NONE;
}
static app_redraw_t reader_indent_hold_tick(app_ctx_t *ctx) {
    if(!s_indent_hold.active)return APP_REDRAW_NONE;
    if(s_view!=READING||s_reader_panel!=READER_PANEL_LAYOUT_SETTINGS||s_clear_confirm||ctx->request_app||ctx->request_menu||ctx->request_return){reader_indent_hold_cancel();return APP_REDRAW_NONE;}
    if(s_indent_hold.cancelled)return APP_REDRAW_NONE;
    bool held=!ctx->released&&ctx->touch&&ctx->touch->touched&&ctx->touch->count==1;
    if(!held||ctx->now_ms<s_indent_hold.last_ms||indent_adjust_hit(ctx->touch->x,ctx->touch->y)!=s_indent_hold.control){
        s_indent_hold.cancelled=true;return reader_indent_feedback(ctx);
    }
    if(ctx->consumed)return APP_REDRAW_NONE;
    s_indent_hold.last_ms=ctx->now_ms;
    if(s_indent_hold.control==1||ctx->now_ms<s_indent_hold.next_ms)return APP_REDRAW_NONE;
    // 推屏慢时不补发跳变，每次仍只改1px；重排和NVS写入留到松手。
    // Never catch up after slow output: retain 1px steps and defer reflow/NVS writes until release.
    s_indent_hold.next_ms=ctx->now_ms+120;s_indent_hold.repeated=true;
    int next=s_indent_hold.value+(s_indent_hold.control==0?-1:1);
    if(next<-100||next>100)return APP_REDRAW_NONE;
    s_indent_hold.value=next;ctx->user_activity=true;
    return reader_indent_feedback(ctx);
}

static app_redraw_t reader_manual_refresh(app_ctx_t* ctx) {
    (void)ctx;
    if (!s_text) return APP_REDRAW_NONE;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = s_reader_cleanup = false;
    s_du_count = s_turns = 0;
    invalidate_prep();
    return APP_REDRAW_FULL;
}

static app_redraw_t apply_reader_option(app_ctx_t* ctx, int kind) {
    (void)ctx;
    if (kind == 0) {
        app_settings_set_reader_power_turn(!app_settings_reader_power_turn());
        return APP_REDRAW_PAGE;
    }
    if (!s_text || kind < 1 || kind > 3) return APP_REDRAW_NONE;
    bool previous = kind == 1 ? app_settings_reader_immersive() :
                    kind == 2 ? app_settings_reader_hide_images() : app_settings_book_vertical();
    // 重新开启插图时，先补齐尺寸；文件读取必须在绘制锁外。
    // Probe missing dimensions before re-enabling images, outside the drawing lock.
    if (kind == 2 && previous) for (size_t i = 0; i < s_block_count; ++i) {
        blk_t *block = &s_blocks[i];
        if (block->image < 0 || (size_t)block->image >= s_image_count ||
            (block->image_width && block->image_height)) continue;
        unsigned width = 0, height = 0;
        if (book_chapter_image_dimensions(s_chapter, s_images[block->image], &width, &height) == ESP_OK) {
            block->image_width = width; block->image_height = height;
        }
        vTaskDelay(1);
    }
    lock_draw();
    size_t off = book_layout_page_start_offset(s_page);
    invalidate_prep();
    if (kind == 1) app_settings_set_reader_immersive(!previous);
    else if (kind == 2) {
        app_settings_set_reader_hide_images(!previous);
        book_layout_set_images_visible(previous);
    } else {
        // 竖排要重排整章：列序自右向左，页界和插图分页都跟着变。
        // Vertical setting re-typesets the chapter: column order runs right to left and both the
        // page boundaries and illustration pagination follow it.
        app_settings_set_book_vertical(!previous);
        book_layout_set_vertical(!previous);
    }
    // 模式变了，题头预留也跟着换轴：竖排留右侧列宽、横排留顶部行高。
    // The mode changed, so the heading's reservation changes axis too: columns to the right
    // vertically, rows at the top horizontally.
    apply_chapter_lead(s_chapter_heading_title, s_chapter_heading_label, s_chapter_lead_skip,
                       s_chapter_lead_row_height);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        if (kind == 1) app_settings_set_reader_immersive(previous);
        else if (kind == 2) {
            app_settings_set_reader_hide_images(previous);
            book_layout_set_images_visible(!previous);
        } else {
            app_settings_set_book_vertical(previous);
            book_layout_set_vertical(previous);
        }
        apply_chapter_lead(s_chapter_heading_title, s_chapter_heading_label, s_chapter_lead_skip,
                       s_chapter_lead_row_height);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        copy_text(s_reader_notice, sizeof(s_reader_notice), "排版失败，请重试");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
    }
    unlock_draw();
    if (ok) {
        release_page_images();
        prepare_inline_image();
        s_jump_offset = SIZE_MAX;
        save_progress();
    }
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = false;
    return APP_REDRAW_PAGE;
}

// 返回主页以本次开书来源为准；分类页码已在书架开书前记忆，不被目录/工具栏覆盖。
// Return to this opening's origin; shelf category/page were saved before opening, independently of TOC/tools.
static app_redraw_t reader_return(app_ctx_t *ctx) {
    save_progress();
    if (s_reader_return_home) {
        ui_nav_request(ctx, 0);
        return APP_REDRAW_NONE;
    }
    free_book();
    s_view = SHELF;
    s_reader_panel = READER_PANEL_NONE;
    return_to_cached_shelf(ctx);
    return APP_REDRAW_PAGE;
}

static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx) {
    (void)ctx;
    if (!s_text) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    s_reader_fullscreen = !s_reader_fullscreen;
    // 全屏切换会换掉整个正文带，题头的预留必须跟着换。
    // Toggling full screen swaps the whole body band, so the heading's reservation must follow.
    refresh_chapter_lead();
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    bool restored = true;
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        s_reader_fullscreen = !s_reader_fullscreen;
        refresh_chapter_lead();
        restored = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) {
        if (!restored) {
            free_book();
            s_view = SHELF;
            copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    release_page_images();
    prepare_inline_image();
    s_jump_offset = SIZE_MAX;
    s_reader_panel = READER_PANEL_NONE;
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t select_reading_font_item(app_ctx_t* ctx, const ttf_font_item_t* item) {
    if (item && ttf_font_path_is_builtin(item->path)) reader_font_choice_begin(item->path);
    if (!item || !strcmp(ttf_font_path(), item->path)) return APP_REDRAW_NONE;
    reader_font_choice_begin(item->path);
    char previous[TTF_FONT_PATH_MAX];
    copy_text(previous, sizeof(previous), ttf_font_path());
    if (ttf_font_open(item->path) != ESP_OK) {
        reader_font_choice_finish();
        return APP_REDRAW_PAGE;
    }
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else if (previous[0]) {
        (void)ttf_font_open(previous);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) { reader_font_choice_finish(); return APP_REDRAW_PAGE; }
    app_settings_set_font_path(item->path);
    copy_text(s_font_path, sizeof(s_font_path), item->path);
    save_progress();
    return APP_REDRAW_PAGE;
}

static int slider_index(EpdRect rect, int x, int count) {
    int span = rect.width - 128;
    int pos = x - rect.x - 64;
    int index = span > 0 && count > 1 ? (pos * (count - 1) + span / 2) / span : 0;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return index;
}
static int reader_margin_input(EpdRect rect, int x, int current, int px, int tracking_px) {
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    int level = reader_margin_level_for(current, px, tracking_px);
    if (x < rect.x + 64) --level;
    else if (x >= rect.x + rect.width - 64) ++level;
    else level = slider_index(rect, x, count);
    return reader_margin_for_level(level, current, px, tracking_px);
}

static EpdRect reader_slider_rect(int slider) {
    // 字号滑杆在双列字体卡片上方，绘制与拖动共用位置。/ The size rail sits above the paired cards; paint and drag share its position.
    if (slider == 0) return (EpdRect){36,700-reader_ui_extra(112),612,76};
    if (slider == 1) return (EpdRect){36, 620, 294, 66};
    if (slider == 2) return (EpdRect){354, 620, 294, 66};
    if (slider == 3) return (EpdRect){36, 740, 612, 66};
    return (EpdRect){36, 860, 612, 66};
}

static int reader_slider_at(uint16_t x, uint16_t y) {
    int first = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 0 : 1;
    int end = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 1 : 5;
    for (int i = first; i < end; ++i) if (ui_rect_hit(reader_slider_rect(i), x, y)) return i;
    return -1;
}

static void reader_slider_map(int slider, int x) {
    if (slider == 0) s_reader_preview_px = BOOK_PX_MIN + slider_index(reader_slider_rect(0), x, BOOK_PX_MAX - BOOK_PX_MIN + 1);
    else if (slider == 1) {
        int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
        int count = reader_margin_levels(s_px, (app_settings_book_tracking() + APP_BOOK_TRACK_MIN), widths, values);
        s_reader_preview_margin = reader_margin_for_level(
            slider_index(reader_slider_rect(1), x, count), s_reader_preview_margin,
            s_px, (app_settings_book_tracking() + APP_BOOK_TRACK_MIN));
    }
    else if (slider == 2) s_reader_preview_line = 110 + slider_index(reader_slider_rect(2), x, 41);
    else if (slider == 3) s_reader_preview_para = app_settings_book_paragraph_value((uint8_t)slider_index(reader_slider_rect(3), x, APP_BOOK_PARAGRAPH_COUNT));
    else if (slider == 4) s_reader_preview_tracking = slider_index(reader_slider_rect(4), x, APP_BOOK_TRACK_COUNT);
}

static void reader_slider_begin(int slider, int x) {
    s_reader_slider = slider;
    s_reader_preview_px = s_px;
    s_reader_preview_margin = s_margin;
    s_reader_preview_line = app_settings_book_line_spacing();
    s_reader_preview_para = app_settings_book_paragraph_spacing();
    s_reader_preview_tracking = app_settings_book_tracking();
    EpdRect rect = reader_slider_rect(slider);
    int *value = slider == 0 ? &s_reader_preview_px : slider == 1 ? &s_reader_preview_margin :
                 slider == 2 ? &s_reader_preview_line : slider == 3 ? &s_reader_preview_para :
                 &s_reader_preview_tracking;
    s_reader_slider_endpoint = x < rect.x + 64 || x >= rect.x + rect.width - 64;
    if (slider == 1) {
        *value = reader_margin_input(rect, x, *value, s_px, (app_settings_book_tracking() + APP_BOOK_TRACK_MIN));
        return;
    }
    if (slider == 3) {
        int level = app_settings_book_paragraph_level((uint8_t)*value);
        if (x < rect.x + 64) --level;
        else if (x >= rect.x + rect.width - 64) ++level;
        else level = slider_index(rect, x, APP_BOOK_PARAGRAPH_COUNT);
        if (level < 0) level = 0;
        if (level >= APP_BOOK_PARAGRAPH_COUNT) level = APP_BOOK_PARAGRAPH_COUNT - 1;
        *value = app_settings_book_paragraph_value((uint8_t)level);
        return;
    }
    int min = slider == 0 ? BOOK_PX_MIN : slider == 2 ? 110 : 0;
    int max = slider == 0 ? BOOK_PX_MAX : slider == 2 ? 150 : APP_BOOK_TRACK_COUNT - 1;
    if (x < rect.x + 64) --*value;
    else if (x >= rect.x + rect.width - 64) ++*value;
    else reader_slider_map(slider, x);
    if (*value < min) *value = min;
    if (*value > max) *value = max;
}

static void reader_slider_move(int x) {
    int slider = s_reader_slider;
    if (slider < 0) return;
    s_reader_slider_endpoint = false;
    reader_slider_map(slider, x);
}

static app_redraw_t reader_slider_commit(app_ctx_t* ctx) {
    if (s_reader_slider < 0) return APP_REDRAW_NONE;
    int slider = s_reader_slider;
    int px = s_reader_preview_px, margin = s_reader_preview_margin, line = s_reader_preview_line;
    int para = s_reader_preview_para;
    int tracking = s_reader_preview_tracking;
    s_reader_slider = -1;
    if (slider == 4) return apply_reader_typography(ctx, tracking);
    return apply_reader_layout(ctx, px, margin, line, (uint8_t)para);
}

static app_redraw_t reading_settings_action(app_ctx_t *ctx, uint16_t x, uint16_t y) {
    if (y < reading_settings_top() || ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y)) {
        s_reader_panel = READER_PANEL_NONE;
    } else if (ui_rect_hit(reading_setting_rect(0), x, y)) {
        static const uint8_t options[] = {5, 10, 15, 30, 0};
        unsigned next = 0;
        for (unsigned i = 0; i < 5; ++i)
            if (app_settings_reader_full_pages() == options[i]) { next = (i + 1) % 5; break; }
        app_settings_set_reader_full_pages(options[next]);
        s_turns = 0;
    } else if (ui_rect_hit(reading_setting_rect(1), x, y)) {
        app_settings_set_reader_turn_effect((app_settings_reader_turn_effect()+1)%3);
        // 新模式从新一轮计数开始，首次翻页不继承旧模式的自动全刷欠账。
        // Start a fresh cleanup cycle so the first turn does not inherit the previous effect's cleanup debt.
        s_turns=0;
    } else if (ui_rect_hit(reading_setting_rect(2), x, y)) {
        s_reader_panel = READER_PANEL_TURN_SETTINGS;
    } else if (ui_rect_hit(reading_setting_rect(4), x, y)) {
        s_reader_panel = READER_PANEL_KEY_SETTINGS;
    } else {
        for (int i = 0; i < 4; ++i)
            if (ui_rect_hit(reading_toggle_rect(i), x, y)) return apply_reader_option(ctx, i);
        return APP_REDRAW_NONE;
    }
    invalidate_prep();
    return APP_REDRAW_PAGE;
}

static void reader_chapter_jump_begin(void) {
    size_t count=book_layout_page_count();
    s_chapter_jump_page=count ? (s_page<count?s_page:count-1) : 0;
    s_chapter_jump_error=false;
    s_reader_panel=READER_PANEL_STATS;
}
static bool reader_chapter_jump_parse(size_t *page) {
    if(!s_chapter_jump_digits[0])return false;
    size_t value=0;
    for(const char *p=s_chapter_jump_digits;*p;++p){
        if(*p<'0'||*p>'9')return false;
        unsigned digit=(unsigned)(*p-'0');
        if(value>(SIZE_MAX-digit)/10)return false;
        value=value*10+digit;
    }
    if(!value||value>book_layout_page_count())return false;
    *page=value-1;
    return true;
}
static app_redraw_t reader_chapter_jump_commit(app_ctx_t *ctx,size_t page,bool restore) {
    if(!s_text||s_view!=READING||!book_layout_page_count())return APP_REDRAW_NONE;
    size_t offset=SIZE_MAX;
    if(restore){
        if(!s_chapter_jump_undo.valid||s_chapter_jump_undo.chapter!=s_chapter||ctx->now_ms>=s_chapter_jump_undo.until_ms)
            return APP_REDRAW_NONE;
        offset=s_chapter_jump_undo.offset;
        page=book_layout_page_for_offset(offset);
    }
    if(page>=book_layout_page_count())return APP_REDRAW_NONE;
    lock_draw();
    invalidate_prep();
    size_t before=reader_page_offset(s_page);
    size_t destination=offset==SIZE_MAX?book_layout_page_start_offset(page):offset;
    bool changed=page!=s_page||destination!=before;
    s_chapter_jump_undo.valid=!restore&&changed;
    s_chapter_jump_undo.chapter=s_chapter;s_chapter_jump_undo.offset=before;
    s_chapter_jump_undo.until_ms=ctx->now_ms+7000;
    s_page=page;s_jump_page=page;s_jump_offset=offset;
    s_reader_panel=READER_PANEL_NONE;s_reader_slider=-1;
    s_reader_tap.pending=false;
    s_water_turn_pending=s_reader_turn_pending=s_reader_text_frame=false;
    s_reader_footer_pending=s_reader_image_refresh_pending=s_reader_cleanup=false;
    unlock_draw();
    release_page_images();prepare_inline_image();
    if(changed)save_progress();
    s_stats_activity_ms=ctx->now_ms;ctx->user_activity=true;
    return APP_REDRAW_PAGE;
}
static app_redraw_t reader_chapter_jump_action(app_ctx_t *ctx,uint16_t x,uint16_t y) {
    int control=reader_chapter_jump_hit(x,y);
    size_t count=book_layout_page_count();
    if(s_reader_panel==READER_PANEL_STATS){
        if(control==0||control==1){s_reader_panel=READER_PANEL_NONE;invalidate_prep();return APP_REDRAW_PAGE;}
        if(control==6){
            s_bookmark_page=0;s_bookmark_edit=s_bookmark_delete_confirm=s_bookmark_delete_error=false;
            s_bookmark_selected=0;s_reader_panel=READER_PANEL_BOOKMARKS;return APP_REDRAW_PAGE;
        }
        if(control==7){
            flush_ticket_stats();s_recent_days_valid=book_ticket_recent_days(s_recent_days);
            s_reader_panel=READER_PANEL_STATS_RECENT;return APP_REDRAW_PAGE;
        }
        if(!count)return APP_REDRAW_NONE;
        if(s_chapter_jump_page>=count)s_chapter_jump_page=count-1;
        if(control==2||control==4){
            if((control==2&&!s_chapter_jump_page)||(control==4&&s_chapter_jump_page+1>=count))return APP_REDRAW_NONE;
            if(control==2)--s_chapter_jump_page;else ++s_chapter_jump_page;
            return APP_REDRAW_PAGE;
        }
        if(control==3){
            snprintf(s_chapter_jump_digits,sizeof(s_chapter_jump_digits),"%u",(unsigned)s_chapter_jump_page+1);
            s_chapter_jump_replace=true;s_chapter_jump_error=false;s_reader_panel=READER_PANEL_CHAPTER_JUMP;
            return APP_REDRAW_PAGE;
        }
        if(control==5)return reader_chapter_jump_commit(ctx,s_chapter_jump_page,false);
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel!=READER_PANEL_CHAPTER_JUMP)return APP_REDRAW_NONE;
    if(control==32){s_reader_panel=READER_PANEL_STATS;return APP_REDRAW_PAGE;}
    if(control==33){
        size_t page=0;
        if(reader_chapter_jump_parse(&page))return reader_chapter_jump_commit(ctx,page,false);
        s_chapter_jump_error=true;return APP_REDRAW_PAGE;
    }
    if(control<20||control>31)return APP_REDRAW_NONE;
    size_t len=strlen(s_chapter_jump_digits);
    int key=control-20;
    if(key==9)s_chapter_jump_digits[0]=0;
    else if(key==11){if(len)s_chapter_jump_digits[len-1]=0;}
    else{
        if(s_chapter_jump_replace)len=0;
        if(len+1>=sizeof(s_chapter_jump_digits))return APP_REDRAW_NONE;
        s_chapter_jump_digits[len]=(char)('0'+(key==10?0:key+1));s_chapter_jump_digits[len+1]=0;
    }
    s_chapter_jump_replace=false;s_chapter_jump_error=false;
    return APP_REDRAW_PAGE;
}
static app_redraw_t reader_panel_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_reader_panel == READER_PANEL_BACKGROUND || s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW)
        return reader_background_action(x, y);
    if (s_reader_panel == READER_PANEL_STATS || s_reader_panel == READER_PANEL_CHAPTER_JUMP)
        return reader_chapter_jump_action(ctx, x, y);
    if (s_reader_panel == READER_PANEL_TOOLS) {
        for (int i = 0; i < BOOK_TOOL_COUNT; ++i) if (ui_rect_hit(tool_rect(i), x, y)) {
            invalidate_prep();
            if (i == 0) {
                save_progress();
                set_reader_view(TOC);
                s_message[0] = 0;
                s_toc_jump_open = s_toc_jump_drag = false;
                s_reader_panel = READER_PANEL_NONE;
                size_t toc = current_toc_position();
                ctx->leaf = toc == SIZE_MAX ? 0 : (int)(toc / BOOK_TOC_ROWS);
            } else if (i == 1) {
                (void)bookmark_toggle();
            } else if (i == 2) {
                reader_chapter_jump_begin();
            } else if (i == 3) {
                s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
            } else {
                s_line_spacing = app_settings_book_line_spacing();
                s_reader_slider = -1;
                s_reader_panel = READER_PANEL_FONT_SETTINGS;
            }
            return APP_REDRAW_PAGE;
        }
        s_reader_panel = READER_PANEL_NONE;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_BOOKMARKS) {
        const int top = 560;
        if (s_bookmark_delete_confirm) {
            if (y >= top + 348 && y < top + 412) {
                if (x < 342) s_bookmark_delete_confirm = s_bookmark_delete_error = false;
                else if (bookmark_delete_selected()) s_bookmark_delete_confirm = s_bookmark_delete_error = false;
                else s_bookmark_delete_error = true;
            }
        } else if (y < top) s_reader_panel = READER_PANEL_NONE;
        else if (y < top + 86) {
            if (x >= 544 && bookmark_count()) {
                s_bookmark_edit = !s_bookmark_edit;
                s_bookmark_selected = 0;
                s_bookmark_page = 0;
            } else reader_chapter_jump_begin();
        } else {
            reader_bookmarks_t marks = {0};
            bool valid = bookmark_load(&marks);
            int pages = bookmark_pages(valid ? marks.count : 0);
            if (s_bookmark_edit && y >= top + 520 && y < top + 584) {
                if (x < 303) {
                    int first = s_bookmark_page * bookmark_rows();
                    for (int row = 0; row < bookmark_rows() && first + row < marks.count; ++row)
                        s_bookmark_selected |= UINT32_C(1) << (marks.count - 1 - (first + row));
                } else if (s_bookmark_selected) s_bookmark_delete_confirm = true;
            } else if (y >= top + (s_bookmark_edit ? 590 : 570)) {
                if (x < UI_LOCK_WIDTH / 2 && s_bookmark_page > 0) --s_bookmark_page;
                else if (x >= UI_LOCK_WIDTH / 2 && s_bookmark_page + 1 < pages) ++s_bookmark_page;
            } else if (valid && y >= top + 96) {
                int row = ((int)y - (top + 96)) / bookmark_row_step();
                int position = s_bookmark_page * bookmark_rows() + row;
                if (row >= 0 && row < bookmark_rows() && position < marks.count &&
                    ui_rect_hit(bookmark_row_rect(row),x,y)) {
                    int newest = (int)marks.count - 1 - position;
                    reader_bookmark_entry_t mark = marks.entries[newest];
                    if (s_bookmark_edit) s_bookmark_selected ^= UINT32_C(1) << newest;
                    else if (mark.chapter < book_chapter_count() && load_chapter(ctx, mark.chapter, mark.byte_off, false)) {
                        s_reader_panel = READER_PANEL_NONE;
                        save_progress();
                    }
                }
            }
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_STATS_RECENT) {
        int top = 584;
        if (y < top) s_reader_panel = READER_PANEL_NONE;
        else if (y < top + 112) reader_chapter_jump_begin();
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_REFRESH_SETTINGS)
        return reading_settings_action(ctx, x, y);
    if (s_reader_panel == READER_PANEL_KEY_SETTINGS || s_reader_panel == READER_PANEL_KEY_PICKER) {
        bool picker = s_reader_panel == READER_PANEL_KEY_PICKER;
        if (y < 270) s_reader_panel = READER_PANEL_NONE;
        else if (ui_rect_hit((EpdRect){36, 299, 56, 56}, x, y) || ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y))
            s_reader_panel = picker ? READER_PANEL_KEY_SETTINGS : READER_PANEL_REFRESH_SETTINGS;
        else if (picker) {
            for (int i = 0; i < APP_READER_KEY_COUNT; ++i) if (ui_rect_hit((EpdRect){36, 390 + i * 94, 612, 78}, x, y)) {
                bool saved = s_reader_key_pick == 3 ? app_settings_set_reader_hold_action(i)
                    : app_settings_set_reader_key_action(s_reader_key_pick, i);
                if (!saved) {
                    copy_text(s_reader_key_error, sizeof(s_reader_key_error), "保存失败，请重试");
                    return APP_REDRAW_PAGE;
                }
                s_reader_panel = READER_PANEL_KEY_SETTINGS;
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        } else {
            for (unsigned i = 0; i < 3; ++i) if (ui_rect_hit(reader_key_card(i), x, y)) {
                s_reader_key_pick = i;
                s_reader_key_error[0] = 0;
                s_reader_panel = READER_PANEL_KEY_PICKER;
                return APP_REDRAW_PAGE;
            }
            if (ui_rect_hit((EpdRect){36, 646, 612, 96}, x, y)) {
                s_reader_key_pick = 3;
                s_reader_key_error[0] = 0;
                s_reader_panel = READER_PANEL_KEY_PICKER;
                return APP_REDRAW_PAGE;
            }
            for (unsigned i = 1; i <= 2; ++i) if (ui_rect_hit(reader_key_preset_card(i), x, y)) {
                app_settings_set_reader_key_preset(i);
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_TURN_SETTINGS) {
        if (y < 270) s_reader_panel = READER_PANEL_NONE;
        else if (ui_rect_hit((EpdRect){36, 299, 56, 56}, x, y)) s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
        else if (ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y)) s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
        else {
            for (int i = 0; i < 2; ++i) {
                if (!ui_rect_hit((EpdRect){36, 370 + i * 322, 612, 292}, x, y)) continue;
                app_settings_set_reader_vertical_turn(i != 0);
                invalidate_prep();
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_WEIGHT_SETTINGS) {
        if (y < 690 || y >= 1141) {
            s_reader_panel = y < 580 ? READER_PANEL_NONE : READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        int step=reader_weight_step(x,y),weight=app_settings_book_weight_fine();
        if(step) {
            int next=weight+step;if(next < -APP_BOOK_WEIGHT_FINE_MAX || next > APP_BOOK_WEIGHT_FINE_MAX)return APP_REDRAW_NONE;
            lock_draw();
            invalidate_prep();
            bool ok = app_settings_set_book_weight_fine(next);
            if (ok) ttf_set_stroke_adjust_fine(next);
            unlock_draw();
            if (!ok) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "保存失败，请重试");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
            }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_reader_panel == READER_PANEL_FONT_SETTINGS) {
        const int top = reader_font_top();
        EpdRect size = reader_slider_rect(0);
        EpdRect font = s_font_card;
        EpdRect layout = s_layout_card;
        EpdRect rule = s_rule_card;
        EpdRect bookfont = s_bookfont_card;
        if (ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y)) {
            s_reader_panel = READER_PANEL_NONE;
            invalidate_prep(); return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(size, x, y)) {
            int px = BOOK_PX_MIN + slider_index(size, x, BOOK_PX_MAX - BOOK_PX_MIN + 1);
            return apply_reader_layout(ctx, px, s_margin, app_settings_book_line_spacing(),
                                       app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(font, x, y)) {
            ttf_font_scan();
            s_font_page = 0;
            int count = ttf_font_count();
            const char* active = ttf_font_path();
            for (int i = 0; i < count; ++i) {
                const ttf_font_item_t* item = ttf_font_item(i);
                if (item && !strcmp(active, item->path)) { s_font_page = i / BOOK_FONT_PAGE; break; }
            }
            s_reader_panel = READER_PANEL_FONT_PICKER;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(s_weight_card, x, y)) {
            s_reader_panel = READER_PANEL_WEIGHT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(s_antialias_card, x, y)) {
            lock_draw();
            invalidate_prep();
            bool on = !app_settings_book_antialias();
            bool ok = app_settings_set_book_antialias(on);
            if (ok) ttf_set_antialias(on);
            unlock_draw();
            if (!ok) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "保存失败，请重试");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
            }
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(layout, x, y)) {
            s_reader_slider = -1;
            s_reader_panel = READER_PANEL_LAYOUT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(rule, x, y)) {
            s_reader_panel = READER_PANEL_RULE_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(bookfont, x, y)) {
            // 触摸回调的栈比 on_tick 深得多：在这里直接重排一章会把主任务栈顶穿（实测溢出过一次）。
            // 只记下意愿，交给下一轮 on_tick 在浅栈上做。
            // The touch callback runs far deeper than on_tick, and laying a chapter out from here
            // overran the main task stack (measured once). Record the intent and let the next
            // on_tick do it on a shallow stack.
            bool on = !app_settings_book_fonts();
            app_settings_set_book_fonts(on);
            s_book_fonts_applied = on;
            book_set_embedded_fonts(on);
            s_fonts_reload_pending = true;
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        s_reader_panel = y < top ? READER_PANEL_NONE : READER_PANEL_TOOLS;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) {
        const int top = 500;
        EpdRect back_icon = {36, top + 29, 56, 56};
        EpdRect back_button = reader_layout_back_rect();
        if (ui_rect_hit(back_icon, x, y) || ui_rect_hit(back_button, x, y)) {
            s_reader_slider = -1;
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        EpdRect margin = reader_slider_rect(1);
        EpdRect line = reader_slider_rect(2);
        EpdRect paragraph = reader_slider_rect(3);
        EpdRect tracking = reader_slider_rect(4);
        if (ui_rect_hit(margin, x, y)) {
            return apply_reader_layout(ctx, s_px,
                                       reader_margin_input(margin, x, s_margin, s_px,
                                                           (app_settings_book_tracking() + APP_BOOK_TRACK_MIN)),
                                       app_settings_book_line_spacing(), app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(line, x, y)) {
            uint8_t value = (uint8_t)(110 + slider_index(line, x, 41));
            return apply_reader_layout(ctx, s_px, s_margin, value,
                                       app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(paragraph, x, y))
            return apply_reader_layout(ctx, s_px, s_margin, app_settings_book_line_spacing(),
                                       app_settings_book_paragraph_value((uint8_t)slider_index(paragraph, x, APP_BOOK_PARAGRAPH_COUNT)));
        if (ui_rect_hit(tracking, x, y))
            return apply_reader_typography(ctx, slider_index(tracking, x, APP_BOOK_TRACK_COUNT));
        for (int i = 0; i < 4; ++i) {
            EpdRect choice = {214 + i * 108, 970, 100, 64};
            if (ui_rect_hit(choice, x, y)) return apply_reader_indent(ctx, i, app_settings_book_indent_adjust());
        }
        for (int i = 0; i < 3; ++i) {
            if (indent_adjust_hit(x,y)!=i) continue;
            int adjustment = i == 1 ? 0 : app_settings_book_indent_adjust() + (i == 0 ? -1 : 1);
            if (adjustment < -100) adjustment = -100;
            if (adjustment > 100) adjustment = 100;
            return apply_reader_indent(ctx, app_settings_book_indent(), adjustment);
        }
        if(y<top){s_reader_panel=READER_PANEL_NONE;invalidate_prep();return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if (s_reader_panel == READER_PANEL_RULE_SETTINGS) {
        if (y < 729 || ui_rect_hit((EpdRect){36, 1113, 612, 72}, x, y)) {
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        for (int i = 0; i < 3; ++i) {
            if (!ui_rect_hit(rule_style_rect(i), x, y)) continue;
            app_settings_set_book_reading_line((uint8_t)i);
            book_layout_set_reading_line((unsigned)i);
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        if (app_settings_book_reading_line()) {
            int offset = app_settings_book_reading_line_offset();
            if (ui_rect_hit(s_rule_offset_up, x, y)) offset -= 2;
            else if (ui_rect_hit(s_rule_offset_reset, x, y)) offset = 0;
            else if (ui_rect_hit(s_rule_offset_down, x, y)) offset += 2;
            else return APP_REDRAW_NONE;
            if (offset < -8) offset = -8;
            if (offset > 8) offset = 8;
            app_settings_set_book_reading_line_offset((int8_t)offset);
            book_layout_set_reading_line_offset(offset);
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_reader_panel == READER_PANEL_FONT_PICKER) {
        const int top = reader_font_picker_top();
        if (y < top + 112) {
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        int count = ttf_font_count(), start = s_font_page * BOOK_FONT_PAGE;
        for (int slot = 0; slot < BOOK_FONT_PAGE && start + slot < count; ++slot) {
            EpdRect card = reader_font_pick_rect(slot);
            if (ui_rect_hit(card, x, y)) return select_reading_font_item(ctx, ttf_font_item(start + slot));
        }
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t adjust_spacing(app_ctx_t* ctx, int control) {
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_para = app_settings_book_paragraph_spacing();
    uint8_t line = old_line, para = old_para;
    if (control == 3 && line > 120) line -= 30;
    if (control == 4 && line < 180) line += 30;
    uint8_t para_level=app_settings_book_paragraph_level(para);
    if (control == 5 && para_level > 0) para=app_settings_book_paragraph_value(para_level-1);
    if (control == 6 && para_level+1 < APP_BOOK_PARAGRAPH_COUNT) para=app_settings_book_paragraph_value(para_level+1);
    if (control == 17) { line = 150; para = 50; }
    if (line == old_line && para == old_para) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_spacing(line, para);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_spacing(old_line, old_para);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_line_spacing(line);
    app_settings_set_book_paragraph_spacing(para);
    save_progress();
    return paint_reading(ctx, MODE_DU);
}
static app_redraw_t select_reading_font(app_ctx_t* ctx, int style) {
    static const char* filenames[] = {
        "Song.ttf", "Hei.ttf", "Kai.ttf", "WenKai.ttf", "FangSong.ttf", "KingHwa.ttf",
        "CangErYunHei05.ttf", "ChillKai.ttf"
    };
    static const char* names[] = {"宋体", "黑体", "楷体", "文楷", "仿宋", "京华老宋", "仓耳云黑05", "寒蝉正楷"};
    char path[160];
    if (snprintf(path, sizeof(path), "%s/%s", app_settings_fonts_dir(), filenames[style]) >= sizeof(path)) return APP_REDRAW_NONE;
    if (!strcmp(ttf_font_path(), path)) return APP_REDRAW_NONE;
    struct stat font_stat;
    if (stat(path, &font_stat) != 0) {
        snprintf(s_message, sizeof(s_message), "请将 %s 字体放入所选字体目录", names[style]);
        return APP_REDRAW_PAGE;
    }
    if (ttf_font_open(path) != ESP_OK) {
        snprintf(s_message, sizeof(s_message), "请将 %s 字体放入所选字体目录", names[style]);
        return APP_REDRAW_PAGE;
    }
    app_settings_set_font_path(path);
    s_message[0] = 0;
    size_t off = book_layout_page_start_offset(s_page);
    save_progress();
    lock_draw();
    invalidate_prep();
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    unlock_draw();
    if (!ok) {
        free_book(); s_view = SHELF;
        copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书");
        return APP_REDRAW_PAGE;
    }
    save_progress();
    return paint_reading(ctx, MODE_GL16);
}
static app_redraw_t manage_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (ui_rect_hit(manage_back_rect(), x, y)) {
        if (s_clear_confirm) s_clear_confirm = false;
        else s_view = SHELF;
        return APP_REDRAW_PAGE;
    }
    if (!s_clear_confirm && !s_file_removed && ui_rect_hit((EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 190, ui_content_width(), 68}, x, y)) {
        extern void app_files_request_folder(int folder);
        app_files_request_folder(0);
        ui_nav_request(ctx, 2);
        return APP_REDRAW_NONE;
    }
    int count = s_clear_confirm || s_file_removed ? 2 : 3;
    for (int i = 0; i < count; ++i) if (ui_rect_hit(manage_rect(i, count), x, y)) {
        if (s_clear_confirm) { if (!i) s_clear_confirm = false; else manage_apply(ctx); }
        else if (!i) s_view = SHELF;
        else if (s_file_removed) manage_apply(ctx);
        else { s_clear_confirm = true; s_delete_confirm = i == 2; s_manage_message[0] = 0; }
        break;
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_save(app_ctx_t* ctx) {
    if (ui_keyboard_pending()) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor_title;
    while (*start == ' ') ++start;
    if (start != s_editor_title) memmove(s_editor_title, start, strlen(start) + 1);
    size_t len = strlen(s_editor_title);
    while (len && s_editor_title[len - 1] == ' ') s_editor_title[--len] = 0;
    if (!s_editor_title[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "书名不能为空");
        return APP_REDRAW_PAGE;
    }
    esp_err_t err = book_title_set(s_managed.path, s_editor_title);
    if (err != ESP_OK) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查书名或存储");
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, s_managed.path)) copy_text(s_shelf[i].name, sizeof(s_shelf[i].name), s_editor_title);
    free(s_editor_cover); s_editor_cover = NULL;
    ui_keyboard_end();
    s_view = SHELF;
    sort_shelf(ctx);
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_paint(app_ctx_t* ctx, bool field) {
    bool search = s_view == SEARCH;
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560,
        search ? (EpdRect){UI_MARGIN, 200, ui_content_width(), 88} : (EpdRect){36, 424, 612, 83},
        search ? UI_PX_BODY : 31, false, NULL, field);
    s_area = update.area; s_mode = update.layout ? MODE_GL16 : MODE_DU; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t editor_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (y < 134) {
        if (x < 160) { ui_keyboard_end(); free(s_editor_cover); s_editor_cover = NULL; s_view = SHELF; return APP_REDRAW_PAGE; }
        if (x > 510) return editor_save(ctx);
        return APP_REDRAW_NONE;
    }
    if (ui_text_input_tap(&s_editor_input, (EpdRect){36, 424, 612, 83}, 31,
                          false, NULL, x, y)) return editor_paint(ctx, true);
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
    if (result == UI_KEYBOARD_DONE) return editor_save(ctx);
    return result == UI_KEYBOARD_CHANGED ? editor_paint(ctx, false) : APP_REDRAW_NONE;
}
// 成功项退出选择，已删文件的失败项仅重试元数据。/ Deselect successes; removed-file failures retry metadata only.
static void batch_apply(app_ctx_t* ctx) {
    unsigned done = 0, failed = 0;
    int write = 0;
    const bool unshelf = s_batch_kind == BATCH_UNSHELF;
    for (int i = 0; i < s_count; ++i) {
        shelf_entry_t item = s_shelf[i];
        bool discard = false;
        if (item.selected) {
            esp_err_t err = ESP_OK;
            if (unshelf) {
                // 只隐藏条目：文件与阅读进度都留在原处，再次打开这本书会重新上书架。
                // Hide the entry only: file and progress stay, and reopening the book puts it back.
                if (shelf_hidden_save(item.path, true)) discard = true;
                else err = ESP_FAIL;
            } else {
                if (!strcmp(s_path, item.path)) free_book();
                if (s_batch_kind == BATCH_DELETE && !item.removed) {
                    delete_retry_t* retry = delete_retry_reserve(&item);
                    if (!retry) { ++failed; s_shelf[write++] = item; continue; }
                    bool removed = false;
                    err = book_store_delete(item.path, &removed);
                    if (removed) {
                        retry->entry.removed = true;
                        item.removed = true; pending_discard(item.path);
                        book_store_notify_changed(); s_store_revision = book_store_revision();
                    } else delete_retry_discard(item.path);
                } else { pending_discard(item.path); err = book_progress_forget(item.path); }
            }
            if (err == ESP_OK) {
                delete_retry_discard(item.path);
                ++done; item.selected = false;
                if (!unshelf) {
                    item.has_progress = false; item.pct = 0;
                    item.chapter = 0; item.recent = 0;
                }
                discard = discard || item.removed;
            } else ++failed;
        }
        if (!discard) s_shelf[write++] = item;
    }
    s_count = write;
    refresh_capacity();
    int leaf = ctx->leaf;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
    s_batch_confirm = false;
    if (unshelf)
        snprintf(s_batch_message, sizeof(s_batch_message), "已移出书架 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
    else
        snprintf(s_batch_message, sizeof(s_batch_message), "成功 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
}
static app_redraw_t batch_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_batch_confirm) {
        if (ui_rect_hit(ui_row_rect(0, 2, 620, UI_BTN_H), x, y)) {
            s_batch_confirm = false;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(ui_row_rect(1, 2, 620, UI_BTN_H), x, y)) {
            batch_apply(ctx);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    for (int i = 0; i < 6; ++i) if (ui_rect_hit(batch_rect(i), x, y)) {
        s_batch_message[0] = 0;
        if (!i) select_page(ctx->leaf);
        else if (i == 1) clear_selection();
        else if (i == 2) { clear_selection(); s_batch_message[0] = 0; scan_shelf(ctx); }
        else if (selected_count()) {
            s_batch_kind = i == 3 ? BATCH_DELETE : i == 4 ? BATCH_CLEAR : BATCH_UNSHELF;
            s_batch_confirm = true;
        }
        else copy_text(s_batch_message, sizeof(s_batch_message), "请先选择图书");
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t bulk_turn_page(app_ctx_t* ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_scan_pending || s_batch_confirm || next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t bulk_finish(app_ctx_t* ctx) {
    int first = ctx->leaf * BOOK_BULK_ROWS;
    clear_selection();
    s_batch_message[0] = 0;
    s_batch_confirm = false;
    s_view = SHELF;
    ctx->leaf = first / shelf_rows();
    if (ctx->leaf >= leaves()) ctx->leaf = leaves() - 1;
    return APP_REDRAW_PAGE;
}
// 批量管理单独路由，不复用带示例菜单的旧底栏。
// Route batch management independently of the legacy bar and demo menu.
static app_redraw_t bulk_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (ui_rect_hit(manage_back_rect(), x, y)) {
        if (s_batch_confirm) { s_batch_confirm = false; return APP_REDRAW_PAGE; }
        return bulk_finish(ctx);
    }
    app_redraw_t result = batch_action(ctx, x, y);
    if (result != APP_REDRAW_NONE || s_batch_confirm || s_scan_pending) return result;
    for (int i = 0; i < 3; ++i) if (ui_rect_hit(bulk_filter_rect(i), x, y)) {
        if (i == 2) { search_begin(); return APP_REDRAW_PAGE; }
        if (i == 0) s_filter = (s_filter + 1) % 3;
        else { s_recent_sort = !s_recent_sort; app_settings_set_shelf_recent_sort(s_recent_sort); }
        clear_selection(); s_batch_message[0] = 0;
        sort_shelf(ctx);
        return APP_REDRAW_PAGE;
    }
    int nav = bulk_nav_hit(x, y);
    if (nav == 1) return bulk_finish(ctx);
    if (nav == 0 || nav == 2) return bulk_turn_page(ctx, nav == 0 ? -1 : 1);
    for (int row = 0; row < BOOK_BULK_ROWS; ++row) {
        int index = ctx->leaf * BOOK_BULK_ROWS + row;
        if (index < s_visible_count && ui_rect_hit(row_rect(row), x, y)) {
            toggle_selection(index); s_batch_message[0] = 0;
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}
static int bulk_control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    *rect = manage_back_rect();
    if (ui_rect_hit(*rect, x, y)) return 711;
    if (s_batch_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 620, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 600 + i;
        }
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        *rect = bulk_filter_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 110 + i;
        *rect = bulk_nav_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    for (int i = 0; i < 6; ++i) {
        *rect = batch_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 610 + i;
        if (ctx->leaf * BOOK_BULK_ROWS + i < s_visible_count) {
            *rect = row_rect(i);
            if (ui_rect_hit(*rect, x, y)) return i;
        }
    }
    return -1;
}
// 整页插图的定点缩放：把点到的那一点当作锚，直接跳到目标倍率并整屏刷灰度。
// Point-anchored jump zoom for a whole-page illustration: the tapped point stays under the
// finger, the target scale is applied at once, and the panel is fully refreshed in grayscale.
static app_redraw_t zoom_jump(app_ctx_t* ctx, int scale, int fx, int fy) {
    EpdRect band = book_layout_image_rect();
    int base_w = 0, base_h = 0;
    reader_image_base_size(&base_w, &base_h);
    s_image_zoom_page = s_page;
    s_image_zoom_start_distance = 0;
    s_image_pan_active = false;
    s_image_zoom_bw = false;
    continuous_du_reset();
    if (scale > READER_IMAGE_ZOOM_MIN) {
        const int cx0 = band.x + band.width / 2, cy0 = band.y + band.height / 2;
        int cx = fx - (int)((int64_t)(fx - cx0) * scale / READER_IMAGE_ZOOM_MIN);
        int cy = fy - (int)((int64_t)(fy - cy0) * scale / READER_IMAGE_ZOOM_MIN);
        int w = (int)((int64_t)base_w * scale / READER_IMAGE_ZOOM_MIN);
        int h = (int)((int64_t)base_h * scale / READER_IMAGE_ZOOM_MIN);
        reader_image_clamp_center(&cx, &cy, w, h, band);
        s_image_zoom = scale;
        s_image_zoom_cx = cx;
        s_image_zoom_cy = cy;
    } else {
        s_image_zoom = READER_IMAGE_ZOOM_MIN;
        s_image_zoom_cx = band.x + band.width / 2;
        s_image_zoom_cy = band.y + band.height / 2;
    }
    render(ctx, ctx->fb);
    guard_draw_result(ctx->hl, update_display_area_full_with(
        ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16,
        (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}));
    return APP_REDRAW_DONE;
}

// 双击正文里的插图：进入缩放模式并以点到的位置为锚放大到 2 倍；再双击退出回到正文。
// Double tap on an inline illustration enters zoom mode at 2x anchored on the tap; another
// double tap leaves zoom mode and returns to the text.
static app_redraw_t inline_zoom_toggle(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_zoom_active) {
        s_zoom_active = false;
        s_image_zoom = READER_IMAGE_ZOOM_MIN;
        s_image_zoom_bw = false;
        s_image_zoom_start_distance = 0;
        s_image_zoom_page = SIZE_MAX;
        s_image_pan_active = false;
        continuous_du_reset();
        render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, update_display_area_full_with(
            ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16,
            (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}));
        return APP_REDRAW_DONE;
    }
    int slot = -1;
    if (!reader_inline_image_at(x, y, &slot)) return APP_REDRAW_NONE;
    s_zoom_active = true;
    s_zoom_slot = slot;
    return zoom_jump(ctx, 2000, x, y);
}

static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_view == BULK) return bulk_action(ctx, x, y);
    if (s_view == IMPORT) {
        if (y < 160 && x < 120) { s_view = SHELF; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(x, y);
        if (tab == 1) { s_view = SHELF; return APP_REDRAW_PAGE; }
        if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        for (int i = 0; i < 5; ++i) if (ui_rect_hit(import_rect(i), x, y)) {
            if (i == 0) {
                extern const app_desc_t app_files;
                ctx->request_app = &app_files;
            } else if (i == 4) {
                extern const app_desc_t app_cloud_sync;
                ctx->request_app = &app_cloud_sync;
            } else {
                extern const app_desc_t app_transfer;
                if (i == 1) app_transfer_request_wifi_upload();
                else if (i == 2) app_transfer_request_hotspot_start();
                else app_transfer_request_usb_start();
                ctx->request_app = &app_transfer;
            }
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == TOC) {
        if (s_toc_jump_open) {
            int choice = book_toc_jump_hit(x, y);
            if (choice == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_percent = book_toc_jump_percent(x);
                return APP_REDRAW_PAGE;
            }
            if (choice == BOOK_TOC_JUMP_CONFIRM)
                ctx->leaf = book_toc_page_from_percent(s_toc_jump_percent, leaves());
            s_toc_jump_open = s_toc_jump_drag = false;
            return APP_REDRAW_PAGE;
        }
        int target = book_toc_hit(x, y, ctx->leaf, book_navigation_count());
        if (target == BOOK_TOC_BACK) {
            s_message[0] = 0;
            set_reader_view(READING);
            s_reader_panel = READER_PANEL_NONE;
            return APP_REDRAW_PAGE;
        }
        if (target == BOOK_TOC_PREV || target == BOOK_TOC_NEXT ||
            target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT) {
            int step = target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT ? 10 : 1;
            int next = ctx->leaf + (target == BOOK_TOC_PREV || target == BOOK_TOC_SKIP_PREV ? -step : step);
            if (next < 0) next = 0;
            if (next >= leaves()) next = leaves() - 1;
            if (next != ctx->leaf) { ctx->leaf = next; return APP_REDRAW_PAGE; }
            return APP_REDRAW_NONE;
        }
        if (target == BOOK_TOC_JUMP && book_navigation_count()) {
            s_toc_jump_open = true;
            s_toc_jump_percent = (ctx->leaf + 1) * 100 / leaves();
            return APP_REDRAW_PAGE;
        }
        if (target >= 0 && !s_message[0]) {
            size_t chapter = book_navigation_chapter((size_t)target);
            if (chapter >= book_chapter_count()) return APP_REDRAW_NONE;
            save_progress();
            set_reader_view(READING);
            if (load_chapter_at(ctx, chapter, 0, false, book_navigation_anchor((size_t)target),
                                book_navigation_source_offset((size_t)target))) {
                s_selected_toc = (size_t)target;
                char entry_title[sizeof(s_title)];
                if (book_navigation_title((size_t)target, entry_title, sizeof(entry_title)) == ESP_OK)
                    copy_text(s_title, sizeof(s_title), entry_title);
                s_view = READING;
                s_reader_panel = READER_PANEL_NONE;
                save_progress();
            } else set_reader_view(s_text ? TOC : SHELF);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == MANAGE) return manage_action(ctx, x, y);
    if (s_view == SEARCH) {
        if (y < 160 && x < 160) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (ui_text_input_tap(&s_search_input, (EpdRect){UI_MARGIN, 200, ui_content_width(), 88},
                              UI_PX_BODY, false, NULL, x, y)) return editor_paint(ctx, true);
        if (ui_rect_hit((EpdRect){36, 392, 180, 66}, x, y)) { s_search_draft[0] = 0; ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft)); ui_keyboard_begin(&s_search_input, false); return APP_REDRAW_PAGE; }
        ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE || (y < 160 && x > 510 && !ui_keyboard_pending())) { search_finish(ctx, true); return APP_REDRAW_PAGE; }
        if (result == UI_KEYBOARD_CHANGED) return editor_paint(ctx, false);
        return APP_REDRAW_NONE;
    }
    if (s_view == READING) {
        if (s_toolbar && !(s_reader_panel == READER_PANEL_TOOLS && y < 1096 && ui_rect_hit(body_rect(), x, y)))
            return reader_panel_action(ctx, x, y);
        if (!s_reader_fullscreen && ui_rect_hit((EpdRect){32, 75, 64, 64}, x, y))
            return reader_return(ctx);
        if (!s_reader_fullscreen && x >= 584 && y >= 76 && y < 145) {
            bool next = !s_reader_favorite;
            if (!favorite_save(s_path, next)) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "收藏保存失败");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
                return APP_REDRAW_PAGE;
            }
            s_reader_favorite = next;
            for (int i = 0; i < s_count; ++i)
                if (!strcmp(s_shelf[i].path, s_path)) s_shelf[i].favorite = next;
            sort_shelf(ctx);
            render(ctx, ctx->fb);
            s_area = (EpdRect){584, 76, 64, 70};
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
        // 通栏插图铺满整屏，点在图两侧的边条上同样要翻页；文字页仍按正文栏判定。
        // A full-bleed illustration spans the panel, so taps on its side strips must turn pages
        // too; text pages keep the body column.
        EpdRect tap_area = body_rect();
        if (reader_image_slots(s_page)) tap_area = ui_rect_union(tap_area, book_layout_image_rect());
        if (!ui_rect_hit(tap_area, x, y)) return APP_REDRAW_NONE;
        // 整页插图任意位置双击都算双击（用于定点缩放），单击延后到双击窗口后再翻页；
        // 其它页面仍是原来的中间双击全屏、两侧单击翻页。
        // On a whole-page illustration a double tap counts anywhere (point-anchored zoom) and a
        // single tap is deferred; other pages keep centre double tap and side taps turning pages.
        // 点在插图上时同样延后单击：只有这样才能把双击插图和单击翻页分开。
        // A tap on an illustration defers the single tap too; that is the only way a double tap
        // on the image can be told apart from a page turn.
        int target = (reader_zoom_mode() || reader_page_image_hit(x, y))
            ? book_reader_tap_feed_any(&s_reader_tap, tap_area, x, y,
                                       app_settings_reader_vertical_turn(),
                                       app_settings_book_vertical(), ctx->now_ms)
            : book_reader_tap_feed(&s_reader_tap, tap_area, x, y,
                                   app_settings_reader_vertical_turn(),
                                   app_settings_book_vertical(), ctx->now_ms);
        if (target == BOOK_READER_TAP_FULLSCREEN) {
            // 整页插图：双击以点到的那一点为中心放大到 2 倍，再双击回到原始大小；
            // 正文插图：双击进入缩放模式（该图居中 2 倍），再双击退出回正文；
            // 其它位置保持原来的全屏切换。
            // A whole-page illustration zooms to 2x centred on the double-tapped point and a
            // second double tap returns to 1:1. An inline illustration opens zoom mode on that
            // image (2x, centred) and closes it again. Blank text keeps the full-screen toggle.
            if (s_zoom_active) return inline_zoom_toggle(ctx, x, y);
            if (reader_single_image_page()) {
                const bool zoomed = s_image_zoom_page == s_page && s_image_zoom > READER_IMAGE_ZOOM_MIN;
                return zoom_jump(ctx, zoomed ? READER_IMAGE_ZOOM_MIN : 2000, x, y);
            }
            if (s_view == READING) {
                app_redraw_t opened = inline_zoom_toggle(ctx, x, y);
                if (opened != APP_REDRAW_NONE) return opened;
            }
            return toggle_reader_fullscreen(ctx);
        }
        if (target) return turn_page(ctx, target);
        return APP_REDRAW_NONE;
    }
    if (s_view == SHELF) {
        if (s_view == SHELF) {
            int tab = ui_nav_hit(x, y);
            if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
            int pager=ui_pager_hit(x,y);
            if(pager>=0)return shelf_turn_page(ctx,ui_pager_target(pager,ctx->leaf,leaves())-ctx->leaf);
        }
        if (s_view == SHELF && ui_rect_hit(shelf_import_rect(), x, y)) {
            s_view = IMPORT;
            return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) {
            clear_selection();
            s_batch_confirm = false;
            s_batch_message[0] = 0;
            s_view = BULK;
            ctx->leaf = 0;
            return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF && s_save_failed && y >= UI_CONTENT_BOTTOM - UI_PX_CAPTION && y < UI_CONTENT_BOTTOM) {
            retry_progress();
            return APP_REDRAW_PAGE;
        }
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return APP_REDRAW_NONE;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    for (int row = 0; row < rows; ++row) if (ui_rect_hit(row_rect(row), x, y)) {
        int i = ctx->leaf * rows + row;
        if (s_view == SHELF && i < s_visible_count) {
            shelf_category_remember(ctx);
            if (s_shelf[i].removed) {
                s_managed = s_shelf[i]; s_view = MANAGE; s_file_removed = s_delete_confirm = true; s_clear_confirm = false;
                copy_text(s_manage_message, sizeof(s_manage_message), "文件已删除，进度清理失败，请重试");
            } else open_book(ctx, s_shelf[i].path);
        }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void on_enter(app_ctx_t* ctx) {
    reader_indent_hold_cancel();
    s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
    shelf_category_init();
    s_category_menu=false;
    s_reader_tap.pending = false;
    s_shelf_page_pending = false;
    // 设置页试按的旧事件不得在开书后翻页。/ Discard remote tests from earlier pages before opening a book.
    ble_pt_event_t old_key;
    ble_pt_raw_t old_raw;
    while (ble_pt_pop_key(&old_key)) {}
    while (ble_pt_pop_raw(&old_raw)) {}
    book_layout_set_spacing(app_settings_book_line_spacing(), app_settings_book_paragraph_spacing());
    book_layout_set_images_visible(!app_settings_reader_hide_images());
    // 插图通栏：整屏宽是面板常量，进入阅读页说一次，之后每次重排都沿用同一条带。
    // Full-bleed illustrations: the panel width is a constant, so publish it once per entry and
    // every later re-layout reuses the same band.
    book_layout_set_image_bleed_width(UI_LOCK_WIDTH);
    // 竖排是阅读设置里的手动开关，进页时同步给排版；关着就是原来的横排。
    // Vertical setting is a manual reader switch, published to the layout on entry; off is the
    // original horizontal setting.
    book_layout_set_vertical(app_settings_book_vertical());
    book_layout_set_typography(((int)app_settings_book_tracking() + APP_BOOK_TRACK_MIN));
    book_layout_set_first_line_indent(app_settings_book_indent());
    book_layout_set_first_line_indent_adjust(app_settings_book_indent_adjust());
    book_layout_set_reading_line(app_settings_book_reading_line());
    book_layout_set_reading_line_offset(app_settings_book_reading_line_offset());
    // 字体页可能刚改过书内字体开关；回正文时重排当前章，别让用户对着旧排版猜。
    // The font page may have just changed the embedded-face switch; re-typeset this chapter
    // so the reader is not left guessing at stale type.
    bool book_fonts = app_settings_book_fonts();
    if (book_fonts != s_book_fonts_applied) {
        s_book_fonts_applied = book_fonts;
        book_set_embedded_fonts(book_fonts);
        if (s_text && book_kind() == BOOK_KIND_EPUB)
            (void)load_chapter(ctx, s_chapter, book_layout_page_start_offset(s_page), false);
    }
    s_reader_fullscreen = false;
    s_bookmark_edit = s_bookmark_delete_confirm = s_bookmark_delete_error = false;
    s_bookmark_selected = 0;
    clear_selection();
    s_view = s_requested_manage ? BULK : SHELF;
    s_reader_return_home = false;
    s_requested_manage = false;
    s_presented_view = -1;
    ctx->leaf = s_view==SHELF && s_categories ? s_categories[s_category_selected].pages[app_settings_shelf_style()==5] : 0;
    s_px = app_settings_book_px();
    s_recent_sort = app_settings_shelf_recent_sort();
    s_margin = app_settings_book_margin();
    s_line_spacing = app_settings_book_line_spacing();
    // 传书页已停止并 join HTTP，安全注销且只丢弃对应路径的旧进度。
    // Transfer has stopped and joined HTTP; safely unregister only invalidated paths.
    pending_drop_invalidated();
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_du_count = 0;
    s_shelf_feedback_pending = false;
    s_cover_batch_present_pending = false;
    s_reader_cleanup = false;
    s_poll_ms = 0;
    read_pico_sd_info_t sd = {0};
    esp_err_t media_err = read_pico_sd_get_info(&sd);
    bool reuse = s_shelf_cache_valid && media_err != ESP_ERR_NOT_FINISHED &&
                 sd.present == s_cache_sd_present && sd.mounted == s_cache_sd_mounted &&
                 s_cache_shelf_style == app_settings_shelf_style() &&
                 s_store_revision == book_store_revision();
    s_scan_pending = !reuse;
    if (reuse) {
        refresh_cached_progress(ctx);
        (void)open_requested_book(ctx);
    } else {
        s_count = s_visible_count = 0;
        s_message[0] = 0;
        s_storage[0] = 0;
        invalidate_covers();
        // 已指定文件可直接打开；书架目录留到真正进入书架时再扫描。
        // Open a requested file directly; scan the shelf only when it is actually shown.
        if (open_requested_book(ctx)) {
            s_scan_pending = false;
            return;
        }
        read_pico_sd_start_probe();
    }

}
static void book_on_exit(app_ctx_t* ctx) {
    reader_indent_hold_cancel();
    // 离开阅读页时丢掉未扫完的连续 DU 相位并还原缩放状态。
    // Leaving the reader drops unfinished continuous-DU phases and the zoom state.
    if (s_image_zoom_bw) continuous_du_reset();
    s_zoom_active = false;
    s_image_zoom = READER_IMAGE_ZOOM_MIN;
    s_image_zoom_bw = false;
    s_image_pan_active = false;
    s_image_zoom_page = SIZE_MAX;
    s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
    book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_reader_tap.pending = false;
    s_shelf_page_pending = false;
    ui_keyboard_end();
    s_presented_view = -1;
    free(s_editor_cover); s_editor_cover = NULL;
    s_pressed_control = -1;
    s_reader_slider = -1;
    save_progress();
    s_reader_return_home = false;
    ui_reader_background_release();
    free(s_backgrounds); s_backgrounds = NULL; s_background_count = 0;
    free_book();
    free(s_next_fb);
    s_next_fb = NULL;
    if(s_prep_task){vTaskDelete(s_prep_task);s_prep_task=NULL;}
    if(s_prep_done){vSemaphoreDelete(s_prep_done);s_prep_done=NULL;}
    if (s_draw_lock) { vSemaphoreDelete(s_draw_lock); s_draw_lock = NULL; }
}
// 先停止使用旧卡句柄与字体预渲染，主循环随后切换内置字体。
// Stop old-card handles and font preparation before the loop switches to the builtin font.
static void book_on_media_lost(app_ctx_t* ctx) {
    reader_indent_hold_cancel();
    s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_reader_tap.pending = false;
    lock_draw();
    save_progress();
    free_book();
    unlock_draw();
    free(s_editor_cover); s_editor_cover = NULL;
    s_view = SHELF;
    s_count = s_visible_count = 0;
    s_shelf_cache_valid = false;
    free(s_shelf); s_shelf = NULL; s_shelf_capacity = 0;
    invalidate_covers();
    ctx->leaf = 0;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_scan_pending = true;
    s_du_count = 0;
    copy_text(s_storage, sizeof(s_storage), "TF 卡已移除");
    copy_text(s_message, sizeof(s_message), "TF 卡已移除");
}
static void book_on_media_ready(app_ctx_t* ctx) {
    (void)ctx;
    s_shelf_cache_valid = false;
    if (s_view == SHELF) s_scan_pending = true;
}
// 控件编号仅用于保持按下与抬起命中同一个目标。/ IDs pair a press with release on the same control.
static int control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    if (s_view==SHELF && (s_category_menu || ui_rect_hit((EpdRect){24,79,388,74},x,y))) return -1;
    if (s_view == BULK) return bulk_control_at(ctx, x, y, rect);
    if (s_view == MANAGE) {
        *rect = manage_back_rect();
        if (ui_rect_hit(*rect, x, y)) return 711;
    }
    if (s_view == IMPORT) {
        *rect = (EpdRect){36, 79, 58, 58};
        if (ui_rect_hit(*rect, x, y)) return 700;
        for (int i = 0; i < 5; ++i) {
            *rect = import_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 701 + i;
        }
        int tab = ui_nav_hit(x, y);
        if (tab >= 0) { *rect = (EpdRect){tab * 171, UI_NAV_TOP, 171, 120}; return 705 + tab; }
        return -1;
    }
    if (s_view == SEARCH) {
        for (int i = 0; i < 45; ++i) { *rect = search_rect(i); if (ui_rect_hit(*rect, x, y)) return 500 + i; }
        return -1;
    }
    if (s_view == MANAGE) {
        if (!s_clear_confirm && !s_file_removed) {
            *rect = (EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 274, ui_content_width(), 68};
            if (ui_rect_hit(*rect, x, y)) return 405;
            *rect = (EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 190, ui_content_width(), 68};
            if (ui_rect_hit(*rect, x, y)) return 404;
        }
        int count = s_clear_confirm || s_file_removed ? 2 : 3;
        for (int i = 0; i < count; ++i) {
            *rect = manage_rect(i, count);
            if (ui_rect_hit(*rect, x, y)) return s_clear_confirm ? 300 + i : s_file_removed && i == 1 ? 403 : 400 + i;
        }
        return -1;
    }
    if (s_clear_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 610, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 300 + i;
        }
        return -1;
    }
    if (s_view == READING) {
        if (s_reader_panel == READER_PANEL_TOOLS) for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            *rect = tool_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 200 + i;
        }
        return -1;
    }
    if (s_view != SHELF) for (int i = 0; i < 3; ++i) {
        *rect = ui_bar_rect(i, 3);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) { *rect = shelf_manage_rect(); return 114; }
    if (s_view == SHELF && ui_rect_hit(shelf_import_rect(), x, y)) { *rect = shelf_import_rect(); return 115; }
    if (s_view == SHELF) {
        for (int i = UI_PAGER_FIRST; i <= UI_PAGER_LAST; ++i) {
            *rect = i==UI_PAGER_PREVIOUS?shelf_page_arrow_rect(-1):i==UI_PAGER_NEXT?shelf_page_arrow_rect(1):ui_pager_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 116 + i;
        }
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return -1;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    int count = s_visible_count;
    for (int row = 0; row < rows && ctx->leaf * rows + row < count; ++row) {
        *rect = row_rect(row);
        if (ui_rect_hit(*rect, x, y)) return row;
    }
    return -1;
}
static app_redraw_t paint_control(app_ctx_t* ctx, EpdRect rect) {
    if(s_view==SHELF)for(int i=UI_PAGER_FIRST;i<=UI_PAGER_LAST;++i){
        EpdRect r=ui_pager_rect(i);
        if(!memcmp(&rect,&r,sizeof(r)))return shelf_key_paint(ctx);
    }
    if (s_view == SHELF && app_settings_shelf_style() == 5) {
        for (int row = 0; row < shelf_rows(); ++row) {
            EpdRect target = row_rect(row);
            if (memcmp(&rect, &target, sizeof(rect))) continue;
            s_area = paint_list_cover_feedback(ctx, row);
            s_mode = app_settings_main_fast_refresh() ? MODE_DU : MODE_GL16;
            s_shelf_feedback_pending = true;
            return APP_REDRAW_AREA;
        }
    }
    // 小按钮反馈只重画按钮，不重新绘制全部封面；其余装饰沿用原绘制路径。
    // Repaint small header buttons alone rather than every cover; other decorations retain their path.
    if (s_view == SHELF && !memcmp(&rect, &(EpdRect){442,94,97,54}, sizeof(rect)))
        draw_shelf_header_button(ctx->fb, rect, false);
    else if (s_view == SHELF && !memcmp(&rect, &(EpdRect){551,94,97,54}, sizeof(rect)))
        draw_shelf_header_button(ctx->fb, rect, true);
    else render(ctx, ctx->fb);
    s_mode = MODE_DU;
    s_shelf_feedback_pending = false;
    if (s_view == SHELF) {
        for (int row = 0; row < shelf_rows(); ++row) {
            EpdRect target = row_rect(row);
            if (rect.x != target.x || rect.y != target.y ||
                rect.width != target.width || rect.height != target.height) continue;
            // 覆盖抽出前后的并集，释放或取消时也擦净上方的旧封面。
            // Refresh both book positions so release or cancellation clears the lifted edge.
            rect.y -= SHELF_BOOK_LIFT_PX;
            rect.height += SHELF_BOOK_LIFT_PX;
            s_mode = MODE_GL16;
            s_shelf_feedback_pending = true;
            break;
        }
    }
    s_area = rect;
    return APP_REDRAW_AREA;
}
// 左右滑动始终可用；上下点按模式另外保留上下滑动，不改变中间轻点切换全屏。
// Horizontal swipes work in both tap modes; vertical mode also keeps vertical swipes.
// 从屏幕底部120px上滑呼出工具栏；其他面板保留自己的滑动，正文中部仍按翻页模式解释。
// Open tools from the bottom 120px; other panels keep their own swipes and mid-body gestures retain turn semantics.
static bool reader_tools_swipe(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_view != READING || !s_text || s_reader_panel != READER_PANEL_NONE ||
        s_clear_confirm || s_scan_pending || ctx->consumed || ev->type != UI_GESTURE_SWIPE_U ||
        ev->x0 >= UI_LOCK_WIDTH || ev->y0 < UI_LOCK_HEIGHT - 120 || ev->y0 >= UI_LOCK_HEIGHT) return false;
    s_reader_tap.pending = false;
    s_pressed_control = s_reader_slider = -1;
    s_reader_panel = READER_PANEL_TOOLS;
    s_stats_activity_ms = ctx->now_ms;
    ctx->user_activity = true;
    invalidate_prep();
    return true;
}
static int reader_swipe_direction(ui_gesture_type_t type, bool vertical) {
    int direction = 0;
    if (type == UI_GESTURE_SWIPE_L || (vertical && type == UI_GESTURE_SWIPE_U)) direction = 1;
    else if (type == UI_GESTURE_SWIPE_R || (vertical && type == UI_GESTURE_SWIPE_D)) direction = -1;
    if (direction && !vertical && app_settings_book_vertical()) direction = -direction;
    return direction;
}

static app_redraw_t reader_chapter_jump_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,bool *handled) {
    *handled=false;
    // 数字键必须按下和抬起落在同一控件；拖离、滑动与取消不提交页码。
    // A digit requires matching start/end controls; movement, swipes and cancellation never commit.
    if (s_view == READING && (s_reader_panel == READER_PANEL_STATS || s_reader_panel == READER_PANEL_CHAPTER_JUMP)) {
        *handled=true;
        if(ev->type==UI_GESTURE_TAP){
            int start=reader_chapter_jump_hit(ev->x0,ev->y0);
            if(start>=0&&start==reader_chapter_jump_hit(ev->x,ev->y))
                return reader_chapter_jump_action(ctx,ev->x0,ev->y0);
        }
        return APP_REDRAW_NONE;
    }
    if(reader_chapter_undo_visible()&&ev->type==UI_GESTURE_TAP&&
       ui_rect_hit(reader_chapter_undo_rect(),ev->x0,ev->y0)&&ui_rect_hit(reader_chapter_undo_rect(),ev->x,ev->y)){
        *handled=true;return reader_chapter_jump_commit(ctx,0,true);
    }
    return APP_REDRAW_NONE;
}
// 交互中的一帧：只生成 1bit 点阵，把它按字节铺进 4bpp 帧缓冲，再用跟随 DU 波形一次刷出。
// 不重采样双线性、不画文字，灰度渲染整个挪到松手之后；跟随波形把整幅黑白一次推完，
// 所以动起来看得到画面，而不是只有停手后才出图。
// One interactive frame: build a 1-bit pattern, blit it byte-wise into the 4bpp framebuffer and
// present it with one follow-DU waveform. No bilinear resample and no text drawing; the grayscale
// render waits for the release. A single waveform push shows the whole frame, so motion is
// visible instead of only the final still.
static void zoom_blit_bits(uint8_t *fb, const uint8_t *bits, EpdRect phys) {
    if (!fb || !bits || phys.width <= 0 || phys.height <= 0) return;
    const int width_bytes = (phys.width + 7) / 8, stride = epd_width() / 2;
    for (int y = 0; y < phys.height; ++y) {
        int py = phys.y + y;
        if (py < 0 || py >= epd_height()) continue;
        for (int x = 0; x < phys.width; ++x) {
            int px = phys.x + x;
            if (px < 0 || px >= epd_width()) continue;
            uint8_t tone = (bits[(size_t)y * width_bytes + x / 8] & (0x80u >> (x & 7))) ? 0 : 15;
            uint8_t *dst = fb + (size_t)py * stride + px / 2;
            *dst = px & 1 ? (*dst & 0x0f) | (tone << 4) : (*dst & 0xf0) | tone;
        }
    }
}
static app_redraw_t zoom_frame(app_ctx_t* ctx) {
    const reader_image_t *image = reader_zoom_image();
    if (!image) { reader_zoom_reset(); return APP_REDRAW_NONE; }
    s_image_zoom_bw = true;
    s_image_zoom_draw_ms = ctx->now_ms;
    EpdRect band = book_layout_image_rect();
    int x0, y0, x1, y1;
    reader_logical_to_phys(band.x, band.y, &x0, &y0);
    reader_logical_to_phys(band.x + band.width - 1, band.y + band.height - 1, &x1, &y1);
    EpdRect phys = {x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
        abs(x1 - x0) + 1, abs(y1 - y0) + 1};
    int base_w = 0, base_h = 0;
    reader_image_base_size(&base_w, &base_h);
    int w = (int)((int64_t)base_w * s_image_zoom / READER_IMAGE_ZOOM_MIN);
    int h = (int)((int64_t)base_h * s_image_zoom / READER_IMAGE_ZOOM_MIN);
    int left = s_image_zoom_cx - w / 2, top = s_image_zoom_cy - h / 2;
    if (!zoom_bits_ready(phys)) {
        // 内存不够时退回 4bpp 重绘的老路，绝不卡住手势。
        // Without memory for the pattern, fall back to the older 4bpp repaint.
        ui_fill_rect_fast(band, 0xFF, ctx->fb);
        draw_zoomed_image(ctx->fb, band, s_image_zoom, s_image_zoom_cx, s_image_zoom_cy, true);
        guard_draw_result(ctx->hl, update_display_area_diff_with(
            ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, reader_area()));
        return APP_REDRAW_DONE;
    }
    int64_t t_build = esp_timer_get_time();
    zoom_bits_build(s_zoom_bits, phys, band, image->width, image->height,
                    w, h, left, top);
    int64_t t_build_end = esp_timer_get_time();
    zoom_blit_bits(ctx->fb, s_zoom_bits, phys);
    rails_keepalive();
    epd_poweron();
    // 跟随 DU：一次波形把整幅黑白推完（8 帧短波形），画面完整可见。
    // Follow DU: one waveform pushes the whole black/white frame (eight short frames), so the
    // picture is fully visible.
    guard_draw_result(ctx->hl, update_display_area_diff_with(
        ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, band));
    int64_t t_done = esp_timer_get_time();
    ESP_LOGI(TAG, "zoom %d build=%.1f blit+du=%.1f ms",
             s_image_zoom, (t_build_end - t_build) / 1000.0, (t_done - t_build_end) / 1000.0);
    return APP_REDRAW_DONE;
}

// 松手定稿：把黑白交互帧换成灰阶整页图；缩回原始大小时也走这里，避免半灰残影。
// Settle after the fingers leave: replace the black/white frames with the grayscale page. Zooming
// all the way back lands here too, which is what clears the half-tone ghosting.
static app_redraw_t zoom_settle(app_ctx_t* ctx) {
    s_image_zoom_start_distance = 0;
    s_image_pan_active = false;
    if (!s_image_zoom_bw && s_image_zoom <= READER_IMAGE_ZOOM_MIN) return APP_REDRAW_NONE;
    s_image_zoom_bw = false;
    continuous_du_reset();
    render(ctx, ctx->fb);
    // 松手后整屏全刷：黑白帧留下的光学状态要整个驱动一遍，差分只剩插图区域会留下残影。
    // Full-screen settle after release: the black/white frames leave optical state that a
    // picture-only difference cannot clear, so drive every pixel of the panel.
    guard_draw_result(ctx->hl, update_display_area_full_with(
        ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16,
        (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}));
    return APP_REDRAW_DONE;
}

static bool wants_pinch(app_ctx_t* ctx) {
    (void)ctx;
    return reader_zoom_mode();
}

// 双指缩放与平移：两指中点是缩放锚点，中点移动连同单指拖动都直接平移图片。
// Two-finger zoom and pan: the contact midpoint anchors the scale, and moving that midpoint --
// or dragging with one finger -- pans the image.
static app_redraw_t on_pinch(app_ctx_t* ctx, const app_pinch_t* pinch) {
    if (s_reader_panel == READER_PANEL_FONT_RECOVERY) return APP_REDRAW_NONE;
    if (!pinch || !reader_zoom_mode()) return APP_REDRAW_NONE;
    EpdRect band = book_layout_image_rect();
    if (!pinch->active) return zoom_settle(ctx);
    int dx = (int)pinch->bx - (int)pinch->ax, dy = (int)pinch->by - (int)pinch->ay;
    uint32_t distance = reader_sqrt((uint32_t)(dx * dx + dy * dy));
    int mx = ((int)pinch->ax + (int)pinch->bx) / 2;
    int my = ((int)pinch->ay + (int)pinch->by) / 2;
    if (s_image_zoom_page != s_page) {
        s_image_zoom_page = s_page;
        s_image_zoom = READER_IMAGE_ZOOM_MIN;
        s_image_zoom_cx = band.x + band.width / 2;
        s_image_zoom_cy = band.y + band.height / 2;
        s_image_zoom_start_distance = 0;
    }
    // 起点太近时比值不可靠，等两指张开一点再记基准；锚点存成 1:1 基准像素下的偏移。
    // A too-near start makes the ratio unreliable, so wait for the fingers to spread; the
    // anchor is stored as an offset in 1:1 base pixels.
    if (!s_image_zoom_start_distance) {
        if (distance < 24) return APP_REDRAW_NONE;
        s_image_zoom_start_distance = distance;
        s_image_zoom_start_scale = s_image_zoom;
        s_image_zoom_start_cx = s_image_zoom_cx;
        s_image_zoom_start_cy = s_image_zoom_cy;
        s_image_zoom_anchor_dx = (mx - s_image_zoom_cx) * READER_IMAGE_ZOOM_MIN / s_image_zoom;
        s_image_zoom_anchor_dy = (my - s_image_zoom_cy) * READER_IMAGE_ZOOM_MIN / s_image_zoom;
        return APP_REDRAW_NONE;
    }
    int scale = (int)((int64_t)s_image_zoom_start_scale * distance / s_image_zoom_start_distance);
    if (scale > READER_IMAGE_ZOOM_MAX) scale = READER_IMAGE_ZOOM_MAX;
    // 缩回原始大小就立刻出一次灰阶：黑白帧留在屏上会变成一片残影。
    // Landing back at 1:1 settles grayscale right away; black/white frames left on the panel
    // would read as ghosting.
    if (scale <= READER_IMAGE_ZOOM_MIN) {
        s_image_zoom = READER_IMAGE_ZOOM_MIN;
        s_image_zoom_cx = band.x + band.width / 2;
        s_image_zoom_cy = band.y + band.height / 2;
        return zoom_settle(ctx);
    }
    int cx = mx - (int)((int64_t)s_image_zoom_anchor_dx * scale / READER_IMAGE_ZOOM_MIN);
    int cy = my - (int)((int64_t)s_image_zoom_anchor_dy * scale / READER_IMAGE_ZOOM_MIN);
    int base_w = 0, base_h = 0;
    reader_image_base_size(&base_w, &base_h);
    int w = (int)((int64_t)base_w * scale / READER_IMAGE_ZOOM_MIN);
    int h = (int)((int64_t)base_h * scale / READER_IMAGE_ZOOM_MIN);
    reader_image_clamp_center(&cx, &cy, w, h, band);
    int moved = cx - s_image_zoom_cx, moved_y = cy - s_image_zoom_cy;
    if (moved < 0) moved = -moved;
    if (moved_y < 0) moved_y = -moved_y;
    int step = scale > s_image_zoom ? scale - s_image_zoom : s_image_zoom - scale;
    // 状态先落地，再决定这一帧要不要推屏：节流只应该省掉刷新，不能吞掉手势。
    // Commit the state first and only then decide whether to present this frame: throttling may
    // skip a refresh, never the gesture itself.
    s_image_zoom = scale;
    s_image_zoom_cx = cx;
    s_image_zoom_cy = cy;
    // 单帧要重采样整块插图，按变化量与时间节流；连续 DU 每扫一个相位约 6 ms，阈值放低也稳。
    // A frame resamples the whole illustration, so throttle by change and time; one continuous-DU
    // phase is about 6 ms, so the thresholds can stay low.
    if (step < 20 && moved + moved_y < 4) return APP_REDRAW_NONE;
    if (ctx->now_ms - s_image_zoom_draw_ms < 40) return APP_REDRAW_NONE;
    return zoom_frame(ctx);
}

// 缩放后单指拖动平移；拖动结束吃掉这次滑动，不翻页也不切工具栏。
// One-finger pan while zoomed; the ending swipe is swallowed so it neither turns the page nor
// toggles the toolbar.
static app_redraw_t image_zoom_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev, bool* handled) {
    *handled = false;
    if (!reader_zoom_mode() || s_image_zoom_page != s_page ||
        s_image_zoom <= READER_IMAGE_ZOOM_MIN) {
        s_image_pan_active = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_PRESS) {
        s_image_pan_active = false;
        s_image_pan_x = ev->x; s_image_pan_y = ev->y;
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_MOVE) {
        int dx = (int)ev->x - (int)s_image_pan_x, dy = (int)ev->y - (int)s_image_pan_y;
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (!s_image_pan_active && ax + ay < 8) return APP_REDRAW_NONE;
        s_image_pan_active = true;
        s_image_pan_x = ev->x; s_image_pan_y = ev->y;
        EpdRect band = book_layout_image_rect();
        int base_w = 0, base_h = 0;
        reader_image_base_size(&base_w, &base_h);
        int cx = s_image_zoom_cx + dx, cy = s_image_zoom_cy + dy;
        reader_image_clamp_center(&cx, &cy,
            (int)((int64_t)base_w * s_image_zoom / READER_IMAGE_ZOOM_MIN),
            (int)((int64_t)base_h * s_image_zoom / READER_IMAGE_ZOOM_MIN), band);
        *handled = true;
        s_image_zoom_cx = cx; s_image_zoom_cy = cy;
        if (ctx->now_ms - s_image_zoom_draw_ms < 60) return APP_REDRAW_NONE;
        return zoom_frame(ctx);
    }
    if (s_image_pan_active) {
        s_image_pan_active = false;
        *handled = true;
        return zoom_settle(ctx);
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t reader_font_recovery_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    (void)ctx;
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    for (int i = 0; i < 2; ++i) {
        EpdRect button = reader_font_recovery_button(i);
        if (!ui_rect_hit(button, ev->x0, ev->y0) || !ui_rect_hit(button, ev->x, ev->y)) continue;
        if (i) s_font_restart_pending = true;
        else { reader_font_recovery_reset(); s_reader_panel = READER_PANEL_FONT_PICKER; }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t gesture_event(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_view == READING && s_reader_panel == READER_PANEL_FONT_RECOVERY)
        return reader_font_recovery_gesture(ctx, ev);
    bool zoom_handled = false;
    app_redraw_t zoom_redraw = image_zoom_gesture(ctx, ev, &zoom_handled);
    if (zoom_handled) return zoom_redraw;
    bool indent_handled=false;
    app_redraw_t indent_redraw=reader_indent_gesture(ctx,ev,&indent_handled);
    if(indent_handled)return indent_redraw;
    if (s_view==SHELF && (s_category_menu || ui_rect_hit((EpdRect){24,79,388,74},ev->x0,ev->y0)))
        return shelf_category_gesture(ctx,ev);
    if (s_view == READING && (s_reader_panel == READER_PANEL_BACKGROUND ||
        s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        return reader_background_step(ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
    }
    if (ev->type == UI_GESTURE_CANCEL || ev->type == UI_GESTURE_LONG_PRESS ||
        ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R ||
        ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D ||
        (ev->type == UI_GESTURE_PRESS && !reader_zoom_mode() &&
         !reader_page_image_hit(ev->x0, ev->y0) &&
         !book_reader_center(body_rect(), ev->x0, ev->y0)))
        s_reader_tap.pending = false;
    if (reader_tools_swipe(ctx, ev)) return APP_REDRAW_PAGE;
    bool jump_handled=false;
    app_redraw_t jump_redraw=reader_chapter_jump_gesture(ctx,ev,&jump_handled);
    if(jump_handled)return jump_redraw;
    bool input_feedback = false;
    if (s_view == SEARCH || s_view == EDIT) {
        if (ev->type == UI_GESTURE_PRESS)
            return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? editor_paint(ctx, false) : APP_REDRAW_NONE;
        input_feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
    }
    if (s_view == SEARCH) {
        if (ev->type == UI_GESTURE_TAP) return action_at(ctx, ev->x0, ev->y0);
        if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
            if (!ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1) && !input_feedback) return APP_REDRAW_NONE;
            return editor_paint(ctx, false);
        }
        return input_feedback ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_view == TOC) {
        if (s_toc_jump_open) {
            if (ev->type == UI_GESTURE_PRESS &&
                book_toc_jump_hit(ev->x0, ev->y0) == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_drag = true;
                s_toc_jump_percent = book_toc_jump_percent(ev->x0);
                return APP_REDRAW_PAGE;
            }
            if (s_toc_jump_drag && ev->type == UI_GESTURE_MOVE) {
                int next = book_toc_jump_percent(ev->x);
                if (next == s_toc_jump_percent) return APP_REDRAW_NONE;
                s_toc_jump_percent = next;
                render(ctx, ctx->fb);
                s_area = (EpdRect){44, 356, 596, 572};
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
            if (s_toc_jump_drag && ev->type != UI_GESTURE_PRESS) {
                s_toc_jump_drag = false;
                if (ev->type == UI_GESTURE_CANCEL) return APP_REDRAW_NONE;
                s_toc_jump_percent = book_toc_jump_percent(ev->x);
                return APP_REDRAW_PAGE;
            }
            if (ev->type == UI_GESTURE_TAP) return action_at(ctx, ev->x0, ev->y0);
            return APP_REDRAW_NONE;
        }
        if (ev->type == UI_GESTURE_TAP) {
            int start = book_toc_hit(ev->x0, ev->y0, ctx->leaf, book_navigation_count());
            int end = book_toc_hit(ev->x, ev->y, ctx->leaf, book_navigation_count());
            if (start != -1 && start == end) return action_at(ctx, ev->x0, ev->y0);
        }
        if (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D) {
            int next = ctx->leaf + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
            if (next >= 0 && next < leaves()) { ctx->leaf = next; return APP_REDRAW_PAGE; }
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == EDIT) {
        if (ev->type == UI_GESTURE_TAP) return editor_action(ctx, ev->x0, ev->y0);
        if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)
            return (ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1) || input_feedback) ? editor_paint(ctx, false) : APP_REDRAW_NONE;
        return input_feedback ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_view == SHELF && ev->type == UI_GESTURE_TAP && ui_nav_hit(ev->x0, ev->y0) >= 0) {
        ui_nav_request(ctx, ui_nav_hit(ev->x0, ev->y0));
        return APP_REDRAW_NONE;
    }
    if (s_view == READING && ev->type != UI_GESTURE_PRESS) s_stats_activity_ms = ctx->now_ms;
    if (s_view == READING &&
        (s_reader_panel == READER_PANEL_FONT_SETTINGS || s_reader_panel == READER_PANEL_LAYOUT_SETTINGS)) {
        if (ev->type == UI_GESTURE_PRESS) {
            int slider = reader_slider_at(ev->x0, ev->y0);
            if (slider >= 0) {
                reader_slider_begin(slider, ev->x0);
                render(ctx, ctx->fb);
                s_area = reader_slider_refresh_rect(slider);
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
        } else if (s_reader_slider >= 0) {
            if (ev->type == UI_GESTURE_MOVE) {
                int slider = s_reader_slider;
                reader_slider_move(ev->x);
                render(ctx, ctx->fb);
                s_area = reader_slider_refresh_rect(slider);
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
            if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_SWIPE_L ||
                ev->type == UI_GESTURE_SWIPE_R || ev->type == UI_GESTURE_SWIPE_U ||
                ev->type == UI_GESTURE_SWIPE_D || ev->type == UI_GESTURE_LONG_PRESS) {
                if (!s_reader_slider_endpoint) reader_slider_move(ev->x);
                return reader_slider_commit(ctx);
            }
            if (ev->type == UI_GESTURE_CANCEL) {
                s_reader_slider = -1;
                return APP_REDRAW_PAGE;
            }
        }
    }
    if (s_view == READING && s_reader_panel == READER_PANEL_FONT_PICKER &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int count = ttf_font_count();
        int pages = count > 0 ? (count + BOOK_FONT_PAGE - 1) / BOOK_FONT_PAGE : 1;
        if (ev->type == UI_GESTURE_SWIPE_U && s_font_page + 1 < pages) ++s_font_page;
        else if (ev->type == UI_GESTURE_SWIPE_D && s_font_page > 0) --s_font_page;
        else return APP_REDRAW_NONE;
        return APP_REDRAW_PAGE;
    }
    EpdRect start_rect = {0}, end_rect = {0};
    int start = control_at(ctx, ev->x0, ev->y0, &start_rect);
    int end = control_at(ctx, ev->x, ev->y, &end_rect);
    if (ev->type == UI_GESTURE_PRESS) {
        s_pressed_control = start;
        return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
    }
    bool decorated = s_pressed_control >= 0;
    s_pressed_control = -1;
    if (ev->type == UI_GESTURE_LONG_PRESS && start == end) {
        if (s_view == SHELF && start >= 0 && start < shelf_rows() && !s_clear_confirm) {
            s_managed = s_shelf[ctx->leaf * shelf_rows() + start];
            s_view = MANAGE;
            s_clear_confirm = false;
            s_delete_confirm = s_file_removed = s_managed.removed;
            copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ? "文件已删除，进度清理失败，请重试" : "");
            return APP_REDRAW_PAGE;
        }
        if (s_view == READING && !s_toolbar && !s_clear_confirm && ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            save_progress();
            set_reader_view(TOC);
            s_message[0] = 0;
            size_t toc = current_toc_position();
            ctx->leaf = toc == SIZE_MAX ? 0 : (int)(toc / BOOK_TOC_ROWS);
            return APP_REDRAW_PAGE;
        }
    }
    if (ev->type == UI_GESTURE_TAP && !s_scan_pending) {
        if (start >= 0 && start == end) {
            // 列表先完成回弹，才释放封面缓存并开书；没有人为等待或整页预刷新。
            // Settle a list bounce before opening frees artwork, without a delay or whole-page pre-refresh.
            if (decorated && s_view == SHELF && app_settings_shelf_style() == 5 && start < shelf_rows())
                (void)present(ctx, paint_control(ctx, start_rect));
            app_redraw_t result = action_at(ctx, ev->x0, ev->y0);
            if (ctx->request_app || ctx->request_menu || ctx->request_return) return result;
            return result != APP_REDRAW_NONE ? result : paint_control(ctx, start_rect);
        }
        if (start < 0 && end < 0 && s_view == READING && !s_clear_confirm)
            return action_at(ctx, ev->x0, ev->y0);
    }
    if (!s_clear_confirm && !s_scan_pending) {
        if (s_view == READING && (!s_toolbar || (s_reader_panel == READER_PANEL_TOOLS && ev->y0 < 1096)) &&
            ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            int direction = reader_swipe_direction(ev->type, app_settings_reader_vertical_turn());
            if (direction) { ctx->user_activity = true; return turn_page(ctx, direction); }
        } else if (s_view == SHELF && (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)) {
            app_redraw_t result = shelf_turn_page(ctx, ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
            if (result != APP_REDRAW_NONE) return result;
        } else if ((s_view == BULK && !s_batch_confirm) &&
                   (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
            return bulk_turn_page(ctx, ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
        }
    }
    return decorated ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
}
static app_redraw_t reader_key_action(app_ctx_t *ctx, app_reader_key_action_t action) {
    s_reader_tap.pending = false;
    switch (action) {
        case APP_READER_KEY_PREV: case APP_READER_KEY_NEXT:
            s_stats_activity_ms = ctx->now_ms;
            return turn_page(ctx, action == APP_READER_KEY_PREV ? -1 : 1);
        case APP_READER_KEY_HOME: return reader_return(ctx);
        case APP_READER_KEY_FULLSCREEN: return toggle_reader_fullscreen(ctx);
        case APP_READER_KEY_REFRESH: return reader_manual_refresh(ctx);
        case APP_READER_KEY_TOOLS:
            s_reader_slider = -1;
            s_reader_panel = s_reader_panel == READER_PANEL_TOOLS ? READER_PANEL_NONE : READER_PANEL_TOOLS;
            invalidate_prep();
            return APP_REDRAW_PAGE;
        default: return APP_REDRAW_NONE;
    }
}
static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if (s_reader_panel == READER_PANEL_FONT_RECOVERY) {
        reader_font_recovery_reset(); s_reader_panel = READER_PANEL_FONT_PICKER;
        return APP_REDRAW_PAGE;
    }
    reader_indent_hold_cancel();
    s_reader_tap.pending = false;
    s_pressed_control = -1;
    if (s_view == TOC && s_toc_jump_open) {
        if (key == UI_KEY_2) s_toc_jump_open = s_toc_jump_drag = false;
        else if (key == UI_KEY_1 || key == UI_KEY_3) {
            s_toc_jump_percent += key == UI_KEY_1 ? -5 : 5;
            if (s_toc_jump_percent < 0) s_toc_jump_percent = 0;
            if (s_toc_jump_percent > 100) s_toc_jump_percent = 100;
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING && s_view != TOC) {
        if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
        if (s_view == EDIT) { ui_keyboard_end(); free(s_editor_cover); s_editor_cover = NULL; s_view = MANAGE; return APP_REDRAW_PAGE; }
        if (s_view == SEARCH) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (s_batch_confirm) { s_batch_confirm = false; return APP_REDRAW_PAGE; }
        if (s_view == MANAGE || s_view == BULK || s_view == IMPORT) {
            if (s_view == BULK) return bulk_finish(ctx);
            s_view = SHELF; return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    }
    if (s_scan_pending || s_clear_confirm) return APP_REDRAW_NONE;
    if (s_view == READING && (key == UI_KEY_1 || key == UI_KEY_2 || key == UI_KEY_3))
        return reader_key_action(ctx, app_settings_reader_key_action(key == UI_KEY_1 ? 0 : key == UI_KEY_2 ? 1 : 2));
    if (key == UI_KEY_2) {
        if (s_view == BULK) s_view = SHELF;
        else if (s_view == SHELF) { ui_nav_request(ctx, 3); return APP_REDRAW_NONE; }
        else if (s_view == READING) {
            s_reader_slider = -1;
            s_reader_panel = s_reader_panel == READER_PANEL_TOOLS ? READER_PANEL_NONE : READER_PANEL_TOOLS;
            invalidate_prep();
        } else if (s_view == TOC) {
            set_reader_view(s_text ? READING : SHELF);
            s_reader_panel = READER_PANEL_NONE;
        } else {
            ui_nav_request(ctx, 0);
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_PAGE;
    }
    if (key != UI_KEY_1 && key != UI_KEY_3) return APP_REDRAW_NONE;
    int dir = key == UI_KEY_1 ? -1 : 1;
    if (s_view == READING) { s_stats_activity_ms = ctx->now_ms; return turn_page(ctx, dir); }
    int next = ctx->leaf + dir;
    if (next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t on_power_short(app_ctx_t* ctx) {
    s_reader_tap.pending = false;
    if (s_view != READING || !s_text || s_clear_confirm ||
        s_reader_panel == READER_PANEL_FONT_RECOVERY || !app_settings_reader_power_turn()) return APP_REDRAW_NONE;
    s_stats_activity_ms = ctx->now_ms;
    app_redraw_t result = turn_page(ctx, 1);
    return result == APP_REDRAW_NONE ? APP_REDRAW_DONE : result;
}
static app_redraw_t on_key_long(app_ctx_t* ctx, int key) {
    if (s_reader_panel == READER_PANEL_FONT_RECOVERY) return APP_REDRAW_NONE;
    reader_indent_hold_cancel();
    s_reader_tap.pending = false;
    if (key != UI_KEY_2) return APP_REDRAW_NONE;
    if (s_view == TOC && s_toc_jump_open) {
        s_toc_jump_open = s_toc_jump_drag = false;
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING && s_view != TOC) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == READING) {
        return reader_key_action(ctx, app_settings_reader_hold_action());
    }
    s_message[0] = 0;
    set_reader_view(s_text ? READING : SHELF);
    s_reader_panel = READER_PANEL_NONE;
    return APP_REDRAW_PAGE;
}
static bool menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    // 图书所有视图只使用产品导航，旧菜单不能抢占右侧分页按钮。
    // All book views use product navigation; the legacy menu must never steal the right pager.
    return false;
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_view != SHELF || s_scan_pending || s_clear_confirm || next == ctx->leaf || next < 0 || next >= leaves())
        return APP_REDRAW_NONE;
    ctx->leaf = next;
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_shelf_page_pending = true;
    s_pressed_control = -1;
    prepare_covers(ctx);
    render(ctx, ctx->fb);
    s_area = (EpdRect){0, 176, UI_LOCK_WIDTH, 912};
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

static app_redraw_t shelf_key_tick(app_ctx_t *ctx) {
    if(s_view!=SHELF || ctx->request_app){
        s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
        return APP_REDRAW_NONE;
    }
    if(!s_shelf_key_mode || !s_shelf_key_pressed || ctx->now_ms<s_shelf_key_until || ctx->consumed)
        return APP_REDRAW_NONE;
    int dir=(s_shelf_key_delta>0)-(s_shelf_key_delta<0);
    if(dir)s_shelf_key_delta-=dir;
    s_shelf_key_pressed=0;
    app_redraw_t redraw=dir?shelf_turn_page(ctx,dir):APP_REDRAW_NONE;
    if(s_shelf_key_delta){s_shelf_key_pressed=s_shelf_key_delta<0?-1:1;s_shelf_key_until=ctx->now_ms+35;}
    return redraw==APP_REDRAW_NONE?shelf_key_paint(ctx):redraw;
}

// 键事件与原始边沿可能属于同一份报告，每轮最多执行一次；其他子页只清理旧输入。
// Keys and raw edges may come from the same report: apply one turn per tick, discard on other subpages.
static int book_remote_direction(void) {
    int direction = 0;
    ble_pt_event_t event;
    while (ble_pt_pop_key(&event)) {
        ble_pt_action_t action = ble_pt_action_for_usage(event.usage, event.mods);
        if (action == BLE_PT_ACTION_PREV) direction = -1;
        else if (action == BLE_PT_ACTION_NEXT) direction = 1;
    }
    ble_pt_raw_t raw;
    while (ble_pt_pop_raw(&raw)) {
        if (!raw.pressed) continue;
        ble_pt_action_t action = ble_pt_action_for_frame(&raw.frame);
        if (action == BLE_PT_ACTION_PREV) direction = -1;
        else if (action == BLE_PT_ACTION_NEXT) direction = 1;
    }
    return direction;
}
static app_redraw_t reader_font_recovery_tick(app_ctx_t *ctx) {
    if (s_view != READING || !s_text) return APP_REDRAW_NONE;
    if (s_font_recovery_pending) {
        s_font_recovery_pending = false;
        s_font_recovery_save_failed = false;
        s_reader_panel = READER_PANEL_FONT_RECOVERY;
        s_reader_tap.pending = false;
        s_reader_slider = s_pressed_control = -1;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel != READER_PANEL_FONT_RECOVERY || !s_font_restart_pending || ctx->consumed)
        return APP_REDRAW_NONE;
    s_font_restart_pending = false;
    // 使用已有保存/预绘制退出入口；存储失败时留在提示框，不能静默丢失进度。
    // Reuse checkpoint/prefetch teardown; keep the dialog on storage failure rather than lose progress silently.
    before_lock(ctx);
    retry_progress();
    if (s_save_failed) {
        s_font_recovery_save_failed = true;
        return APP_REDRAW_PAGE;
    }
    app_restart_host();
    return APP_REDRAW_DONE;
}
static app_redraw_t on_tick(app_ctx_t* ctx) {
    app_redraw_t recovery = reader_font_recovery_tick(ctx);
    if (recovery != APP_REDRAW_NONE) return recovery;
    if (s_reader_panel == READER_PANEL_FONT_RECOVERY) return APP_REDRAW_NONE;

    app_redraw_t indent=reader_indent_hold_tick(ctx);
    if(indent!=APP_REDRAW_NONE)return indent;
    app_redraw_t keys=shelf_key_tick(ctx);
    if(keys!=APP_REDRAW_NONE || ctx->request_app)return keys;
    // 字体开关留下的重排在这里做：触摸回调的栈太深，一章排下来会把主任务栈顶穿（实测溢出过）。
    // The font switch's re-typeset happens here: the touch callback runs too deep, and laying out a
    // chapter from there overran the main task stack (measured).
    if (s_fonts_reload_pending) {
        s_fonts_reload_pending = false;
        if (s_text && book_kind() == BOOK_KIND_EPUB)
            (void)load_chapter(ctx, s_chapter, book_layout_page_start_offset(s_page), false);
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING || (s_toolbar && s_reader_panel != READER_PANEL_TOOLS) || s_clear_confirm || ctx->consumed) s_reader_tap.pending = false;
    else {
        int tap = book_reader_tap_tick(&s_reader_tap, ctx->now_ms, ctx->touch && ctx->touch->touched);
        if (tap) return turn_page(ctx, tap);
    }
    track_ticket_stats(ctx);
    if (s_view == SEARCH || s_view == EDIT) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return editor_paint(ctx, false);
        return ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED
            ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (ctx->consumed) return APP_REDRAW_NONE;
    // 上/左为上一页，下/右为下一页；书架复用滑动的局部刷新，不闪动题头和底栏。
    // Up/left turn back, down/right turn forward; the shelf reuses swipe refresh without flashing chrome.
    if (app_settings_ble_turner()) {
        int ble_dir = book_remote_direction();
        if (ble_dir && s_view == SHELF) {
            app_redraw_t redraw = shelf_turn_page(ctx, ble_dir);
            if (redraw != APP_REDRAW_NONE) return redraw;
        } else if (ble_dir && s_view == READING) {
            s_stats_activity_ms = ctx->now_ms;
            return turn_page(ctx, ble_dir);
        }
    }
    if (s_chapter_jump_undo.valid && ctx->now_ms >= s_chapter_jump_undo.until_ms) {
        bool visible=reader_chapter_undo_visible();s_chapter_jump_undo.valid=false;
        invalidate_prep();
        if(visible){
            render(ctx,ctx->fb);s_area=(EpdRect){96,152,492,142};s_mode=MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    if (s_reader_notice[0] && ctx->now_ms >= s_reader_notice_until) {
        s_reader_notice[0] = 0;
        if (s_view == READING) {
            // 区域刷新不会自动重绘帧缓冲；先移除提示，再只更新提示所在区域。
            // Area refreshes do not rerender the framebuffer; erase the toast before updating its region.
            render(ctx, ctx->fb);
            s_area = (EpdRect){208, 150, 268, 68};
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    if (s_save_failed && ctx->now_ms - s_save_retry_ms >= 15000) {
        s_save_retry_ms = ctx->now_ms;
        bool failed = s_save_failed;
        retry_progress();
        if (failed != s_save_failed) { invalidate_prep(); return APP_REDRAW_PAGE; }
    }
    app_redraw_t background = reader_background_tick();
    if (background != APP_REDRAW_NONE) return background;
    if (s_scan_pending && ctx->now_ms - s_poll_ms >= 500) {
        s_poll_ms = ctx->now_ms;
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        scan_shelf(ctx);
        if (ctx->request_app) return APP_REDRAW_NONE;
        (void)open_requested_book(ctx);
        return APP_REDRAW_PAGE;
    }
    if ((s_view == SHELF || s_view == MANAGE) && !(ctx->touch && ctx->touch->touched)) {
        (void)prepare_cover_batch(ctx, false);
        // 整页准备好再合并推屏；占位页或失败输出不能消费已加载封面的待刷标记。
        // Reveal only a complete cover group; placeholders and failed output cannot consume pending artwork.
        unsigned changed=shelf_cover_reveal_mask();
        if (ctx->request_app) return APP_REDRAW_NONE;
        EpdRect area = {0};
        for (int row = 0; row < shelf_rows(); ++row) {
            if (!(changed & (1u << row))) continue;
            EpdRect r = row_rect(row);
            if (!area.width) area = r;
            else {
                int right = area.x+area.width > r.x+r.width ? area.x+area.width : r.x+r.width;
                int bottom = area.y+area.height > r.y+r.height ? area.y+area.height : r.y+r.height;
                if (r.x < area.x) area.x = r.x;
                if (r.y < area.y) area.y = r.y;
                area.width = right-area.x; area.height = bottom-area.y;
            }
        }
        if (area.width) {
            render(ctx, ctx->fb);
            s_area = area;
            s_mode = s_view == SHELF && app_settings_main_fast_refresh() ? MODE_DU : MODE_GL16;
            s_shelf_feedback_pending = s_view == SHELF;
            s_cover_batch_present_pending = true;
            s_cover_present_mask=changed;
            return APP_REDRAW_AREA;
        }
    }
    warm_shelf_lock_cover(ctx);
    if (ctx->request_app) return APP_REDRAW_NONE;
    // 目录暂借系统字体，不应把正文按系统字体重新分页。/ The directory borrows the system face without repaginating the reader.
    if (s_view == READING && s_text && strcmp(s_font_path, ttf_font_path())) {
        size_t off = book_layout_page_start_offset(s_page);
        save_progress();
        lock_draw();
        invalidate_prep();
        bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        if (ok) s_page = book_layout_page_for_offset(off);
        copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
        unlock_draw();
        if (!ok) { free_book(); s_view = SHELF; copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书"); }
        return APP_REDRAW_PAGE;
    }
    app_redraw_t clock=reader_clock_tick(ctx);
    if(clock==APP_REDRAW_NONE)reader_prefetch_idle(ctx);
    return clock;
}
static void before_lock(app_ctx_t* ctx) {
    reader_indent_hold_cancel();
    s_shelf_key_mode=s_shelf_key_paint_pending=false;s_shelf_key_delta=s_shelf_key_pressed=0;
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_reader_tap.pending = false;
    track_ticket_stats(ctx);
    save_progress();
    // 锁屏前丢弃分行预绘，后续只按当前字体重建。/ Discard idle prefetch before lock; rebuild only with the current face.
    lock_draw();
    invalidate_prep();
    unlock_draw();
    // 锁屏票根始终以当前正在阅读的书为准；不要依赖书架最近项是否已经同步。
    if (s_text && s_path[0]) book_progress_set_last_path(s_path);
    flush_ticket_stats();
    s_stats_last_ms = 0;
}
static EpdRect area_hint(app_ctx_t* ctx) { (void)ctx; return s_area; }
static bool main_page_visible(app_ctx_t* ctx) {
    (void)ctx;
    return s_view == SHELF && !s_clear_confirm && !s_batch_confirm;
}

const app_desc_t app_book = {
    .title = "图书 Books", .detail = "TF 卡 txt / epub 阅读", .enter_full = false, .owns_keys = true,
    .defer_middle_short = true,
    .menu_handle_enabled = menu_handle_enabled,
    .main_page_visible = main_page_visible,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = book_on_exit,
    .on_media_lost = book_on_media_lost, .on_media_ready = book_on_media_ready,
    .on_before_lock = before_lock, .on_main_key = shelf_main_key,
    .on_gesture = gesture_event, .on_key = on_key, .on_key_long = on_key_long,
    .wants_pinch = wants_pinch, .on_pinch = on_pinch,
    .on_power_short = on_power_short,
    .on_tick = on_tick, .area_hint = area_hint,
};
