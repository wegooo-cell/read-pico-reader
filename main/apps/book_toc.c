/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：目录只呈现章节导航，和书架列表、旧示例菜单完全分离。
 * English: The directory only navigates chapters and shares no shelf or demo-menu rendering.
 * 用户修复：书名与章名统一使用当前系统字体的标题路径；原生字号、整行字体来源和灰阶抗锯齿保持一致，不放大补充点阵。
 * User fix: book/chapter names share the active system title path with native sizes, stable whole-line font selection and gray antialiasing, without enlarged bitmap supplements.
 */
#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include "book_toc.h"

#include <stdio.h>
#include <string.h>
#include "book_source.h"
#include "book_chapter_label.h"
#include "ui_kit.h"
#include "ui_nav.h"

enum { TOC_LEFT = 36, TOC_RIGHT = 648, TOC_ROW_TOP = 252,
       TOC_ROW_STEP = 108, TOC_ROW_HEIGHT = 94 };
enum { TOC_VOLUME_HIT = -1000 };
static size_t s_open_volume=SIZE_MAX;
static bool s_volume_picker;
static int s_picker_return_page;

static size_t volume_body_begin(const book_volume_t *v) {
    return v->begin+(!v->include_title && v->title==v->begin);
}
static size_t volume_body_count(size_t group) {
    book_volume_t v;
    return book_navigation_volume(group,&v)?v.end-volume_body_begin(&v):0;
}
static size_t visible_count(size_t chapters) {
    size_t groups=book_navigation_volume_count();
    return groups?groups+(s_volume_picker?0:volume_body_count(s_open_volume)):chapters;
}
// 可见行映射回原目录索引；只存一个展开卷，不复制章节或标题数组。
// Map visible rows to original indices; retain one expanded volume without copying chapter/title lists.
static int visible_target(size_t row,size_t chapters) {
    size_t groups=book_navigation_volume_count();
    if(!groups)return row<chapters?(int)row:-1;
    for(size_t i=0;i<groups;++i) {
        if(!row)return TOC_VOLUME_HIT-(int)i;
        --row;
        if(!s_volume_picker&&i==s_open_volume) {
            book_volume_t v;
            if(!book_navigation_volume(i,&v))return -1;
            size_t begin=volume_body_begin(&v),count=v.end-begin;
            if(row<count)return (int)(begin+row);
            row-=count;
        }
    }
    return -1;
}
int book_toc_open(size_t current) {
    s_volume_picker=false;
    s_open_volume=book_navigation_volume_for(current);
    book_volume_t v;
    if(!book_navigation_volume(s_open_volume,&v))return current==SIZE_MAX?0:(int)(current/BOOK_TOC_ROWS);
    size_t row=s_open_volume;
    if(current>=volume_body_begin(&v))row+=1+current-volume_body_begin(&v);
    return (int)(row/BOOK_TOC_ROWS);
}
bool book_toc_toggle(int target,int *page) {
    if(target>TOC_VOLUME_HIT||!page)return false;
    size_t group=(size_t)(-(target+1000));
    if(group>=book_navigation_volume_count())return false;
    s_open_volume=!s_volume_picker&&s_open_volume==group?SIZE_MAX:group;
    s_volume_picker=false;
    *page=(int)(group/BOOK_TOC_ROWS);
    return true;
}
bool book_toc_picker_close(int *page) {
    if(!page||!s_volume_picker)return false;
    s_volume_picker=false;
    *page=s_picker_return_page;
    return true;
}
bool book_toc_picker_toggle(int *page) {
    size_t groups=book_navigation_volume_count();
    if(!page||!groups)return false;
    if(book_toc_picker_close(page))return true;
    s_picker_return_page=*page;
    s_volume_picker=true;
    *page=s_open_volume<groups?(int)(s_open_volume/BOOK_TOC_ROWS):0;
    return true;
}

int book_toc_pages(size_t chapters) {
    chapters=visible_count(chapters);
    return chapters ? (int)(1 + (chapters - 1) / BOOK_TOC_ROWS) : 1;
}

