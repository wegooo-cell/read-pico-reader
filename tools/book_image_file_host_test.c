/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"
#include "jpeg_decoder.h"
#include "tjpgd.h"
// 模拟 ESP32-S3 ROM 里 2012 年版 TJpgDec：只接受三分量 YCbCr，灰度 JPEG 返回 JDR_FMT3。
// Emulate the 2012 TJpgDec in the ESP32-S3 ROM, which accepts three-component YCbCr only and
// rejects grayscale JPEGs with JDR_FMT3.
JRESULT host_rom_jd_prepare(JDEC *jd, size_t (*in)(JDEC *, uint8_t *, size_t), void *pool, size_t size, void *dev) {
    JRESULT result = jd_prepare(jd, in, pool, size, dev);
    return result == JDR_OK && jd->ncomp != 3 ? JDR_FMT3 : result;
}
// 本测试不覆盖 WebP；解码器链接需要这个入口。/ WebP is out of scope here; the decoder link needs this entry.
bool read_pico_webp_rgb565(const uint8_t *data, size_t size, size_t max_pixels, uint8_t **out,
                           unsigned *width, unsigned *height) {
    (void)data; (void)size; (void)max_pixels; (void)out; (void)width; (void)height;
    return false;
}
#if !KIIKO_BOARD_METALIO47
// 基线内存测试必须走实际 JPEGDEC 主路径。/ Require the actual JPEGDEC path for baseline RAM tests.
esp_err_t esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    (void)cfg; (void)info;
    abort();
}

esp_err_t esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    (void)cfg; (void)info;
    abort();
}

typedef struct {
    unsigned calls;
    unsigned stop_at;
} checkpoint_t;

static bool keep_decoding(void *context) {
    checkpoint_t *checkpoint = context;
    ++checkpoint->calls;
    return !checkpoint->stop_at || checkpoint->calls < checkpoint->stop_at;
}

static void check_speculative(const char *path, bool png, bool supported) {
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *encoded = malloc((size_t)size);
    assert(encoded && fread(encoded, 1, (size_t)size, file) == (size_t)size);
    fclose(file);

    enum { WIDTH = 684, HEIGHT = 1216, PIXELS = WIDTH * HEIGHT, GUARD = 16 };
    uint8_t *foreground = malloc(PIXELS);
    uint8_t *speculative = malloc(PIXELS + GUARD);
    assert(foreground && speculative);
    memset(speculative, 0xcc, PIXELS + GUARD);
    bool is_png = false;
    unsigned native_width, native_height;
    assert(book_image_speculative_dimensions(encoded, (size_t)size, &is_png,
                                             &native_width, &native_height) == supported);
    assert(is_png == png);
    assert(book_image_grayscale(encoded, (size_t)size, png, WIDTH, HEIGHT, foreground));
    checkpoint_t checkpoint = {0};
    assert(book_image_grayscale_speculative(encoded, (size_t)size, png, WIDTH, HEIGHT,
                                           speculative, keep_decoding, &checkpoint) == supported);
    if (supported) {
        assert(native_width > 0 && native_height > 0);
        assert(checkpoint.calls > 4);
        assert(memcmp(foreground, speculative, PIXELS) == 0);
        uint8_t darkest = UINT8_MAX, lightest = 0;
        for (size_t i = 0; i < PIXELS; ++i) {
            if (foreground[i] < darkest) darkest = foreground[i];
            if (foreground[i] > lightest) lightest = foreground[i];
        }
        assert(lightest - darkest > 32);
        const unsigned final_checkpoint = checkpoint.calls;
        const unsigned stops[] = {1, 4, final_checkpoint};
        for (size_t i = 0; i < sizeof(stops) / sizeof(stops[0]); ++i) {
            checkpoint = (checkpoint_t){.stop_at = stops[i]};
            assert(!book_image_grayscale_speculative(encoded, (size_t)size, png, WIDTH, HEIGHT,
                                                    speculative, keep_decoding, &checkpoint));
            assert(checkpoint.calls >= checkpoint.stop_at);
        }
    }
    for (size_t i = PIXELS; i < PIXELS + GUARD; ++i) assert(speculative[i] == 0xcc);
    free(speculative);
    free(foreground);
    free(encoded);
}
#endif

int main(int argc, char **argv) {
    assert(argc > 2);
#if !KIIKO_BOARD_METALIO47
    if (strcmp(argv[1], "speculate") == 0) {
        assert(argc == 5);
        check_speculative(argv[2], true, true);
        check_speculative(argv[3], false, true);
        check_speculative(argv[4], false, false);
        puts("PASS: full-size RGBA PNG/baseline JPEG foreground parity and cancellation, progressive JPEG on demand");
        return 0;
    }
#endif
    for (int i = 2; i < argc; ++i) {
        bool png = strstr(argv[i], ".png") != NULL;
        unsigned w = 0, h = 0;
        const unsigned ow = 176, oh = 240;
        uint8_t *pixels = malloc((size_t)ow * oh + 16);
        assert(pixels);
        memset(pixels, 0xcc, (size_t)ow * oh + 16);
        if (!strcmp(argv[1], "reject")) {
            assert(!book_image_file_grayscale(argv[i], png, ow, oh, pixels));
        } else {
            assert(book_image_file_dimensions(argv[i], png, &w, &h));
            assert(book_image_file_grayscale(argv[i], png, ow, oh, pixels));
            unsigned sum = 0;
            for (unsigned j = 0; j < ow * oh; ++j) sum += pixels[j];
            assert(sum > 30u * ow * oh && sum < 230u * ow * oh);
            printf("decoded: %s (%ux%u)\n", argv[i], w, h);
        }
        for (unsigned j = 0; j < 16; ++j) assert(pixels[(size_t)ow * oh + j] == 0xcc);
        free(pixels);
    }
}
