/* SPDX-License-Identifier: Apache-2.0 */
#include "book_cover.h"
#include <string.h>

bool book_jpeg_frame(const uint8_t *data, size_t size, uint8_t *sof,
                       unsigned *width, unsigned *height) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t at = 2;
    while (at + 1 < size) {
        if (data[at] != 0xFF) { ++at; continue; }   // 段间的填充字节 / fill bytes between segments
        uint8_t marker = data[at + 1];
        if (marker == 0xFF) { ++at; continue; }
        // 无长度字段的标记。/ Markers without a length field.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) { at += 2; continue; }
        if (at + 4 > size) return false;
        size_t length = ((size_t)data[at + 2] << 8) | data[at + 3];
        if (length < 2) return false;
        // SOF0..SOF15，跳过 DHT(0xC4)、JPG(0xC8)、DAC(0xCC)。
        // SOF0..SOF15, skipping DHT (0xC4), JPG (0xC8) and DAC (0xCC).
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (at + 9 > size) return false;
            if (height) *height = ((unsigned)data[at + 5] << 8) | data[at + 6];
            if (width) *width = ((unsigned)data[at + 7] << 8) | data[at + 8];
            if (sof) *sof = marker;
            return true;
        }
        at += 2 + length;
    }
    return false;
}

bool book_image_dimensions(const uint8_t *data, size_t size, bool png,
                           unsigned *width, unsigned *height) {
    if (!data || !width || !height) return false;
    *width = *height = 0;
    // WebP 先按签名认，png 标记只描述 PNG；解码入口同样按签名分派。
    // WebP is identified by its signature first; the png flag only describes PNG, and the decode
    // entry dispatches on the signature as well.
    if (book_webp_is(data, size)) {
        if (!book_webp_frame(data, size, width, height)) return false;
        return *width > 0 && *height > 0 && *width <= 8192 && *height <= 8192;
    }
    // 内嵌 GIF（正文图标/小插图）用逻辑屏幕尺寸参与排版；gif 到达时 png=false。
    // Inline GIFs use the logical screen size for pagination; they arrive with png=false.
    if (book_gif_is(data, size)) {
        if (!book_gif_frame(data, size, width, height)) return false;
        return *width > 0 && *height > 0 && *width <= 8192 && *height <= 8192;
    }
    if (png) {
        static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
        if (size < 24 || memcmp(data, signature, 8) || memcmp(data + 12, "IHDR", 4)) return false;
        *width = (uint32_t)data[16] << 24 | (uint32_t)data[17] << 16 | (uint32_t)data[18] << 8 | data[19];
        *height = (uint32_t)data[20] << 24 | (uint32_t)data[21] << 16 | (uint32_t)data[22] << 8 | data[23];
    } else {
        unsigned frame_width = 0, frame_height = 0;
        // 帧头对基线、渐进式都能给出尺寸，而 esp_jpeg 只认基线。
        // The frame header yields the size for baseline and progressive alike, where esp_jpeg
        // only handles baseline.
        if (!book_jpeg_frame(data, size, NULL, &frame_width, &frame_height)) return false;
        *width = frame_width; *height = frame_height;
    }
    return *width > 0 && *height > 0 && *width <= 8192 && *height <= 8192;
}

/* ---- WebP 头 / WebP header ---- */

static uint32_t webp_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool book_webp_is(const uint8_t *data, size_t size) {
    return data && size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4);
}

// 按 RIFF 块走到第一个能给尺寸的块：VP8X 画布、VP8（有损帧头）、VP8L（无损位头）。
// Walk the RIFF chunks to the first one that carries dimensions: the VP8X canvas, the VP8 lossy
// frame header, or the VP8L lossless bit header.
bool book_webp_frame(const uint8_t *data, size_t size, unsigned *width, unsigned *height) {
    if (!book_webp_is(data, size)) return false;
    size_t at = 12;
    while (at + 8 <= size) {
        const uint8_t *tag = data + at;
        const uint32_t length = webp_le32(data + at + 4);
        const size_t payload = at + 8;
        if (!memcmp(tag, "VP8X", 4)) {
            if (length < 10 || size - payload < 10) return false;
            const unsigned w = (unsigned)data[payload + 4] | (unsigned)data[payload + 5] << 8 |
                               (unsigned)data[payload + 6] << 16;
            const unsigned h = (unsigned)data[payload + 7] | (unsigned)data[payload + 8] << 8 |
                               (unsigned)data[payload + 9] << 16;
            if (width) *width = w + 1;
            if (height) *height = h + 1;
            return true;
        }
        if (!memcmp(tag, "VP8 ", 4)) {
            // 3 字节帧标签 + 0x9d 0x01 0x2a 起始码 + 14 位宽 + 14 位高。
            // Three-byte frame tag, the 0x9d 0x01 0x2a start code, then 14-bit width and height.
            if (length < 10 || size - payload < 10 || data[payload + 3] != 0x9d || data[payload + 4] != 0x01 ||
                data[payload + 5] != 0x2a) return false;
            const unsigned w = ((unsigned)data[payload + 6] | (unsigned)data[payload + 7] << 8) & 0x3fff;
            const unsigned h = ((unsigned)data[payload + 8] | (unsigned)data[payload + 9] << 8) & 0x3fff;
            if (!w || !h) return false;
            if (width) *width = w;
            if (height) *height = h;
            return true;
        }
        if (!memcmp(tag, "VP8L", 4)) {
            if (length < 5 || size - payload < 5 || data[payload] != 0x2f) return false;
            const uint32_t bits = webp_le32(data + payload + 1);
            if (width) *width = (unsigned)(bits & 0x3fff) + 1;
            if (height) *height = (unsigned)((bits >> 14) & 0x3fff) + 1;
            return true;
        }
        if (length > size - payload) return false;
        const size_t next = payload + length + (length & 1u);  // 块按偶数字节对齐 / chunks are even-padded
        if (next <= at) return false;
        at = next;
    }
    return false;
}

/* ---- GIF 头 / GIF header ---- */

bool book_gif_is(const uint8_t *data, size_t size) {
    return data && size >= 13 &&
        (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6));
}

// 逻辑屏幕描述符紧跟 6 字节签名，宽高为小端 16 位，不解帧数据。
// The logical screen descriptor follows the 6-byte signature; width/height are little-endian
// 16-bit values and no frame data is decoded.
bool book_gif_frame(const uint8_t *data, size_t size, unsigned *width, unsigned *height) {
    if (!book_gif_is(data, size)) return false;
    const unsigned w = data[6] | ((unsigned)data[7] << 8);
    const unsigned h = data[8] | ((unsigned)data[9] << 8);
    if (!w || !h || w > 8192 || h > 8192) return false;
    if (width) *width = w;
    if (height) *height = h;
    return true;
}
