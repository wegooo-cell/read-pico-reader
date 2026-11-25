/* SPDX-License-Identifier: Apache-2.0
 * 中文：用实际OTA代码访问真实平台、真实SHA与Range；闪存接口仅为内存替身。
 * English: Run production OTA code against real HTTPS, SHA and Range with memory-only flash substitutes. */
#define MINDRESET_LIVE_HOST 1
#define MINDRESET_TEST_IMAGE_SIZE 0x400000
#define MINDRESET_TEST_RUNNING_VERSION "0.3.3-alpha.0"
#include <CommonCrypto/CommonDigest.h>
#include "ota_online_host_test.c"
int main(int argc,char **argv){
 assert(argc==2);fixture();wifi_ps=WIFI_PS_MIN_MODEM;
 assert(pico_online_check()==ESP_OK);run();
 if(s_status->state!=PICO_UPDATE_AVAILABLE){fprintf(stderr,"Live query failed: %s; HTTPS failures=%u\n",s_status->message,live_https_failures);return 1;}
 assert(pico_online_download()==ESP_OK);run();
 if(s_status->state!=PICO_UPDATE_READY){fprintf(stderr,"Live download failed: %s; HTTPS failures=%u\n",s_status->message,live_https_failures);return 1;}
 assert(live_disconnected&&range_requests==1&&grant_requests==2&&begin_calls==1&&!boot_calls&&!http_alive);
 FILE *expected=fopen(argv[1],"rb");assert(expected);assert(!fseek(expected,0,SEEK_END));long size=ftell(expected);assert(size>0&&(size_t)size==written_size);rewind(expected);
 uint8_t *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,expected)==(size_t)size);fclose(expected);
 assert(!memcmp(bytes,written,written_size));free(bytes);
 assert(pico_online_commit()==ESP_OK&&boot_calls==1);
 const char *transport=getenv("MINDRESET_HOST_REPLAY_DIR")?"recorded MindReset responses replayed locally":"curl HTTPS TLS1.2 HTTP1.1 with SDK CA PEM";
 printf("{\"passed\":true,\"transport\":\"%s\",\"currentVersion\":\"%s\",\"downloadVersion\":\"%s\",\"downloadBytes\":%zu,\"queryRequests\":%u,\"freshGrantRequests\":%u,\"rangeRequests\":%d,\"fullSha256Verified\":true,\"writtenBytesMatchPublishedImage\":true,\"simulatedBootCommitOnly\":true,\"physicalDeviceAccessed\":false}\n",transport,current.version,s_status->release.version,written_size,query_requests,grant_requests,range_requests);
 free(s_status);s_status=NULL;assert(!pthread_mutex_destroy(&flash_display_mutex));return 0;
}
