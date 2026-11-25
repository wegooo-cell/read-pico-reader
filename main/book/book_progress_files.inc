/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOOK_PROGRESS_DIR
#define BOOK_PROGRESS_DIR "/flash/progress"
#endif
#ifndef BOOK_PROGRESS_SD_ROOT
#define BOOK_PROGRESS_SD_ROOT "/sdcard/.readpico"
#endif
#define BOOK_PROGRESS_SD_DIR BOOK_PROGRESS_SD_ROOT "/progress"
#define PROGRESS_FILE_MAX 192
#define PROGRESS_ROOT_MAX 160
static char s_restore_dir[PROGRESS_ROOT_MAX];
static bool s_restore_active;

static bool progress_key_valid(const char *key) {
    return !strncmp(key, "b_", 2) && strlen(key) == 10 && strspn(key + 2, "0123456789abcdef") == 8;
}

static bool progress_key_matches(const char *key, const uint8_t *data, size_t len) {
    if (!record_valid(data, len)) return false;
    char expected[11];
    make_key((const char *)data + HEADER_SIZE, expected);
    return !strcmp(key, expected);
}

static void slot_path_at(char path[PROGRESS_FILE_MAX], const char *dir, const char *key, unsigned slot) {
    snprintf(path, PROGRESS_FILE_MAX, "%s/%.8s.%u", dir, key + 2, slot);
}

static uint32_t record_seal(const uint8_t *data, size_t len) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < len; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

static esp_err_t slot_read_at(const char *dir, const char *key, unsigned slot, uint8_t *data, size_t *len) {
    char path[PROGRESS_FILE_MAX];
    slot_path_at(path, dir, key, slot);
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : ESP_FAIL;
    uint8_t bytes[RECORD_CAP + 4];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    bool complete = fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!complete) return ESP_ERR_INVALID_STATE;
    if (size < HEADER_SIZE + 6 || !progress_key_matches(key, bytes, size - 4) ||
        get32(bytes + size - 4) != record_seal(bytes, size - 4)) return ESP_ERR_INVALID_STATE;
    if (*len < size - 4) return ESP_ERR_INVALID_SIZE;
    *len = size - 4;
    memcpy(data, bytes, *len);
    return ESP_OK;
}

static uint32_t record_order(const uint8_t *data) { return data[3] == 2 ? get32(data + 16) : 0; }
static bool record_newer(const uint8_t *a,const uint8_t *b);

static esp_err_t disk_read_at(const char *dir, const char *key, uint8_t *data, size_t *len, unsigned *slot) {
    if (!progress_key_valid(key)) return ESP_ERR_INVALID_ARG;
    size_t first_len = *len;
    esp_err_t first = slot_read_at(dir, key, 0, data, &first_len);
    uint8_t second_data[RECORD_CAP];
    size_t second_len = sizeof(second_data);
    esp_err_t second = slot_read_at(dir, key, 1, second_data, &second_len);
    if (second == ESP_OK && (first != ESP_OK || record_newer(second_data, data))) {
        if (*len < second_len) return ESP_ERR_INVALID_SIZE;
        memcpy(data, second_data, second_len);
        *len = second_len;
        if (slot) *slot = 1;
        return ESP_OK;
    }
    if (first == ESP_OK) {
        *len = first_len;
        if (slot) *slot = 0;
        return ESP_OK;
    }
    if (first == ESP_FAIL || second == ESP_FAIL) return ESP_FAIL;
    return first == ESP_ERR_NVS_NOT_FOUND ? second : first;
}

