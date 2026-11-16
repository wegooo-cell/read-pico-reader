#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
typedef bool (*ttf_face_draw_fn)(void *);
bool ttf_font_draw_with_path(const char *path, ttf_face_draw_fn draw, void *context);
const char *ttf_font_path(void);
bool ttf_font_path_is_builtin(const char *path);
int ttf_font_selected(void);
bool ttf_font_slot_ready(int slot);
bool ttf_font_slot_has_text(int slot, const char *text);
bool ttf_get_antialias(void);
void ttf_set_antialias(bool enabled);
void ttf_measure_line_px(int px, const char *text, int *above, int *below);
int ttf_text_width_px(int px, const char *text);
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg);
