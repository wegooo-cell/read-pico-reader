// 两块板共用正文与设置预览的题头绘制。/ Both boards share the heading renderer and picker samples.
#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif
// 排版事务先测候选字号，保存成功后绘制才读取已选字号。/ Layout measures a candidate size before drawing uses the committed preference.
static unsigned s_heading_layout_size;
static void reader_heading_viewport(void) {
    // 章首页与续页使用同一个固定正文区，临时工具栏不能提前截断首段。
    // Chapter starts use the same stable body as continuation pages; chrome only overlays it.
    book_layout_set_chapter_viewport(body_rect().height, 0);
}

// 绘制设计框与正文预留分开；正文按实际题头高度再空一行。/ Separate the design canvas from the measured heading plus one body row.
static int reader_heading_band_height(unsigned size) {
    return size > 120 ? 184 + (int)(size - 120) * 11 / 10 : 184;
}

static int reader_heading_column_px(unsigned size) {
    return 44 * (int)size / 100;
}

static int heading_offset(app_book_heading_align_t align, int space) {
    if (align == APP_BOOK_HEADING_ALIGN_RIGHT) return space;
    if (align == APP_BOOK_HEADING_ALIGN_CENTER) return space / 2;
    return 0;
}

static int fit_heading_line(char *line, size_t size, const char *text, int px, int width, int row_limit) {
    epub_lead_copy(line, size, text, strlen(text));
    int fitted = ui_text_title_fit(line, px, width, text);
    if (fitted > row_limit || (strcmp(line, text) && px > 18)) {
        int smaller = fitted > row_limit ? px * row_limit / fitted : px * 3 / 4;
        epub_lead_copy(line, size, text, strlen(text));
        fitted = ui_text_title_fit(line, smaller, width, text);
    }
    return fitted > row_limit ? row_limit : fitted;
}

