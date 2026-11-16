/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：真实文件名识别回归；不访问书库、不改名，包含用户提供的嵌套作者样本。
 * English: Actual filename regressions without library IO/renames, including supplied nested authors.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "book_title.h"
#include "nvs.h"
static const char *saved_path, *saved_title;

bool book_weread_cover_metadata(const char *path, char *title, size_t title_cap,
                                char *author, size_t author_cap) {
    (void)path; (void)title; (void)title_cap; (void)author; (void)author_cap;
    return false;
}


esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
    assert(mode == NVS_READONLY);
    assert(!strcmp(ns, "rp_titles") || !strcmp(ns, "mt_titles"));
    *handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    assert(handle == 1);
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length) {
    assert(handle == 1 && key[0] == 't');
    if (!saved_path) return ESP_ERR_NVS_NOT_FOUND;
    const size_t path_size = strlen(saved_path) + 1, title_size = strlen(saved_title) + 1;
    assert(*length >= path_size + title_size);
    memcpy(data, saved_path, path_size);
    memcpy((char *)data + path_size, saved_title, title_size);
    *length = path_size + title_size;
    return ESP_OK;
}

static void check(const char *input,const char *title,const char *author){
    char t[512],a[512],original[4096];snprintf(original,sizeof(original),"%s",input);
    assert(book_name_from_path(input,t,sizeof(t),a,sizeof(a)));
    if(strcmp(t,title)||strcmp(a,author)){fprintf(stderr,"input=%s\nwant=%s | %s\ngot=%s | %s\n",input,title,author,t,a);abort();}
    assert(!strcmp(original,input));
    char single[512];assert(book_title_from_path(input,single,sizeof(single))&&!strcmp(single,t));
}
int main(void){
    const char *source=" (z-library.sk, 1lib.sk, z-lib.sk)";
    check("/sdcard/books/沉睡的人 (乔治·佩雷克 Georges Perec) (z-library.sk, 1lib.sk, z-lib.sk).epub","沉睡的人","乔治·佩雷克");
    check("活山 ([英]娜恩·谢泼德 [[英]娜恩·谢泼德]) (z-library.sk, 1lib.sk, z-lib.sk)","活山","娜恩·谢泼德");
    check("我们为何迷恋真实 = This Is Not Who I Am Our Authenticity Obsession ([英] 埃米莉・布特尔 (Emily Bootle) 著  马雅 译) (z-library.sk, 1lib.sk, z-lib.sk).epub","我们为何迷恋真实","埃米莉·布特尔");
    // 用户批量样本：宣传截断不猜作者，未注明职责的两个人名都保留。
    // Supplied batch: truncated promotion does not imply an author; keep both names when roles are unspecified.
    check("津巴多普通心理学(第8版) = Psychology Core Concepts ([美] 菲利普・津巴多 (Philip G. Zimbardo) etc.) (z-library.sk, 1lib.sk, z-lib.sk)","津巴多普通心理学(第8版)","菲利普·津巴多");
    check("县乡生活一场生活方式的巨变 (吕德文,胡晓映) (z-library.sk, 1lib.sk, z-lib.sk)","县乡生活一场生活方式的巨变","吕德文,胡晓映");
    check("银河系边缘的小失常【短篇鬼才凯雷特全新故事集，以色列最高文学奖萨皮尔奖获奖作品，全美犹太人图书奖获奖作品《泰晤士报》《金融时报》纽... (z-library.sk, 1lib.sk, z-lib.sk)","银河系边缘的小失常","");
    check("阅读的艺术 (戴蒙·扬) (z-library.sk, 1lib.sk, z-lib.sk)","阅读的艺术","戴蒙·扬");
    check("其中一个是谎言 (金爱烂,薛舟) (z-library.sk, 1lib.sk, z-lib.sk)_副本","其中一个是谎言","金爱烂,薛舟");
    check("敦煌七窟 (邱华栋) (z-library.sk, 1lib.sk, z-lib.sk)","敦煌七窟","邱华栋");
    check("沉睡的人 (乔治·佩雷克 Georges Perec) (z-library.sk, 1lib.sk, z-lib.sk)","沉睡的人","乔治·佩雷克");
    check("活山（[英]娜恩·谢泼德）（Z-Library）.EPUB","活山","娜恩·谢泼德");
    check("Reader (Some Name) [Z-Library].txt","Reader","Some Name");
    check("重生之女将星 (千山茶客) [ZLib].txt","重生之女将星","千山茶客");
    check("沉睡的人（乔治·佩雷克）【z-lib.sk】.EPUB","沉睡的人","乔治·佩雷克");
    check("Compression Patterns (zlib.net).epub","Compression Patterns (zlib.net)","");
    check("The zlib Handbook.txt","The zlib Handbook","");
    check("敦煌七窟 (邱华栋) (z-library.sk, 1lib.sk, z-lib.sk)_副本.epub","敦煌七窟","邱华栋");
    check("县乡生活一场生活方式的巨变 (吕德文,胡晓映) (z-library.sk, 1lib.sk, z-lib.sk).epub","县乡生活一场生活方式的巨变","吕德文,胡晓映");
    check("The Book (2nd edition) (z-library.sk).epub","The Book (2nd edition)","");
    check("津巴多普通心理学(第8版) = Psychology Core Concepts ([美] 菲利普・津巴多 (Philip G. Zimbardo) etc.) (z-library.sk).epub","津巴多普通心理学(第8版)","菲利普·津巴多");
    check("普通书名 (作者) (another-site.example).epub","普通书名 (作者) (another-site.example)","");
    check("Book (A Novel).epub","Book (A Novel)","");
    check("原名 = Chinese Subtitle.txt","原名 = Chinese Subtitle","");
    check("书名 = 中文副标题 (作者) (Z-Library).txt","书名 = 中文副标题","作者");
    check("假标记 (z-library.sk, other.site).epub","假标记 (z-library.sk, other.site)","");
    check("另一书名 (z-libraryish).txt","另一书名 (z-libraryish)","");
    check("嵌套作者 (Name (Alias)) (Z-Library).epub","嵌套作者","Name (Alias)");
    check("我的..书名 (全三册) (Z-Library).epub","我的..书名 (全三册)","");
    check("v1.2.txt","v1.2","");
    check("银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品】 (作者) (z-library.sk).epub","银河系边缘的小失常","作者");
    check("银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品… (z-library.sk).epub","银河系边缘的小失常","");
    check("银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品 (other.site).epub","银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品 (other.site)","");
    check("银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品 (z-library.sk).epub","银河系边缘的小失常【短篇鬼才全新故事集，最高文学奖获奖作品，图书奖获奖作品","");
    check("书名【第1卷】 (作者) (Z-Library).epub","书名【第1卷】","作者");
    check("活山 (【英】娜恩·谢泼德) (Z-Library).epub","活山","娜恩·谢泼德");
    check("双作者 ([英]张三, [美]李四) (Z-Library).epub","双作者","张三,李四");
    char imported[128]="【英】埃米莉・布特尔 (Emily Bootle) 著  马雅 译";
    assert(book_author_clean_display(imported,sizeof(imported))&&!strcmp(imported,"埃米莉·布特尔"));
    assert(book_author_clean_display(imported,sizeof(imported))&&!strcmp(imported,"埃米莉·布特尔"));
    strcpy(imported,"乔治·佩雷克 Georges Perec");assert(book_author_clean_display(imported,sizeof(imported))&&!strcmp(imported,"乔治·佩雷克"));
    strcpy(imported,"[UK] Emily Bootle");assert(!book_author_clean_display(imported,sizeof(imported))&&!strcmp(imported,"Emily Bootle"));
    char long_name[4096];memset(long_name,'x',180);snprintf(long_name+180,sizeof(long_name)-180," (Long Author)%s.epub",source);
    char title[512],author[128];assert(book_name_from_path(long_name,title,sizeof(title),author,sizeof(author))&&strlen(title)==180&&!strcmp(author,"Long Author"));
    for(size_t cap=1;cap<32;++cap){
        unsigned char guard[40];memset(guard,0x5a,sizeof(guard));
        (void)book_name_from_path("活山 (作者) (Z-Library).txt",(char*)guard,cap,NULL,0);
        assert(guard[cap]==0x5a);assert(memchr(guard,0,cap));
        size_t n=strlen((char*)guard);assert(n==0||n==3||n==6);
    }
    // 有界畸形括号与各字节边界只能回退，不能越界或修改来源。
    // Malformed brackets and byte boundaries only fall back; they must not overrun or mutate input.
    for(size_t n=1;n<2000;n+=7){memset(long_name,'(',n);snprintf(long_name+n,sizeof(long_name)-n,"x)%s.epub",source);assert(book_name_from_path(long_name,title,sizeof(title),author,sizeof(author)));}
    char out[4]="old";assert(!book_name_from_path(NULL,out,sizeof(out),NULL,0)&&!out[0]);
    assert(book_name_from_path(".epub",out,sizeof(out),NULL,0)&&!strcmp(out,".ep"));
    const char *ticket_path = "/sdcard/books/沉睡的人（乔治·佩雷克）【ZLib】.epub";
    assert(book_display_name_from_path(ticket_path, title, sizeof(title), author, sizeof(author)));
    assert(!strcmp(title, "沉睡的人") && !strcmp(author, "乔治·佩雷克"));
    saved_path = ticket_path;
    saved_title = "我的收藏名";
    assert(book_display_name_from_path(ticket_path, title, sizeof(title), author, sizeof(author)));
    assert(!strcmp(title, saved_title) && !strcmp(author, "乔治·佩雷克"));
    saved_path = "/sdcard/books/其他书.epub";
    assert(book_display_name_from_path(ticket_path, title, sizeof(title), author, sizeof(author)));
    assert(!strcmp(title, "沉睡的人") && !strcmp(author, "乔治·佩雷克"));
    puts("PASS: supplied Z-Library names, nested/duplicate author, translator credit, bilingual title, conservative fallbacks, UTF-8 bounds and ticket custom-title precedence; originals unchanged");
}
