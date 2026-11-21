/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * S31 only: read P1.7 before application loading, through a bounded open-drain bus.
 * Never write expander configuration/outputs or use the shared IRQ as key evidence.
 * User2026-11-20: keep XOE/MODE inactive before app loading to protect retained lock art.
 */
#include "sdkconfig.h"
#include "metalio_early_key_record.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "hal/gpio_ll.h"
#include "esp_image_format.h"
#include "esp_app_desc.h"
#include "bootloader_flash_priv.h"
#include "soc/soc.h"

#define SDA 0
#define SCL 1
// Initialized .data survives bootloader_init's BSS clear; never put this in BSS.
static metalio_early_key_record_t captured={.magic=METALIO_EARLY_KEY_MAGIC};
void bootloader_hooks_include(void) {}

static void delay_us(unsigned us) { esp_rom_delay_us(us); }
static void set(unsigned pin,bool high) { gpio_ll_set_level(&GPIO,pin,high); }
static bool high(unsigned pin) { return gpio_ll_get_level(&GPIO,pin)!=0; }
static bool clock_high(void) {
    set(SCL,true);
    for(unsigned n=0;n<100;n++) {
        if(high(SCL)){delay_us(5);return true;}
        delay_us(1);
    }
    return false;
}
static bool start(void) {
    set(SDA,true);
    if(!clock_high() || !high(SDA))return false;
    set(SDA,false);delay_us(5);set(SCL,false);delay_us(5);return true;
}
static bool stop(void) {
    set(SCL,false);set(SDA,false);delay_us(5);
    bool ok=clock_high();set(SDA,true);delay_us(5);return ok;
}
static bool write_byte(uint8_t byte) {
    for(unsigned bit=0;bit<8;bit++) {
        set(SDA,(byte&0x80)!=0);byte<<=1;delay_us(5);
        if(!clock_high())return false;
        set(SCL,false);delay_us(5);
    }
    set(SDA,true);delay_us(5);
    if(!clock_high())return false;
    bool ack=!high(SDA);set(SCL,false);delay_us(5);return ack;
}
static bool read_register(uint8_t reg,uint8_t *value) {
    bool ok=start() && write_byte(0x40) && write_byte(reg) &&
        start() && write_byte(0x41);
    uint8_t byte=0;
    if(ok) {
        set(SDA,true);
        for(unsigned bit=0;bit<8;bit++) {
            if(!clock_high()){ok=false;break;}
            byte=(uint8_t)((byte<<1)|high(SDA));set(SCL,false);delay_us(5);
        }
    }
    if(ok) { // final NACK: this is a one-byte register read
        set(SDA,true);ok=clock_high();set(SCL,false);delay_us(5);
    }
    bool stopped=stop();
    if(ok && stopped)*value=byte;
    return ok && stopped;
}
static void panel_idle_early(void) {
    const unsigned pins[]={18,19};
    for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);++i) {
        unsigned pin=pins[i];
        gpio_ll_set_level(&GPIO,pin,0);
        gpio_ll_func_sel(&GPIO,pin,PIN_FUNC_GPIO);
        esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
        gpio_ll_output_enable(&GPIO,pin);
        gpio_ll_hold_dis(&GPIO,pin);
    }
}
void bootloader_before_init(void) {
    panel_idle_early();
    unsigned reason=(unsigned)esp_rom_get_reset_reason(0);
    if(reason!=METALIO_EARLY_KEY_POWERON) {
        captured=metalio_early_key_sample(reason,read_register,delay_us);
        return;
    }
    // GPIO0/1 only; open drain + existing board pull-ups. Every error releases both.
    for(unsigned pin=0;pin<=1;pin++) {
        gpio_ll_set_level(&GPIO,pin,1);
        gpio_ll_func_sel(&GPIO,pin,PIN_FUNC_GPIO);
        esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
        gpio_ll_od_enable(&GPIO,pin);
        gpio_ll_input_enable(&GPIO,pin);
        gpio_ll_pullup_en(&GPIO,pin);
        gpio_ll_pulldown_dis(&GPIO,pin);
        gpio_ll_output_enable(&GPIO,pin);
        gpio_ll_hold_dis(&GPIO,pin);
    }
    captured=metalio_early_key_sample(reason,read_register,delay_us);
    for(unsigned pin=0;pin<=1;pin++) {
        set(pin,true);
        gpio_ll_output_disable(&GPIO,pin);
    }
}
esp_err_t __real_bootloader_load_image(const esp_partition_pos_t *part,esp_image_metadata_t *data);
esp_err_t __wrap_bootloader_load_image(const esp_partition_pos_t *part,esp_image_metadata_t *data) {
    // Keep SDK slot selection, validation, rollback and loading unchanged.
    esp_err_t result=__real_bootloader_load_image(part,data);
    if(result!=ESP_OK || captured.reason!=METALIO_EARLY_KEY_POWERON ||
       data->start_addr!=part->offset || data->image.chip_id!=32 ||
       (part->offset!=0x10000 && part->offset!=0x510000) || part->size!=0x500000 ||
       !data->image.segment_count || data->image.segment_count>ESP_IMAGE_MAX_SEGMENTS ||
       data->segments[0].data_len<sizeof(esp_app_desc_t)+sizeof(metalio_early_key_app_desc_t))return result;
    metalio_early_key_app_desc_t desc;
    if(bootloader_flash_read(data->segment_data[0]+sizeof(esp_app_desc_t),&desc,sizeof(desc),true)!=ESP_OK ||
       !metalio_early_key_descriptor_valid(&desc,SOC_RTC_DRAM_LOW,SOC_RTC_DRAM_HIGH))return result;
    // A noinit handoff must never overlap code/data loaded from the image.
    for(unsigned i=0;i<data->image.segment_count;i++) {
        uint64_t begin=data->segments[i].load_addr,end=begin+data->segments[i].data_len;
        if((uint64_t)desc.address<end && (uint64_t)desc.address+desc.size>begin)return result;
    }
    // Legacy images have no descriptor and receive no writes; no SDK RTC layout changes.
    memcpy((void *)(uintptr_t)desc.address,&captured,sizeof(captured));
    return result;
}

