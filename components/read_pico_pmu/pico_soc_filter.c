/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：五样本中位数与有界高段下降，拔电回落不伪装成瞬时耗电；无电流计不声称精确容量。
 * English: A five-sample median and bounded upper-range descent reject unplug sag, without claiming coulomb-counter accuracy.
 */
#include "pico_soc_filter.h"
#include <string.h>
// 采样中位数：单个离谱读数不该直接改变显示。
// Median of the samples: one wild reading must not move the display directly.
static uint16_t soc_median(const uint16_t *v,unsigned n){
    uint16_t s[5];memcpy(s,v,n*sizeof(*s));
    for(unsigned i=1;i<n;++i){uint16_t x=s[i];unsigned j=i;while(j&&s[j-1]>x){s[j]=s[j-1];--j;}s[j]=x;}
    return s[n/2];
}

// 电压下限：空载电压已经很低时，不允许库仑读数继续虚高。
// Voltage floor: a low resting voltage overrules an optimistic gauge reading.
static uint16_t soc_voltage_floor(uint16_t mv){
    if(mv<=3100)return 10;
    if(mv<=3200)return 30;
    if(mv<=3300)return 60;
    if(mv<=3400)return 120;
    if(mv<=3500)return 250;
    if(mv<=3600)return 450;
    if(mv<=3700)return 650;
    return 0;
}

uint16_t pico_soc_filter_update(pico_soc_filter_t *s,uint16_t raw,uint16_t mv,
                               bool powered,bool full,bool low,uint32_t now){
    if(!s||raw>1000)return raw;
    if(!s->valid){
        *s=(pico_soc_filter_t){.shown=raw,.sampled_ms=now,.changed_ms=now,.powered=powered,.valid=true};
        s->samples[0]=raw;s->mvs[0]=mv;s->count=1;return raw;
    }
    if(s->powered!=powered){s->powered=powered;s->changed_ms=now;s->hold_ms=0;}
    uint32_t elapsed=now-s->sampled_ms;
    if(elapsed<1000)return s->shown;
    s->sampled_ms=now;
    s->samples[s->next]=raw;s->mvs[s->next]=mv;
    s->next=(s->next+1)%5;if(s->count<5)++s->count;
    uint16_t target=soc_median(s->samples,s->count);
    uint16_t mvmed=soc_median(s->mvs,s->count);
    if(full&&raw>=995){s->shown=1000;s->hold_ms=0;return s->shown;}
    bool near_empty=false;
    if(!powered&&mvmed){
        uint16_t floor=soc_voltage_floor(mvmed);
        if(floor&&target>floor)target=floor;
        near_empty=floor&&floor<300;
    }
    // 2% 滞回：小噪声不刷新数字；只有持续偏差超过保持时间才移动。
    // 2% hysteresis keeps noise off the number; move only after the gap persists.
    if(!(target>s->shown+20||target+20<s->shown)){s->hold_ms=0;return s->shown;}
    uint32_t hold_need=near_empty?1000:(low?5000:20000);
    if(s->hold_ms<hold_need){s->hold_ms+=elapsed;if(s->hold_ms>hold_need)s->hold_ms=hold_need;return s->shown;}
    uint32_t window=elapsed>5000?5000:elapsed;
    uint16_t down=(uint16_t)(window/(near_empty?50:(low?200:1000)));
    uint16_t up=(uint16_t)(window/200);
    if(!down){down=1;}
    if(!up){up=1;}
    if(target>s->shown){
        uint16_t step=target-s->shown;
        if(step>up)step=up;
        s->shown+=step;
    }else{
        uint16_t step=s->shown-target;
        if(step>down)step=down;
        s->shown-=step;
    }
    return s->shown;
}
