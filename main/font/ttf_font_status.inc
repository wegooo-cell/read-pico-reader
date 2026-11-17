/*
 * SPDX-License-Identifier: Apache-2.0
 * Share the status glyph cache across the S31 and Pico TrueType engines.
 */

// Retain only the glyphs needed by the status bar when slot 0 switches to the reader face.
static glyph_entry_t *s_status_glyphs;

static const glyph_entry_t *status_glyph(uint32_t cp, int px) {
    for (const glyph_entry_t *g = s_status_glyphs; g; g = g->hash_next)
        if (g->codepoint == cp && g->size == px) return g;
    return NULL;
}

void ttf_status_capture(const char *signature, int number_px, int signature_px) {
    while (s_status_glyphs) {
        glyph_entry_t *old = s_status_glyphs;
        s_status_glyphs = old->hash_next;
        heap_caps_free(old);
    }
    if (!signature || !font_ready) return;

    size_t remaining = 48 * 1024;
    const char *samples[] = {"0123456789:%-", signature};
    for (int i = 0; i < 2; ++i) {
        const int px = i == 0 ? number_px : signature_px;
        const char *cursor = samples[i];
        warm_runs(px, cursor);
        while (*cursor) {
            uint32_t cp = decode_utf8(&cursor);
            if (status_glyph(cp, px)) continue;
            const glyph_entry_t *g = get_glyph(cp, px);
            if (!g) return;
            size_t size = sizeof(*g) + g->bitmap_bytes;
            if (size > remaining) return;
            glyph_entry_t *copy = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!copy) return;
            *copy = *g;
            if (g->bitmap_bytes) {
                copy->bitmap = (uint8_t *)(copy + 1);
                memcpy(copy->bitmap, g->bitmap, g->bitmap_bytes);
            }
            copy->hash_next = s_status_glyphs;
            s_status_glyphs = copy;
            remaining -= size;
        }
    }
}

bool ttf_status_draw(uint8_t *fb, int x, int y, int px, const char *text,
                     enum EpdFontFlags align) {
    if (!s_status_glyphs) return false;
    int width = 0, above = 0, below = 0;
    for (const char *p = text; *p;) {
        const glyph_entry_t *g = status_glyph(decode_utf8(&p), px);
        if (!g) return false;
        width += g->advance_x;
        if (g->top > above) above = g->top;
        if (g->height - g->top > below) below = g->height - g->top;
    }
    if (align == EPD_DRAW_ALIGN_CENTER) x -= width / 2;
    if (align == EPD_DRAW_ALIGN_RIGHT) x -= width;
    y += (above - below) / 2;
    ttf_cover_lut_init();
    while (*text) {
        const glyph_entry_t *g = status_glyph(decode_utf8(&text), px);
        blit_upright(fb, x, y, g, 0, 15, true);
        x += g->advance_x;
    }
    return true;
}
