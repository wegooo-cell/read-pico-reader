/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：正文补字按原始墨量校正放大灰边，独立于UI的旧位图缩放。
 * English: Reader supplements calibrate enlarged edges to source ink, separate from legacy UI scaling.
 */
#pragma once
#include "ui_hanzi.h"
static inline int hanzi_floor(int n,int d){return n>=0?n/d:-((-n+d-1)/d);}
static inline void ui_hanzi_aa_bounds(const uint8_t*r,int px,int*x0,int*y0,int*x1,int*y1){
 *x0=hanzi_floor((int8_t)r[0]*px,24);*y0=hanzi_floor((int8_t)r[1]*px,24);
 *x1=-hanzi_floor(-((int8_t)r[0]+r[2])*px,24);*y1=-hanzi_floor(-((int8_t)r[1]+r[3])*px,24);
}
static inline unsigned hanzi_aa_sample(const uint8_t*r,int x,int y){
 if(x<0||y<0||x>=r[2]||y>=r[3])return 0;
 unsigned p=(unsigned)y*24+x;return ((r[5+p/2]>>((p%2)*4))&15)*17;
}
static inline void hanzi_cubic_weights(unsigned fraction,float w[4]){
 float t=fraction/256.f,t2=t*t,t3=t2*t;
 w[0]=-.5f*t+t2-.5f*t3;w[1]=1-2.5f*t2+1.5f*t3;
 w[2]=.5f*t+2*t2-1.5f*t3;w[3]=-.5f*t2+.5f*t3;
}
static inline uint8_t ui_hanzi_aa_sample_scaled(const uint8_t*r,int px,int x,int y){
 if(px<8||px>160)return 0;
 int ox=(int8_t)r[0],oy=(int8_t)r[1];
 if(px<24){
  int l=hanzi_floor(x*24*256,px)-ox*256,t=hanzi_floor(y*24*256,px)-oy*256;
  int right=hanzi_floor((x+1)*24*256,px)-ox*256,bottom=hanzi_floor((y+1)*24*256,px)-oy*256;
  unsigned total=(unsigned)(right-l)*(unsigned)(bottom-t),sum=0;
  for(int sy=hanzi_floor(t,256);sy*256<bottom;++sy)for(int sx=hanzi_floor(l,256);sx*256<right;++sx){
   int a=l>sx*256?l:sx*256,b=right<(sx+1)*256?right:(sx+1)*256;
   int c=t>sy*256?t:sy*256,d=bottom<(sy+1)*256?bottom:(sy+1)*256;
   sum+=hanzi_aa_sample(r,sx,sy)*(unsigned)(b-a)*(unsigned)(d-c);
  }
  return total?(uint8_t)((sum+total/2)/total):0;
 }
 int sx=hanzi_floor((2*x+1)*24*256,2*px)-ox*256-128,sy=hanzi_floor((2*y+1)*24*256,2*px)-oy*256-128;
 int ix=hanzi_floor(sx,256),iy=hanzi_floor(sy,256);unsigned fx=sx-ix*256,fy=sy-iy*256;
 if(px==24)return (uint8_t)hanzi_aa_sample(r,ix,iy);
 // 四点插值减少低分辨率灰边向笔画间隙扩散。/ Four-tap interpolation limits gray-edge spread into stroke gaps.
 float wx[4],wy[4],sum=0;hanzi_cubic_weights(fx,wx);hanzi_cubic_weights(fy,wy);
 for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx)sum+=hanzi_aa_sample(r,ix+xx-1,iy+yy-1)*wx[xx]*wy[yy];
 int coverage=(int)(sum+.5f);
 return (uint8_t)(coverage<0?0:coverage>255?255:coverage);
}
static inline uint8_t hanzi_aa_edge(unsigned alpha,int px,int center){
 if(!alpha)return 0;
 int a=128+((int)alpha*256-center)*px/(24*256);
 return (uint8_t)(a<0?0:a>255?255:a);
}
static inline void ui_hanzi_aa_raster(const uint8_t*r,int px,int x0,int y0,
                                    int width,int height,uint8_t*out,const uint8_t cover[256]){
 uint16_t histogram[256]={0};
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){
  uint8_t a=ui_hanzi_aa_sample_scaled(r,px,x0+x,y0+y);out[y*width+x]=a;++histogram[a];
 }
 if(px<=24)return;
 // 面积随字号平方变化，灰边的附加墨量随周长变化；避免将密集字一律加粗。
 // Area scales quadratically and extra AA ink with perimeter, avoiding blanket thickening of dense glyphs.
 uint32_t area=0,edge=0;
 for(int y=0;y<r[3];++y)for(int x=0;x<r[2];++x){
  unsigned a=hanzi_aa_sample(r,x,y);area+=a;edge+=cover[a];
 }
 uint64_t total=(uint64_t)area*px*px;
 if(edge>=area)total+=(uint64_t)(edge-area)*px*24;
 else total-=(uint64_t)(area-edge)*px*24;
 uint32_t target=(uint32_t)((total+288)/576);
 int low=0,high=255*256;
 for(int pass=0;pass<16&&low<high;++pass){
  int center=(low+high)/2;uint32_t ink=0;
  for(unsigned a=1;a<256;++a)if(histogram[a])ink+=(uint32_t)histogram[a]*cover[hanzi_aa_edge(a,px,center)];
  if(ink>target)low=center+1;else high=center;
 }
 uint8_t lut[256];
 for(unsigned a=0;a<256;++a)lut[a]=hanzi_aa_edge(a,px,(low+high)/2);
 for(int i=0;i<width*height;++i)out[i]=lut[out[i]];
}