EpdRect book_toc_row_rect(int row) {
    return (EpdRect){TOC_LEFT, TOC_ROW_TOP + row * TOC_ROW_STEP,
                     TOC_RIGHT - TOC_LEFT, TOC_ROW_HEIGHT};
}

// 把 EPUB/TXT 中的换行、制表符和控制符收敛为一个空格，防止绘制越过本行。
// Collapse line breaks, tabs and controls from EPUB/TXT to one space so glyphs stay in one row.
static void one_line(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t out = 0;
    bool gap = false;
    for (const unsigned char *p = (const unsigned char *)src; *p && out + 1 < cap;) {
        unsigned char c = *p;
        if (c <= 0x20 || c == 0x7f) { gap = out != 0; ++p; continue; }
        if (gap && out + 2 < cap) dst[out++] = ' ';
        gap = false;
        int bytes = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (out + (size_t)bytes >= cap) break;
        for (int i = 0; i < bytes && *p; ++i) dst[out++] = (char)*p++;
    }
    while (out && dst[out - 1] == ' ') --out;
    dst[out] = 0;
}

static void fit_line(char *text, int px, int width) {
    size_t len = strlen(text);
    bool trimmed = false;
    while (len && (ui_text_fixed_width_px(px, text) > width || len > 96)) {
        do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        text[len] = 0;
        trimmed = true;
    }
    if (trimmed && len > 3) {
        // Cut a little more before adding the ellipsis so its glyph cannot cross the status area.
        // 添加省略号前预留字形宽度，避免跨过右侧当前章节标记。
        while (len && ui_text_fixed_width_px(px, text) + ui_text_fixed_width_px(px, "…") > width) {
            do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
            text[len] = 0;
        }
        if (len + sizeof("…") <= 160) strcat(text, "…");
    }
}

// 用完整标题固定字体来源，截短后也不因缺字判断改变粗细。/ Keep the full title as the font sample so truncation cannot change the face or weight.
static void toc_title(uint8_t *fb, EpdRect box, int preferred_px, const char *text, uint8_t background) {
    char sample[160], shown[160];
    one_line(sample, sizeof(sample), text);
    memcpy(shown, sample, strlen(sample) + 1);
    int px = ui_text_effective_px(preferred_px);
    if (px > box.height - 2) px = box.height - 2;
    px = ui_text_title_fit(shown, px, box.width, sample);
    ui_text_title_background_vc(fb, box.x, box.y + box.height / 2, px, shown, sample,
                                EPD_DRAW_ALIGN_LEFT, background);
}

int book_toc_hit(uint16_t x, uint16_t y, int page, size_t chapters) {
    if (x < 100 && y >= 76 && y < 151) return BOOK_TOC_BACK;
    if (y >= 1018 && y < 1086) {
        if (x >= 36 && x < 136) return BOOK_TOC_SKIP_PREV;
        if (x >= 146 && x < 258) return BOOK_TOC_PREV;
        if (x >= 426 && x < 538) return BOOK_TOC_NEXT;
        if (x >= 548 && x < 648) return BOOK_TOC_SKIP_NEXT;
    }
    if (x >= 36 && x < 648 && y >= 1100 && y < 1174) {
        if(book_navigation_volume_count()) {
            if(s_volume_picker||x<232)return BOOK_TOC_SELECT_VOLUME;
            if(x<248)return -1;
        }
        return BOOK_TOC_JUMP;
    }
    for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
        size_t index = (size_t)page * BOOK_TOC_ROWS + row;
        int target=visible_target(index,chapters);
        if(target==-1)break;
        if (ui_rect_hit(book_toc_row_rect(row), x, y)) return target;
    }
    return -1;
}

