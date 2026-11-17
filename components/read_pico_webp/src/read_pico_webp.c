/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：把 libwebp 的 config API 收成一个入口：验证头部、按上限等比缩小、解成 RGB565 平面。
 *
 * English: One entry over libwebp's config API: validate the header, pre-scale to the pixel cap,
 * decode into an RGB565 plane.
 *
 * 冻结：不写文件、不做裁剪/灰度换算、不缓存解码器状态。
 * Frozen: no file writes, no cropping or grayscale math, and no cached decoder state.
 */
#include "read_pico_webp.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "src/webp/decode.h"

bool read_pico_webp_is(const uint8_t *data, size_t size) {
    // 12 字节足够盖住 RIFF 头与 WEBP 四字符码。/ Twelve bytes cover the RIFF header and WEBP fourcc.
    return data && size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4);
}

bool read_pico_webp_info(const uint8_t *data, size_t size, read_pico_webp_info_t *out) {
    if (!read_pico_webp_is(data, size)) return false;
    WebPBitstreamFeatures features;
    memset(&features, 0, sizeof(features));
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) return false;
    if (!features.width || !features.height) return false;
    if (out) {
        *out = (read_pico_webp_info_t){
            .width = (unsigned)features.width,
            .height = (unsigned)features.height,
            .has_alpha = features.has_alpha != 0,
            .has_animation = features.has_animation != 0,
        };
    }
    return true;
}

bool read_pico_webp_rgb565(const uint8_t *data, size_t size, size_t max_pixels,
                           uint8_t **out, unsigned *width, unsigned *height) {
    if (!out || !width || !height) return false;
    *out = NULL;
    *width = *height = 0;
    WebPBitstreamFeatures features;
    if (!read_pico_webp_is(data, size) || WebPGetFeatures(data, size, &features) != VP8_STATUS_OK ||
        features.width <= 0 || features.height <= 0) return false;
    // 动画一帧的尺寸和画布不一定相等，整段解码又不在这一层的预算里；交给占位提示。
    // An animation frame need not match the canvas and whole-stream decoding is outside this
    // layer's budget, so the caller falls back to its placeholder.
    if (features.has_animation) return false;

    if (!max_pixels) max_pixels = READ_PICO_WEBP_DEFAULT_MAX_PIXELS;
    unsigned source_width = features.width, source_height = features.height;
    unsigned scaled_width = source_width, scaled_height = source_height;
    bool scaled = false;
    if ((size_t)source_width * source_height > max_pixels) {
        // 等比缩到上限；只用于省内存，最终尺寸仍由调用方裁剪缩放。
        // Scale down proportionally to the cap; this only saves memory, the final size is still
        // cropped/scaled by the caller.
        const double ratio = sqrt((double)max_pixels /
                                  ((double)source_width * (double)source_height));
        scaled_width = (unsigned)((double)source_width * ratio);
        scaled_height = (unsigned)((double)source_height * ratio);
        if (!scaled_width) scaled_width = 1;
        if (!scaled_height) scaled_height = 1;
        scaled = true;
    }

    const size_t pixels = (size_t)scaled_width * scaled_height;
    if (pixels > SIZE_MAX / 2) return false;
#ifdef ESP_PLATFORM
    // Scaling only bounds the output plane. Lossless and alpha decoding can also keep
    // a source-sized plane, so budget for that separately.
    size_t source_pixels = (size_t)source_width * source_height;
    size_t scratch = source_pixels * (features.format == 2 ? 5u : features.has_alpha ? 2u : 0u);
    size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (scratch + pixels * 2 + 512u * 1024u > available) return false;
#endif
    uint8_t *plane = heap_caps_malloc(pixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!plane) return false;

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) { heap_caps_free(plane); return false; }
    config.options.use_scaling = scaled ? 1 : 0;
    config.options.scaled_width = (int)scaled_width;
    config.options.scaled_height = (int)scaled_height;
    config.output.colorspace = MODE_RGB_565;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = plane;
    config.output.u.RGBA.stride = (int)(scaled_width * 2);
    config.output.u.RGBA.size = pixels * 2;

    const VP8StatusCode status = WebPDecode(data, size, &config);
    if (status != VP8_STATUS_OK) { heap_caps_free(plane); return false; }
    // libwebp 的 RGB565 按字节写 [高字节 rg, 低字节 gb]（与 WEBP_SWAP_16BIT_CSP
    // 的默认一致），本工程的其它解码路径（esp_jpeg）给出的是本机 uint16 顺序。
    // 这里统一成后者，否则 R/G/B 会被拆错，灰阶发色、整体发蓝。
    // libwebp writes RGB565 as bytes [high rg, low gb] (its default byte order), while the tree's
    // other decode path (esp_jpeg) yields native uint16 words. Normalise to the latter, otherwise
    // the channels are split wrong and the grayscale comes out tinted.
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t *pixel = plane + i * 2;
        const uint8_t high = pixel[0];
        pixel[0] = pixel[1];
        pixel[1] = high;
    }
    *out = plane;
    *width = scaled_width;
    *height = scaled_height;
    return true;
}