static int horizontal_heading_at_size(uint8_t *fb, EpdRect body, const char *title, const char *number,
                                      app_book_heading_style_t style, app_book_heading_align_t align, unsigned size) {
    const int band = reader_heading_band_height(size);
    const int height = body.height < band ? body.height : band;
    const int design_height = height < 184 ? height : 184;
    if (height < 56 || body.width < 48) return 0;

    const bool right = align == APP_BOOK_HEADING_ALIGN_RIGHT;
    const bool sidebar = style == APP_BOOK_HEADING_SIDE;
    const int base_px = 52 * design_height * (int)size / 18400;
    const int rule_thickness = style == APP_BOOK_HEADING_RULE ? (7 * design_height + 183) / 184 : 2;
    const int rule_slot = 13 * design_height / 184 + rule_thickness;
    const int inset = style == APP_BOOK_HEADING_STAIR && number[0] ? 40 * design_height / 184 : 0;
    const int text_width = body.width - (sidebar || style == APP_BOOK_HEADING_RULE ? rule_slot : inset);
    const int gap = design_height / 14;
    const int room = height - 5 - design_height / 18 - rule_thickness;
    const bool paired = !sidebar && title[0] && number[0];
    const int max_px = paired
        ? (style == APP_BOOK_HEADING_RULE ? (room - gap) * 5 / 9 : (room - gap) / 2)
        : room;
    char heading[sizeof(s_chapter_heading_title)], label[sizeof(s_chapter_heading_label)];
    const bool thick_rule = style == APP_BOOK_HEADING_RULE;
    const int label_px = fit_heading_line(label, sizeof(label), number,
        thick_rule ? base_px * 4 / 5 : base_px,
        sidebar && title[0] ? text_width / 2 : text_width,
        paired && thick_rule ? max_px * 4 / 5 : max_px);
    const int label_w = ui_text_title_width_px(label_px, label, number);
    const int title_px = fit_heading_line(heading, sizeof(heading), title, base_px,
        sidebar ? text_width - label_w : text_width, max_px);
    const int title_w = ui_text_title_width_px(title_px, heading, title);
    if (!label_w && !title_w) return 0;

    const int top = body.y + 3;
    const int row = label_w && label_px > title_px ? label_px : title_w ? title_px : label_px;
    const bool stacked = label_w && title_w && !sidebar;
    const int single_y = top + row / 2;
    const int label_y = stacked ? top + label_px / 2 : single_y;
    const int title_y = stacked ? top + label_px + gap + title_px / 2 : single_y;
    const int bottom = title_y + (title_w ? title_px : label_px) / 2;
    const int stack_w = label_w > title_w ? label_w : title_w;

    if (sidebar) {
        if (!fb) return top + row - body.y;
        const int x = body.x + heading_offset(align, body.width - label_w - rule_slot - title_w);
        const int rule_x = x + label_w + (label_w ? rule_slot / 2 : 0);
        const int rule_height = row;
        ui_fill_rect_fast((EpdRect){rule_x, single_y - rule_height / 2, rule_thickness, rule_height},
                          UI_GRAY_BLACK, fb);
        ui_text_title_vc(fb, x, single_y, label_px, label, number, EPD_DRAW_ALIGN_LEFT);
        ui_text_title_vc(fb, x + label_w + rule_slot, single_y, title_px, heading, title, EPD_DRAW_ALIGN_LEFT);
        return top + row - body.y;
    }

    int ink_bottom = bottom;
    if (style == APP_BOOK_HEADING_STAIR) ink_bottom += design_height / 18 + rule_thickness;
    if (style == APP_BOOK_HEADING_FLYLEAF && !stacked) ink_bottom += 4;
    if (!fb) return ink_bottom - body.y;

    int width = stack_w;
    if (style == APP_BOOK_HEADING_RULE) width += rule_slot;
    if (style == APP_BOOK_HEADING_STAIR) width += inset;
    const int x = body.x + heading_offset(align, body.width - width);
    int label_x = x + heading_offset(align, width - label_w);
    int title_x = x + heading_offset(align, width - title_w);

    if (style == APP_BOOK_HEADING_RULE) {
        const int rule_x = right ? x + width - rule_thickness : x;
        const int text_x = right ? x : x + rule_slot;
        label_x = text_x + heading_offset(align, stack_w - label_w);
        title_x = text_x + heading_offset(align, stack_w - title_w);
        ui_fill_rect_fast((EpdRect){rule_x, top, rule_thickness, bottom - top}, UI_GRAY_BLACK, fb);
    } else if (style == APP_BOOK_HEADING_FLYLEAF) {
        const int rule_y = stacked ? top + label_px + gap / 2 : bottom + 3;
        ui_hairline(fb, rule_y, x, width, UI_GRAY_BLACK);
    } else {
        label_x = x + (right ? inset : 0) + heading_offset(align, stack_w - label_w);
        title_x = x + (right ? 0 : inset) + heading_offset(align, stack_w - title_w);
        const int rule_width = base_px < width ? base_px : width;
        ui_fill_rect_fast((EpdRect){x + heading_offset(align, width - rule_width),
                                   bottom + design_height / 18, rule_width, rule_thickness},
                          UI_GRAY_BLACK, fb);
    }

    ui_text_title_vc(fb, label_x, label_y, label_px, label, number, EPD_DRAW_ALIGN_LEFT);
    ui_text_title_vc(fb, title_x, title_y, title_px, heading, title, EPD_DRAW_ALIGN_LEFT);
    return ink_bottom - body.y;
}

static void draw_horizontal_heading_text(uint8_t *fb, EpdRect body, const char *title, const char *number,
                                         app_book_heading_style_t style, app_book_heading_align_t align) {
    (void)horizontal_heading_at_size(fb, body, title, number, style, align, app_settings_book_heading_size());
}

static void draw_horizontal_heading(uint8_t *fb, EpdRect body) {
    draw_horizontal_heading_text(fb, body, s_chapter_heading_title, s_chapter_heading_label,
                                 app_settings_book_heading_style(), app_settings_book_heading_align());
}

typedef struct {
    int width;
    int px;
} reader_heading_t;