int book_toc_jump_hit(uint16_t x, uint16_t y) {
    if (x >= 75 && x < 330 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CANCEL;
    if (x >= 354 && x < 609 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CONFIRM;
    if (x >= 75 && x < 609 && y >= 600 && y < 711) return BOOK_TOC_JUMP_TRACK;
    return -1;
}

int book_toc_jump_percent(uint16_t x) {
    if (x <= 92) return 0;
    if (x >= 592) return 100;
    return ((int)x - 92) * 100 / 500;
}

int book_toc_page_from_percent(int percent, int pages) {
    if (pages <= 1 || percent <= 0) return 0;
    if (percent >= 100) return pages - 1;
    int page = (percent * pages + 99) / 100 - 1;
    return page < pages ? page : pages - 1;
}

// 双像素深色轮廓让白色卡片在墨水屏上仍有清晰边界。/ Dark two-pixel outlines keep white cards legible on e-paper.
static void toc_border(uint8_t *fb, EpdRect rect, int radius, bool selected) {
    uint8_t gray = selected ? 0x38 : 0x60;
    ui_draw_round_rect(fb, rect, radius, gray);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2},
                       radius - 1, gray);
}

static void toc_pill(uint8_t *fb, EpdRect rect, const char *text, int px, bool selected) {
    ui_fill_round_rect(fb, rect, 20, selected ? 0xc0 : UI_GRAY_WHITE);
    toc_border(fb, rect, 20, selected);
    char label[160];
    one_line(label, sizeof(label), text);
    fit_line(label, px, rect.width - 16);
    ui_text_box(fb,ui_inset_rect(rect,8),px,label,EPD_DRAW_ALIGN_CENTER,false);
}

static void volume_chevron(uint8_t *fb,EpdRect r,bool open) {
    for(int i=0;i<8;++i) {
        int x=r.x+24+i,y=r.y+39+i;
        ui_fill_rect_fast((EpdRect){x,y,2,2},0x30,fb);
        x=r.x+24+(open?15-i:i);y=r.y+39+(open?i:15-i);
        ui_fill_rect_fast((EpdRect){x,y,2,2},0x30,fb);
    }
}

