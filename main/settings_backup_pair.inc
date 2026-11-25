/* SPDX-License-Identifier: Apache-2.0
 * Keep an immutable v25 interchange file beside a checked full-format companion.
 * Old firmware sees only the interchange file. A legacy save invalidates the pair
 * fingerprint, so a newer firmware never restores stale companion settings.
 */
#define BACKUP_FULL APP_SETTINGS_BACKUP_ROOT "/Kiikoread-settings.full.backup"
#define BACKUP_FULL_TEMP APP_SETTINGS_BACKUP_ROOT "/Kiikoread-settings.full.tmp"
#define BACKUP_PAIR APP_SETTINGS_BACKUP_ROOT "/Kiikoread-settings.pair"
#define BACKUP_PAIR_TEMP APP_SETTINGS_BACKUP_ROOT "/Kiikoread-settings.pair.tmp"
#define BACKUP_BYTE_LIMIT (4u * 1024u * 1024u)
static bool backup_fingerprint(const char *path, uint32_t out[2]) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t bytes[512]; uint32_t hash = UINT32_C(2166136261), size = 0;
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file))) {
        if (count > BACKUP_BYTE_LIMIT - size) { fclose(file); return false; }
        size += count;
        for (size_t i=0; i<count; ++i) hash=(hash^bytes[i])*UINT32_C(16777619);
    }
    bool ok = !ferror(file);
    if (fclose(file)) ok=false;
    if (ok) {out[0]=size;out[1]=hash;}
    return ok;
}
static uint32_t backup_pair_seal(const uint32_t pair[6]) {
    uint32_t hash=UINT32_C(2166136261);
    for(unsigned i=0;i<5;++i) for(unsigned shift=0;shift<32;shift+=8)
        hash=(hash^(uint8_t)(pair[i]>>shift))*UINT32_C(16777619);
    return hash;
}
// FatFs不能重命名覆盖同名文件；保留旧副本，目标空出后再安装新文件。
// FatFs rejects overwriting rename destinations. Retain the old copy before installing a new file.
static esp_err_t backup_replace_retaining(const char *source,const char *target) {
    struct stat st;char previous[384]={0};bool moved=false;
    if(!stat(target,&st)){
        if(!S_ISREG(st.st_mode))return ESP_ERR_INVALID_STATE;
        bool available=false;
        for(unsigned i=1;i<=1024;++i){
            int n=snprintf(previous,sizeof(previous),"%s.previous.%04u",target,i);
            if(n<0||(size_t)n>=sizeof(previous))return ESP_ERR_INVALID_SIZE;
            if(!stat(previous,&st))continue;
            if(errno!=ENOENT)return ESP_FAIL;
            available=true;break;
        }
        if(!available)return ESP_ERR_INVALID_STATE;
        if(rename(target,previous))return ESP_FAIL;
        moved=true;
    }else if(errno!=ENOENT)return ESP_FAIL;
    if(!rename(source,target))return ESP_OK;
    // 新文件安装失败时尽力恢复原名字，原副本始终保留在至少一个路径上。
    // Restore the old name when installation fails; never delete either valid copy.
    if(moved&&stat(target,&st)&&errno==ENOENT)(void)rename(previous,target);
    return ESP_FAIL;
}

static esp_err_t backup_pair_commit(void) {
    uint32_t pair[6]={UINT32_C(0x3150424b)};
    if (!backup_fingerprint(BACKUP_FILE,pair+1) || !backup_fingerprint(BACKUP_FULL_TEMP,pair+3)) return ESP_FAIL;
    pair[5]=backup_pair_seal(pair);
    FILE *file=fopen(BACKUP_PAIR_TEMP,"wb");
    if(!file)return ESP_FAIL;
    bool ok=fwrite(pair,1,sizeof(pair),file)==sizeof(pair)&&fflush(file)==0&&fsync(fileno(file))==0;
    if(fclose(file))ok=false;
    if(!ok)return ESP_FAIL;
    if(backup_replace_retaining(BACKUP_FULL_TEMP,BACKUP_FULL)!=ESP_OK ||
       backup_replace_retaining(BACKUP_PAIR_TEMP,BACKUP_PAIR)!=ESP_OK)return ESP_FAIL;
    return ESP_OK;
}
static bool backup_pair_matches(const char *base) {
    uint32_t pair[6],actual[2];
    FILE *file=fopen(BACKUP_PAIR,"rb");
    if(!file)return false;
    bool ok=fread(pair,1,sizeof(pair),file)==sizeof(pair)&&fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=false;
    return ok&&pair[0]==UINT32_C(0x3150424b)&&pair[5]==backup_pair_seal(pair)&&
        backup_fingerprint(base,actual)&&actual[0]==pair[1]&&actual[1]==pair[2]&&
        backup_fingerprint(BACKUP_FULL,actual)&&actual[0]==pair[3]&&actual[1]==pair[4];
}
