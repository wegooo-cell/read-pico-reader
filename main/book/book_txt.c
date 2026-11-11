/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式TXT编码、分块目录定位及按需标题/正文转码。
 * English: Streaming TXT encoding, paged navigation locations and on-demand title/body conversion.
 * 用户修订：保留2048个旧阅读分段及进度/书签编号；独立目录最多16384项，仅按页分配定位，不加载整本书。
 * User revision: retain2048 legacy reading segments/progress/bookmark IDs; navigation has at most16384 demand-paged locations; never load the whole book.
 */
#include "book_source_internal.h"
#include "book_index_cache.h"
#include "gbk.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#define READ_BLOCK (64 * 1024)
#define PART_TARGET (48 * 1024)
// 两板缓存只重建定位表，不迁移原进度、书签或TXT。/ Rebuild locations on both boards without migrating progress, bookmarks or source TXT.
#define TXT_CACHE_VERSION 5u
#define TXT_LINE_BYTES 192u
#define TXT_TITLE_BYTES 160u
#define NAV_STRONG 1u
#define NAV_SEPARATOR 2u
#define NAV_NUMBER 4u
#define NAV_FRONT 8u
#define NAV_VOLUME 16u
#define NAV_CHAPTER 32u
#define NAV_ENGLISH 64u
#define NAV_ORDINAL_SHIFT 8u
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
typedef struct {
    uint32_t total;
    uint32_t bom;
    uint32_t count;
    uint32_t nav_count;
    uint32_t checksum;
    uint8_t gbk;
    uint8_t reserved[3];
} txt_cache_payload_t;
typedef struct {
    FILE *file; ///< 只读句柄 / Read-only handle
    unsigned char bytes[READ_BLOCK + 4]; ///< 跨块字符余量 / Room for a cross-block character
    size_t pos, used; ///< 缓冲游标 / Buffer cursors
    uint32_t offset; ///< 原文件游标 / Source cursor
    bool gbk; ///< 编码 / Encoding
#ifdef ESP_PLATFORM
    uint8_t refills; ///< 每512KiB让出一次，不给单章加载增加固定等待 / Yield every512KiB, without a fixed per-chapter wait
#endif
#if KIIKO_BOARD_METALIO47
    uint8_t utf16; ///< UTF16字节序 / UTF16 byte order
#endif
} reader_t;

// 验证一个 UTF-8 标量；无效时仅消费首字节。/ Validate one UTF-8 scalar; consume only its first byte on error.
static size_t utf8_one(const unsigned char *s, size_t n, uint32_t *cp) {
    *cp = 0xfffd;
    if (!n) return 0;
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    size_t k = s[0] >= 0xc2 && s[0] <= 0xdf ? 2 : s[0] >= 0xe0 && s[0] <= 0xef ? 3 : s[0] >= 0xf0 && s[0] <= 0xf4 ? 4 : 0;
    if (!k || k > n) return 1;
    uint32_t c = s[0] & (0x7f >> k);
    for (size_t j = 1; j < k; ++j) { if ((s[j] & 0xc0) != 0x80) return 1; c = (c << 6) | (s[j] & 63); }
    if ((k == 2 && c < 0x80) || (k == 3 && c < 0x800) || (k == 4 && c < 0x10000) || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) return 1;
    *cp = c; return k;
}
static size_t encode(uint32_t cp, char out[4]) {
    if (!cp) cp = 0xfffd;
    if (cp < 0x80) { out[0] = cp; return 1; }
    if (cp < 0x800) { out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63); out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}
#if KIIKO_BOARD_METALIO47
static uint16_t utf16_unit(const unsigned char *s, bool be) {
    return be?((uint16_t)s[0]<<8)|s[1]:((uint16_t)s[1]<<8)|s[0];
}
// 不吞掉坏代理项后面的正常字符；奇数末字节也转为替换字符。
// Preserve ordinary characters following bad surrogates; replace an odd trailing byte too.
static size_t utf16_one(const unsigned char *s, size_t n, bool be, uint32_t *cp) {
    *cp=0xfffd;
    if(n<2)return n;
    uint32_t first=utf16_unit(s,be);
    if(first<0xd800||first>0xdfff){*cp=first;return 2;}
    if(first<=0xdbff&&n>=4){
        uint32_t second=utf16_unit(s+2,be);
        if(second>=0xdc00&&second<=0xdfff){*cp=0x10000+((first-0xd800)<<10)+(second-0xdc00);return 4;}
    }
    return 2;
}
static uint8_t utf16_probe(const unsigned char *s,size_t n) {
    unsigned le=0,be=0;
    for(size_t i=0;i+1<n;i+=2){
        unsigned a=s[i],b=s[i+1];
        le+=!b&&(a==9||a==10||a==13||(a>=32&&a<=126));
        be+=!a&&(b==9||b==10||b==13||(b>=32&&b<=126));
    }
    // 无BOM仅在同侧反复出现ASCII零字节结构时识别；模糊文件保留原UTF8/GBK规则。
    // Without a BOM require repeated same-side ASCII/NUL structure; ambiguous input keeps UTF8/GBK detection.
    uint8_t result=le>=4&&le>be*4?1:be>=4&&be>le*4?2:0;
    if(!result)return 0;
    unsigned errors=0,scalars=0;
    for(size_t i=0;i+1<n;){uint32_t cp;size_t k=utf16_one(s+i,n-i,result==2,&cp);
        errors+=cp==0xfffd&&utf16_unit(s+i,result==2)!=0xfffd;++scalars;i+=k;}
    return errors*100>scalars?0:result;
}
#endif
static bool next(reader_t *r, uint32_t *cp) {
    if (r->used - r->pos < 4) {
        size_t tail = r->used - r->pos;
        memmove(r->bytes, r->bytes + r->pos, tail);
        r->used = tail + fread(r->bytes + tail, 1, READ_BLOCK, r->file); r->pos = 0;
#ifdef ESP_PLATFORM
        if((++r->refills&7u)==0)vTaskDelay(1); // 长索引扫描让出调度。/ Yield during long index scans.
#endif
    }
    size_t n = r->used - r->pos;
    if (!n) return false;
    const unsigned char *s = r->bytes + r->pos; size_t k = 1;
#if KIIKO_BOARD_METALIO47
    if(r->utf16)k=utf16_one(s,n,r->utf16==2,cp);
    else
#endif
    if (!r->gbk) k = utf8_one(s, n, cp);
    else if (s[0] < 0x80) *cp = s[0];
    else {
        *cp = 0xfffd;
        if (s[0] >= 0x81 && s[0] <= 0xfe && n > 1 && s[1] >= 0x40 && s[1] <= 0xfe && s[1] != 0x7f) { *cp = gbk_codepoint(s[0], s[1]); k = 2; }
    }
    r->pos += k; r->offset += k; return true;
}
static esp_err_t reset(reader_t *r, book_txt_t *b, uint32_t offset) {
    memset(r, 0, sizeof(*r)); r->file = b->file; r->gbk = b->gbk; r->offset = offset;
#if KIIKO_BOARD_METALIO47
    r->utf16=b->utf16;
#endif
    clearerr(b->file);
    return fseek(b->file, (long)offset, SEEK_SET) ? ESP_FAIL : ESP_OK;
}
// 阅读分段与可见目录分离，旧chapter/UTF8进度不因新增识别规则跳错位置。
// Separate reading segments from visible navigation so new rules cannot shift legacy chapter/UTF8 progress.
typedef struct book_txt_nav {
    uint32_t offset;
    // 建目录时为候选标记；正文载入后复用为该分段内UTF8位置，不增加常驻数组。
    // Candidate flags while indexing; reuse as segment-local UTF8 positions after body load, with no additional resident array.
    union { uint32_t flags, text_offset; };
} txt_nav_t;
typedef struct book_txt_titles {
    struct { size_t key; char text[TXT_TITLE_BYTES]; } slot[8];
    unsigned next;
} txt_titles_t;
typedef struct {
    size_t strong, separated_strong, numeric, consecutive;
    unsigned last_number, front_lines;
    size_t front_strong;
    uint32_t front_offset;
    bool separator, blank, have_number, seen_content, front, front_titles;
} nav_scan_t;

