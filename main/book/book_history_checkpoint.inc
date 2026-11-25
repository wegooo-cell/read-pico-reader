/* SPDX-License-Identifier: Apache-2.0 */
/* 恢复前保留逐键快照；失败/断电恢复只撤回本次写入，绝不擦除整个NVS。
 * Keep a checked per-key checkpoint before restore; rollback never erases an NVS partition. */
#define CHECKPOINT_MAX_BYTES (1024 * 1024)
#define CHECKPOINT_MAX_RECORDS 2048
#if KIIKO_BOARD_METALIO47
#define CHECKPOINT_BOARD 1
#else
#define CHECKPOINT_BOARD 0
#endif

static const char *checkpoint_namespace(unsigned ns,const char *settings) {
    if(!ns)return settings;
#if KIIKO_BOARD_METALIO47
    if(ns==1)return "metalio_wifi";
#else
    if(ns==1)return "rp_wifi";
#endif
    return ns<8?namespaces[ns-2]:NULL;
}

static bool checkpoint_type(uint8_t type) {
    return type==NVS_TYPE_U8||type==NVS_TYPE_U32||type==NVS_TYPE_STR||type==NVS_TYPE_BLOB;
}

esp_err_t book_history_checkpoint_write(FILE *file,const char *settings,uint32_t generation) {
    if(!file||!settings)return ESP_ERR_INVALID_ARG;
    uint8_t head[12]={'K','R','T','X','N',1,CHECKPOINT_BOARD,0};
    for(unsigned i=0;i<4;++i)head[8+i]=(uint8_t)(generation>>(8*i));
    uint32_t hash=hash_bytes(UINT32_C(2166136261),head,sizeof(head));uint16_t count=0;
    if(fwrite(head,1,sizeof(head),file)!=sizeof(head))return ESP_FAIL;
    for(unsigned ns=0;ns<8;++ns){
        nvs_handle_t h;esp_err_t err=nvs_open(checkpoint_namespace(ns,settings),NVS_READONLY,&h);
        if(err==ESP_ERR_NVS_NOT_FOUND)continue;
        if(err!=ESP_OK)return err;
        nvs_iterator_t it=NULL;err=nvs_entry_find_in_handle(h,NVS_TYPE_ANY,&it);
        while(err==ESP_OK){
            nvs_entry_info_t info;err=nvs_entry_info(it,&info);if(err!=ESP_OK)break;
            // 其他类型不参与配置导入，原样保留，不因旧/第三方键拒绝恢复。
            // Unsupported types are never imported or deleted; preserve unrelated legacy keys.
            if(checkpoint_type(info.type)){
                uint8_t *value=NULL;size_t len=0;err=read_value(h,&info,&value,&len);
                if(err==ESP_OK){
                    uint8_t record[20]={(uint8_t)ns,info.type};
                    memcpy(record+2,info.key,strlen(info.key));record[18]=(uint8_t)len;record[19]=(uint8_t)(len>>8);
                    if(count==CHECKPOINT_MAX_RECORDS)err=ESP_ERR_INVALID_SIZE;
                    else if(fwrite(record,1,20,file)!=20||fwrite(value,1,len,file)!=len)err=ESP_FAIL;
                    else{hash=hash_bytes(hash_bytes(hash,record,20),value,len);++count;}
                }
                if(value){memset(value,0,len);free(value);}if(err!=ESP_OK)break;
            }
            err=nvs_entry_next(&it);
        }
        nvs_release_iterator(it);nvs_close(h);
        if(err!=ESP_ERR_NVS_NOT_FOUND)return err;
    }
    uint8_t end[20]={0xff},seal[6]={(uint8_t)count,(uint8_t)(count>>8)};
    for(unsigned i=0;i<4;++i)seal[2+i]=(uint8_t)(hash>>(8*i));
    return fwrite(end,1,20,file)==20&&fwrite(seal,1,6,file)==6?ESP_OK:ESP_FAIL;
}

static const uint8_t *checkpoint_find(const uint8_t *data,size_t end,unsigned ns,const char *key) {
    for(size_t p=12;p<end;){
        const uint8_t *record=data+p;
        if(record[0]==ns&&!strcmp((const char*)record+2,key))return record;
        p+=20+((size_t)record[18]|((size_t)record[19]<<8));
    }
    return NULL;
}