static reader_heading_t reader_heading_metrics_at_size(const char *title, const char *number, EpdRect body,
                                               unsigned size) {
    const int columns = (title[0] != 0) + (number[0] != 0);
    if (!columns || body.width < 68 || body.height < 80) return (reader_heading_t){0};
    // 每款使用同样的预留列宽，切换题头不改变阅读进度。/ Keep the reserved columns stable across styles.
    const int gutters = columns == 2 ? 28 : 20;
    int px = (body.width - gutters) / columns;
    const int preferred = reader_heading_column_px(size);
    if (px > preferred) px = preferred;
    return (reader_heading_t){px * columns + gutters, px};
}

static reader_heading_t reader_heading_metrics(const char *title, const char *number, EpdRect body) {
    return reader_heading_metrics_at_size(title, number, body, app_settings_book_heading_size());
}

static int fit_vertical_heading(char *line, size_t size, const char *text, int *px, int extent) {
    epub_lead_copy(line, size, text, strlen(text));
    int height = ttf_vertical_text_height_px(*px, line, 0);
    if (height > extent && *px > 22) {
        *px = *px * extent / height;
        if (*px < 22) *px = 22;
        height = ttf_vertical_text_height_px(*px, line, 0);
    }
    while (height > extent && *line) {
        size_t end = strlen(line) - 1;
        while (end && ((unsigned char)line[end] & 0xc0) == 0x80) --end;
        line[end] = 0;
        height = ttf_vertical_text_height_px(*px, line, 0);
    }
    return height;
}

// 竖排保留从右至左的读序，题头对齐沿文字推进方向。/ Align along the reading axis in vertical text.
static void draw_vertical_heading_style(uint8_t *fb, EpdRect body, reader_heading_t metrics,
                                        const char *title, const char *number) {
    const app_book_heading_style_t style = app_settings_book_heading_style();
    const app_book_heading_align_t align = app_settings_book_heading_align();
    const bool right = align == APP_BOOK_HEADING_ALIGN_RIGHT;
    const int step = style == APP_BOOK_HEADING_STAIR ? 24 : 0;
    const int extent = body.height - 12 - step;
    char heading[sizeof(s_chapter_heading_title)], label[sizeof(s_chapter_heading_label)];
    const int column_px = metrics.px;
    int title_px = column_px;
    int label_px = style == APP_BOOK_HEADING_RULE ? column_px * 4 / 5 : column_px;
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    const int title_h = fit_vertical_heading(heading, sizeof(heading), title, &title_px, extent);
    const int label_h = fit_vertical_heading(label, sizeof(label), number, &label_px, extent);
    int height = title_h > label_h ? title_h : label_h;
    if (!height) return;
    const int left = body.x + body.width - metrics.width;
    const int edge = body.x + body.width;
    const int label_x = edge - 10 - column_px;
    const bool two_columns = *number && *title;
    const int title_x = two_columns ? left + 10 : label_x;
    const int y = body.y + 3;
    int title_y = y, label_y = y;
    const int separator = two_columns ? (title_x + column_px + label_x) / 2 - 1 : left + 4;
    if (style == APP_BOOK_HEADING_RULE) {
        ui_fill_rect_fast((EpdRect){right ? edge - 7 : left + 2, y, 5, height}, UI_GRAY_BLACK, fb);
    } else if (style == APP_BOOK_HEADING_FLYLEAF) {
        int line_height = body.height * 78 / 100;
        if (line_height < height) line_height = height;
        ui_fill_rect_fast((EpdRect){separator, y, 2, line_height}, UI_GRAY_BLACK, fb);
    } else if (style == APP_BOOK_HEADING_STAIR) {
        title_y += right ? 0 : step;
        label_y += right ? step : 0;
        ui_hairline(fb, title_y + title_h + 6, title_x, title_px / 2, UI_GRAY_BLACK);
    } else {
        ui_fill_rect_fast((EpdRect){separator, y, 2, height}, UI_GRAY_BLACK, fb);
    }
    if (label_h) ttf_draw_text_px_vertical(fb, label_x, label_y, label_px, label, 0, 0, 15);
    if (title_h) ttf_draw_text_px_vertical(fb, title_x, title_y, title_px, heading, 0, 0, 15);
}

