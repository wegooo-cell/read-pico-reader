/* SPDX-License-Identifier: Apache-2.0 */
/* TF上的恢复日志在写入前落盘；断电后先回退未提交事务。旧进度目录始终保留。
 * Persist an undo journal before writes and recover unfinished restores after power loss. */
#ifdef ESP_PLATFORM
#define RESTORE_PENDING APP_SETTINGS_BACKUP_ROOT "/.kiikoread-restore.pending"
#define RESTORE_TEMP APP_SETTINGS_BACKUP_ROOT "/.kiikoread-restore.tmp"
#define RESTORE_COMPLETE APP_SETTINGS_BACKUP_ROOT "/.kiikoread-restore.previous"

esp_err_t app_settings_restore_recover(void) {
    if(!backup_card_ready())return ESP_ERR_INVALID_STATE;
    FILE *file=fopen(RESTORE_PENDING,"rb");
    if(!file)return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    uint32_t generation=0;
    esp_err_t err=book_history_checkpoint_restore(file,NVS_NS,&generation,false);
    if(err==ESP_OK)err=book_history_checkpoint_restore(file,NVS_NS,&generation,true);
    if(fclose(file)&&err==ESP_OK)err=ESP_FAIL;
    if(err==ESP_OK)err=book_progress_restore_rewind(generation);
    if(err==ESP_OK)err=backup_replace_retaining(RESTORE_PENDING,RESTORE_COMPLETE);
    if(err!=ESP_OK)ESP_LOGW(TAG,"restore rollback retained: %s",esp_err_to_name(err));
    else book_progress_notify_changed();
    return err;
}

static esp_err_t restore_checkpoint_begin(void) {
    esp_err_t err=app_settings_restore_recover();
    if(err!=ESP_OK&&err!=ESP_ERR_NOT_FOUND)return err;
    FILE *file=fopen(RESTORE_TEMP,"wb");if(!file)return ESP_FAIL;
    err=book_history_checkpoint_write(file,NVS_NS,book_progress_restore_generation());
    if(err==ESP_OK&&(fflush(file)||fsync(fileno(file))))err=ESP_FAIL;
    if(fclose(file)&&err==ESP_OK)err=ESP_FAIL;
    if(err!=ESP_OK)return err;
    file=fopen(RESTORE_TEMP,"rb");if(!file)return ESP_FAIL;
    uint32_t ignored=0;err=book_history_checkpoint_restore(file,NVS_NS,&ignored,false);
    if(fclose(file)&&err==ESP_OK)err=ESP_FAIL;
    if(err==ESP_OK&&rename(RESTORE_TEMP,RESTORE_PENDING))err=ESP_FAIL;
    return err;
}
#else
esp_err_t app_settings_restore_recover(void){return ESP_ERR_NOT_FOUND;}
#endif

static esp_err_t backup_apply_stream(FILE *file,bool apply) {
    long start=ftell(file);if(start<0)return ESP_FAIL;
    esp_err_t err=backup_apply_stream_raw(file,false);
    if(err!=ESP_OK||!apply)return err;
    if(fseek(file,start,SEEK_SET))return ESP_FAIL;
#ifdef ESP_PLATFORM
    err=restore_checkpoint_begin();if(err!=ESP_OK)return err;
    err=book_progress_restore_begin();
    if(err==ESP_OK)err=backup_apply_stream_raw(file,true);
    if(err==ESP_OK)err=book_progress_restore_commit();
    if(err==ESP_OK)err=backup_replace_retaining(RESTORE_PENDING,RESTORE_COMPLETE);
    if(err!=ESP_OK){
        book_progress_restore_abort();
        esp_err_t rollback=app_settings_restore_recover();
        if(rollback!=ESP_OK)ESP_LOGW(TAG,"configuration rollback pending: %s",esp_err_to_name(rollback));
        return err;
    }
#else
    err=backup_apply_stream_raw(file,true);if(err!=ESP_OK)return err;
#endif
    // 全部持久化成功之后才公开新设置，失败时保留原有运行状态。
    // Publish in-memory preferences only after all persistent records commit.
    app_settings_init();return ESP_OK;
}
