/* 中文：EPUB 目录和题头共用的编号识别。/ English: Numbered labels shared by the EPUB TOC and reader. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

// PICO的4MiB应用槽只对目录/题头字符串识别按体积编译，不影响正文栅格与刷屏。
// Size-optimize Pico's heading/TOC string helpers within its 4MiB slots, leaving glyph and display loops alone.
#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

typedef enum {
    CHAPTER_LABEL_NONE, CHAPTER_LABEL_BARE, CHAPTER_LABEL_CHAPTER, CHAPTER_LABEL_VOLUME,
    CHAPTER_LABEL_SECTION, CHAPTER_LABEL_EPISODE, CHAPTER_LABEL_PART,
} chapter_label_kind_t;

typedef struct {
    chapter_label_kind_t kind;
    unsigned number;
    const char *begin, *end, *subtitle;
} chapter_label_t;

static inline const char *chapter_label_space(const char *p) {
    while (*p) {
        if (*p == ' ' || (*p >= '\t' && *p <= '\r')) ++p;
        else if (!strncmp(p, "　", 3)) p += 3;
        else if (!strncmp(p, "\xc2\xa0", 2)) p += 2;
        else break;
    }
    return p;
}

// 只排除完整的书前信息名称，不误伤“序章”或包含“目录”的正文章名。
// Exclude complete front-matter labels, not prologues or body titles containing those words.
static inline bool chapter_label_auxiliary(const char *text) {
    if (!text) return false;
    static const char *const labels[] = {
        "目录", "目次", "总目录", "简目", "细目", "版权页", "版权信息", "简介", "内容简介", "作品简介",
        "作者简介", "前言", "序言", "自序", "译序", "序", "前记", "导读", "引言", "绪论",
        "献辞", "献词", "题献", "编者按", "编者的话", "写在前面", "出版说明", "编辑说明",
        "阅读指南", "凡例", "contents", "tableofcontents", "preface", "foreword", "introduction",
        "dedication", "acknowledgments", "acknowledgements",
    };
    char compact[96];
    size_t used = 0;
    for (const char *p = text; *p;) {
        const char *next = chapter_label_space(p);
        if (next != p) { p = next; continue; }
        if (used + 1 == sizeof(compact)) return false;
        unsigned char c = (unsigned char)*p++;
        compact[used++] = c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
    }
    compact[used] = 0;
    for (size_t i = 0; i < sizeof(labels) / sizeof(*labels); ++i) {
        const size_t length = strlen(labels[i]);
        if (!strncmp(compact, labels[i], length) &&
            (!compact[length] || compact[length] == ':' || !strncmp(compact + length, "：", 3))) return true;
    }
    return false;
}

// Consume one heading separator, including full-width punctuation.
// 读取一个题头分隔符，兼容全角标点。
static inline size_t chapter_label_separator(const char *p) {
    static const char *const wide[] = {"、", "。", "：", "．", "）", "－", "—", "·"};
    if (*p && strchr(".:)-_", *p)) return 1;
    for (size_t i = 0; i < sizeof(wide) / sizeof(*wide); ++i)
        if (!strncmp(p, wide[i], strlen(wide[i]))) return strlen(wide[i]);
    return 0;
}

static inline int chapter_label_numeral(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (*s >= '0' && *s <= '9') { ++*p; return *s - '0'; }
    if (s[0] == 0xef && s[1] == 0xbc && s[2] >= 0x90 && s[2] <= 0x99) {
        *p += 3; return s[2] - 0x90;
    }
    static const char glyphs[] = "零〇一二三四五六七八九两兩壹贰貳叁參肆伍陆陸柒捌玖十拾百佰千仟万萬";
    static const int values[] = {
        0,0,1,2,3,4,5,6,7,8,9,2,2,1,2,2,3,3,4,5,6,6,7,8,9,
        10,10,100,100,1000,1000,10000,10000,
    };
    if (s[0] < 0xe0 || s[0] >= 0xf0 || !s[1] || !s[2]) return -1;
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        if (memcmp(*p, glyphs + i * 3, 3)) continue;
        *p += 3;
        return values[i];
    }
    return -1;
}

static inline bool chapter_label_number(const char **p, unsigned *out) {
    unsigned total = 0, group = 0, digits = 0, count = 0;
    bool have_digits = false;
    for (int n; (n = chapter_label_numeral(p)) >= 0;) {
        if (++count > 12 || digits > 999999u) return false;
        if (n < 10) {
            digits = digits * 10 + (unsigned)n;
            have_digits = true;
        } else {
            if (digits > 9999u) return false;
            if (n == 10000) {
                unsigned section = group + (have_digits ? digits : !group);
                if (section > 9999u) return false;
                total += section * 10000u;
                group = 0;
            } else {
                group += (have_digits ? digits : 1) * (unsigned)n;
            }
            digits = 0;
            have_digits = false;
        }
        if (total > 99999999u || group > 99999999u) return false;
    }
    *out = total + group + digits;
    return count != 0;
}

static inline bool chapter_label_roman(const char **at, unsigned *out) {
    static const char letters[] = "IVXLCDM";
    static const unsigned values[] = {1, 5, 10, 50, 100, 500, 1000};
    const char *p = *at;
    int number = 0, prior = 0;
    while (*p && p - *at < 12) {
        unsigned char c = (unsigned char)*p;
        const char *symbol = strchr(letters, c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        if (!symbol) break;
        int value = (int)values[symbol - letters];
        number += value > prior ? value - 2 * prior : value;
        prior = value;
        ++p;
    }
    if (p == *at || number <= 0 || number > 3999) return false;
    *out = (unsigned)number;
    *at = p;
    return true;
}

// Ordinary paragraphs require a separator or an isolated number; real headings can omit a gap.
// 普通段落须有分隔符或独立编号，正式题头可以连写章名。
static inline chapter_label_t chapter_label_parse(const char *text, bool heading) {
    static const char *const units[] = {"章", "卷", "节", "節", "回", "篇", "部", "集", "册", "冊"};
    static const chapter_label_kind_t kinds[] = {
        CHAPTER_LABEL_CHAPTER, CHAPTER_LABEL_VOLUME, CHAPTER_LABEL_SECTION, CHAPTER_LABEL_SECTION,
        CHAPTER_LABEL_EPISODE, CHAPTER_LABEL_PART, CHAPTER_LABEL_VOLUME, CHAPTER_LABEL_PART,
        CHAPTER_LABEL_VOLUME, CHAPTER_LABEL_VOLUME,
    };
    chapter_label_t label = {0};
    if (!text) return label;
    const char *p = chapter_label_space(text);
    label.begin = p;
    bool ordinal = !strncmp(p, "第", 3);
    bool english = false;
    if (ordinal) p = chapter_label_space(p + 3);
    else if (!strncmp(p, "卷", 3) || !strncmp(p, "部", 3)) {
        label.kind = CHAPTER_LABEL_VOLUME;
        p = chapter_label_space(p + 3);
    } else if (!strncasecmp(p, "chapter", 7) && chapter_label_space(p + 7) != p + 7) {
        label.kind = CHAPTER_LABEL_CHAPTER;
        p = chapter_label_space(p + 7);
        english = true;
    } else if (!strncasecmp(p, "volume", 6) && chapter_label_space(p + 6) != p + 6) {
        label.kind = CHAPTER_LABEL_VOLUME;
        p = chapter_label_space(p + 6);
        english = true;
    } else if (!strncasecmp(p, "part", 4) && chapter_label_space(p + 4) != p + 4) {
        label.kind = CHAPTER_LABEL_VOLUME;
        p = chapter_label_space(p + 4);
        english = true;
    }
    const char *digits = p;
    if (!chapter_label_number(&p, &label.number) &&
        !(english && p == digits && chapter_label_roman(&p, &label.number))) return (chapter_label_t){0};
    if (ordinal) {
        p = chapter_label_space(p);
        for (size_t i = 0; i < sizeof(units) / sizeof(*units); ++i) {
            if (strncmp(p, units[i], 3)) continue;
            label.kind = kinds[i];
            p += 3;
            break;
        }
        if (!label.kind) return (chapter_label_t){0};
    } else if (!label.kind) label.kind = CHAPTER_LABEL_BARE;
    label.end = p;
    const char *after = chapter_label_space(p);
    size_t separator = chapter_label_separator(after);
    bool delimited = separator || after != p;
    while (separator) {
        after = chapter_label_space(after + separator);
        separator = chapter_label_separator(after);
    }
    if (*after && !delimited && (!heading || english || label.kind == CHAPTER_LABEL_BARE))
        return (chapter_label_t){0};
    // A bare number followed by prose needs punctuation, or an actual heading tag.
    // 无章字的编号加标题时，需要分隔标点或实际标题标签。
    if (label.kind == CHAPTER_LABEL_BARE &&
        ((p - digits > 12) || (*after && ((!heading && after == chapter_label_space(p)) ||
          (*after >= '0' && *after <= '9'))))) return (chapter_label_t){0};
    label.subtitle = after;
    return label;
}

static inline bool chapter_label_text_equal(const char *a, const char *b) {
    a = chapter_label_space(a);
    b = chapter_label_space(b);
    while (*a && *b) {
        const char *next_a = chapter_label_space(a), *next_b = chapter_label_space(b);
        if (next_a != a || next_b != b) {
            if (next_a == a || next_b == b) break;
            a = next_a; b = next_b;
            continue;
        }
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*chapter_label_space(a) && !*chapter_label_space(b);
}

static inline bool chapter_label_same_number(chapter_label_t a, chapter_label_t b) {
    return a.kind && b.kind && a.number == b.number &&
        (a.kind == b.kind ||
         (a.kind == CHAPTER_LABEL_BARE && b.kind == CHAPTER_LABEL_CHAPTER) ||
         (b.kind == CHAPTER_LABEL_BARE && a.kind == CHAPTER_LABEL_CHAPTER));
}

// A number-only repetition may omit a subtitle; prose after a matching prefix may not.
// 单独重复章号可以省略副题，只有前缀相同的正文不能跳过。
static inline bool chapter_label_repeats(const char *original, const char *candidate, bool heading) {
    if (chapter_label_text_equal(original, candidate)) return true;
    const chapter_label_t a = chapter_label_parse(original, true);
    chapter_label_t b = chapter_label_parse(candidate, heading);
    if (!b.kind && !heading && a.kind && *a.subtitle) {
        b = chapter_label_parse(candidate, true);
        return chapter_label_same_number(a, b) && chapter_label_text_equal(a.subtitle, b.subtitle);
    }
    return chapter_label_same_number(a, b) &&
        (!*b.subtitle || chapter_label_text_equal(a.subtitle, b.subtitle));
}

#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