esp_err_t book_history_checkpoint_restore(FILE *file,const char *settings,uint32_t *generation,bool apply) {
    if(!file||!settings||!generation)return ESP_ERR_INVALID_ARG;
    if(fseek(file,0,SEEK_END))return ESP_FAIL;
    long bytes=ftell(file);if(bytes<38||bytes>CHECKPOINT_MAX_BYTES||fseek(file,0,SEEK_SET))return ESP_ERR_INVALID_RESPONSE;
    uint8_t *data=malloc((size_t)bytes);if(!data)return ESP_ERR_NO_MEM;
    esp_err_t err=ESP_ERR_INVALID_RESPONSE;
    if(fread(data,1,(size_t)bytes,file)!=(size_t)bytes||memcmp(data,"KRTXN\1",6)||
       data[6]!=CHECKPOINT_BOARD||data[7])goto done;
    uint32_t hash=hash_bytes(UINT32_C(2166136261),data,12);size_t end=12;unsigned count=0;
    while(end+26<=(size_t)bytes && data[end]!=0xff){
        const uint8_t *r=data+end;size_t len=(size_t)r[18]|((size_t)r[19]<<8);
        if(r[0]>=8||!checkpoint_type(r[1])||!r[2]||strnlen((const char*)r+2,16)==16||
           !len||len>HISTORY_MAX_VALUE||end+20+len+26>(size_t)bytes||++count>CHECKPOINT_MAX_RECORDS||
           (r[1]==NVS_TYPE_U8&&len!=1)||(r[1]==NVS_TYPE_U32&&len!=4)||
           (r[1]==NVS_TYPE_STR&&(r[20+len-1]||strnlen((const char*)r+20,len)!=len-1)))goto done;
        hash=hash_bytes(hash,r,20+len);end+=20+len;
    }
    if(end+26!=(size_t)bytes||data[end]!=0xff)goto done;
    for(unsigned i=1;i<20;++i)if(data[end+i])goto done;
    if(((unsigned)data[end+20]|((unsigned)data[end+21]<<8))!=count)goto done;
    for(unsigned i=0;i<4;++i)if(data[end+22+i]!=(uint8_t)(hash>>(8*i)))goto done;
    *generation=0;for(unsigned i=0;i<4;++i)*generation|=(uint32_t)data[8+i]<<(8*i);
    err=ESP_OK;if(!apply)goto done;
    for(unsigned ns=0;ns<8&&err==ESP_OK;++ns){
        nvs_handle_t h;err=nvs_open(checkpoint_namespace(ns,settings),NVS_READWRITE,&h);if(err!=ESP_OK)break;
        // 删除仅本次恢复新建的键；每次删除后重新枚举，避免迭代器失效。
        // Remove only keys introduced by the interrupted restore; restart iteration after deletion.
        bool removed;
        do{
            removed=false;nvs_iterator_t it=NULL;esp_err_t scan=nvs_entry_find_in_handle(h,NVS_TYPE_ANY,&it);
            while(scan==ESP_OK){
                nvs_entry_info_t info;scan=nvs_entry_info(it,&info);if(scan!=ESP_OK)break;
                if(checkpoint_type(info.type)&&!checkpoint_find(data,end,ns,info.key)){
                    err=nvs_erase_key(h,info.key);removed=err==ESP_OK;break;
                }
                scan=nvs_entry_next(&it);
            }
            nvs_release_iterator(it);
            if(err==ESP_OK&&scan!=ESP_OK&&scan!=ESP_ERR_NVS_NOT_FOUND)err=scan;
        }while(removed&&err==ESP_OK);
        for(size_t p=12;p<end&&err==ESP_OK;){
            const uint8_t *r=data+p,*value=r+20;size_t len=(size_t)r[18]|((size_t)r[19]<<8);
            const char *key=(const char*)r+2;
            if(r[0]==ns){
                if(r[1]==NVS_TYPE_U8)err=nvs_set_u8(h,key,value[0]);
                else if(r[1]==NVS_TYPE_U32){uint32_t v;memcpy(&v,value,4);err=nvs_set_u32(h,key,v);}
                else if(r[1]==NVS_TYPE_STR)err=nvs_set_str(h,key,(const char*)value);
                else err=nvs_set_blob(h,key,value,len);
            }
            p+=20+len;
        }
        if(err==ESP_OK)err=nvs_commit(h);
        for(size_t p=12;p<end&&err==ESP_OK;){
            const uint8_t *r=data+p;size_t len=(size_t)r[18]|((size_t)r[19]<<8);
            if(r[0]==ns){nvs_entry_info_t info={.type=r[1]};memcpy(info.key,r+2,16);
                uint8_t *check=NULL;size_t n=0;err=read_value(h,&info,&check,&n);
                if(err==ESP_OK&&(n!=len||memcmp(check,r+20,len)))err=ESP_FAIL;
                if(check){memset(check,0,n);free(check);}}
            p+=20+len;
        }
        nvs_close(h);
    }
done:
    memset(data,0,(size_t)bytes);free(data);return err;
}
