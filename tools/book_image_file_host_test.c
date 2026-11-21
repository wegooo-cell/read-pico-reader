/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"
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
int main(int argc, char **argv) {
    assert(argc > 2);
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
