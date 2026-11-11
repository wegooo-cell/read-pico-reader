// 中文：自定义动作只在有效正文中执行；菜单和手动输入暂缓计时但不关闭开关。
// English: Custom actions require a valid reader; menus and manual input defer turns without disabling the switch.
static bool rc_zoom_active(void){return s_zoom_active||s_image_zoom>READER_IMAGE_ZOOM_MIN||s_image_pan_active;}
// 中文：交互只延后计时，不关闭用户开关；仅显式关闭/离书/锁屏/书末才停止。
// English: Interactions defer turns without disabling the switch; explicit off/lifecycle/end stop it.
static void rc_pause_auto(void){if(s_rc_auto.running)s_rc_auto_waiting=true;}
static void rc_stop_auto(void){reader_auto_stop(&s_rc_auto);s_rc_auto_waiting=false;}
void app_book_pause_auto(void){rc_pause_auto();}
static void rc_notice(app_ctx_t*ctx,const char*text){copy_text(s_reader_notice,sizeof(s_reader_notice),text);s_reader_notice_until=ctx->now_ms+1800;}
static void rc_invalidate(void){
#if KIIKO_BOARD_METALIO47
    pause_reader_prep();
#else
    invalidate_prep();
#endif
}
static app_redraw_t rc_open(app_ctx_t*ctx,reader_panel_t panel){
    rc_pause_auto();s_rc_menu_slot=-1;s_rc_double_pending=false;s_reader_tap.pending=false;s_reader_slider=-1;s_pressed_control=-1;
    if(s_reader_fullscreen)(void)toggle_reader_fullscreen(ctx);
    s_reader_panel=panel;rc_invalidate();return APP_REDRAW_PAGE;
}
static app_redraw_t rc_to_root(app_ctx_t*ctx,unsigned tab){s_rc_menu_slot=-1;s_rc_tab=tab;return rc_open(ctx,READER_PANEL_REFRESH_SETTINGS);}
static app_redraw_t rc_execute(app_ctx_t*ctx,unsigned action){
    if(!s_text||s_view!=READING||s_clear_confirm||!reader_controls_action_allowed(action,KIIKO_BOARD_METALIO47))return APP_REDRAW_NONE;
    if(action!=RC_AUTOTURN)rc_pause_auto();
    s_reader_tap.pending=false;s_rc_double_pending=false;
    switch(action){
    case RC_NONE:return APP_REDRAW_NONE;
    case RC_PREVIOUS:return turn_page(ctx,-1);
    case RC_NEXT:return turn_page(ctx,1);
    case RC_BOOKMARK:{bool before=bookmark_is_current();bool ok=bookmark_toggle();rc_notice(ctx,ok?(before?"已取消书签":"已添加书签"):"书签保存失败");return APP_REDRAW_PAGE;}
    case RC_AUTOTURN:
        if(s_rc_auto.running){rc_stop_auto();rc_notice(ctx,"自动翻页已暂停");}
        else{
            if(rc_zoom_active()||s_footnote_note>=0){rc_notice(ctx,"请先返回阅读正文");return APP_REDRAW_PAGE;}
            if(s_page+1>=book_layout_page_count()&&s_chapter+1>=book_chapter_count()){rc_notice(ctx,"已到书末");return APP_REDRAW_PAGE;}
            s_reader_panel=READER_PANEL_NONE;s_rc_auto_waiting=false;reader_auto_start(&s_rc_auto,ctx->now_ms,reader_controls_seconds(app_settings_reader_controls()));rc_notice(ctx,"自动翻页已开启");
        }
        return APP_REDRAW_PAGE;
    case RC_EFFECT:{unsigned effect=(app_settings_reader_turn_effect()+1)%3;app_settings_set_reader_turn_effect(effect);s_turns=0;rc_invalidate();rc_notice(ctx,effect==0?"普通翻页":effect==1?"水波纹翻页":"速读黑白翻页");return APP_REDRAW_PAGE;}
    case RC_REFRESH:return reader_manual_refresh(ctx);
    case RC_FULLSCREEN:return toggle_reader_fullscreen(ctx);
    case RC_TOOLS:{
        app_redraw_t redraw=s_reader_fullscreen?toggle_reader_fullscreen(ctx):APP_REDRAW_PAGE;
        s_reader_panel=READER_PANEL_TOOLS;rc_invalidate();return redraw;}
    case RC_TOOLS_TOGGLE:
        if(!s_reader_fullscreen)return toggle_reader_fullscreen(ctx);
        {app_redraw_t redraw=toggle_reader_fullscreen(ctx);s_reader_panel=READER_PANEL_TOOLS;rc_invalidate();return redraw;}
    case RC_GUIDE:{unsigned style=app_settings_book_reading_line();app_settings_set_book_reading_line(style?0:1);rc_invalidate();rc_notice(ctx,style?"阅读线已关闭":"阅读线已开启");return APP_REDRAW_PAGE;}
    case RC_CONTENTS:
        save_progress();set_reader_view(TOC);s_message[0]=0;s_toc_jump_open=s_toc_jump_drag=false;s_reader_panel=READER_PANEL_NONE;ctx->leaf=book_toc_open(current_toc_position());return APP_REDRAW_PAGE;
    case RC_BOOKMARKS:
        s_bookmark_edit=s_bookmark_delete_confirm=s_bookmark_delete_error=false;s_bookmark_selected=0;s_bookmark_page=0;return rc_open(ctx,READER_PANEL_BOOKMARKS);
    case RC_PROGRESS:case RC_STATS:
        if(s_reader_fullscreen)(void)toggle_reader_fullscreen(ctx);
        reader_chapter_jump_begin();rc_invalidate();return APP_REDRAW_PAGE;
    case RC_TEXT:s_rc_font_root=true;return rc_open(ctx,READER_PANEL_FONT_SETTINGS);
    case RC_LAYOUT:return rc_open(ctx,READER_PANEL_LAYOUT_SETTINGS);
    case RC_DISPLAY:return rc_open(ctx,READER_PANEL_READING_DISPLAY);
    case RC_TURN_SETTINGS:return rc_to_root(ctx,1);
#if KIIKO_BOARD_METALIO47
    case RC_LIGHT:metalio_frontlight_toggle();rc_notice(ctx,metalio_frontlight_enabled()?"前光已开启":"前光已关闭");return APP_REDRAW_PAGE;
    case RC_BRIGHTNESS:case RC_WARMTH:s_rc_light_warm=action==RC_WARMTH;return rc_open(ctx,READER_PANEL_READING_LIGHT);
#endif
    default:return APP_REDRAW_NONE;
    }
}
static void rc_open_binding(unsigned slot,EpdRect anchor){s_rc_menu_slot=(int)slot;s_rc_menu_group=0;s_rc_anchor=anchor;}
static app_redraw_t rc_binding_tap(app_ctx_t*ctx,int x,int y){
    EpdRect r=rc_menu_rect();bool side=s_rc_menu_slot>=RC_SLOT_COUNT;
    if(!ui_rect_hit(r,x,y)){s_rc_menu_slot=-1;return APP_REDRAW_PAGE;}
    if(!side&&y<r.y+52){s_rc_menu_group=x<r.x+r.width/2?0:1;return APP_REDRAW_PAGE;}
    int index=(y-r.y-(side?6:54))/48;unsigned count=side?(KIIKO_BOARD_METALIO47?5:3):rc_pool_count(s_rc_menu_group);
    if(index<0||(unsigned)index>=count)return APP_REDRAW_NONE;
    bool ok=side?app_settings_set_reader_side(s_rc_menu_slot-RC_SLOT_COUNT,rc_side_pool(index)):
        app_settings_set_reader_control(s_rc_menu_slot,rc_pool_action(s_rc_menu_group,index));
    if(ok)s_rc_menu_slot=-1;else rc_notice(ctx,"保存失败，请重试");return APP_REDRAW_PAGE;
}
static app_redraw_t rc_panel_action(app_ctx_t*ctx,int x,int y,bool*handled){
    *handled=false;
    if(s_view!=READING||s_clear_confirm)return APP_REDRAW_NONE;
    if(s_reader_panel==READER_PANEL_READING_LIGHT){
        *handled=true;if(y<790||ui_rect_hit((EpdRect){32,815,64,64},x,y)||ui_rect_hit(rc_done_rect(),x,y))return rc_open(ctx,READER_PANEL_NONE);
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_TOUCH_SETTINGS||s_reader_panel==READER_PANEL_TOUCH_MORE){
        *handled=true;rc_pause_auto();
        if(s_rc_menu_slot>=0)return rc_binding_tap(ctx,x,y);
        if(ui_rect_hit((EpdRect){608,14,64,64},x,y))return s_reader_panel==READER_PANEL_TOUCH_MORE?rc_open(ctx,READER_PANEL_TOUCH_SETTINGS):rc_to_root(ctx,2);
        if(s_reader_panel==READER_PANEL_TOUCH_SETTINGS){
            if(ui_rect_hit((EpdRect){12,14,64,64},x,y))return rc_open(ctx,READER_PANEL_TOUCH_MORE);
            const unsigned slots[]={RC_TAP_CENTER,RC_HOLD,RC_BOTTOM_LEFT,RC_BOTTOM_CENTER,RC_BOTTOM_RIGHT,RC_SLOT_COUNT,RC_SLOT_COUNT+1};
            for(unsigned i=0;i<sizeof(slots)/sizeof(slots[0]);++i){EpdRect a=rc_touch_rect(slots[i]);if(ui_rect_hit(a,x,y)){rc_open_binding(slots[i],a);return APP_REDRAW_PAGE;}}
        }else{
            if(ui_rect_hit(rc_done_rect(),x,y))return rc_open(ctx,READER_PANEL_TOUCH_SETTINGS);
            for(unsigned i=0;i<11;++i)if(rc_more_slots[i]>=0&&ui_rect_hit(rc_more_rect(i),x,y)){rc_open_binding(rc_more_slots[i],rc_more_rect(i));return APP_REDRAW_PAGE;}
        }
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_READING_DISPLAY){
        *handled=true;if(y<740||ui_rect_hit((EpdRect){32,765,64,64},x,y)||ui_rect_hit(rc_done_rect(),x,y))return rc_to_root(ctx,0);
        for(int i=0;i<4;++i)if(ui_rect_hit(rc_display_rect(i),x,y)){
            if(!i){if(!app_settings_set_reader_default_fullscreen(!app_settings_reader_default_fullscreen()))rc_notice(ctx,"保存失败，请重试");return APP_REDRAW_PAGE;}
            return apply_reader_option(ctx,i);
        }
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS&&
       (ui_rect_hit(rc_done_rect(),x,y)||ui_rect_hit((EpdRect){32,reader_font_top()+25,64,64},x,y))){
        *handled=true;return s_rc_font_root?rc_to_root(ctx,0):rc_open(ctx,READER_PANEL_NONE);
    }
    if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS&&
       (ui_rect_hit(reader_layout_back_rect(),x,y)||ui_rect_hit((EpdRect){32,525,64,64},x,y))){*handled=true;return rc_to_root(ctx,0);}
    if(s_reader_panel==READER_PANEL_RULE_SETTINGS&&
       (ui_rect_hit((EpdRect){36,1113,612,72},x,y)||ui_rect_hit((EpdRect){32,605,64,64},x,y))){*handled=true;return rc_to_root(ctx,0);}
    if(s_reader_panel!=READER_PANEL_REFRESH_SETTINGS)return APP_REDRAW_NONE;
    *handled=true;rc_pause_auto();
    if(y<rc_root_top()||ui_rect_hit(rc_done_rect(),x,y))return rc_open(ctx,READER_PANEL_NONE);
    for(int i=0;i<3;++i)if(ui_rect_hit(rc_tab_rect(i),x,y)){s_rc_tab=i;return APP_REDRAW_PAGE;}
    if(s_rc_tab==0){
        for(int i=0;i<4;++i)if(ui_rect_hit(rc_grid_rect(i),x,y)){
            const reader_panel_t panels[]={READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_READING_DISPLAY,READER_PANEL_RULE_SETTINGS};
            s_rc_font_root=true;return rc_open(ctx,panels[i]);}
    }else if(s_rc_tab==1){
        for(int i=0;i<3;++i)if(ui_rect_hit(rc_effect_rect(i),x,y)){app_settings_set_reader_turn_effect(i);s_turns=0;rc_invalidate();return APP_REDRAW_PAGE;}
        if(ui_rect_hit(rc_refresh_rect(),x,y)){
            static const uint8_t values[]={0,5,10,15,
#if KIIKO_BOARD_METALIO47
                20,
#endif
                30};
            unsigned n=sizeof(values)/sizeof(values[0]),i=0;for(;i<n;++i)if(values[i]==app_settings_reader_full_pages())break;
            app_settings_set_reader_full_pages(values[(i+1)%n]);s_turns=0;return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit(rc_auto_card(0),x,y))return rc_execute(ctx,RC_AUTOTURN);
        for(unsigned plus=0;plus<2;++plus)if(ui_rect_hit(rc_interval_rect(plus),x,y)){
            unsigned seconds=reader_controls_seconds(app_settings_reader_controls());
            if (plus ? seconds >= READER_AUTO_MAX_SECONDS : seconds <= READER_AUTO_MIN_SECONDS)
                return APP_REDRAW_NONE;
            if(!app_settings_set_reader_auto_seconds(plus?seconds+1:seconds-1))rc_notice(ctx,"保存失败，请重试");
            return APP_REDRAW_PAGE;
        }
    }else{
        if(ui_rect_hit((EpdRect){36,746,612,110},x,y)){s_rc_menu_slot=-1;return rc_open(ctx,READER_PANEL_TOUCH_SETTINGS);}
        if(ui_rect_hit((EpdRect){36,878,612,110},x,y))return rc_open(ctx,READER_PANEL_TURN_SETTINGS);
        if(ui_rect_hit((EpdRect){36,1010,612,110},x,y))return rc_open(ctx,READER_PANEL_KEY_SETTINGS);
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t rc_tick(app_ctx_t*ctx){
    bool body=s_view==READING&&s_text&&!s_scan_pending&&!s_clear_confirm&&
        (s_reader_panel==READER_PANEL_NONE||s_reader_panel==READER_PANEL_TOOLS)&&s_footnote_note<0&&!rc_zoom_active()&&!s_reader_tap.pending&&!ctx->consumed&&!ctx->request_app&&!ctx->request_menu&&!ctx->request_return;
    if(!body){
        if(!s_text||s_view==SHELF||s_view==BULK||s_view==MANAGE)rc_stop_auto();else rc_pause_auto();
        s_rc_double_pending=false;return APP_REDRAW_NONE;
    }
    if(s_rc_auto.running&&s_rc_auto_waiting){s_rc_auto_waiting=false;reader_auto_start(&s_rc_auto,ctx->now_ms,reader_controls_seconds(app_settings_reader_controls()));}
    if(s_rc_double_pending&&ctx->now_ms>=s_rc_double_until&&!(ctx->touch&&ctx->touch->touched)){
        s_rc_double_pending=false;return rc_execute(ctx,app_settings_reader_controls()->actions[RC_TAP_CENTER]);
    }
    if(ctx->touch&&ctx->touch->touched)return APP_REDRAW_NONE;
    bool end=s_page+1>=book_layout_page_count()&&s_chapter+1>=book_chapter_count();
    if(end&&s_rc_auto.running){rc_stop_auto();rc_notice(ctx,"已到书末，自动翻页已停止");return APP_REDRAW_PAGE;}
    if(reader_auto_due(&s_rc_auto,ctx->now_ms,reader_controls_seconds(app_settings_reader_controls()),body,end)){
        size_t chapter=s_chapter,page=s_page;s_rc_auto_turning=true;app_redraw_t redraw=turn_page(ctx,1);s_rc_auto_turning=false;
        if(s_view!=READING||!s_text||(chapter==s_chapter&&page==s_page))rc_stop_auto();
        else ctx->user_activity=true;
        return redraw;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t rc_bottom_swipe(app_ctx_t*ctx,const ui_gesture_event_t*ev,bool*handled){
    *handled=false;
    if(s_view!=READING||!s_text||s_clear_confirm||s_scan_pending||ctx->consumed||
       (s_reader_panel!=READER_PANEL_NONE&&s_reader_panel!=READER_PANEL_TOOLS)||
       ev->type!=UI_GESTURE_SWIPE_U||ev->y0<1096||ev->y0>=1216||ev->x0>=684)return APP_REDRAW_NONE;
    *handled=true;unsigned slot=RC_BOTTOM_LEFT+ev->x0/228;
    s_pressed_control=s_reader_slider=-1;s_reader_tap.pending=false;
    app_redraw_t redraw=rc_execute(ctx,app_settings_reader_controls()->actions[slot]);
    return redraw==APP_REDRAW_NONE?APP_REDRAW_PAGE:redraw;
}
static bool rc_custom_modal(void){return s_view==READING&&(s_reader_panel==READER_PANEL_REFRESH_SETTINGS||s_reader_panel==READER_PANEL_READING_DISPLAY||s_reader_panel==READER_PANEL_TOUCH_SETTINGS||s_reader_panel==READER_PANEL_TOUCH_MORE||s_reader_panel==READER_PANEL_READING_LIGHT);}
static int rc_hit_target(int x,int y){
    if(s_rc_menu_slot>=0){EpdRect r=rc_menu_rect();if(!ui_rect_hit(r,x,y))return 999;
        bool side=s_rc_menu_slot>=RC_SLOT_COUNT;if(!side&&y<r.y+52)return 400+(x>=r.x+r.width/2);
        return 500+(y-r.y-(side?6:54))/48;}
    if(s_reader_panel==READER_PANEL_TOUCH_SETTINGS){
        if(ui_rect_hit((EpdRect){608,14,64,64},x,y))return 999;
        if(ui_rect_hit((EpdRect){12,14,64,64},x,y))return 998;
        for(unsigned i=0;i<RC_SLOT_COUNT+2;++i)if(ui_rect_hit(rc_touch_rect(i),x,y))return 100+i;
        return -1;
    }
    if(s_reader_panel==READER_PANEL_TOUCH_MORE){
        if(ui_rect_hit((EpdRect){608,14,64,64},x,y)||ui_rect_hit(rc_done_rect(),x,y))return 999;
        for(unsigned i=0;i<11;++i)if(rc_more_slots[i]>=0&&ui_rect_hit(rc_more_rect(i),x,y))return 100+i;
        return -1;
    }
    if(s_reader_panel==READER_PANEL_READING_DISPLAY){
        if(y<826||ui_rect_hit(rc_done_rect(),x,y))return 999;
        for(int i=0;i<4;++i)if(ui_rect_hit(rc_display_rect(i),x,y))return 100+i;
        return -1;
    }
    if(s_reader_panel==READER_PANEL_READING_LIGHT){if(y<876||ui_rect_hit(rc_done_rect(),x,y))return 999;return -1;}
    if(s_reader_panel==READER_PANEL_REFRESH_SETTINGS){
        if(y<rc_root_top()||ui_rect_hit(rc_done_rect(),x,y))return 999;
        for(int i=0;i<3;++i)if(ui_rect_hit(rc_tab_rect(i),x,y))return 10+i;
        if(s_rc_tab==0){for(int i=0;i<4;++i)if(ui_rect_hit(rc_grid_rect(i),x,y))return 100+i;}
        if(s_rc_tab==1){for(int i=0;i<3;++i)if(ui_rect_hit(rc_effect_rect(i),x,y))return 100+i;
            if(ui_rect_hit(rc_refresh_rect(),x,y))return 110;
            if(ui_rect_hit(rc_auto_card(0),x,y))return 111;
            for(unsigned i=0;i<2;++i)if(ui_rect_hit(rc_interval_rect(i),x,y))return 112+i;}
        if(s_rc_tab==2){for(int i=0;i<3;++i)if(ui_rect_hit((EpdRect){36,746+i*132,612,110},x,y))return 100+i;}
        return -1;
    }
    return -1;
}
static void rc_cancel_side(void){
#if KIIKO_BOARD_METALIO47
    if(s_rc_side_active&&s_rc_side_captured)metalio_frontlight_restore();
#endif
    s_rc_side_active=s_rc_side_captured=false;
}
static app_redraw_t rc_gesture(app_ctx_t*ctx,const ui_gesture_event_t*ev,bool*handled){
    *handled=false;
    if(rc_custom_modal()){
        *handled=true;rc_pause_auto();
#if KIIKO_BOARD_METALIO47
        if(s_reader_panel==READER_PANEL_READING_LIGHT){
            EpdRect slider=rc_light_slider_rect();
            if(ev->type==UI_GESTURE_PRESS)s_rc_light_drag=ui_rect_hit(slider,ev->x0,ev->y0);
            if(s_rc_light_drag){
                if(ev->type==UI_GESTURE_CANCEL){s_rc_light_drag=false;metalio_frontlight_restore();return APP_REDRAW_PAGE;}
                int level=((int)ev->x-slider.x-64)*METALIO_FRONTLIGHT_RANGE/(slider.width-128);if(level<0)level=0;if(level>METALIO_FRONTLIGHT_RANGE)level=METALIO_FRONTLIGHT_RANGE;
                metalio_frontlight_preview_position(s_rc_light_warm?metalio_frontlight_position(false):(unsigned)level,s_rc_light_warm?(unsigned)level:metalio_frontlight_position(true));
                if(ev->type!=UI_GESTURE_MOVE&&ev->type!=UI_GESTURE_PRESS){s_rc_light_drag=false;metalio_frontlight_commit();}
                render(ctx,ctx->fb);s_area=(EpdRect){36,930,612,120};s_mode=MODE_GL16;return APP_REDRAW_AREA;
            }
        }
#endif
        if(ev->type==UI_GESTURE_PRESS){s_rc_press_target=rc_hit_target(ev->x0,ev->y0);return APP_REDRAW_NONE;}
        if(ev->type==UI_GESTURE_TAP){int end=rc_hit_target(ev->x,ev->y);if(end>=0&&end==s_rc_press_target){bool used;return rc_panel_action(ctx,ev->x,ev->y,&used);}}
        return APP_REDRAW_NONE;
    }
    if(s_view!=READING||!s_text||s_clear_confirm||s_scan_pending||ctx->consumed||
       (s_reader_panel!=READER_PANEL_NONE&&s_reader_panel!=READER_PANEL_TOOLS)){
        rc_cancel_side();return APP_REDRAW_NONE;
    }
    if(ev->type==UI_GESTURE_PRESS){
        s_rc_side_active=false;s_rc_side_captured=false;
        if(ev->y0>=176&&ev->y0<1096&&(ev->x0<56||ev->x0>=628)){
            s_rc_side_index=ev->x0>=628;s_rc_side_action=app_settings_reader_controls()->sides[s_rc_side_index];
            s_rc_side_active=s_rc_side_action!=RC_SIDE_OFF;s_rc_side_value=s_rc_side_action==RC_SIDE_FONT?s_px:app_settings_book_line_spacing();
#if KIIKO_BOARD_METALIO47
            if(s_rc_side_action==RC_SIDE_BRIGHTNESS||s_rc_side_action==RC_SIDE_WARMTH)s_rc_side_value=metalio_frontlight_position(s_rc_side_action==RC_SIDE_WARMTH);
#endif
            s_rc_side_start=s_rc_side_value;
        }
    }
    int dx=(int)ev->x-ev->x0,dy=(int)ev->y-ev->y0;
    if(s_rc_side_active&&(s_rc_side_captured||(ev->type==UI_GESTURE_MOVE&&abs(dy)>24&&abs(dy)>abs(dx)))){
        *handled=true;s_rc_side_captured=true;rc_pause_auto();
        if(ev->type==UI_GESTURE_CANCEL){rc_cancel_side();return APP_REDRAW_PAGE;}
        int min=0,max=240,step=2;
        if(s_rc_side_action==RC_SIDE_FONT){min=BOOK_PX_MIN;max=BOOK_PX_MAX;step=24;}
        if(s_rc_side_action==RC_SIDE_LINE){min=110;max=150;step=24;}
        int value=s_rc_side_start-dy/step;if(value<min)value=min;if(value>max)value=max;
        bool changed=value!=s_rc_side_value;s_rc_side_value=value;
#if KIIKO_BOARD_METALIO47
        if((s_rc_side_action==RC_SIDE_BRIGHTNESS||s_rc_side_action==RC_SIDE_WARMTH)&&changed)
            metalio_frontlight_preview_position(s_rc_side_action==RC_SIDE_BRIGHTNESS?value:metalio_frontlight_position(false),s_rc_side_action==RC_SIDE_WARMTH?value:metalio_frontlight_position(true));
#endif
        if(ev->type!=UI_GESTURE_MOVE&&ev->type!=UI_GESTURE_PRESS){
            s_rc_side_active=s_rc_side_captured=false;
#if KIIKO_BOARD_METALIO47
            if(s_rc_side_action==RC_SIDE_BRIGHTNESS||s_rc_side_action==RC_SIDE_WARMTH)metalio_frontlight_commit();
#endif
            if(s_rc_side_action==RC_SIDE_FONT)return apply_reader_layout(ctx,value,s_margin,app_settings_book_line_spacing(),app_settings_book_paragraph_spacing());
            if(s_rc_side_action==RC_SIDE_LINE)return apply_reader_layout(ctx,s_px,s_margin,value,app_settings_book_paragraph_spacing());
            return APP_REDRAW_PAGE;
        }
        if(changed){char text[64];snprintf(text,sizeof(text),"%s %d",rc_side_name(s_rc_side_action),value);rc_notice(ctx,text);render(ctx,ctx->fb);s_area=(EpdRect){208,150,268,68};s_mode=MODE_GL16;return APP_REDRAW_AREA;}
        return APP_REDRAW_NONE;
    }
    if(ev->type==UI_GESTURE_CANCEL)rc_cancel_side();
    if(ev->y0<1096&&ui_rect_hit(body_rect(),ev->x0,ev->y0)&&!reader_zoom_mode()){
        if(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R){*handled=true;rc_cancel_side();return rc_execute(ctx,ev->type==UI_GESTURE_SWIPE_L?RC_NEXT:RC_PREVIOUS);}
        if(ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){*handled=true;rc_cancel_side();return rc_execute(ctx,ev->type==UI_GESTURE_SWIPE_U?RC_NEXT:RC_PREVIOUS);}
        if(ev->type==UI_GESTURE_LONG_PRESS&&!reader_page_image_hit(ev->x0,ev->y0)&&s_footnote_note<0){*handled=true;return rc_execute(ctx,app_settings_reader_controls()->actions[RC_HOLD]);}
        if(ev->type==UI_GESTURE_MOVE&&abs(dy)>24)rc_pause_auto();
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t rc_body_tap(app_ctx_t*ctx,int x,int y,bool image,bool*handled){
    *handled=false;
    if(image||s_clear_confirm||(s_reader_panel!=READER_PANEL_NONE&&s_reader_panel!=READER_PANEL_TOOLS)||!ui_rect_hit(body_rect(),x,y))return APP_REDRAW_NONE;
    const reader_controls_config_t*c=app_settings_reader_controls();bool center=book_reader_center(body_rect(),x,y);
    if(center&&c->actions[RC_DOUBLE_CENTER]!=RC_NONE){
        *handled=true;rc_pause_auto();
        if(s_rc_double_pending&&ctx->now_ms<=s_rc_double_until){s_rc_double_pending=false;return rc_execute(ctx,c->actions[RC_DOUBLE_CENTER]);}
        s_rc_double_pending=true;s_rc_double_until=ctx->now_ms+300;return APP_REDRAW_NONE;
    }
    if(center&&(!s_reader_fullscreen||c->actions[RC_TAP_CENTER]!=RC_FULLSCREEN)){
        *handled=true;return rc_execute(ctx,c->actions[RC_TAP_CENTER]);
    }
    EpdRect r=body_rect();bool previous=app_settings_reader_vertical_turn()?y<r.y+r.height/3:x<r.x+r.width/2;
    unsigned slot=previous?RC_TAP_PREVIOUS:RC_TAP_NEXT,action=c->actions[slot];
    if(action!=(unsigned)(previous?RC_PREVIOUS:RC_NEXT)){*handled=true;return rc_execute(ctx,action);}
    return APP_REDRAW_NONE;
}