void book_toc_render(uint8_t *fb, const char *book_title, size_t chapters,
                     size_t current, int page, const char *message) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, 0xe0, fb);
    ui_nav_status(fb);
    ui_nav_back(fb, TOC_LEFT, 79);
    ui_text_box(fb,(EpdRect){120,79,444,72},34,s_volume_picker?"选择卷":"目录",EPD_DRAW_ALIGN_CENTER,false);

    char title[160];
    one_line(title, sizeof(title), book_title ? book_title : "");
    if (!title[0]) snprintf(title,sizeof(title),"%s","当前书籍");
    toc_title(fb, (EpdRect){TOC_LEFT,160,420,48}, 23, title, 0xe0);
    char total[48];
    size_t volumes=book_navigation_volume_count();
    bool epub=book_kind()==BOOK_KIND_EPUB;
    book_volume_t first_volume;
    bool front=book_navigation_volume(0,&first_volume)&&first_volume.number==0;
    if(volumes)snprintf(total,sizeof(total),"%u 卷",(unsigned)(volumes-front));
    else snprintf(total, sizeof(total), "%u 节", (unsigned)chapters);
    ui_text_box(fb,(EpdRect){482,160,166,48},23,total,EPD_DRAW_ALIGN_RIGHT,false);
    ui_fill_rect_fast((EpdRect){TOC_LEFT, 213, TOC_RIGHT - TOC_LEFT, 2}, 0x68, fb);
    if(volumes) {
        size_t group=book_navigation_volume_for(current);
        char volume[160];
        if(s_volume_picker)
            toc_title(fb,(EpdRect){TOC_LEFT,216,TOC_RIGHT-TOC_LEFT,30},20,"点选卷名，展开该卷目录",0xe0);
        else if(book_navigation_volume_title(group,volume,sizeof(volume))==ESP_OK)
            toc_title(fb,(EpdRect){TOC_LEFT,216,TOC_RIGHT-TOC_LEFT,30},20,volume,0xe0);
    }

    if (message && *message) {
        char info[160];one_line(info,sizeof(info),message);fit_line(info,ui_text_effective_px(23),540);
        ui_text_box(fb,(EpdRect){72,408,540,72},23,info,EPD_DRAW_ALIGN_CENTER,false);
    } else if (!chapters) {
        ui_text_box(fb,(EpdRect){72,408,540,72},25,"这本书没有可用目录",EPD_DRAW_ALIGN_CENTER,false);
    } else {
        for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
            int target=visible_target((size_t)page*BOOK_TOC_ROWS+row,chapters);
            if(target==-1)break;
            EpdRect r = book_toc_row_rect(row);
            if(target<=TOC_VOLUME_HIT) {
                size_t group=(size_t)(-(target+1000));
                uint8_t background=group==s_open_volume?0xd8:UI_GRAY_WHITE;
                ui_fill_round_rect(fb,r,20,background);toc_border(fb,r,20,false);
                volume_chevron(fb,r,group==s_open_volume);
                char title[160],count[32];
                if(book_navigation_volume_title(group,title,sizeof(title))!=ESP_OK)
                    snprintf(title,sizeof(title),"第 %u 卷",(unsigned)group+1);
                toc_title(fb,(EpdRect){r.x+54,r.y+9,416,76},29,title,background);
                snprintf(count,sizeof(count),"%u 节",(unsigned)volume_body_count(group));
                toc_title(fb,(EpdRect){r.x+478,r.y+9,112,76},21,count,background);
                continue;
            }
            size_t index=(size_t)target;
            size_t source_index = book_navigation_chapter(index);
            if (source_index == SIZE_MAX) break;
            bool selected = index == current;
            ui_fill_round_rect(fb, r, 20, selected ? 0xc0 : UI_GRAY_WHITE);
            toc_border(fb, r, 20, selected);
            char raw[160] = "", label[160];
            if (book_navigation_title(index, raw, sizeof(raw)) != ESP_OK || !raw[0])
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            char spine_default[32];
            snprintf(spine_default, sizeof(spine_default), "第 %u 节", (unsigned)source_index + 1);
            if (!epub && strcmp(raw, spine_default) == 0)
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            book_volume_t volume;
            if (epub && volumes && book_navigation_volume(s_open_volume,&volume) &&
                volume.include_title && index==volume.title)
                snprintf(raw,sizeof(raw),"卷首");
            one_line(label, sizeof(label), raw);
            if (!label[0]) snprintf(label, sizeof(label), "第 %u 节", (unsigned)index + 1);
            bool show_number=volumes || !epub || !chapter_label_parse(label,true).kind;
            if (show_number) {
                char number[16];
                if (volumes) snprintf(number,sizeof(number),"·");
                else snprintf(number,sizeof(number),"%02u",(unsigned)index+1);
                ui_text_box(fb,(EpdRect){r.x+14,r.y+11,58,72},21,number,EPD_DRAW_ALIGN_LEFT,false);
            }
            int inset=show_number?79:25;
            toc_title(fb, (EpdRect){r.x+inset,r.y+9,(selected?499:574)-inset,76}, 30, label,
                       selected ? 0xc0 : UI_GRAY_WHITE);
            if (selected)
                ui_text_box(fb,(EpdRect){r.x+r.width-93,r.y+11,71,72},24,"当前",EPD_DRAW_ALIGN_RIGHT,false);
        }
    }
    const int pages = book_toc_pages(chapters);
    toc_pill(fb, (EpdRect){36, 1018, 100, 68}, "« 10页", 24, false);
    toc_pill(fb, (EpdRect){146, 1018, 112, 68}, "‹ 上页", 26, false);
    char position[32]; snprintf(position, sizeof(position), "%02d / %02d", page + 1, pages);
    toc_pill(fb, (EpdRect){268, 1018, 148, 68}, position, 25, true);
    toc_pill(fb, (EpdRect){426, 1018, 112, 68}, "下页 ›", 26, false);
    toc_pill(fb, (EpdRect){548, 1018, 100, 68}, "10页 »", 24, false);
    EpdRect jump = {36, 1100, 612, 74};
    if(volumes) {
        if(s_volume_picker) {
            toc_pill(fb,jump,"返回章节",28,false);
            return;
        }
        toc_pill(fb,(EpdRect){36,1100,196,74},"选择卷",28,false);
        jump=(EpdRect){248,1100,400,74};
        ui_fill_round_rect(fb,jump,21,UI_GRAY_WHITE);toc_border(fb,jump,21,false);
        toc_title(fb,(EpdRect){266,1109,246,56},28,"百分比跳转",UI_GRAY_WHITE);
        char percent[24];snprintf(percent,sizeof(percent),"%d%% ›",(page+1)*100/pages);
        toc_title(fb,(EpdRect){526,1109,104,56},24,percent,UI_GRAY_WHITE);
        return;
    }
    ui_fill_round_rect(fb, jump, 21, UI_GRAY_WHITE);
    toc_border(fb, jump, 21, false);
    ui_text_box(fb,(EpdRect){58,1109,388,56},28,"按百分比跳转",EPD_DRAW_ALIGN_LEFT,false);
    char percent[24];
    snprintf(percent, sizeof(percent), "%d%%  ›", (page + 1) * 100 / pages);
    ui_text_box(fb,(EpdRect){468,1109,158,56},28,percent,EPD_DRAW_ALIGN_RIGHT,false);
}

