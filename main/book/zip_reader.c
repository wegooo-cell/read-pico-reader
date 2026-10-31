/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 验证 ZIP 中央目录与本地头，用 ROM miniz 解压到调用方缓冲。
 * Validate ZIP central/local headers and inflate through ROM miniz into caller buffers.
 *
 * 冻结：只读文件，PSRAM 有界；拒绝加密、真正的 ZIP64、多磁盘及重复路径。
 * Frozen: read-only files and bounded PSRAM; reject encryption, actual ZIP64, multiple disks and duplicate paths.
 */
#include "zip_reader.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if KIIKO_BOARD_METALIO47
#include <sys/stat.h>
#else
#endif
#include "esp_heap_caps.h"
#include "miniz.h"
#if KIIKO_BOARD_METALIO47
#include "metalio_memory.h"
#else
#endif

#define PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define NAME_MAX_BYTES 1024U
#define DIRECTORY_MAX (4U * 1024U * 1024U)
#define ARCHIVE_ENTRY_MAX (256U * 1024U * 1024U)
#define INFLATE_CHUNK 32768U
#define TRAILING_WHITESPACE_MAX 4096U

typedef struct {
    char* name;
    uint32_t offset, packed, unpacked, crc;
    uint16_t method, flags;
} zip_entry_t;

struct zip_reader {
    FILE* file;
#if KIIKO_BOARD_METALIO47
    bool io_suspended;
    struct stat io_stat;
#else
#endif
    zip_entry_t* entries;
    uint16_t* slots;
    uint16_t slot_mask;
    uint32_t size, directory;
    uint16_t count;
#if KIIKO_BOARD_METALIO47
    uint8_t *prefetch;
    size_t prefetch_size,prefetch_read;
    uint32_t prefetch_offset;
    int prefetch_index;
    bool prefetch_ready;
    struct stat prefetch_stat;
#else
#endif
};

static uint32_t name_hash(const char* name) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    return hash;
}

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16); }
static bool read_at(zip_reader_t* z, uint32_t pos, void* dst, size_t size) {
#if KIIKO_BOARD_METALIO47
    if(z->prefetch_ready && pos>=z->prefetch_offset &&
       (size_t)(pos-z->prefetch_offset)<=z->prefetch_size &&
       size<=z->prefetch_size-(size_t)(pos-z->prefetch_offset)) {
        memcpy(dst,z->prefetch+pos-z->prefetch_offset,size);return true;
    }
    return z->file && pos <= z->size && size <= z->size - pos &&
#else
    return pos <= z->size && size <= z->size - pos &&
#endif
        fseek(z->file, (long)pos, SEEK_SET) == 0 && fread(dst, 1, size, z->file) == size;
}

// 只兼容本地头单个全零的16字节ZIP64预留载荷；尺寸仍须由两份32位头一致给出。
// Accept only one zero-filled 16-byte ZIP64 placeholder in a local header, with matching 32-bit sizes.
static bool extras_valid(zip_reader_t* z, uint32_t pos, uint16_t len, bool local_padding) {
    bool padding_seen = false;
    while (len) {
        uint8_t h[4];
        if (len < 4 || !read_at(z, pos, h, 4) || u16(h + 2) > len - 4) return false;
        if (u16(h) == 1) {
            uint8_t reserved[16];
            if (!local_padding || padding_seen || u16(h + 2) != sizeof(reserved) ||
                !read_at(z, pos + 4, reserved, sizeof(reserved))) return false;
            for (size_t i = 0; i < sizeof(reserved); ++i) if (reserved[i]) return false;
            padding_seen = true;
        }
        uint32_t step = 4U + u16(h + 2);
        pos += step;
        len = (uint16_t)(len - step);
    }
    return true;
}

static uint32_t zip_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = UINT32_MAX;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool trailing_whitespace(const uint8_t* data, size_t len) {
    if (len > TRAILING_WHITESPACE_MAX) return false;
    for (size_t i = 0; i < len; ++i)
        if (data[i] != ' ' && data[i] != '\t' && data[i] != '\r' && data[i] != '\n') return false;
    return true;
}