static txt_nav_t *nav_at(const book_txt_t *b, size_t i) {
    return b->navigation[i / TXT_NAV_PAGE] + i % TXT_NAV_PAGE;
}
static void nav_clear(book_txt_t *b) {
    for (size_t i=0; i<TXT_NAV_PAGES; ++i) { free(b->navigation[i]); b->navigation[i]=NULL; }
    b->nav_count=0;
    free(b->volumes);b->volumes=NULL;b->volume_count=0;
}
void book_txt_close(book_txt_t *b) {
    if (b->file) fclose(b->file);
    free(b->entries); free(b->titles); nav_clear(b); memset(b,0,sizeof(*b));
}
static bool nav_add(book_txt_t *b, uint32_t offset, unsigned flags) {
    if (b->nav_incomplete) return false;
    if (b->nav_count>=TXT_NAV_MAX) goto unavailable;
    size_t page=b->nav_count/TXT_NAV_PAGE;
    if (!b->navigation[page]) {
        b->navigation[page]=heap_caps_malloc(TXT_NAV_PAGE*sizeof(txt_nav_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!b->navigation[page]) goto unavailable;
    }
    *nav_at(b,b->nav_count++)=(txt_nav_t){.offset=offset,.flags=flags}; return true;
unavailable:
    // 可见目录不足不阻止阅读；释放候选，保留旧分段，并禁止缓存这次不完整结果。
    // Navigation exhaustion must not block reading; drop candidates, retain legacy segments and do not cache an incomplete result.
    nav_clear(b); b->nav_incomplete=true; return false;
}
static const char *skip_space(const char *s) {
    while (*s==' ' || *s=='\t' || !strncmp(s,"\xe3\x80\x80",3) || !strncmp(s,"\xc2\xa0",2))
        s+=(*s==' '||*s=='\t')?1:(unsigned char)*s==0xc2?2:3;
    return s;
}
static char *trim_line(char *s) {
    s=(char *)skip_space(s);
    size_t n=strlen(s);
    while(n && (s[n-1]==' '||s[n-1]=='\t'))s[--n]=0;
    while(n>=3 && !memcmp(s+n-3,"\xe3\x80\x80",3)){n-=3;s[n]=0;}
    return s;
}
static bool numeral(const char *s, size_t *used) {
    uint32_t cp;size_t n=utf8_one((const unsigned char *)s,strlen(s),&cp);
    char token[5]={0}; if(n)memcpy(token,s,n);
    bool yes=(cp>='0'&&cp<='9') || (cp>=0xff10&&cp<=0xff19) ||
        (n==3&&strstr("零〇一二三四五六七八九十百千万两兩壹贰貳叁參肆伍陆陸柒捌玖拾佰仟廿卅",token));
    *used=n;return yes;
}
// 标题前缀必须在边界结束，避免把正文中的“Chapter123abc”当作目录。
// Require a heading-token boundary so prose such as Chapter123abc cannot become navigation.
static bool title_boundary(const char *s) {
    if (!*s || skip_space(s) != s) return true;
    uint32_t cp; utf8_one((const unsigned char *)s, strlen(s), &cp);
    return (cp < 128 && strchr(":.-_)]!?", (int)cp)) || cp==0xff1a || cp==0x3001 ||
        cp==0x3002 || cp==0xff09 || cp==0x3011 || cp==0x00b7;
}
static bool roman_heading_number(const char *s, size_t n) {
    if (!n || n>15) return false;
    static const char *hundreds[]={"","C","CC","CCC","CD","D","DC","DCC","DCCC","CM"};
    static const char *tens[]={"","X","XX","XXX","XL","L","LX","LXX","LXXX","XC"};
    static const char *ones[]={"","I","II","III","IV","V","VI","VII","VIII","IX"};
    static const char digits[]="IVXLCDM";
    unsigned value=0,previous=0;
    for (size_t i=n;i>0;--i) {
        const char *at=strchr(digits,toupper((unsigned char)s[i-1]));
        if (!at) return false;
        static const unsigned units[]={1,5,10,50,100,500,1000};
        unsigned v=units[at-digits];
        if(v<previous)value-=v;else{value+=v;previous=v;}
    }
    if (!value || value>3999) return false;
    char canonical[16];size_t used=0;
    for(unsigned i=0;i<value/1000;++i)canonical[used++]='M';
    const char *parts[]={hundreds[value/100%10],tens[value/10%10],ones[value%10]};
    for(unsigned i=0;i<3;++i){size_t k=strlen(parts[i]);memcpy(canonical+used,parts[i],k);used+=k;}
    return used==n && !strncasecmp(s,canonical,n);
}
static bool english_heading_number(const char *s) {
    const char *begin=s;
    if(isdigit((unsigned char)*s)) {
        while(isdigit((unsigned char)*s)&&s-begin<9)++s;
        return s-begin<=8 && title_boundary(s);
    }
    while(*s && strchr("IVXLCDMivxlcdm",*s) && s-begin<16)++s;
    return title_boundary(s) && roman_heading_number(begin,(size_t)(s-begin));
}
static bool strong_heading(const char *line, bool isolated) {
    const char *s=skip_space(line);
    if(*s=='#') {
        unsigned hashes=0;while(s[hashes]=='#'&&hashes<4)++hashes;
        if(hashes<=3 && skip_space(s+hashes)!=s+hashes && *skip_space(s+hashes))return true;
    }
    // 有限去掉标题装饰，不能把任意括号对白当作目录。/ Strip bounded decorations, not arbitrary parenthesized dialogue.
    for (unsigned i=0;i<8;++i) {
        uint32_t cp;size_t n=utf8_one((const unsigned char *)s,strlen(s),&cp);
        if (*s=='*'||cp==0x2606||cp==0x2605||cp==0x25c6||cp==0x25c7||cp==0x203b||cp==0x3010||cp==0x3001)
            s=skip_space(s+n);
        else break;
    }
    const char *english[]={"chapter","part"};
    for(unsigned i=0;i<2;++i) {
        size_t n=strlen(english[i]);
        if(!strncasecmp(s,english[i],n) && (skip_space(s+n)!=s+n || isdigit((unsigned char)s[n])))
            return english_heading_number(skip_space(s+n));
    }
    static const char *special[]={"简介","内容简介","序","序言","序章","前言","楔子","引子","尾声","后记","终章","跋","附录","Prologue","Epilogue","Appendix"};
    if (!strncmp(s,"番外",6)) return true;
    for(size_t i=0;i<sizeof(special)/sizeof(*special);++i) {
        size_t n=strlen(special[i]);
        if(!strncasecmp(s,special[i],n) && title_boundary(s+n))return true;
    }
    bool ordinal=!strncmp(s,"第",3);
    if (ordinal) s=skip_space(s+3);
    else if(!strncmp(s,"卷",3)||!strncmp(s,"部",3)||!strncmp(s,"篇",3))s=skip_space(s+3);
    else return false;
    bool number=false;size_t used;
    while(*s && numeral(s,&used)){number=true;s=skip_space(s+used);}
    if(!number)return false;
    if(!ordinal)return title_boundary(s);
    uint32_t cp;size_t n=utf8_one((const unsigned char *)s,strlen(s),&cp);
    char token[5]={0};if(n)memcpy(token,s,n);
    if(n!=3 || !strstr("章节卷回集部篇册",token))return false;
    s+=n;
    if(title_boundary(s))return true;
    // 无分隔紧凑章名仍兼容，但须独立成行、较短且无正文句读。
    // Retain compact unseparated titles only on isolated short lines without prose sentence punctuation.
    return isolated && strlen(s)<=72 && !strpbrk(s,".!?") && !strstr(s,"。") && !strstr(s,"！") && !strstr(s,"？");
}
// 编号只用于分卷推断，不能拿来排序、去重或改变旧阅读分段。
// Ordinals only infer volume boundaries; never sort, deduplicate or change legacy reading segments with them.
static unsigned heading_ordinal(const char **cursor) {
    const char *s=*cursor;unsigned total=0,section=0,digit=0;bool any=false,abbreviated=false;
    size_t n;
    while(*s && numeral(s,&n)) {
        uint32_t cp;utf8_one((const unsigned char *)s,strlen(s),&cp);
        unsigned value=0,unit=0;
        const char *digits[]={"零〇","一壹","二两兩贰貳","三叁參","四肆","五伍","六陆陸","七柒","八捌","九玖"};
        char token[5]={0};memcpy(token,s,n);
        if(cp>='0'&&cp<='9')value=cp-'0';
        else if(cp>=0xff10&&cp<=0xff19)value=cp-0xff10;
        else if(strstr("十拾",token))unit=10;
        else if(strstr("百佰",token))unit=100;
        else if(strstr("千仟",token))unit=1000;
        else if(!strcmp(token,"万"))unit=10000;
        else if(!strcmp(token,"廿"))value=20;
        else if(!strcmp(token,"卅"))value=30;
        else for(unsigned i=0;i<10;++i)if(strstr(digits[i],token)){value=i;break;}
        if(unit==10000){total+=(section+digit?section+digit:1)*unit;section=digit=0;}
        else if(unit){section+=(digit?digit:1)*unit;digit=0;}
        else if(value>=20){digit+=value;abbreviated=true;}
        else if(abbreviated)digit+=value;
        else digit=digit*10+value;
        if(total+section+digit>UINT16_MAX)return 0;
        any=true;s=skip_space(s+n);
    }
    *cursor=s;return any?total+section+digit:0;
}
static unsigned navigation_numbering(const char *line) {
    const char *s=skip_space(line);
    if(*s=='#'){while(*s=='#')++s;s=skip_space(s);}
    for(unsigned i=0;i<8;++i){
        uint32_t cp;size_t n=utf8_one((const unsigned char *)s,strlen(s),&cp);
        if(*s=='*'||cp==0x2606||cp==0x2605||cp==0x25c6||cp==0x25c7||cp==0x203b||cp==0x3010||cp==0x3001)s=skip_space(s+n);
        else break;
    }
    bool volume=false,english_number=false;unsigned number=0;
    const char *english[]={"chapter","part"};
    for(unsigned i=0;i<2;++i) {
        size_t n=strlen(english[i]);
        if(strncasecmp(s,english[i],n) || (skip_space(s+n)==s+n&&!isdigit((unsigned char)s[n])))continue;
        s=skip_space(s+n);const char *begin=s;
        if(isdigit((unsigned char)*s))number=heading_ordinal(&s);
        else {
            while(*s&&strchr("IVXLCDMivxlcdm",*s)&&s-begin<16)++s;
            if(!roman_heading_number(begin,(size_t)(s-begin)))return 0;
            unsigned previous=0;
            static const char roman_digits[]="IVXLCDM";
            for(const char *p=s;p>begin;){
                const char *at=strchr(roman_digits,toupper((unsigned char)*--p));
                static const unsigned values[]={1,5,10,50,100,500,1000};
                unsigned v=values[at-roman_digits];
                if(v<previous)number-=v;else{number+=v;previous=v;}
            }
        }
        if(!number||!title_boundary(s))return 0;
        volume=i==1;english_number=true;goto classified;
    }
    if(!strncmp(s,"第",3)) {
        s=skip_space(s+3);number=heading_ordinal(&s);
        if(!number)return 0;
        if(!strncmp(s,"章",3)||!strncmp(s,"回",3)||!strncmp(s,"节",3))s+=3;
        else if(!strncmp(s,"卷",3)||!strncmp(s,"部",3)||!strncmp(s,"册",3)){volume=true;s+=3;}
        else return 0;
    } else if(!strncmp(s,"卷",3)||!strncmp(s,"部",3)) {
        volume=true;s=skip_space(s+3);number=heading_ordinal(&s);
        if(!number)return 0;
    } else return 0;
classified:
    if(volume) {
        s=skip_space(s);
        const char *ending=strstr(s,"终"),*finished=strstr(s,"完");
        if(strlen(line)>120 || strstr(s,"总结")||strstr(s,"感言")||strstr(s,"完结")||
           strstr(s,"结束")||strstr(s,"后记")||strstr(s,"。")||strstr(s,"！")||strstr(s,"？")||
           strstr(s,"（完）")||strstr(s,"(完)")||
           !strncmp(s,"的",3)||!strncmp(s,"是",3)||!strncmp(s,"还",3)||
           (ending&&title_boundary(ending+3))||(finished&&title_boundary(finished+3)))return 0;
    }
    return (volume?NAV_VOLUME:NAV_CHAPTER)|(english_number?NAV_ENGLISH:0)|(number<<NAV_ORDINAL_SHIFT);
}
static bool part_has_chapters(const book_txt_t *b,size_t at) {
    unsigned chapters=0;
    for(size_t i=at+1;i<b->nav_count&&i<=at+8;++i) {
        unsigned flags=nav_at(b,i)->flags;
        if(flags&NAV_VOLUME)break;
        if(flags&NAV_CHAPTER){if(!(flags&NAV_ENGLISH))break;if(++chapters==2)return true;}
    }
    return false;
}
static bool volume_start(book_txt_t *b,size_t first,size_t title,unsigned number) {
    if(!b->volumes) {
        b->volumes=heap_caps_malloc(TXT_VOLUME_MAX*sizeof(*b->volumes),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!b->volumes)goto incomplete;
        if(first)b->volumes[b->volume_count++]=(book_txt_volume_t){0,0,UINT16_MAX,title==SIZE_MAX?1:0};
    }
    if(b->volume_count && b->volumes[b->volume_count-1].begin==first)return true;
    if(b->volume_count==TXT_VOLUME_MAX)goto incomplete;
    if(b->volume_count)b->volumes[b->volume_count-1].end=(uint16_t)first;
    b->volumes[b->volume_count++]=(book_txt_volume_t){(uint16_t)first,0,
        title==SIZE_MAX?UINT16_MAX:(uint16_t)title,(uint16_t)number};
    return true;
incomplete:
    free(b->volumes);b->volumes=NULL;b->volume_count=0;b->volumes_incomplete=true;return false;
}
static void volumes_finish(book_txt_t *b) {
    unsigned previous=0;
    for(size_t i=0;i<b->nav_count;++i) {
        unsigned flags=nav_at(b,i)->flags,number=flags>>NAV_ORDINAL_SHIFT;
        if(flags&NAV_VOLUME) {
            if(!volume_start(b,i,i,number))break;
            previous=0;continue;
        }
        if(!(flags&NAV_CHAPTER))continue;
        if(number==1 && previous>=5) {
            unsigned expected=2;
            // 只看后续有限目录项，连续确认2、3章；卷标题或其他编号终止确认。
            // Inspect a bounded window for chapters2 and3; another ordinal or explicit volume rejects the inference.
            for(size_t j=i+1;j<b->nav_count && j<=i+8 && expected<=3;++j) {
                unsigned next_flags=nav_at(b,j)->flags;
                if(next_flags&NAV_VOLUME)break;
                if(next_flags&NAV_CHAPTER){if(next_flags>>NAV_ORDINAL_SHIFT!=expected)break;++expected;}
            }
            if(expected==4) {
                unsigned prior=b->volume_count?b->volumes[b->volume_count-1].number:1;
                unsigned ordinal=prior<UINT16_MAX?prior+1:(unsigned)b->volume_count+1;
                if(!volume_start(b,i,SIZE_MAX,ordinal))break;
            }
        }
        previous=number;
    }
    if(b->volume_count)b->volumes[b->volume_count-1].end=(uint16_t)b->nav_count;
}
static bool numeric_heading(const char *s, unsigned *number) {
    if(!isdigit((unsigned char)*s))return false;
    unsigned value=0;
    while(isdigit((unsigned char)*s)){
        if(value>100000)return false;
        value=value*10+(unsigned)(*s++-'0');
    }
    s=skip_space(s);
    if(*s && strncmp(s,"、",3) && *s!='.' && *s!=')' && strncmp(s,"）",3))return false;
    *number=value;return true;
}
static bool separator_line(const char *s) {
    size_t n=strlen(s);if(n<6)return false;
    char c=*s;if(c!='-'&&c!='=')return false;
    while(*s==c)++s;
    return !*s;
}
static void nav_observe(book_txt_t *b,nav_scan_t *scan,uint32_t offset,char *line,bool overflow) {
    char *s=trim_line(line);
    if(!*s){
        if(scan->front_titles)scan->front=false;
        scan->blank=true;return;
    }
    if(!scan->seen_content) {
        scan->seen_content=true;
        if(!strcmp(s,"目录")||!strcasecmp(s,"contents")||!strcasecmp(s,"table of contents")) {
            scan->front=true;scan->front_offset=offset;scan->blank=true;return;
        }
    }
    if(scan->front && (++scan->front_lines>128 || offset-scan->front_offset>32768))scan->front=false;
    if(!overflow&&separator_line(s)){scan->separator=true;scan->blank=true;return;}
    bool strong=strong_heading(s,scan->blank||scan->separator);
    unsigned number=0;
    bool numeric=!overflow && scan->blank && numeric_heading(s,&number);
    unsigned flags=(strong?NAV_STRONG:0)|(scan->separator?NAV_SEPARATOR:0)|(numeric?NAV_NUMBER:0);
    if(strong)flags|=navigation_numbering(s);
    if(scan->front && flags){flags|=NAV_FRONT;scan->front_titles=true;if(strong)++scan->front_strong;}
    if(strong){++scan->strong;if(scan->separator)++scan->separated_strong;}
    if(numeric){
        ++scan->numeric;
        if(scan->have_number && number==scan->last_number+1)++scan->consecutive;
        scan->last_number=number;scan->have_number=true;
    }
    if(flags && (!overflow||strong))nav_add(b,offset,flags);
    scan->separator=false;scan->blank=false;
}
static void nav_finish(book_txt_t *b,const nav_scan_t *scan) {
    unsigned mode=scan->separated_strong>=2 && scan->separated_strong*4>=scan->strong*3 ? NAV_SEPARATOR :
        scan->strong>=2?NAV_STRONG:
        scan->numeric>=3 && (scan->consecutive+1)*4>=scan->numeric*3 ? NAV_NUMBER:0;
    size_t count=0;
    // 只有正文另有足够章节时，才排除卷首清单，避免空目录误伤只有前置列表的文件。
    // Exclude front lists only when body headings independently suffice, preserving ambiguous list-only documents.
    bool discard_front=scan->strong-scan->front_strong>=2;
    // 感言正文里的Part小节不是卷；必须有同一标题体系的后续英文章节。
    // A Part inside author notes is not a volume without following chapters in the same English heading family.
    for(size_t i=0;i<b->nav_count;++i) {
        unsigned flags=nav_at(b,i)->flags;
        if((flags&(NAV_ENGLISH|NAV_VOLUME))==(NAV_ENGLISH|NAV_VOLUME)&&!part_has_chapters(b,i))
            nav_at(b,i)->flags&=~NAV_VOLUME;
    }
    for(size_t i=0;i<b->nav_count;++i) {
        unsigned flags=nav_at(b,i)->flags;
        if(mode && (flags&(mode|NAV_VOLUME)) && (!discard_front || !(flags&NAV_FRONT)))*nav_at(b,count++)=*nav_at(b,i);
    }
    b->nav_count=count;
    volumes_finish(b);
    for(size_t i=0;i<count;++i)nav_at(b,i)->flags&=7u;
    for(size_t page=(count+TXT_NAV_PAGE-1)/TXT_NAV_PAGE;page<TXT_NAV_PAGES;++page){
        free(b->navigation[page]);b->navigation[page]=NULL;
    }
}
size_t book_txt_nav_chapter(const book_txt_t *b,size_t position) {
    if(position>=b->nav_count)return SIZE_MAX;
    uint32_t offset=nav_at(b,position)->offset;
    size_t lo=0,hi=b->count;
    while(lo+1<hi){size_t mid=lo+(hi-lo)/2;if(b->entries[mid].offset<=offset)lo=mid;else hi=mid;}
    return lo;
}
size_t book_txt_nav_source_offset(const book_txt_t *b,size_t position) {
    size_t chapter=book_txt_nav_chapter(b,position);
    return chapter==SIZE_MAX?SIZE_MAX:nav_at(b,position)->offset-b->entries[chapter].offset;
}
size_t book_txt_nav_position(const book_txt_t *b,size_t chapter,size_t text_offset) {
    if(!b->mapped || chapter!=b->mapped_chapter || !b->nav_count)return SIZE_MAX;
    size_t lo=b->mapped_begin,hi=b->mapped_end;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(nav_at(b,mid)->text_offset<=text_offset)lo=mid+1;else hi=mid;}
    return lo?lo-1:0;
}
static size_t title_scalar(const book_txt_t *b,const unsigned char *s,size_t n,uint32_t *cp) {
#if KIIKO_BOARD_METALIO47
    if(b->utf16)return utf16_one(s,n,b->utf16==2,cp);
#endif
    if(!b->gbk)return utf8_one(s,n,cp);
    if(s[0]<0x80){*cp=s[0];return 1;}
    *cp=0xfffd;
    if(n>1&&s[0]>=0x81&&s[0]<=0xfe&&s[1]>=0x40&&s[1]<=0xfe&&s[1]!=0x7f){*cp=gbk_codepoint(s[0],s[1]);return 2;}
    return 1;
}
static esp_err_t copy_title(const char *title,bool navigation,char *buf,size_t cap) {
    size_t n=strlen(title);
    if(n>=cap){
        if(!navigation)return ESP_ERR_INVALID_SIZE;
        n=cap-1;while(n&&((unsigned char)title[n]&0xc0)==0x80)--n;
    }
    memcpy(buf,title,n);buf[n]=0;return ESP_OK;
}
esp_err_t book_txt_title(book_txt_t *b,size_t i,bool navigation,char *buf,size_t cap) {
    size_t count=navigation?b->nav_count:b->count;
    if(!buf||!cap||i>=count)return ESP_ERR_INVALID_ARG;
    buf[0]=0;
    size_t key=i*2+(navigation?1:0);
    if(b->titles)for(unsigned j=0;j<8;++j)if(b->titles->slot[j].key==key){
        return copy_title(b->titles->slot[j].text,navigation,buf,cap);
    }
    uint32_t offset=navigation?nav_at(b,i)->offset:b->entries[i].title_offset;
    char label[TXT_TITLE_BYTES]={0};
    if(offset==UINT32_MAX)snprintf(label,sizeof(label),"Part %u",(unsigned)i+1);
    else {
        if(!b->file)return ESP_FAIL;
        unsigned char raw[512+4];
        clearerr(b->file);
        if(fseek(b->file,(long)offset,SEEK_SET))return ESP_FAIL;
        size_t bytes=fread(raw,1,sizeof(raw),b->file),used=0,limit=navigation?sizeof(label):41;
        if(ferror(b->file)||!bytes)return ESP_FAIL;
        for(size_t at=0;at<bytes;){
            uint32_t cp;size_t k=title_scalar(b,raw+at,bytes-at,&cp);at+=k;
            if(cp=='\n'||cp=='\r')break;
            if(!used && (cp==' '||cp=='\t'||cp==0x3000||cp==0xa0))continue;
            char scalar[4];size_t n=encode(cp,scalar);
            if(used+n>=limit)break;
            memcpy(label+used,scalar,n);used+=n;label[used]=0;
        }
        char *trimmed=trim_line(label);if(trimmed!=label)memmove(label,trimmed,strlen(trimmed)+1);
    }
    size_t n=strlen(label);
    esp_err_t copied=copy_title(label,navigation,buf,cap);
    if(copied!=ESP_OK)return copied;
    if(!b->titles){
        b->titles=heap_caps_malloc(sizeof(*b->titles),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(b->titles){memset(b->titles,0,sizeof(*b->titles));for(unsigned j=0;j<8;++j)b->titles->slot[j].key=SIZE_MAX;}
    }
    if(b->titles){unsigned j=b->titles->next++%8;b->titles->slot[j].key=key;memcpy(b->titles->slot[j].text,label,n+1);}
    return ESP_OK;
}

static bool heading(const char *s) {
    if (!strncmp(s, "Chapter", 7)) {
        s += 7; while (*s == ' ' || *s == '\t') ++s;
        return isdigit((unsigned char)*s) != 0;
    }
    if (strncmp(s, "第", 3)) return false;
    s += 3; bool number = false;
    while (*s) {
        uint32_t cp; size_t n = utf8_one((const unsigned char *)s, strlen(s), &cp);
        char token[5] = {0}; memcpy(token, s, n);
        if ((cp >= '0' && cp <= '9') || (n == 3 && strstr("零一二三四五六七八九十百千万两", token))) { number = true; s += n; }
        else return number && n == 3 && strstr("章节卷回集部篇", token);
    }
    return false;
}
static bool add(book_txt_t *b, uint32_t offset, const char *title) {
    if (b->count == BOOK_CHAPTER_MAX) return false;
    book_entry_t *e = &b->entries[b->count++]; e->offset = offset;
    e->title_offset=title?offset:UINT32_MAX;
    return true;
}
static esp_err_t index_book(reader_t *r, book_txt_t *b) {
    esp_err_t err = reset(r, b, b->bom); if (err) return err;
    char line[TXT_LINE_BYTES+1] = {0}; size_t len = 0; bool overflow = false, too_many = false;
    nav_scan_t scan={.blank=true};
    uint32_t start = b->bom, cp;
    while (next(r, &cp)) {
        if (cp == '\n') {
            if (!overflow && len<=40 && heading(line) && !add(b, start, line)) too_many = true;
            nav_observe(b,&scan,start,line,overflow);
            start = r->offset; len = 0; line[0] = 0; overflow = false;
        } else if (cp != '\r') {
            char bytes[4]; size_t n = encode(cp, bytes);
            if (len + n <= TXT_LINE_BYTES && !overflow) { memcpy(line + len, bytes, n); len += n; line[len] = 0; }
            else overflow = true;
        }
    }
    if (ferror(b->file)) return ESP_FAIL;
    if (!overflow && len<=40 && heading(line) && !add(b, start, line)) too_many = true;
    nav_observe(b,&scan,start,line,overflow);
    nav_finish(b,&scan);
    bool fallback = b->count < 2 || too_many;
    if (b->count) b->entries[0].offset = 0;
    for (size_t i = 0; i < b->count; ++i) {
        uint32_t end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
        if (end - b->entries[i].offset > BOOK_CHAPTER_BYTES_MAX) fallback = true;
    }
    if (!fallback) return ESP_OK;
    b->count = 0; add(b, 0, NULL); err = reset(r, b, b->bom); if (err) return err;
    uint32_t part = 0;
    while (next(r, &cp)) {
        uint32_t size = r->offset - part;
        // 目标后首个换行切段；无换行时在硬上限前按字符切。/ Split at the first newline after the target, or before the hard cap at a character boundary.
        if (r->offset < b->total && ((size >= PART_TARGET && cp == '\n') || size >= BOOK_CHAPTER_BYTES_MAX - 4)) {
            if (!add(b, r->offset, NULL)) return ESP_ERR_INVALID_SIZE;
            part = r->offset;
        }
    }
    return ferror(b->file) ? ESP_FAIL : ESP_OK;
}

static uint32_t index_hash(uint32_t value,const void *bytes,size_t size) {
    const unsigned char *p=bytes;
    while(size--)value=(value^*p++)*UINT32_C(16777619);
    return value;
}
static bool txt_cache_load(const char *source,book_txt_t *b) {
    char path[112];book_index_cache_header_t key;
    if(!book_index_cache_prepare(source,"txt",TXT_CACHE_VERSION,path,sizeof(path),&key))return false;
    FILE *cache=book_index_cache_open_read(path,&key);if(!cache)return false;
    txt_cache_payload_t payload={0};
    bool ok=fread(&payload,1,sizeof(payload),cache)==sizeof(payload) && payload.total==b->total &&
        payload.bom<=3 && payload.bom<=payload.total && payload.count && payload.count<=BOOK_CHAPTER_MAX &&
        payload.nav_count<=TXT_NAV_MAX && payload.gbk<=1;
    unsigned volumes=payload.reserved[1]|((unsigned)payload.reserved[2]<<8);
    ok=ok&&volumes<=TXT_VOLUME_MAX&&(!volumes||payload.nav_count);
#if KIIKO_BOARD_METALIO47
    ok=ok&&payload.reserved[0]<=2&&(!payload.reserved[0]||(!payload.gbk&&(payload.bom==0||payload.bom==2)));
#else
    ok=ok&&!payload.reserved[0];
#endif
    uint32_t expected_hash=payload.checksum;payload.checksum=0;
    uint32_t hash=index_hash(UINT32_C(2166136261),&payload,sizeof(payload));
    if(ok){
        size_t bytes=payload.count*sizeof(*b->entries);
        ok=fread(b->entries,1,bytes,cache)==bytes;
        if(ok)hash=index_hash(hash,b->entries,bytes);
    }
    for(size_t i=0;ok&&i<payload.count;++i){
        const book_entry_t *e=&b->entries[i];
        uint32_t end=i+1<payload.count?b->entries[i+1].offset:b->total;
        ok=(!i?e->offset==0:e->offset>b->entries[i-1].offset) && end>=e->offset &&
           end-e->offset<=BOOK_CHAPTER_BYTES_MAX && end<=b->total &&
           (e->title_offset==UINT32_MAX||(e->title_offset>=e->offset&&e->title_offset<end));
        if(payload.reserved[0])ok=ok&&!(e->offset%2)&&
            (e->title_offset==UINT32_MAX||!(e->title_offset%2));
    }
    for(size_t i=0;ok&&i<payload.nav_count;++i){
        txt_nav_t nav;
        ok=fread(&nav,1,sizeof(nav),cache)==sizeof(nav);
        if(!ok)break;
        hash=index_hash(hash,&nav,sizeof(nav));
        ok=nav.offset>=payload.bom&&nav.offset<b->total &&
            (!i||nav.offset>nav_at(b,i-1)->offset) && nav.flags && !(nav.flags&~7u) &&
            (!payload.reserved[0]||!(nav.offset%2));
        if(ok)ok=nav_add(b,nav.offset,nav.flags);
    }
    if(ok&&volumes) {
        b->volumes=heap_caps_malloc(TXT_VOLUME_MAX*sizeof(*b->volumes),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        ok=b->volumes!=NULL;
        for(size_t i=0;ok&&i<volumes;++i) {
            book_txt_volume_t *v=&b->volumes[i];
            ok=fread(v,1,sizeof(*v),cache)==sizeof(*v);
            if(!ok)break;
            hash=index_hash(hash,v,sizeof(*v));
            ok=v->begin<v->end&&v->end<=payload.nav_count&&
                (i?v->begin==b->volumes[i-1].end:v->begin==0)&&
                (v->title==UINT16_MAX||v->title==v->begin)&&
                (v->number||!i);
            if(ok)++b->volume_count;
        }
        ok=ok&&b->volume_count==volumes&&b->volumes[volumes-1].end==payload.nav_count;
    }
    ok=ok && hash==expected_hash && fgetc(cache)==EOF && !ferror(cache);
    fclose(cache);
    if(!ok){nav_clear(b);b->count=0;return false;}
    b->bom=payload.bom;b->gbk=payload.gbk!=0;b->count=payload.count;
#if KIIKO_BOARD_METALIO47
    b->utf16=payload.reserved[0];
#endif
    return true;
}

// 只读已存在的索引并校验完整载荷，不扫描TXT、不分配目录、不改当前书源。
// Validate an existing index payload without scanning TXT, allocating a TOC or changing the open source.
uint16_t book_txt_cached_chapter_count(const char *source,uint32_t size,int64_t modified) {
    char path[112];book_index_cache_header_t key;
    if(!book_index_cache_prepare_file(source,"txt",TXT_CACHE_VERSION,size,modified,path,sizeof(path),&key))return 0;
    FILE *cache=book_index_cache_open_read(path,&key);if(!cache)return 0;
    txt_cache_payload_t p={0};
    bool ok=fread(&p,1,sizeof(p),cache)==sizeof(p)&&p.total==size&&p.bom<=3&&p.bom<=size&&
        p.count&&p.count<=BOOK_CHAPTER_MAX&&p.nav_count<=TXT_NAV_MAX&&p.gbk<=1;
    unsigned volumes=p.reserved[1]|((unsigned)p.reserved[2]<<8);
    ok=ok&&volumes<=TXT_VOLUME_MAX&&(!volumes||p.nav_count);
#if KIIKO_BOARD_METALIO47
    ok=ok&&p.reserved[0]<=2&&(!p.reserved[0]||(!p.gbk&&(p.bom==0||p.bom==2)));
#else
    ok=ok&&!p.reserved[0];
#endif
    uint32_t expected=p.checksum;p.checksum=0;
    uint32_t hash=index_hash(UINT32_C(2166136261),&p,sizeof(p));
    size_t remaining=ok?p.count*sizeof(book_entry_t)+p.nav_count*sizeof(txt_nav_t)+volumes*sizeof(book_txt_volume_t):0;
    unsigned char bytes[256];
    while(ok&&remaining){size_t n=remaining<sizeof(bytes)?remaining:sizeof(bytes);ok=fread(bytes,1,n,cache)==n;if(ok){hash=index_hash(hash,bytes,n);remaining-=n;}}
    ok=ok&&hash==expected&&fgetc(cache)==EOF&&!ferror(cache);
    fclose(cache);return ok?(uint16_t)p.count:0;
}

static void txt_cache_save(const char *source,const book_txt_t *b) {
    if(b->nav_incomplete||b->volumes_incomplete)return;
    char path[112],temp[120];book_index_cache_header_t key;
    if(!book_index_cache_prepare(source,"txt",TXT_CACHE_VERSION,path,sizeof(path),&key))return;
    txt_cache_payload_t payload={.total=b->total,.bom=b->bom,.count=(uint32_t)b->count,
        .nav_count=(uint32_t)b->nav_count,.gbk=b->gbk};
    payload.reserved[1]=(uint8_t)b->volume_count;payload.reserved[2]=(uint8_t)(b->volume_count>>8);
#if KIIKO_BOARD_METALIO47
    payload.reserved[0]=b->utf16;
#endif
    uint32_t hash=index_hash(UINT32_C(2166136261),&payload,sizeof(payload));
    hash=index_hash(hash,b->entries,b->count*sizeof(*b->entries));
    for(size_t i=0;i<b->nav_count;++i)hash=index_hash(hash,nav_at(b,i),sizeof(txt_nav_t));
    if(b->volume_count)hash=index_hash(hash,b->volumes,b->volume_count*sizeof(*b->volumes));
    payload.checksum=hash;
    FILE *cache=book_index_cache_open_write(path,&key,temp,sizeof(temp));if(!cache)return;
    bool ok=fwrite(&payload,1,sizeof(payload),cache)==sizeof(payload) &&
        fwrite(b->entries,sizeof(*b->entries),b->count,cache)==b->count;
    for(size_t i=0;ok&&i<b->nav_count;i+=TXT_NAV_PAGE){
        size_t count=b->nav_count-i;if(count>TXT_NAV_PAGE)count=TXT_NAV_PAGE;
        ok=fwrite(nav_at(b,i),sizeof(txt_nav_t),count,cache)==count;
    }
    if(ok&&b->volume_count)ok=fwrite(b->volumes,sizeof(*b->volumes),b->volume_count,cache)==b->volume_count;
    (void)book_index_cache_finish_write(cache,temp,path,ok);
}

esp_err_t book_txt_open(book_txt_t *b, const char *path) {
#if KIIKO_BOARD_METALIO47
    b->utf16=0;b->gbk=false;b->bom=0;
#endif
    b->file = fopen(path, "rb"); if (!b->file) return ESP_ERR_NOT_FOUND;
    if (fseek(b->file, 0, SEEK_END)) return ESP_FAIL;
    long size = ftell(b->file);
    if (size < 0 || (unsigned long)size > UINT32_MAX || size > LONG_MAX - 4) return ESP_ERR_INVALID_SIZE;
    b->total = (uint32_t)size;
    b->entries = heap_caps_malloc(sizeof(book_entry_t) * BOOK_CHAPTER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b->entries) return ESP_ERR_NO_MEM;
    if (txt_cache_load(path, b)) return ESP_OK;
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r) return ESP_ERR_NO_MEM;
    esp_err_t err = reset(r, b, 0);
    if (!err) {
        size_t n = fread(r->bytes, 1, READ_BLOCK + 3, b->file);
        if (ferror(b->file)) err = ESP_FAIL;
#if KIIKO_BOARD_METALIO47
        else if(n>=2&&r->bytes[0]==0xff&&r->bytes[1]==0xfe){b->utf16=1;b->bom=2;}
        else if(n>=2&&r->bytes[0]==0xfe&&r->bytes[1]==0xff){b->utf16=2;b->bom=2;}
#endif
        else if (n >= 3 && !memcmp(r->bytes, "\xef\xbb\xbf", 3)) b->bom = 3;
#if KIIKO_BOARD_METALIO47
        else if((b->utf16=utf16_probe(r->bytes,n))!=0){}
#endif
        else for (size_t i = 0; i < n && i < READ_BLOCK;) {
            uint32_t cp; size_t k = utf8_one(r->bytes + i, n - i, &cp);
            if (k == 1 && r->bytes[i] >= 0x80) { b->gbk = true; break; } i += k;
        }
    }
    if (!err) err = index_book(r, b);
    if (!err) txt_cache_save(path, b);
    free(r); return err;
}
esp_err_t book_txt_load_target(book_txt_t *b, size_t i, size_t source_offset,
                               size_t *text_offset, char **utf8, size_t *len) {
    if(!b||!b->file||!utf8||!len||i>=b->count)return ESP_ERR_INVALID_ARG;
    *utf8=NULL;*len=0;if(text_offset)*text_offset=0;
    b->mapped=false;
    uint32_t base=b->entries[i].offset;
    uint32_t start = b->entries[i].offset, end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
    if(source_offset!=SIZE_MAX && source_offset>end-base)return ESP_ERR_INVALID_ARG;
    uint32_t target=source_offset==SIZE_MAX?UINT32_MAX:base+(uint32_t)source_offset;
    if (!start) start = b->bom;
    size_t cap = (size_t)(end - start) * 3 + 1;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out || !r) { free(out); free(r); return ESP_ERR_NO_MEM; }
    esp_err_t err = reset(r, b, start); size_t used = 0; uint32_t cp;
    size_t begin=0,hi=b->nav_count;
    while(begin<hi){size_t mid=begin+(hi-begin)/2;if(nav_at(b,mid)->offset<start)begin=mid+1;else hi=mid;}
    size_t navigation=begin;
    if (!err) while (r->offset < end) {
        if(text_offset && target!=UINT32_MAX && r->offset>=target){*text_offset=used;target=UINT32_MAX;}
        while(navigation<b->nav_count && nav_at(b,navigation)->offset<=r->offset && nav_at(b,navigation)->offset<end)
            nav_at(b,navigation++)->text_offset=(uint32_t)used;
        if(!next(r,&cp))break;
        char bytes[4]; size_t n = encode(cp, bytes);
        // 文件被外部改写时拒绝越过已索引边界。/ Reject crossing indexed bounds if the file changed externally.
        if (r->offset > end || n >= cap - used) { err = ESP_ERR_INVALID_SIZE; break; }
        memcpy(out + used, bytes, n); used += n;
    }
    if (ferror(b->file) || r->offset != end) err = ESP_FAIL;
    if(text_offset && target!=UINT32_MAX && r->offset==target)*text_offset=used;
    free(r);
    if (err) { free(out); return err; }
    b->mapped=true;b->mapped_chapter=i;b->mapped_begin=begin;b->mapped_end=navigation;
    out[used] = 0; *utf8 = out; *len = used; return ESP_OK;
}

esp_err_t book_txt_load(book_txt_t *b,size_t i,char **utf8,size_t *len) {
    return book_txt_load_target(b,i,SIZE_MAX,NULL,utf8,len);
}
