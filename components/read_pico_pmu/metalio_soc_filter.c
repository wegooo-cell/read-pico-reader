/* SPDX-License-Identifier: MIT
 * C port of CloudZao/Metalio-E-INK4-Plus@690a023c66862af103a3187464373ee7c5efcd14,
 * verified unchanged in vendor v2.0.77@874538eb54f7f7eb593cd40cd1ba6dc8497226e7.
 * bq27220_gauge.{h,cc}. Original notice: ../../licenses/Metalio-MIT.txt.
 * 中文：显示算法不写电池或充电芯片，不改变截止电压、电流或保护。
 * English: Display policy only; never writes gauge/charger or changes limits/protection.
 */
#include "metalio_soc_filter.h"
#include <string.h>
#define SECOND_US INT64_C(1000000)
#define STEP_US (30*SECOND_US)
float metalio_soc_voltage_percent(uint16_t mv,bool charging) {
    static const uint16_t discharge[]={3300,3481,3565,3624,3663,3702,3757,3828,3919,4021,4211};
    if(mv<=3300)return 0;
    if(charging)return mv>=4300?100.0f:(mv-3300)*100.0f/1000;
    for(unsigned i=1;i<11;++i)if(mv<=discharge[i])
        return (i-1)*10.0f+(mv-discharge[i-1])*10.0f/(discharge[i]-discharge[i-1]);
    return 100;
}
void metalio_soc_filter_reset(metalio_soc_filter_t *f,int64_t now) {
    if(!f)return;
    bool full=f->full_confirmed;
    memset(f,0,sizeof(*f));f->displayed=-1;f->ready_at=now+3*SECOND_US;f->full_confirmed=full;
}
bool metalio_soc_filter_update(metalio_soc_filter_t *f,bool valid,uint16_t mv,int16_t ma,
                               int64_t now,int *level,bool *charging) {
    if(!f||!level||!charging)return false;
    *charging=false;
    if(now<f->ready_at)return false;
    if(!valid||mv<2500||mv>5000){f->full_since=0;return false;}
    if(f->last_sample&&now-f->last_sample>2*SECOND_US)f->full_since=0;
    f->last_sample=now;
    const bool raw=ma>5;
    // 换曲线重新填充窗口，显示值与步进计时仍保留。/ Re-prime on curve changes while retaining displayed SOC and stepping.
    if(f->displayed>=0&&raw!=(f->last_direction>0))f->primed=false;
    if(!f->primed||now-f->last_filter>=SECOND_US){
        float sample=metalio_soc_voltage_percent(mv,raw);
        if(!f->primed){
            for(unsigned i=0;i<60;++i)f->samples[i]=sample;
            f->sum=sample*60;f->next=0;f->primed=true;
        }else{
            f->sum-=f->samples[f->next];f->samples[f->next]=sample;f->sum+=sample;f->next=(f->next+1)%60;
        }
        f->last_filter=now;
    }
    int target=(int)(f->sum/60);
    if(target<0)target=0;
    if(target>100)target=100;
    if(mv>=4300){
        if(!f->full_since)f->full_since=now;
        if(now-f->full_since>=STEP_US)f->full_confirmed=true;
    }else f->full_since=0;
    if(mv<4280)f->full_confirmed=false;
    if(target>=100&&!f->full_confirmed)target=95;
    if(f->full_confirmed)target=100;
    int direction=raw?1:ma< -5?-1:0;
    if(f->displayed>=0&&direction!=f->last_direction)f->last_step=now;
    f->last_direction=direction;
    target=target/5*5;
    if(f->displayed<0){f->displayed=target;f->last_step=now;}
    else if(now-f->last_step>=STEP_US){
        bool rising=raw||f->full_confirmed;
        if((rising&&target>f->displayed)||(!rising&&target<f->displayed)){
            f->displayed+=rising?5:-5;f->last_step=now;
        }
    }
    *level=f->displayed;*charging=raw&&*level<100;return true;
}