// 卷页不套章名的线条与对齐，按当前可见正文区域双向居中；横竖排均独占一页。
// Volume dividers use no chapter decoration and center in the visible body, in either writing direction.
static void draw_volume_heading(uint8_t *fb, EpdRect body, const char *title, const char *number, bool vertical) {
    EpdRect visible = reader_area();
    if (body.y < visible.y) { body.height -= visible.y - body.y; body.y = visible.y; }
    if (body.y + body.height > visible.y + visible.height) body.height = visible.y + visible.height - body.y;
    if (body.width < 32 || body.height < 32) return;
    char name[128], label[32];
    int px = 52 * (int)app_settings_book_heading_size() / 100;
    if (vertical) {
        int title_px = px, number_px = px * 4 / 5;
        int height = fit_vertical_heading(name, sizeof(name), title, &title_px, body.height - 16);
        int label_h = fit_vertical_heading(label, sizeof(label), number, &number_px, body.height - 16);
        int gap = height && label_h ? px / 3 : 0;
        int width = (height ? title_px : 0) + (label_h ? number_px : 0) + gap;
        int x = body.x + (body.width - width) / 2;
        if (height) ttf_draw_text_px_vertical(fb, x, body.y + (body.height - height) / 2, title_px, name, 0, 0, 15);
        if (label_h) ttf_draw_text_px_vertical(fb, x + (height ? title_px + gap : 0),
            body.y + (body.height - label_h) / 2, number_px, label, 0, 0, 15);
    } else {
        int limit = body.height / (title[0] && number[0] ? 3 : 2);
        int title_px = fit_heading_line(name, sizeof(name), title, px, body.width - 16, limit);
        int number_px = fit_heading_line(label, sizeof(label), number, px * 4 / 5, body.width - 16, limit);
        int gap = name[0] && label[0] ? px / 3 : 0;
        int height = (name[0] ? title_px : 0) + (label[0] ? number_px : 0) + gap;
        int y = body.y + (body.height - height) / 2;
        if (label[0]) {
            ui_text_title_vc(fb, body.x + body.width / 2, y + number_px / 2, number_px, label, number, EPD_DRAW_ALIGN_CENTER);
            y += number_px + gap;
        }
        if (name[0]) ui_text_title_vc(fb, body.x + body.width / 2, y + title_px / 2,
            title_px, name, title, EPD_DRAW_ALIGN_CENTER);
    }
}

static void draw_reader_flow_heading(uint8_t *fb, EpdRect body, const char *text, size_t len, bool vertical, bool volume) {
    char line[193], title[sizeof(s_chapter_heading_title)], label[sizeof(s_chapter_heading_label)];
    epub_lead_copy(line, sizeof(line), text, len);
    const chapter_label_t parts = chapter_label_parse(line, true);
    book_heading_parts(line, parts, parts.subtitle, title, sizeof(title), label, sizeof(label));
    if (volume) {
        draw_volume_heading(fb, body, title, label, vertical);
    } else if (vertical) {
        reader_heading_t metrics = reader_heading_metrics(title, label, body);
        if (metrics.width) draw_vertical_heading_style(fb, body, metrics, title, label);
    } else {
        body.height = reader_heading_band_height(app_settings_book_heading_size());
        draw_horizontal_heading_text(fb, body, title, label,
                                     app_settings_book_heading_style(), app_settings_book_heading_align());
    }
}

// 测量与绘制共用字形拟合和装饰边界，单行/双行及四款样式不会留下固定空框。
// Measure with the same fitting and decoration bounds so single/paired headings never leave a fixed empty band.
static int measure_reader_flow_heading(EpdRect body, const char *text, size_t len) {
    char line[193], title[sizeof(s_chapter_heading_title)], label[sizeof(s_chapter_heading_label)];
    epub_lead_copy(line, sizeof(line), text, len);
    const chapter_label_t parts = chapter_label_parse(line, true);
    book_heading_parts(line, parts, parts.subtitle, title, sizeof(title), label, sizeof(label));
    return horizontal_heading_at_size(NULL, body, title, label,
        app_settings_book_heading_style(), app_settings_book_heading_align(),
        s_heading_layout_size ? s_heading_layout_size : app_settings_book_heading_size());
}

#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
