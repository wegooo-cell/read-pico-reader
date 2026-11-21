/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：真实文件系统回归首次收录、迁移、删除再导入和损坏尾部回退。
 * English: Real-filesystem first-added, migration, reimport and damaged-tail regressions.
 */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char test_sd[100], test_flash[100];
#define BOOK_ADDED_SD_ROOT test_sd
#define BOOK_ADDED_FLASH_ROOT test_flash
#include "../main/book/book_added.c"
int main(void) {
    char temp[]="/tmp/kiiko-added-XXXXXX";
    assert(mkdtemp(temp));
    snprintf(test_sd,sizeof(test_sd),"%s/sd",temp);
    snprintf(test_flash,sizeof(test_flash),"%s/flash",temp);
    assert(!mkdir(test_sd,0700)&&!mkdir(test_flash,0700));
    char a[288],b[288],c[288],moved[288],journal[288];
    snprintf(a,sizeof(a),"%s/甲.epub",test_sd);
    snprintf(b,sizeof(b),"%s/乙.txt",test_sd);
    snprintf(c,sizeof(c),"%s/丙.epub",test_flash);
    snprintf(moved,sizeof(moved),"%s/新目录/甲.epub",test_sd);
    uint64_t x=0,y=0,z=0;
    book_added_item_t items[]={{b,&y},{c,&z},{a,&x}};
    book_added_load(items,3);
    assert(x&&y&&z&&x!=y&&x!=z&&y!=z);
    uint64_t old_x=x,old_y=y,old_z=z;
    x=y=z=0;book_added_load(items,3);
    assert(x==old_x&&y==old_y&&z==old_z);
    assert(book_added_migrate(a,moved));
    uint64_t moved_order=0;book_added_item_t one={moved,&moved_order};
    book_added_load(&one,1);assert(moved_order==old_x);
    book_added_forget(a);
    uint64_t recreated=0;book_added_item_t original_again={a,&recreated};book_added_load(&original_again,1);
    assert(recreated>old_x&&moved_order==old_x);
    // Restore the source fixture identity for the independent damaged-tail tests below.
    assert(write_identity(a,old_x));
    book_added_forget(b);y=0;book_added_item_t again={b,&y};
    book_added_load(&again,1);assert(y>old_x&&y>old_y&&y>old_z);
    assert(journal_path(test_sd,journal));
    FILE *f=fopen(journal,"ab");assert(f&&fputc('K',f)=='K'&&!fclose(f));
    struct stat before,after;assert(!stat(journal,&before));
    uint64_t missing=0;char unknown[288];snprintf(unknown,sizeof(unknown),"%s/未收录.epub",test_sd);
    book_added_item_t damaged[]={{a,&x},{unknown,&missing}};x=0;
    book_added_load(damaged,2);
    assert(x==old_x&&!missing&&!stat(journal,&after)&&before.st_size==after.st_size);
    book_added_forget(a);assert(!stat(journal,&after)&&before.st_size==after.st_size);
    assert(!unlink(journal));
    assert(journal_path(test_flash,journal)&&!unlink(journal));
    char escaped[288];snprintf(escaped,sizeof(escaped),"%s-evil/book.epub",test_sd);
    assert(!medium(escaped));
    uint64_t invalid=0;book_added_item_t outside={escaped,&invalid};book_added_load(&outside,1);assert(!invalid);
    // 到上限只登记剩余额度，后续调用不扩张索引。/ Append only the remaining allowance; later calls cannot grow a full journal.
    assert(journal_path(test_sd,journal));f=fopen(journal,"wb");assert(f);
    for(unsigned i=0;i<ADDED_RECORD_MAX-1;++i)assert(append(f,a,1));assert(close_written(f));
    uint64_t new1=0,new2=0;book_added_item_t limited[]={{b,&new1},{unknown,&new2}};book_added_load(limited,2);
    assert((new1!=0)+(new2!=0)==1);assert(!stat(journal,&before));
    new1=new2=0;book_added_load(limited,2);book_added_forget(a);
    assert(!stat(journal,&after)&&before.st_size==after.st_size);assert(!unlink(journal));
    // 索引不能跟随指向任意文件的符号链接。/ Never follow a metadata symlink into an unrelated file.
    assert(journal_path(test_sd,journal));char victim[288];snprintf(victim,sizeof(victim),"%s/victim",temp);
    f=fopen(victim,"wb");assert(f&&fwrite("unchanged",1,9,f)==9&&!fclose(f));
    assert(!symlink(victim,journal));x=0;book_added_item_t symlinked={a,&x};book_added_load(&symlinked,1);assert(!x);
    f=fopen(victim,"rb");char payload[10]={0};assert(f&&fread(payload,1,9,f)==9&&!fclose(f)&&!strcmp(payload,"unchanged"));
    assert(!unlink(journal)&&!unlink(victim)&&!rmdir(test_sd)&&!rmdir(test_flash)&&!rmdir(temp));
    puts("PASS first-added ordering: reload, independent media, migration, reimport, damaged tails, root boundaries and symlink refusal");
}