// 尾部容忍：空白（历史行为）、短打包器标记，以及 Metalio 用户书中已知的 [*AT*]+32 hex。
// Trailing tolerance: whitespace (existing), a short packer tag, and the known Metalio
// [*AT*]+32-hex marker. Central-directory/entry bounds and CRC checks are unchanged.
#define TRAILING_PADDING_MAX 64U
static bool trailing_padding(const uint8_t* data, size_t len) {
    if (trailing_whitespace(data, len)) return true;
    if (len <= TRAILING_PADDING_MAX) return true;
#if KIIKO_BOARD_METALIO47
    if (len > TRAILING_WHITESPACE_MAX) return false;
    size_t at = 0;
    while (at < len && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n')) ++at;
    if (len - at < 38 || memcmp(data + at, "[*AT*]", 6)) return false;
    for (size_t i = at + 6; i < at + 38; i++) {
        uint8_t c = data[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return trailing_whitespace(data + at + 38, len - at - 38);
#else
    return false;
#endif
}
void zip_close(zip_reader_t* z) {
    if (!z) return;
#if KIIKO_BOARD_METALIO47
    zip_prefetch_clear(z);
#else
#endif
    if (z->file) fclose(z->file);
    if (z->entries) for (unsigned i = 0; i < z->count; ++i) free(z->entries[i].name);
    free(z->entries);
    free(z->slots);
    free(z);
#if KIIKO_BOARD_METALIO47
}

esp_err_t zip_suspend_io(zip_reader_t *z) {
    if (!z || z->io_suspended) return ESP_OK;
    zip_prefetch_clear(z);
    if (!z->file || fstat(fileno(z->file), &z->io_stat)) return ESP_FAIL;
    int result = fclose(z->file);
    z->file = NULL;
    z->io_suspended = true;
    return result ? ESP_FAIL : ESP_OK;
}

esp_err_t zip_resume_io(zip_reader_t *z, const char *path) {
    if (!z || !z->io_suspended) return ESP_OK;
    FILE *file = path && *path ? fopen(path, "rb") : NULL;
    struct stat now;
    if (!file || fstat(fileno(file), &now) || now.st_size != z->io_stat.st_size ||
        now.st_mtime != z->io_stat.st_mtime) {
        if (file) fclose(file);
        return ESP_FAIL;
    }
    z->file = file;
    z->io_suspended = false;
    return ESP_OK;
#else
#endif
}

esp_err_t zip_open(const char* path, zip_reader_t** out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    zip_reader_t* z = calloc(1, sizeof(*z));
    if (!z) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_SIZE;
    uint8_t* tail = NULL;
    z->file = fopen(path, "rb");
    if (!z->file) { err = ESP_ERR_NOT_FOUND; goto fail; }
    if (fseek(z->file, 0, SEEK_END)) goto fail;
    long size = ftell(z->file);
    if (size < 22 || (uint64_t)size > UINT32_MAX || (uint64_t)size > INT32_MAX) goto fail;
    z->size = (uint32_t)size;
    size_t tail_len = z->size < 22U + UINT16_MAX + TRAILING_WHITESPACE_MAX ?
        z->size : 22U + UINT16_MAX + TRAILING_WHITESPACE_MAX;
    tail = heap_caps_malloc(tail_len, PSRAM);
    if (!tail) { err = ESP_ERR_NO_MEM; goto fail; }
    uint32_t tail_pos = z->size - tail_len;
    if (!read_at(z, tail_pos, tail, tail_len)) goto fail;
    size_t end = tail_len - 22;
    for (;;) {
        if (u32(tail + end) == UINT32_C(0x06054b50)) {
            size_t declared_end = end + 22U + u16(tail + end + 20);
            if (declared_end <= tail_len &&
                trailing_padding(tail + declared_end, tail_len - declared_end)) break;
        }
        if (!end) goto fail;
        --end;
    }
    const uint8_t* eocd = tail + end;
    err = ESP_ERR_NOT_SUPPORTED;
    if (u16(eocd + 4) || u16(eocd + 6) || u16(eocd + 8) != u16(eocd + 10) ||
        u16(eocd + 10) > ZIP_ENTRY_MAX || u32(eocd + 12) == UINT32_MAX || u32(eocd + 16) == UINT32_MAX) goto fail;
    z->count = u16(eocd + 10);
    z->directory = u32(eocd + 16);
    uint32_t dir_size = u32(eocd + 12), eocd_pos = tail_pos + end;
    err = ESP_ERR_INVALID_SIZE;
    if (dir_size > DIRECTORY_MAX || z->directory > eocd_pos || dir_size != eocd_pos - z->directory) goto fail;
    free(tail); tail = NULL;
    if (z->count) {
        z->entries = heap_caps_calloc(z->count, sizeof(*z->entries), PSRAM);
        unsigned slots = 16;
        while (slots < (unsigned)z->count * 2) slots *= 2;
        z->slots = heap_caps_calloc(slots, sizeof(*z->slots), PSRAM);
        z->slot_mask = (uint16_t)(slots - 1);
        if (!z->entries || !z->slots) { err = ESP_ERR_NO_MEM; goto fail; }
    }
    uint32_t pos = z->directory;
    for (unsigned i = 0; i < z->count; ++i) {
        uint8_t h[46];
        err = ESP_ERR_INVALID_SIZE;
        if (pos > eocd_pos || eocd_pos - pos < sizeof(h) || !read_at(z, pos, h, sizeof(h)) || u32(h) != UINT32_C(0x02014b50)) goto fail;
        zip_entry_t* entry = &z->entries[i];
        entry->flags = u16(h + 8); entry->method = u16(h + 10);
        entry->crc = u32(h + 16); entry->packed = u32(h + 20); entry->unpacked = u32(h + 24); entry->offset = u32(h + 42);
        uint16_t name_len = u16(h + 28), extra_len = u16(h + 30), comment_len = u16(h + 32);
        uint32_t record_len = sizeof(h) + (uint32_t)name_len + extra_len + comment_len;
        err = ESP_ERR_NOT_SUPPORTED;
        // 部分制作工具为普通存储或 DEFLATE 条目设置 bit 4；内容仍按 method 与 CRC 验证。
        // Some EPUB packagers set bit 4 on stored/DEFLATE entries; method and CRC still validate the payload.
        if ((entry->flags & ~UINT16_C(0x081e)) || u16(h + 34) ||
            (entry->method != 0 && entry->method != 8) ||
            entry->packed > ARCHIVE_ENTRY_MAX || entry->unpacked > ARCHIVE_ENTRY_MAX) goto fail;
        err = ESP_ERR_INVALID_SIZE;
        if (!name_len || name_len > NAME_MAX_BYTES || record_len > eocd_pos - pos ||
            entry->offset >= z->directory || z->directory - entry->offset < 30 ||
            entry->packed > z->directory - entry->offset - 30 ||
            (entry->method == 0 && entry->packed != entry->unpacked)) goto fail;
        entry->name = heap_caps_malloc((size_t)name_len + 1, PSRAM);
        if (!entry->name) { err = ESP_ERR_NO_MEM; goto fail; }
        if (!read_at(z, pos + 46, entry->name, name_len) || memchr(entry->name, 0, name_len)) goto fail;
        entry->name[name_len] = 0;
        uint16_t slot = (uint16_t)(name_hash(entry->name) & z->slot_mask);
        while (z->slots[slot]) {
            if (!strcmp(entry->name, z->entries[z->slots[slot] - 1].name)) goto fail;
            slot = (uint16_t)((slot + 1) & z->slot_mask);
        }
        z->slots[slot] = (uint16_t)(i + 1);
        if (!extras_valid(z, pos + 46 + name_len, extra_len, false)) goto fail;
        pos += record_len;
    }
    if (pos != eocd_pos) goto fail;
    *out = z;
    return ESP_OK;
fail:
    free(tail);
    zip_close(z);
    return err;
}

int zip_find(const zip_reader_t* z, const char* name) {
    if (z && name && z->slots) {
        uint16_t slot = (uint16_t)(name_hash(name) & z->slot_mask);
        while (z->slots[slot]) {
            int index = z->slots[slot] - 1;
            if (!strcmp(z->entries[index].name, name)) return index;
            slot = (uint16_t)((slot + 1) & z->slot_mask);
        }
    }
    return -1;
}

const char* zip_entry_name(const zip_reader_t* z, int index) {
    return z && index >= 0 && index < z->count ? z->entries[index].name : NULL;
}

size_t zip_entry_count(const zip_reader_t* z) { return z ? z->count : 0; }

size_t zip_entry_size(const zip_reader_t* z, int index) {
    return z && index >= 0 && index < z->count ? z->entries[index].unpacked : 0;
}

static esp_err_t extract_impl(zip_reader_t* z, int index, void* dst, size_t cap, bool prefix) {
    if (!z || index < 0 || index >= z->count) return ESP_ERR_INVALID_ARG;
#if KIIKO_BOARD_METALIO47
    if(z->prefetch_ready) {
        struct stat now;
        if(!z->file||fstat(fileno(z->file),&now)||now.st_size!=z->prefetch_stat.st_size||
           now.st_mtime!=z->prefetch_stat.st_mtime)zip_prefetch_clear(z);
    }
#else
#endif
    const zip_entry_t* entry = &z->entries[index];
    if ((!prefix && (entry->unpacked > ZIP_OUTPUT_MAX || entry->packed > ZIP_INPUT_MAX)) ||
        (!prefix && cap < entry->unpacked) || (prefix && (!cap || cap > 65536)) || (!dst && entry->unpacked)) return ESP_ERR_INVALID_SIZE;
    uint8_t h[30];
    if (!read_at(z, entry->offset, h, sizeof(h)) || u32(h) != UINT32_C(0x04034b50) ||
        u16(h + 6) != entry->flags || u16(h + 8) != entry->method ||
        u32(h + 18) == UINT32_MAX || u32(h + 22) == UINT32_MAX) return ESP_ERR_INVALID_SIZE;
    uint16_t name_len = u16(h + 26), extra_len = u16(h + 28);
    uint32_t header_size = 30U + name_len + extra_len;
    if (name_len != strlen(entry->name) || header_size > z->directory - entry->offset ||
        entry->packed > z->directory - entry->offset - header_size) return ESP_ERR_INVALID_SIZE;
    if (!(entry->flags & 8) && (u32(h + 14) != entry->crc || u32(h + 18) != entry->packed || u32(h + 22) != entry->unpacked)) return ESP_ERR_INVALID_SIZE;
    // 名字最长 1024 字节；留在栈上的话下面的 inflate 就顶着它跑，放 PSRAM，用完立刻还。
    // The name runs to 1024 bytes; on the stack the inflate below would sit on top of it, so it
    // lives in PSRAM and is handed back before the inflate starts.
    uint8_t* name = heap_caps_malloc(NAME_MAX_BYTES, PSRAM);
    if (name == NULL) return ESP_ERR_NO_MEM;
    bool name_ok = read_at(z, entry->offset + 30, name, name_len) &&
                   memcmp(name, entry->name, name_len) == 0 &&
                   extras_valid(z, entry->offset + 30 + name_len, extra_len, !(entry->flags & 8));
    free(name);
    if (!name_ok) return ESP_ERR_INVALID_SIZE;
    uint32_t data_pos = entry->offset + header_size;
    size_t target = prefix && cap < entry->unpacked ? cap : entry->unpacked;
    uint8_t empty_output;
    uint8_t* output = dst ? dst : &empty_output;
    if (entry->method == 0) {
        if (!read_at(z, data_pos, output, target)) return ESP_FAIL;
    } else {
        // 尺寸探测用小输入块，避免32字节输出也从TF读满32KiB；完整提取保留原吞吐。
        // Use small input chunks for advisory probes; full extraction retains its bulk read size and CRC.
        const size_t input_cap = prefix ? (target <= 512 ? 512 : 4096) : INFLATE_CHUNK;
        uint8_t* input = heap_caps_malloc(input_cap, PSRAM);
        tinfl_decompressor* state = heap_caps_malloc(sizeof(*state), PSRAM);
        if (!input || !state) { free(input); free(state); return ESP_ERR_NO_MEM; }
#if KIIKO_BOARD_METALIO47
        const uint8_t *packed=z->prefetch_ready&&z->prefetch_index==index?
            z->prefetch+data_pos-z->prefetch_offset:NULL;
        bool ok = packed || fseek(z->file, (long)data_pos, SEEK_SET) == 0;
#else
        bool ok = fseek(z->file, (long)data_pos, SEEK_SET) == 0;
#endif
        if (ok) {
            tinfl_init(state);
            size_t remaining = entry->packed, buffered = 0, consumed = 0, produced = 0;
            while (ok) {
                if (consumed == buffered && remaining) {
                    buffered = remaining < input_cap ? remaining : input_cap;
#if KIIKO_BOARD_METALIO47
                    if(packed)memcpy(input,packed+entry->packed-remaining,buffered);
                    else if (fread(input, 1, buffered, z->file) != buffered) { ok = false; break; }
#else
                    if (fread(input, 1, buffered, z->file) != buffered) { ok = false; break; }
#endif
                    remaining -= buffered;
                    consumed = 0;
                }
                size_t in_size = buffered - consumed;
                size_t out_size = target - produced;
                tinfl_status status = tinfl_decompress(state, input + consumed, &in_size,
                    output, output + produced, &out_size,
                    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF |
                    (remaining ? TINFL_FLAG_HAS_MORE_INPUT : 0));
                consumed += in_size;
                produced += out_size;
                if (prefix && produced == target && status >= 0) { ok = true; break; }
                if (status == TINFL_STATUS_DONE) {
                    ok = !remaining && consumed == buffered && produced == entry->unpacked;
                    break;
                }
                if (status < 0 || (status == TINFL_STATUS_NEEDS_MORE_INPUT &&
                                   !remaining && consumed == buffered) ||
                    (status == TINFL_STATUS_HAS_MORE_OUTPUT && produced == entry->unpacked) ||
                    (!in_size && !out_size && consumed != buffered)) {
                    ok = false; break;
                }
            }
        }
        free(input); free(state);
        if (!ok) return ESP_ERR_INVALID_SIZE;
    }
    if (prefix && target < entry->unpacked) return ESP_OK;
    return zip_crc32(output, entry->unpacked) == entry->crc ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t zip_extract(zip_reader_t *z, int index, void *dst, size_t cap) { return extract_impl(z, index, dst, cap, false); }
esp_err_t zip_extract_prefix(zip_reader_t *z, int index, void *dst, size_t cap) { return extract_impl(z, index, dst, cap, true); }
#if KIIKO_BOARD_METALIO47

void zip_prefetch_clear(zip_reader_t *z) {
    if(!z)return;
    free(z->prefetch);z->prefetch=NULL;z->prefetch_size=z->prefetch_read=0;
    z->prefetch_index=-1;z->prefetch_ready=false;
}
int zip_prefetch_step(zip_reader_t *z,int index) {
    if(!z||!z->file||index<0||index>=z->count)return -1;
    if(z->prefetch&&z->prefetch_index==index&&z->prefetch_ready)return 1;
    if(!z->prefetch||z->prefetch_index!=index) {
        zip_prefetch_clear(z);
        const zip_entry_t *e=&z->entries[index];uint8_t h[30];
        if(!read_at(z,e->offset,h,sizeof(h))||u32(h)!=UINT32_C(0x04034b50))return -1;
        size_t header=30u+u16(h+26)+u16(h+28);
        if(e->offset>z->directory||header>z->directory-e->offset||
           e->packed>z->directory-e->offset-header)return -1;
        size_t need=header+e->packed;
        if(need>KIIKO_BOARD_ZIP_PREFETCH_KB*1024u||!metalio_cache_can_grow(need))return -1;
        if(fstat(fileno(z->file),&z->prefetch_stat))return -1;
        z->prefetch=heap_caps_malloc(need,PSRAM);if(!z->prefetch)return -1;
        z->prefetch_size=need;z->prefetch_index=index;z->prefetch_offset=e->offset;
    }
    size_t remaining=z->prefetch_size-z->prefetch_read;
    size_t n=remaining>4096?4096:remaining;
    if(!read_at(z,z->prefetch_offset+z->prefetch_read,z->prefetch+z->prefetch_read,n)) {
        zip_prefetch_clear(z);return -1;
    }
    z->prefetch_read+=n;
    if(z->prefetch_read==z->prefetch_size) {
        struct stat now;
        if(fstat(fileno(z->file),&now)||now.st_size!=z->prefetch_stat.st_size||
           now.st_mtime!=z->prefetch_stat.st_mtime){zip_prefetch_clear(z);return -1;}
        z->prefetch_ready=true;return 1;
    }
    return 0;
}
#else
#endif
