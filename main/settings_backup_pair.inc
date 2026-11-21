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
static esp_err_t backup_pair_commit(void) {
    uint32_t pair[6]={UINT32_C(0x3150424b)};
    if (!backup_fingerprint(BACKUP_FILE,pair+1) || !backup_fingerprint(BACKUP_FULL_TEMP,pair+3)) return ESP_FAIL;
    pair[5]=backup_pair_seal(pair);
    FILE *file=fopen(BACKUP_PAIR_TEMP,"wb");
    if(!file)return ESP_FAIL;
    bool ok=fwrite(pair,1,sizeof(pair),file)==sizeof(pair)&&fflush(file)==0&&fsync(fileno(file))==0;
    if(fclose(file))ok=false;
    if(!ok)return ESP_FAIL;
    if(rename(BACKUP_FULL_TEMP,BACKUP_FULL)||rename(BACKUP_PAIR_TEMP,BACKUP_PAIR))return ESP_FAIL;
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