static esp_err_t disk_write_at(const char *dir, const char *key, const uint8_t *data, size_t len) {
    uint8_t previous[RECORD_CAP];
    size_t previous_len = sizeof(previous);
    unsigned active = 1;
    esp_err_t err = disk_read_at(dir, key, previous, &previous_len, &active);
    if (err == ESP_OK) {
        if (strcmp((const char *)previous + HEADER_SIZE, (const char *)data + HEADER_SIZE))
            return ESP_ERR_INVALID_STATE;
        if (previous_len == len && !memcmp(previous, data, len)) return ESP_OK;
    } else if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_INVALID_STATE) return err;

    // Commit to the inactive slot while keeping the last checked copy readable.
    unsigned slot = 1 - active;
    char path[PROGRESS_FILE_MAX];
    slot_path_at(path, dir, key, slot);
    // 长度相同时原地覆盖：FAT 建在 4 KB 磨损均衡扇区上，"wb" 先截断再重新分配会多重写数个扇区，
    // 使一次保存从约 0.15 s 变成约 0.5 s。长度不同或文件不存在时仍整文件重写。
    // Overwrite in place when the size matches: FAT sits on 4 KB wear-levelled sectors, and "wb"
    // truncates and reallocates, rewriting several extra sectors (about 0.5 s instead of 0.15 s per save).
    // A different size or a missing file still rewrites the whole file.
    FILE *file = fopen(path, "r+b");
    if (file && (fseek(file, 0, SEEK_END) != 0 || ftell(file) != (long)(len + 4) ||
                 fseek(file, 0, SEEK_SET) != 0)) {
        fclose(file);
        file = NULL;
    }
    if (!file) file = fopen(path, "wb");
    if (!file) return ESP_FAIL;
    uint8_t seal[4];
    put32(seal, record_seal(data, len));
    bool ok = fwrite(data, 1, len, file) == len && fwrite(seal, 1, sizeof(seal), file) == sizeof(seal) &&
              fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    previous_len = sizeof(previous);
    if (!ok || slot_read_at(dir, key, slot, previous, &previous_len) != ESP_OK ||
        previous_len != len || memcmp(data, previous, len)) {
        (void)unlink(path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t disk_erase_at(const char *dir, const char *key) {
    for (unsigned slot = 0; slot < 2; ++slot) {
        char path[PROGRESS_FILE_MAX];
        slot_path_at(path, dir, key, slot);
        if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    }
    return ESP_OK;
}

// 机内不可用时使用TF卡双槽；仅完整核验的副本参与选取，不格式化任何旧数据。
// Fail over to sealed TF slots; only checked copies participate, without formatting old storage.
static esp_err_t progress_dir_prepare(const char *dir) {
    if (mkdir(dir, 0777) == 0) return ESP_OK;
    struct stat st;
    return errno == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
}

static bool progress_card_ready(void) { return book_store_card_ready(); }

static esp_err_t progress_card_prepare(void) {
    if (!progress_card_ready()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = progress_dir_prepare(BOOK_PROGRESS_SD_ROOT);
    return err == ESP_OK ? progress_dir_prepare(BOOK_PROGRESS_SD_DIR) : err;
}

static bool progress_active_dir(char out[PROGRESS_ROOT_MAX], uint32_t *generation) {
    if (!progress_card_ready()) return false;
    bool found = false; uint32_t best = 0;
    for (unsigned slot = 0; slot < 2; ++slot) {
        char path[PROGRESS_FILE_MAX]; uint8_t bytes[16];
        snprintf(path, sizeof(path), BOOK_PROGRESS_SD_ROOT "/progress.active.%u", slot);
        FILE *file = fopen(path, "rb"); if (!file) continue;
        bool ok = fread(bytes,1,sizeof(bytes),file)==sizeof(bytes) && fgetc(file)==EOF && !ferror(file);
        if (fclose(file)) ok=false;
        if (!ok || memcmp(bytes,"KPA1",4) || get32(bytes+8)!=~get32(bytes+4) ||
            get32(bytes+12)!=record_seal(bytes,12)) continue;
        uint32_t id=get32(bytes+4); char candidate[PROGRESS_ROOT_MAX]; struct stat st;
        snprintf(candidate,sizeof(candidate),BOOK_PROGRESS_SD_ROOT "/progress.r%08" PRIx32,id);
        if (!id || stat(candidate,&st) || !S_ISDIR(st.st_mode)) continue;
        if (!found || id>best) {found=true;best=id;strcpy(out,candidate);}
    }
    if (generation) *generation=best;
    return found;
}

bool book_progress_using_card_storage(void) {
    char active[PROGRESS_ROOT_MAX];
    return progress_card_ready() && (!book_store_flash_ready() || progress_active_dir(active,NULL));
}

static esp_err_t disk_prepare(void) {
    esp_err_t err=book_store_prepare_flash();
    if (err==ESP_OK) err=progress_dir_prepare(BOOK_PROGRESS_DIR);
    char active[PROGRESS_ROOT_MAX];
    if (progress_active_dir(active,NULL)) return ESP_OK;
    return err==ESP_OK ? ESP_OK : progress_card_prepare()==ESP_OK ? ESP_OK : err;
}

static bool disk_available(void) { return disk_prepare()==ESP_OK; }

static bool record_uncertain(const uint8_t *data) {return data[3]==2 && (data[21]&0x80);}
static bool record_newer(const uint8_t *a,const uint8_t *b) {
    if(record_uncertain(a)!=record_uncertain(b))return !record_uncertain(a);
    return record_order(a)>record_order(b);
}

static esp_err_t disk_read(const char *key,uint8_t *data,size_t *len,unsigned *slot) {
    char active[PROGRESS_ROOT_MAX];
    if(progress_active_dir(active,NULL)) {
        esp_err_t err=disk_read_at(active,key,data,len,slot);
        if(err==ESP_OK)return err;
        if(err!=ESP_ERR_NVS_NOT_FOUND)return err;
    }
    uint8_t other[RECORD_CAP];size_t count=sizeof(other);unsigned second_slot=0;
    size_t first_len=*len;
    esp_err_t first=book_store_flash_ready()?disk_read_at(BOOK_PROGRESS_DIR,key,data,&first_len,slot):ESP_ERR_INVALID_STATE;
    esp_err_t second=progress_card_ready()?disk_read_at(BOOK_PROGRESS_SD_DIR,key,other,&count,&second_slot):ESP_ERR_NVS_NOT_FOUND;
    if(second==ESP_OK && (first!=ESP_OK || record_newer(other,data))) {
        if(*len<count)return ESP_ERR_INVALID_SIZE;
        memcpy(data,other,count);*len=count;if(slot)*slot=second_slot;return ESP_OK;
    }
    if(first==ESP_OK){*len=first_len;return ESP_OK;}
    // 已有TF进度区可接收新书；未知机内原记录仍原样保留，新记录带保留标志。
    // An available TF store can journal new sessions; retain unknown internal originals and mark the new record.
    if(!book_store_flash_ready() && progress_card_ready() && second==ESP_ERR_NVS_NOT_FOUND)return second;
    return first==ESP_ERR_NVS_NOT_FOUND?second:first;
}

static esp_err_t disk_write(const char *key,const uint8_t *data,size_t len) {
    char active[PROGRESS_ROOT_MAX];
    if(progress_active_dir(active,NULL))return disk_write_at(active,key,data,len);
    esp_err_t err=book_store_flash_ready()?disk_write_at(BOOK_PROGRESS_DIR,key,data,len):ESP_ERR_INVALID_STATE;
    if(err==ESP_OK || err==ESP_ERR_INVALID_STATE)return err==ESP_ERR_INVALID_STATE && !book_store_flash_ready() && progress_card_prepare()==ESP_OK
        ? disk_write_at(BOOK_PROGRESS_SD_DIR,key,data,len) : err;
    return progress_card_prepare()==ESP_OK ? disk_write_at(BOOK_PROGRESS_SD_DIR,key,data,len) : err;
}

static esp_err_t disk_erase(const char *key) {
    char active[PROGRESS_ROOT_MAX];esp_err_t err=ESP_OK;
    if(progress_active_dir(active,NULL))err=disk_erase_at(active,key);
    if(err==ESP_OK && book_store_flash_ready())err=disk_erase_at(BOOK_PROGRESS_DIR,key);
    if(err==ESP_OK && progress_card_ready())err=disk_erase_at(BOOK_PROGRESS_SD_DIR,key);
    return err;
}

static esp_err_t disk_walk(book_progress_record_fn visit,void *ctx,bool clear) {
    if(!disk_available())return clear?ESP_ERR_INVALID_STATE:ESP_OK;
    char active[PROGRESS_ROOT_MAX];const char *roots[3];unsigned count=0;
    if(progress_active_dir(active,NULL))roots[count++]=active;
    if(progress_card_ready())roots[count++]=BOOK_PROGRESS_SD_DIR;
    if(book_store_flash_ready())roots[count++]=BOOK_PROGRESS_DIR;
    for(unsigned i=0;i<count;++i){
        DIR *dir=opendir(roots[i]);if(!dir){if(errno==ENOENT)continue;return ESP_FAIL;}
        esp_err_t err=ESP_OK;
        for(struct dirent *ent;err==ESP_OK && (ent=readdir(dir));){
            const char *name=ent->d_name;
            if(strlen(name)!=10 || strspn(name,"0123456789abcdefABCDEF")!=8 || name[8]!='.' || (name[9]!='0'&&name[9]!='1'))continue;
            char key[11]="b_";for(unsigned j=0;j<8;++j)key[j+2]=(char)tolower((unsigned char)name[j]);
            uint8_t data[RECORD_CAP];size_t len=sizeof(data);
            if(clear){err=disk_erase_at(roots[i],key);continue;}
            unsigned chosen=0;if(disk_read_at(roots[i],key,data,&len,&chosen)!=ESP_OK || chosen!=(unsigned)(name[9]-'0'))continue;
            bool seen=false;
            for(unsigned j=0;j<i&&!seen;++j){size_t n=sizeof(data);seen=disk_read_at(roots[j],key,data,&n,NULL)==ESP_OK;}
            if(seen)continue;
            len=sizeof(data);if(disk_read(key,data,&len,NULL)==ESP_OK)err=visit(key,data,len,ctx);
        }
        closedir(dir);if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}

static esp_err_t progress_copy_to_stage(const char *key,const uint8_t *data,size_t len,void *ctx) {
    (void)ctx;return disk_write_at(s_restore_dir,key,data,len);
}

esp_err_t book_progress_restore_begin(void) {
    if(s_restore_active)return ESP_ERR_INVALID_STATE;
    esp_err_t err=progress_card_prepare();if(err!=ESP_OK)return err;
    char current[PROGRESS_ROOT_MAX];uint32_t id=0;progress_active_dir(current,&id);
    for(unsigned tries=0;tries<1024;++tries){
        if(id==UINT32_MAX)return ESP_ERR_INVALID_STATE;
        ++id;snprintf(s_restore_dir,sizeof(s_restore_dir),BOOK_PROGRESS_SD_ROOT "/progress.r%08" PRIx32,id);
        if(!mkdir(s_restore_dir,0777))break;
        if(errno!=EEXIST)return ESP_FAIL;
        if(tries==1023)return ESP_ERR_INVALID_STATE;
    }
    // 先复制当前可校验的记录，新目录在提交前不可见；旧目录与旧分区不清空。
    // Copy checked current records before applying backup values; keep the unpublished directory and original stores separate.
    err=book_progress_records(progress_copy_to_stage,NULL);
    if(err!=ESP_OK){s_restore_dir[0]=0;return err;}
    s_restore_active=true;return ESP_OK;
}

void book_progress_restore_abort(void){s_restore_active=false;s_restore_dir[0]=0;}

uint32_t book_progress_restore_generation(void) {
    char active[PROGRESS_ROOT_MAX];uint32_t id=0;progress_active_dir(active,&id);return id;
}

esp_err_t book_progress_restore_rewind(uint32_t generation) {
    if(!progress_card_ready())return ESP_ERR_INVALID_STATE;
    for(unsigned slot=0;slot<2;++slot){
        char path[PROGRESS_FILE_MAX];uint8_t bytes[16];
        snprintf(path,sizeof(path),BOOK_PROGRESS_SD_ROOT "/progress.active.%u",slot);
        FILE *file=fopen(path,"rb");if(!file){if(errno==ENOENT)continue;return ESP_FAIL;}
        bool ok=fread(bytes,1,sizeof(bytes),file)==sizeof(bytes)&&!memcmp(bytes,"KPA1",4)&&
            get32(bytes+8)==~get32(bytes+4)&&get32(bytes+12)==record_seal(bytes,12);
        if(fclose(file))return ESP_FAIL;
        if((!ok || get32(bytes+4)>generation) && remove(path))return ESP_FAIL;
    }
    book_progress_restore_abort();
    return book_progress_restore_generation()==generation?ESP_OK:ESP_FAIL;
}

esp_err_t book_progress_restore_commit(void) {
    if(!s_restore_active)return ESP_ERR_INVALID_STATE;
    const char *base=strrchr(s_restore_dir,'r');unsigned long parsed=0;
    if(!base || sscanf(base+1,"%8lx",&parsed)!=1 || !parsed)return ESP_ERR_INVALID_STATE;
    uint32_t id=(uint32_t)parsed;uint8_t bytes[16]={'K','P','A','1'};put32(bytes+4,id);put32(bytes+8,~id);put32(bytes+12,record_seal(bytes,12));
    // 目录编号可能因中断而跳号；始终写另一指针槽，保留当前可用指针。
    // Interrupted staging can skip directory numbers; always retain the currently selected pointer slot.
    uint32_t previous=book_progress_restore_generation();unsigned target=0;
    char path[PROGRESS_FILE_MAX];
    for(unsigned slot=0;slot<2&&previous;++slot){
        uint8_t old[16];snprintf(path,sizeof(path),BOOK_PROGRESS_SD_ROOT "/progress.active.%u",slot);
        FILE *prior=fopen(path,"rb");if(!prior)continue;
        bool current=fread(old,1,sizeof(old),prior)==sizeof(old)&&get32(old+4)==previous;
        fclose(prior);if(current){target=1-slot;break;}
    }
    snprintf(path,sizeof(path),BOOK_PROGRESS_SD_ROOT "/progress.active.%u",target);
    FILE *file=fopen(path,"wb");if(!file)return ESP_FAIL;
    bool ok=fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes)&&fflush(file)==0&&fsync(fileno(file))==0;
    if(fclose(file))ok=false;
    char active[PROGRESS_ROOT_MAX];uint32_t current=0;
    bool selected=progress_active_dir(active,&current)&&current==id;
    if(!ok || !selected)return ESP_FAIL;
    book_progress_restore_abort();book_progress_notify_changed();return ESP_OK;
}

void book_progress_prepare(void) {
    static bool migrated;
    if (migrated || disk_prepare() != ESP_OK || book_progress_using_card_storage()) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    unsigned count = 0;
    nvs_iterator_t it = NULL;
    for (err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_BLOB, &it);
         err == ESP_OK; err = nvs_entry_next(&it)) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) break;
        uint8_t old[RECORD_CAP], current[RECORD_CAP];
        size_t len = sizeof(old), current_len = sizeof(current);
        if (nvs_get_blob(h, info.key, old, &len) != ESP_OK || !progress_key_matches(info.key, old, len)) continue;
        err = disk_read(info.key, current, &current_len, NULL);
        if (err == ESP_OK && strcmp((const char *)old + HEADER_SIZE, (const char *)current + HEADER_SIZE))
            err = ESP_ERR_INVALID_STATE;
        if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_STATE ||
            (err == ESP_OK && record_order(current) < record_order(old)))
            err = disk_write(info.key, old, len);
        if (err != ESP_OK) break;
        // TF回退成功也不能删NVS原件：必须再次确认机内副本本身可读且不旧于原件。
        // A successful TF fallback is insufficient for migration: verify the internal copy itself before deleting legacy NVS.
        current_len=sizeof(current);
        err=disk_read_at(BOOK_PROGRESS_DIR,info.key,current,&current_len,NULL);
        if(err==ESP_OK && (strcmp((const char*)old+HEADER_SIZE,(const char*)current+HEADER_SIZE)||record_newer(old,current)))
            err=ESP_ERR_INVALID_STATE;
        if(err!=ESP_OK)break;
        err = nvs_erase_key(h, info.key);
        if (err != ESP_OK) break;
        ++count;
    }
    nvs_release_iterator(it);
    if (count) {
        esp_err_t commit = nvs_commit(h);
        if (commit != ESP_OK) err = commit;
        ESP_LOGI(TAG, "progress moved from NVS: %u", count);
    }
    nvs_close(h);
    migrated = err == ESP_ERR_NVS_NOT_FOUND;
}

