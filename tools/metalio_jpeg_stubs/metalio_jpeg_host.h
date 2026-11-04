/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include <assert.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_LOGI(tag,...) do{(void)(tag);if(0)printf(__VA_ARGS__);}while(0)
#define ESP_LOGD ESP_LOGI
static size_t test_free=32u*1024u*1024u,test_largest=32u*1024u*1024u;
static int test_hw_error,test_alloc_fail,test_allocs,test_engines,test_hw_calls;
static int64_t test_time;
static int64_t test_time_step;
static int64_t esp_timer_get_time(void){test_time+=test_time_step;return test_time;}
static unsigned test_cancel_after_yields;
static void vTaskDelay(int ticks){assert(ticks==1);test_time+=1000;
 if(test_cancel_after_yields && !--test_cancel_after_yields)metalio_jpeg_cancel();}
static size_t heap_caps_get_free_size(int caps){(void)caps;return test_free;}
static size_t heap_caps_get_largest_free_block(int caps){(void)caps;return test_largest;}
static void *heap_caps_malloc(size_t bytes,int caps){(void)caps;return malloc(bytes);}
typedef void *jpeg_decoder_handle_t;
enum {JPEG_DEC_ALLOC_INPUT_BUFFER,JPEG_DEC_ALLOC_OUTPUT_BUFFER,JPEG_DECODE_OUT_FORMAT_GRAY,JPEG_DECODE_OUT_FORMAT_RGB888,
      JPEG_DEC_RGB_ELEMENT_ORDER_RGB,JPEG_YUV_RGB_CONV_STD_BT601};
typedef struct{int buffer_direction;}jpeg_decode_memory_alloc_cfg_t;
typedef struct{int timeout_ms;}jpeg_decode_engine_cfg_t;
typedef struct{int output_format,rgb_order,conv_std;}jpeg_decode_cfg_t;
static void *jpeg_alloc_decoder_mem(size_t n,const jpeg_decode_memory_alloc_cfg_t *cfg,size_t *actual){
 (void)cfg;*actual=(n+63)&~(size_t)63;return ++test_allocs==test_alloc_fail?NULL:calloc(1,*actual);
}
static int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *cfg,jpeg_decoder_handle_t *h){
 assert(cfg->timeout_ms==200);++test_engines;*h=(void*)1;return 0;
}
static int jpeg_del_decoder_engine(jpeg_decoder_handle_t h){assert(h==(void*)1);--test_engines;return 0;}
// 用真实libjpeg产生带MCU填充的硬件替身，覆盖步长/收尾/颜色转换，不冒充硬件计时。
// Real libjpeg supplies MCU-padded hardware fixtures for stride/cleanup/color tests, never hardware timing.
static int jpeg_decoder_process(jpeg_decoder_handle_t engine,const jpeg_decode_cfg_t *cfg,const uint8_t *data,
 uint32_t size,uint8_t *out,uint32_t capacity,uint32_t *used){
 assert(engine==(void*)1&&cfg->rgb_order==JPEG_DEC_RGB_ELEMENT_ORDER_RGB);++test_hw_calls;
 if(test_hw_error)return test_hw_error;
 struct jpeg_decompress_struct j={0};struct jpeg_error_mgr err;
 j.err=jpeg_std_error(&err);jpeg_create_decompress(&j);jpeg_mem_src(&j,data,size);jpeg_read_header(&j,TRUE);
 unsigned mx=j.comp_info[0].h_samp_factor*8,my=j.comp_info[0].v_samp_factor*8;
 unsigned pw=(j.image_width+mx-1)/mx*mx,ph=(j.image_height+my-1)/my*my;
 j.out_color_space=cfg->output_format==JPEG_DECODE_OUT_FORMAT_GRAY?JCS_GRAYSCALE:JCS_RGB;
 jpeg_start_decompress(&j);unsigned stride=pw*j.output_components;
 assert((size_t)stride*ph<=capacity);memset(out,0xc7,capacity);
 while(j.output_scanline<j.output_height){JSAMPROW p=out+(size_t)j.output_scanline*stride;assert(jpeg_read_scanlines(&j,&p,1)==1);}
 *used=stride*ph;jpeg_finish_decompress(&j);jpeg_destroy_decompress(&j);return 0;
}
