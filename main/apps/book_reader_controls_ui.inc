// 中文：阅读设置和手势示意共用命中矩形；绘图不执行存储或翻页。
// English: Reading sheets and gesture diagrams share hit rectangles; painting never stores or turns pages.
static int rc_root_top(void) { return 576; }
static EpdRect rc_done_rect(void){return(EpdRect){36,1141,612,58};}
static EpdRect rc_tab_rect(int i){return(EpdRect){40+i*202,rc_root_top()+100,200,46};}
static EpdRect rc_grid_rect(int i) { return (EpdRect){36+(i%2)*314,746+(i/2)*198,298,176}; }
static EpdRect rc_effect_rect(int i) { return (EpdRect){36+i*208,786,196,68}; }
static EpdRect rc_refresh_rect(void) { return (EpdRect){36,882,612,84}; }
static EpdRect rc_auto_card(int column) { return (EpdRect){36+column*314,994,298,126}; }
static EpdRect rc_interval_rect(unsigned plus) {
    EpdRect card = rc_auto_card(1);
    return (EpdRect){card.x+(plus?220:16),card.y+card.height-56,62,48};
}
static EpdRect rc_display_rect(int i){return(EpdRect){36+(i%2)*314,880+(i/2)*130,298,110};}
static EpdRect rc_touch_rect(int target){
    switch(target){
    case RC_TAP_CENTER:return(EpdRect){210,934,264,52};
    case RC_HOLD:return(EpdRect){224,758,236,56};
    case RC_BOTTOM_LEFT:return(EpdRect){36,1112,204,52};
    case RC_BOTTOM_CENTER:return(EpdRect){248,1112,188,52};
    case RC_BOTTOM_RIGHT:return(EpdRect){448,1112,200,52};
    case RC_SLOT_COUNT:return(EpdRect){72,856,164,52};
    case RC_SLOT_COUNT+1:return(EpdRect){448,856,164,52};
    default:return(EpdRect){0};
    }
}
static const char *rc_action_name(unsigned action){
    static const char*const names[]={"关闭","上一页","下一页","呼出工具栏","切换全屏","目录","添加/取消书签","自动翻页开关","切换翻页效果","全刷一次","阅读线开关","书签列表","阅读进度","文字设置","排版设置","阅读显示","翻页设置","阅读统计","前光开关","亮度调节","色温调节","工具栏开关"};
    return names[action<RC_ACTION_COUNT?action:RC_NONE];
}
static const char *rc_side_name(unsigned side){
    static const char*const names[]={"关闭","亮度","色温","字号","行间距"};return names[side<RC_SIDE_COUNT?side:0];
}
static void rc_text(uint8_t*fb,EpdRect r,int px,const char*text,bool center,bool inverse){ui_text_box(fb,r,px,text,center?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT,inverse);}
static void rc_card(uint8_t*fb,EpdRect r){ui_fill_round_rect(fb,r,20,UI_GRAY_WHITE);ui_draw_control_frame(fb,r,20,0x68);}
static void rc_done(uint8_t*fb,const char*label){EpdRect r=rc_done_rect();ui_fill_round_rect(fb,r,20,UI_GRAY_BLACK);rc_text(fb,(EpdRect){50,1146,584,48},26,label,true,true);}
static void rc_link(uint8_t*fb,EpdRect r,const char*title,const char*value){
    rc_card(fb,r);
    if(r.height<100){
        rc_text(fb,(EpdRect){r.x+21,r.y+17,r.width-240,50},26,title,false,false);
        rc_text(fb,(EpdRect){r.x+r.width-200,r.y+17,145,50},24,value,true,false);
        ui_draw_icon(fb,r.x+r.width-25,r.y+r.height/2,25,UI_ICON_CHEVRON_RIGHT,0x38);return;
    }
    rc_text(fb,(EpdRect){r.x+21,r.y+12,r.width-42,44},r.height>120?30:26,title,false,false);
    rc_text(fb,(EpdRect){r.x+21,r.y+r.height-56,r.width-76,40},21,value,false,false);
    ui_draw_icon(fb,r.x+r.width-25,r.y+r.height-36,25,UI_ICON_CHEVRON_RIGHT,0x38);
}
static void rc_toggle_card(uint8_t*fb,EpdRect r,const char*title,bool on){
    rc_card(fb,r);rc_text(fb,(EpdRect){r.x+21,r.y+8,r.width-42,38},23,title,false,false);
    int row = r.y + r.height - 54;
    rc_text(fb,(EpdRect){r.x+21,row,r.width-122,44},26,on?"开启":"关闭",false,false);
    draw_reading_switch(fb,(EpdRect){r.x+r.width-89,row+8,69,39},on);
}
static void rc_draw_settings(uint8_t*fb){
    ui_text_set_system_scale(true);int top=rc_root_top();draw_sheet(fb,top,"阅读设置");
    static const char*const tabs[]={"排版","翻页","手势"};
    ui_fill_round_rect(fb,(EpdRect){36,top+96,612,54},18,0xe8);
    for(int i=0;i<3;++i){EpdRect r=rc_tab_rect(i);if(i==(int)s_rc_tab)ui_fill_round_rect(fb,r,15,UI_GRAY_BLACK);rc_text(fb,r,26,tabs[i],true,i==(int)s_rc_tab);}
    if(s_rc_tab==0){
        const char*const titles[]={"文字设置","排版设置","阅读显示","阅读线"};
        const char*const values[]={"字号 · 字体 · 字重","边距 · 行距 · 缩进","全屏 · 图片 · 竖排","样式 · 范围 · 位置"};
        for(int i=0;i<4;++i)rc_link(fb,rc_grid_rect(i),titles[i],values[i]);
    }else if(s_rc_tab==1){
        rc_text(fb,(EpdRect){42,746,590,30},21,"翻页效果",false,false);
        const char*const effects[]={"普通","水波纹","速读黑白"};
        for(int i=0;i<3;++i){EpdRect r=rc_effect_rect(i);bool on=app_settings_reader_turn_effect()==i;
            ui_fill_round_rect(fb,r,17,on?UI_GRAY_BLACK:0xf0);ui_draw_control_frame(fb,r,17,on?UI_GRAY_BLACK:0x98);rc_text(fb,r,24,effects[i],true,on);}
        char value[24];unsigned full=app_settings_reader_full_pages();if(full)snprintf(value,sizeof(value),"每 %u 页",full);else snprintf(value,sizeof(value),"不强制");
        rc_link(fb,rc_refresh_rect(),"全刷间隔",value);
        rc_toggle_card(fb,rc_auto_card(0),"自动翻页",s_rc_auto.running);
        const unsigned seconds = reader_controls_seconds(app_settings_reader_controls());
        EpdRect card = rc_auto_card(1), step = rc_interval_rect(0);
        snprintf(value,sizeof(value),"%u 秒",seconds);
        rc_card(fb,card);
        rc_text(fb,(EpdRect){card.x+21,card.y+8,card.width-42,38},23,"翻页间隔",false,false);
        rc_text(fb,(EpdRect){card.x+82,step.y,134,step.height},26,value,true,false);
        for (unsigned i=0; i<2; ++i) {
            EpdRect r = rc_interval_rect(i);
            bool enabled = i ? seconds < READER_AUTO_MAX_SECONDS : seconds > READER_AUTO_MIN_SECONDS;
            ui_fill_round_rect(fb,r,12,enabled?0xe8:0xf6);
            ui_draw_control_frame(fb,r,12,enabled?0x60:0xc0);
            rc_text(fb,r,30,i?"+":"−",true,false);
        }
    }else{
        rc_link(fb,(EpdRect){36,746,612,110},"触摸操作","全屏示意 · 点按 · 长按 · 滑动");
        rc_link(fb,(EpdRect){36,878,612,110},"点击翻页区域",app_settings_reader_vertical_turn()?"上下翻页":"左右翻页");
#if KIIKO_BOARD_METALIO47
        rc_link(fb,(EpdRect){36,1010,612,110},"按键控制","左键 · 圆圈键 · 右键");
#else
        rc_link(fb,(EpdRect){36,1010,612,110},"按键控制","左键 · 中键 · 右键");
#endif
    }
    rc_done(fb,"完成");ui_text_set_system_scale(false);
}
static void rc_draw_display(uint8_t*fb){
    ui_text_set_system_scale(true);draw_sheet(fb,740,"阅读显示");draw_sheet_back(fb,740);
    rc_toggle_card(fb,rc_display_rect(0),"默认全屏",app_settings_reader_default_fullscreen());
    rc_toggle_card(fb,rc_display_rect(1),"全屏沉浸",app_settings_reader_immersive());
    rc_toggle_card(fb,rc_display_rect(2),"显示书内图片",!app_settings_reader_hide_images());
    rc_toggle_card(fb,rc_display_rect(3),"竖排（右至左）",app_settings_book_vertical());
    rc_done(fb,"返回排版");ui_text_set_system_scale(false);
}
static void rc_badge(uint8_t*fb,EpdRect r,const char*label,bool fixed,bool white){
    if(white)rc_card(fb,r);else ui_fill_round_rect(fb,r,12,UI_GRAY_BLACK);
    rc_text(fb,(EpdRect){r.x+10,r.y+4,r.width-44,r.height-8},25,label,true,!white);
    ui_draw_icon(fb,r.x+r.width-22,r.y+r.height/2,23,fixed?UI_ICON_LOCK:UI_ICON_CHEVRON_RIGHT,white?0x38:UI_GRAY_WHITE);
}
static void rc_arrow(uint8_t*fb,int x1,int y1,int x2,int y2,uint8_t gray){
    for(int i=-1;i<=1;++i)epd_draw_line(x1+i,y1,x2+i,y2,gray,fb);
    int dx=x2-x1,dy=y2-y1;
    if(abs(dx)>abs(dy)){int sign=dx>0?-1:1;epd_draw_line(x2,y2,x2+sign*12,y2-9,gray,fb);epd_draw_line(x2,y2,x2+sign*12,y2+9,gray,fb);}
    else{int sign=dy>0?-1:1;epd_draw_line(x2,y2,x2-9,y2+sign*12,gray,fb);epd_draw_line(x2,y2,x2+9,y2+sign*12,gray,fb);}
}
static unsigned rc_pool_count(unsigned group){return group==0?(KIIKO_BOARD_METALIO47?12:11):(KIIKO_BOARD_METALIO47?10:8);}
static unsigned rc_pool_action(unsigned group,unsigned index){
    static const uint8_t quick[]={RC_BOOKMARK,RC_AUTOTURN,RC_EFFECT,RC_REFRESH,RC_FULLSCREEN,RC_TOOLS,RC_TOOLS_TOGGLE,RC_GUIDE,RC_PREVIOUS,RC_NEXT,RC_NONE,RC_LIGHT};
    static const uint8_t pages[]={RC_CONTENTS,RC_BOOKMARKS,RC_PROGRESS,RC_TEXT,RC_LAYOUT,RC_DISPLAY,RC_TURN_SETTINGS,RC_STATS,RC_BRIGHTNESS,RC_WARMTH};
    return group==0?quick[index<12?index:10]:pages[index<10?index:0];
}
static unsigned rc_side_pool(unsigned index){
    static const uint8_t ids_s31[]={RC_SIDE_BRIGHTNESS,RC_SIDE_WARMTH,RC_SIDE_OFF,RC_SIDE_FONT,RC_SIDE_LINE};
    static const uint8_t ids_pico[]={RC_SIDE_OFF,RC_SIDE_FONT,RC_SIDE_LINE};
    return KIIKO_BOARD_METALIO47?ids_s31[index<5?index:2]:ids_pico[index<3?index:0];
}
static EpdRect rc_menu_rect(void){
    unsigned count=s_rc_menu_slot>=RC_SLOT_COUNT?(KIIKO_BOARD_METALIO47?5:3):rc_pool_count(s_rc_menu_group);
    int h=(s_rc_menu_slot>=RC_SLOT_COUNT?12:64)+(int)count*48;
    int x=s_rc_anchor.x;if(x+340>672)x=332;if(x<12)x=12;
    int y=s_rc_anchor.y+s_rc_anchor.height+8;if(y+h>1204)y=s_rc_anchor.y-8-h;if(y<12)y=12;
    return(EpdRect){x,y,340,h};
}
static void rc_draw_menu(uint8_t*fb){
    if(s_rc_menu_slot<0)return;
    const reader_controls_config_t*c=app_settings_reader_controls();EpdRect r=rc_menu_rect();
    ui_fill_round_rect(fb,(EpdRect){r.x+5,r.y+5,r.width,r.height},15,0xd0);rc_card(fb,r);
    bool side=s_rc_menu_slot>=RC_SLOT_COUNT;unsigned count=side?(KIIKO_BOARD_METALIO47?5:3):rc_pool_count(s_rc_menu_group);int offset=side?6:54;
    if(!side){const char*const tabs[]={"快捷动作","打开页面"};for(int i=0;i<2;++i){EpdRect t={r.x+6+i*164,r.y+6,163,42};ui_fill_round_rect(fb,t,10,i==(int)s_rc_menu_group?UI_GRAY_BLACK:0xe8);rc_text(fb,t,21,tabs[i],true,i==(int)s_rc_menu_group);}}
    for(unsigned i=0;i<count;++i){int y=r.y+offset+(int)i*48;unsigned a=side?rc_side_pool(i):rc_pool_action(s_rc_menu_group,i);
        bool selected=side?c->sides[s_rc_menu_slot-RC_SLOT_COUNT]==a:c->actions[s_rc_menu_slot]==a;
        if(selected)ui_fill_round_rect(fb,(EpdRect){r.x+6,y,r.width-12,46},9,0xe4);
        rc_text(fb,(EpdRect){r.x+18,y+2,r.width-62,42},25,side?rc_side_name(a):rc_action_name(a),false,false);
        if(selected)epd_fill_circle(r.x+r.width-25,y+23,6,UI_GRAY_BLACK,fb);
        if(i+1<count)ui_hairline(fb,y+47,r.x+12,r.width-24,0xd0);
    }
}
static void rc_draw_touch(uint8_t*fb){
    const reader_controls_config_t*c=app_settings_reader_controls();ui_text_set_system_scale(true);ui_clear_page(fb);rc_card(fb,(EpdRect){12,12,660,1192});
    ui_draw_icon(fb,44,46,32,UI_ICON_SLIDERS_HORIZONTAL,0x38);ui_draw_icon(fb,640,46,32,UI_ICON_X,0x38);
    ui_fill_round_rect(fb,(EpdRect){284,13,116,40},11,UI_GRAY_BLACK);ui_draw_icon(fb,342,32,27,UI_ICON_CHEVRON_DOWN,UI_GRAY_WHITE);
    rc_badge(fb,(EpdRect){210,83,264,56},"下拉 · 控制中心",true,false);
    rc_text(fb,(EpdRect){250,188,184,32},20,"上下滑动",true,false);rc_badge(fb,(EpdRect){224,230,236,56},"上下滑动翻页",true,false);
    rc_arrow(fb,342,516,342,304,UI_GRAY_BLACK);epd_fill_circle(342,556,9,UI_GRAY_BLACK,fb);rc_arrow(fb,342,596,342,716,UI_GRAY_BLACK);
    rc_badge(fb,(EpdRect){64,520,190,72},"右滑 · 上一页",true,false);rc_badge(fb,(EpdRect){430,520,190,72},"左滑 · 下一页",true,false);
    rc_arrow(fb,276,556,320,556,UI_GRAY_BLACK);rc_arrow(fb,408,556,364,556,UI_GRAY_BLACK);
    char label[96];snprintf(label,sizeof(label),"长按 · %s",rc_action_name(c->actions[RC_HOLD]));rc_badge(fb,rc_touch_rect(RC_HOLD),label,false,false);
    for(unsigned side=0;side<2;++side){int x=side?636:48;uint8_t gray=c->sides[side]?UI_GRAY_BLACK:0xa0;
        rc_arrow(fb,x,840,x,370,gray);rc_arrow(fb,x,930,x,1012,gray);
        rc_badge(fb,rc_touch_rect(RC_SLOT_COUNT+side),rc_side_name(c->sides[side]),false,!c->sides[side]);
        rc_text(fb,(EpdRect){side?442:62,1026,180,28},19,side?"右侧纵滑":"左侧纵滑",true,false);
    }
    snprintf(label,sizeof(label),"单击 · %s",c->actions[RC_TAP_CENTER]==RC_FULLSCREEN?"全屏":rc_action_name(c->actions[RC_TAP_CENTER]));rc_badge(fb,rc_touch_rect(RC_TAP_CENTER),label,false,false);
    rc_text(fb,(EpdRect){132,1070,420,30},21,"底部三个区域：向上滑动",true,false);
    for(int i=0;i<3;++i){unsigned a=c->actions[RC_BOTTOM_LEFT+i];const char*name=a==RC_BOOKMARK?"书签":a==RC_TOOLS?"工具栏":a==RC_AUTOTURN?"自动翻页":rc_action_name(a);
        rc_badge(fb,rc_touch_rect(RC_BOTTOM_LEFT+i),name,false,false);int x=138+i*204;
        ui_fill_round_rect(fb,(EpdRect){x-37,1174,74,30},9,UI_GRAY_BLACK);ui_draw_icon(fb,x,1189,25,UI_ICON_CHEVRON_UP,UI_GRAY_WHITE);}
    rc_draw_menu(fb);ui_text_set_system_scale(false);
}
static const int rc_more_slots[]={RC_BOTTOM_LEFT,RC_BOTTOM_CENTER,RC_BOTTOM_RIGHT,RC_HOLD,RC_TAP_CENTER,RC_DOUBLE_CENTER,-1,-1,-1,RC_TAP_PREVIOUS,RC_TAP_NEXT};
static EpdRect rc_more_rect(unsigned i){return(EpdRect){36+(int)(i%2)*314,84+(int)(i/2)*174,298,154};}
static void rc_draw_touch_more(uint8_t*fb){
    const reader_controls_config_t*c=app_settings_reader_controls();ui_text_set_system_scale(true);ui_clear_page(fb);ui_draw_icon(fb,640,44,32,UI_ICON_X,0x38);
    const char*const names[]={"底部左侧上滑","底部中间上滑","底部右侧上滑","正文长按","中间单击","中间双击","上下滑动","向左滑动","向右滑动","左侧单击","右侧单击"};
    for(unsigned i=0;i<11;++i){EpdRect r=rc_more_rect(i);rc_card(fb,r);int slot=rc_more_slots[i];
        rc_text(fb,(EpdRect){r.x+20,r.y+12,254,42},21,names[i],false,false);
        const char*action=slot>=0?rc_action_name(c->actions[slot]):i==6?"上下翻页":i==7?"下一页":"上一页";
        rc_text(fb,(EpdRect){r.x+20,r.y+88,224,42},25,action,false,false);ui_draw_icon(fb,r.x+272,r.y+109,24,slot<0?UI_ICON_LOCK:UI_ICON_CHEVRON_RIGHT,0x38);}
    rc_done(fb,"返回触摸操作");rc_draw_menu(fb);ui_text_set_system_scale(false);
}
static EpdRect rc_light_slider_rect(void){return(EpdRect){36,950,612,76};}
static void rc_draw_light(uint8_t*fb){
#if KIIKO_BOARD_METALIO47
    ui_text_set_system_scale(true);draw_sheet(fb,790,s_rc_light_warm?"色温调节":"亮度调节");draw_sheet_back(fb,790);
    unsigned level=metalio_frontlight_position(s_rc_light_warm);char value[16];snprintf(value,sizeof(value),"%u%%",level*100/METALIO_FRONTLIGHT_RANGE);
    draw_pill_slider(fb,rc_light_slider_rect(),s_rc_light_warm?"冷":"暗",s_rc_light_warm?"暖":"亮",value,level,METALIO_FRONTLIGHT_RANGE+1,21,21);
    rc_text(fb,(EpdRect){42,1048,590,44},22,"拖动调节，松手保存",true,false);rc_done(fb,"返回阅读");ui_text_set_system_scale(false);
#else
    (void)fb;
#endif
}
static bool rc_draw_current(uint8_t*fb){
    switch(s_reader_panel){
    case READER_PANEL_REFRESH_SETTINGS:rc_draw_settings(fb);return true;
    case READER_PANEL_READING_DISPLAY:rc_draw_display(fb);return true;
    case READER_PANEL_TOUCH_SETTINGS:rc_draw_touch(fb);return true;
    case READER_PANEL_TOUCH_MORE:rc_draw_touch_more(fb);return true;
    case READER_PANEL_READING_LIGHT:rc_draw_light(fb);return true;
    default:return false;
    }
}
