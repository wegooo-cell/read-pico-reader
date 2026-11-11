/*
 * C adaptation of CloudZao/Metalio-E-INK4-Plus Bluetooth button decoders.
 * Upstream: 874538eb54f7f7eb593cd40cd1ba6dc8497226e7
 * SPDX-License-Identifier: MIT
 * MIT License
 *
 * Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 * Copyright (c) 2025 Project Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef enum { PT_GENERIC, PT_FREE2, PT_FREE3R, PT_SMART1E, PT_HBTR002, PT_HBTR003 } pt_model_t;
enum { PT_INVALID=255, PT_RELEASE=0, PT_PREV=1, PT_NEXT=2, PT_STANDBY=3 };
typedef struct { bool touching, standby_down; int start_x, start_y; uint8_t held; } pt_decoder_t;
static inline pt_model_t pt_model_from_name(const char *name) {
    static const char *const names[]={"Free 2","Free3-R","Smart 1-E","HBTR002-X","HBTR003-X"};
    if(name)for(unsigned i=0;i<5;++i)if(!strcmp(name,names[i]))return (pt_model_t)(i+1);
    return PT_GENERIC;
}
static inline bool pt_hbtr(pt_model_t m) { return m==PT_HBTR002 || m==PT_HBTR003; }
static inline bool pt_report_supported(pt_model_t m,uint8_t id,uint8_t type) {
    if(type!=1)return false;
    if(m==PT_FREE2)return id==1||id==3||id==4;
    if(m==PT_FREE3R)return id==3;
    if(m==PT_SMART1E)return id==1;
    return pt_hbtr(m)&&(id==1||id==2);
}
// Pure direction lookup also serves raw manual-binding fallback; report IDs are checked by the transport.
static inline uint8_t pt_button_code(pt_model_t m,const uint8_t *data,size_t n) {
    if(!data)return PT_INVALID;
    if(m==PT_FREE2) {
        if(n==1&&(data[0]==1||data[0]==2))return data[0];
        if(n==3&&!data[1]&&!data[2]&&data[0]<=2)return data[0];
    } else if(m==PT_FREE3R&&n==3&&!data[1]&&!data[2]) {
        switch(data[0]) { case 0:return PT_RELEASE;case 2:return PT_PREV;case 1:return PT_NEXT;case 8:return PT_STANDBY; }
    } else if(m==PT_SMART1E&&n==1) {
        switch(data[0]) { case 1:return PT_PREV;case 2:return PT_NEXT;case 6:return PT_STANDBY; }
    } else if(pt_hbtr(m)&&n==1&&(data[0]==1||data[0]==2))return data[0]; // completed, canonical gesture
    return PT_INVALID;
}
// One normalized event per physical click or complete horizontal gesture. No generic HID debounce here.
static inline uint8_t pt_decode(pt_model_t m,pt_decoder_t *s,uint8_t id,const uint8_t *d,size_t n) {
    if(!d||!pt_report_supported(m,id,1))return PT_INVALID;
    if(!pt_hbtr(m)) {
        if(m==PT_FREE2&&((id==1&&n!=1)||(id!=1&&n!=3)))return PT_INVALID;
        uint8_t code=pt_button_code(m,d,n);
        if(code==PT_INVALID)return code;
        if(n==3) { uint8_t last=s->held;s->held=code;if(!code||code==last)return PT_INVALID; }
        return code;
    }
    if(id==1&&n==2) {
        if(!d[0]&&!d[1])s->standby_down=false;
        else if(!d[0]&&d[1]==0x10&&!s->standby_down) { s->standby_down=true;return PT_STANDBY; }
        return PT_INVALID;
    }
    if(id!=2)return PT_INVALID;
    if(n!=4||(d[0]!=0&&d[0]!=2&&d[0]!=7)) { s->touching=false;return PT_INVALID; }
    const int x=d[1]|((d[2]&15)<<8),y=(d[2]>>4)|(d[3]<<4);
    if(d[0]==7) { if(!s->touching) {s->start_x=x;s->start_y=y;}s->touching=true;return PT_INVALID; }
    if(!s->touching)return PT_INVALID;
    s->touching=false;
    const int dx=x-s->start_x,dy=y-s->start_y;
    const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    if(ax<100||ay>ax)return PT_INVALID;
    return dx>0?PT_PREV:PT_NEXT;
}