void book_progress_prepare_at_boot(void) {
    // 进度已移出NVS，空闲条目多也必须在显示/字体大分配前保留文件系统资源。
    // Progress lives in files even when NVS has space; mount before large display/font allocations.
    esp_err_t err = book_progress_prepare_storage();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "progress storage startup: %s", esp_err_to_name(err));
        return;
    }
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK && stats.available_entries < 64)
        book_progress_prepare();
}

esp_err_t book_progress_prepare_storage(void) { return disk_prepare(); }

esp_err_t book_progress_restore_record(const char *key, const uint8_t *data, size_t len) {
    if(!progress_key_matches(key,data,len))return ESP_ERR_INVALID_ARG;
    if(!s_restore_active)return disk_write(key,data,len);
    uint8_t previous[RECORD_CAP];size_t count=sizeof(previous);
    esp_err_t err=disk_read_at(s_restore_dir,key,previous,&count,NULL);
    if(err==ESP_OK && strcmp((const char*)previous+HEADER_SIZE,(const char*)data+HEADER_SIZE))return ESP_ERR_INVALID_STATE;
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND)return err;
    // 只替换未提交的新副本，原可用记录始终保留在原位置。
    // Replace only this unpublished copy; original checked records remain in place.
    err=disk_erase_at(s_restore_dir,key);
    return err==ESP_OK?disk_write_at(s_restore_dir,key,data,len):err;
}

esp_err_t book_progress_clear_records(void) {
    esp_err_t err = disk_prepare();
    return err == ESP_OK ? disk_walk(NULL, NULL, true) : err;
}
