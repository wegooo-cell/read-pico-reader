/* SPDX-License-Identifier: Apache-2.0
 * 中文：由原OTA元数据测试扩展的MindReset协议、身份和地址边界测试。
 * English: Extend the original OTA metadata tests for MindReset identity, protocol and URL boundaries. */
#include "ota_online.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *current="0.3.3-alpha.2";
static const char *valid="{\"series\":\"read\",\"model\":\"read_0\",\"edition\":1,\"firmwareId\":\"" PICO_MINDRESET_FIRMWARE_ID "\",\"status\":\"ALPHA\",\"version\":\"0.3.3-alpha.3\",\"updateVersion\":\"0.3.3-alpha.3\",\"needUpdate\":true,\"ota\":{\"downloadUrl\":\"https://dot.mindreset.tech/download/Example_123\",\"size\":4096,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}}";
static unsigned rejected;
static void reject(const char *json){
 pico_release_t out;bool update=true;
 assert(pico_mindreset_parse(json,strlen(json),current,&out,&update)!=ESP_OK);
 assert(!update&&!out.version[0]&&!out.url[0]);++rejected;
}
static void changed(const char *name,cJSON *value,bool ota){
 cJSON *root=cJSON_Parse(valid);assert(root);
 cJSON *target=ota?cJSON_GetObjectItemCaseSensitive(root,"ota"):root;
 assert(cJSON_ReplaceItemInObjectCaseSensitive(target,name,value));
 char *text=cJSON_PrintUnformatted(root);assert(text);reject(text);free(text);cJSON_Delete(root);
}
int main(int argc,char **argv){
 assert(!strcmp(pico_mindreset_channel("1.0.0"),"RELEASED"));
 assert(!strcmp(pico_mindreset_channel("0.3.3-rc110"),"RELEASED"));
 assert(!strcmp(pico_mindreset_channel("1.0.0-alpha.1"),"ALPHA"));
 assert(!strcmp(pico_mindreset_channel("1.0.0-beta.1"),"TESTING"));
 assert(!pico_ota_same_channel("1.0.0-alpha.3","1.0.0"));
 assert(!pico_ota_same_channel("1.0.0-beta.3","1.0.0-alpha.2"));
 assert(pico_ota_same_channel("1.0.1-rc1","1.0.0"));
 char formal_query[384];assert(pico_mindreset_query_body("1.0.0",formal_query,sizeof(formal_query))==ESP_OK&&strstr(formal_query,"RELEASED"));
 assert(!pico_github_url_valid("https://kiikoread.com/Pico-update-1.0.0.bin"));

 pico_release_t out;bool update=false;
 if(argc==3){
  FILE *file=fopen(argv[1],"rb");assert(file);char data[8193];size_t size=fread(data,1,sizeof(data),file);fclose(file);
  assert(size<=8192&&pico_mindreset_parse(data,size,argv[2],&out,&update)==ESP_OK);
  printf("{\"version\":\"%s\",\"needUpdate\":%s,\"size\":%u,\"sha256\":\"%s\"}\n",out.version,update?"true":"false",out.size,out.sha256);return 0;
 }
 assert(pico_mindreset_parse(valid,strlen(valid),current,&out,&update)==ESP_OK&&update&&out.size==4096);
 assert(!strcmp(out.project,"Read_Pico")&&!strcmp(out.layout,"pico-dual-4m-v1")&&!out.minimum_base_version[0]);
 char body[384];assert(pico_mindreset_query_body(current,body,sizeof(body))==ESP_OK);
 cJSON *root=cJSON_Parse(body);assert(root&&cJSON_GetArraySize(root)==6);
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(root,"firmwareId")->valuestring,PICO_MINDRESET_FIRMWARE_ID));
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(root,"status")->valuestring,"ALPHA"));
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(root,"currentVersion")->valuestring,current));
 assert(!cJSON_GetObjectItemCaseSensitive(root,"id")&&!strstr(body,"token")&&!strstr(body,"key"));cJSON_Delete(root);
 assert(pico_mindreset_query_body(current,body,2)==ESP_ERR_INVALID_SIZE&&!body[0]);
 assert(pico_mindreset_query_body("bad\"version",body,sizeof(body))!=ESP_OK);
 assert(pico_version_compare("0.3.3-alpha.10","0.3.3-alpha.9")>0);
 assert(pico_version_compare("0.3.3-beta.1","0.3.3-alpha.65535")>0);
 assert(pico_version_compare("0.3.3-rc110",current)>0&&pico_version_compare("0.3.3","0.3.3-rc110")>0);
 assert(pico_version_compare(current,"0.3.3-alpha.3")<0);
 const char *names[]={"series","model","firmwareId","status","updateVersion","version"};
 for(unsigned i=0;i<6;++i)changed(names[i],cJSON_CreateString("wrong"),false);
 changed("edition",cJSON_CreateNumber(2),false);changed("edition",cJSON_CreateNumber(1.5),false);
 changed("needUpdate",cJSON_CreateBool(false),false);changed("needUpdate",cJSON_CreateNumber(1),false);
 changed("status",cJSON_CreateString("RELEASED"),false);
 const char *bad_versions[]={"0.3.3-alpha.","0.3.3-alpha.01","0.3.3-alpha.65536","0.3.3-alpha.x","0.3.3-alpha.1.extra","0.3.3-preview.1"};
 for(unsigned i=0;i<6;++i)changed("version",cJSON_CreateString(bad_versions[i]),false);
 const double sizes[]={0,287,4194305,1e10,4096.5};
 for(unsigned i=0;i<5;++i)changed("size",cJSON_CreateNumber(sizes[i]),true);
 changed("sha256",cJSON_CreateString("bad"),true);
 changed("sha256",cJSON_CreateString("z123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),true);
 const char *bad_urls[]={"http://dot.mindreset.tech/download/a","https://dot.mindreset.tech.evil/download/a","https://dot.mindreset.tech@evil/download/a","https://dot.mindreset.tech:443/download/a","https://dot.mindreset.tech/download/../a","https://dot.mindreset.tech/download/a?url=evil","https://kiikoread.com/a.bin","https://wegooo-cell.github.io/read-pico-reader/a.bin","https://dot.mindreset.tech/download/"};
 for(unsigned i=0;i<9;++i)changed("downloadUrl",cJSON_CreateString(bad_urls[i]),true);
 assert(pico_mindreset_cdn_url_valid("https://cdn.mindreset.tech/a/b.bin?sign=123%2F456"));
 const char *bad_cdn[]={"http://cdn.mindreset.tech/a","https://cdn.mindreset.tech.evil/a","https://cdn.mindreset.tech@evil/a","https://cdn.mindreset.tech:443/a","https://cdn.mindreset.tech/a\r\nX:evil","https://cdn.mindreset.tech/a#bad","https://cdn.mindreset.tech/a\\bad"};
 for(unsigned i=0;i<7;++i)assert(!pico_mindreset_cdn_url_valid(bad_cdn[i]));
 char oversized[PICO_MINDRESET_CDN_URL_MAX+1];memset(oversized,'a',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
 memcpy(oversized,PICO_MINDRESET_CDN_PREFIX,strlen(PICO_MINDRESET_CDN_PREFIX));assert(!pico_mindreset_cdn_url_valid(oversized));
 reject("{}");reject("{\"a\":[[[[[[[[[0]]]]]]]]]}");
 char duplicate[2200];snprintf(duplicate,sizeof(duplicate),"{\"status\":\"ALPHA\",%s",valid+1);reject(duplicate);
 snprintf(duplicate,sizeof(duplicate),"%s trailing",valid);reject(duplicate);
 root=cJSON_Parse(valid);cJSON *ota=cJSON_GetObjectItemCaseSensitive(root,"ota");cJSON_AddNumberToObject(ota,"size",4096);
 char *text=cJSON_PrintUnformatted(root);reject(text);free(text);cJSON_Delete(root);
 reject("{\"version\":\"0.3.3-alpha.3\\u0000evil\"}");
 assert(pico_mindreset_parse(valid,strlen(valid)+1,current,&out,&update)!=ESP_OK);
 // 同版本与旧版本均可解析，但不能标为更新；没有OTA文件也不伪造下载入口。
 // Equal/older versions parse without an update; missing artifacts never invent a download URL.
 root=cJSON_Parse(valid);cJSON_ReplaceItemInObjectCaseSensitive(root,"ota",cJSON_CreateNull());
 text=cJSON_PrintUnformatted(root);assert(pico_mindreset_parse(text,strlen(text),current,&out,&update)==ESP_OK&&update&&!out.size&&!out.url[0]);free(text);
 cJSON_ReplaceItemInObjectCaseSensitive(root,"version",cJSON_CreateString(current));
 cJSON_ReplaceItemInObjectCaseSensitive(root,"updateVersion",cJSON_CreateString(current));
 cJSON_ReplaceItemInObjectCaseSensitive(root,"needUpdate",cJSON_CreateBool(false));
 text=cJSON_PrintUnformatted(root);assert(pico_mindreset_parse(text,strlen(text),current,&out,&update)==ESP_OK&&!update);free(text);cJSON_Delete(root);
 printf("PASS: MindReset identity/channel/version/artifact contract, anonymous query, signed URL bounds; %u malformed payloads rejected\n",rejected);
}