void book_toc_render_jump(uint8_t *fb, size_t chapters, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    EpdRect card = {44, 356, 596, 572};
    ui_fill_round_rect(fb, card, 30, UI_GRAY_WHITE);
    toc_border(fb, card, 30, true);
    ui_text_box(fb,(EpdRect){(342)-250,393,500,44},32,"跳到目录位置",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){(342)-250,450,500,44},24,"选择百分比，先预览目标位置",
                  EPD_DRAW_ALIGN_CENTER, false);
    char value[24]; snprintf(value, sizeof(value), "%d%%", percent);
    ui_text_fixed(fb, 342, 515, 55, value,EPD_DRAW_ALIGN_CENTER,false);
    ui_fill_round_rect(fb, (EpdRect){92, 637, 500, 8}, 4, 0x80);
    int width = 500 * percent / 100;
    if (width) ui_fill_round_rect(fb, (EpdRect){92, 637, width, 8}, 4, 0x40);
    int knob = 92 + width;
    epd_fill_circle(knob, 641, 25, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, 641, 25, 0x40, fb);
    static const char *ticks[] = {"0", "25", "50", "75", "100%"};
    for (int i = 0; i < 5; ++i)
        ui_text_box(fb,(EpdRect){(92 + i * 125)-250,678,500,44},24,ticks[i],EPD_DRAW_ALIGN_CENTER,false);
    EpdRect target_card = {75, 740, 534, 84};
    ui_fill_round_rect(fb, target_card, 19, 0xe8);
    toc_border(fb, target_card, 19, false);
    ui_text_box(fb,(EpdRect){99,747,486,30},23,"目标位置",EPD_DRAW_ALIGN_LEFT,false);
    int pages = book_toc_pages(chapters);
    int target = book_toc_page_from_percent(percent, pages);
    size_t first = (size_t)target * BOOK_TOC_ROWS + 1;
    size_t last = first + BOOK_TOC_ROWS - 1;
    if (last > visible_count(chapters)) last = visible_count(chapters);
    char summary[160];
    if (chapters)
        snprintf(summary, sizeof(summary), book_navigation_volume_count()?"约第 %d / %d 页 · 第 %u～%u 行":"约第 %d / %d 页 · 第 %u～%u 节",
                 target + 1, pages, (unsigned)first, (unsigned)last);
    else snprintf(summary, sizeof(summary), "这本书没有可用目录");
    fit_line(summary, 25, 486);
    ui_text_box(fb,(EpdRect){99,783,486,34},25,summary,EPD_DRAW_ALIGN_LEFT,false);
    toc_pill(fb, (EpdRect){75, 850, 255, 57}, "取消", 26, false);
    ui_fill_round_rect(fb, (EpdRect){354, 850, 255, 57}, 20, 0x40);
    ui_text_box(fb,(EpdRect){362,858,239,41},26,"跳转",EPD_DRAW_ALIGN_CENTER,true);
}
