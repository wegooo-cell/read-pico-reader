/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：双板共享已确认的书架管理绘制；只绘图，操作由所属页面回调处理。
 * English: Approved bookshelf-management painting shared by both boards; actions stay in page callbacks.
 */
#pragma once
static void bulk_draw_title(uint8_t *fb,const char *name,EpdRect box,int preferred,bool grid,uint8_t background){
    char line[256];copy_text(line,sizeof(line),name);
    int px=ui_text_title_fit(line,preferred,box.width,name);
    const char *rest=name+strlen(line);
    int x=box.x+(grid?box.width/2:0);
    enum EpdFontFlags align=grid?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT;
    ui_text_title_background_vc(fb,x,box.y+(*rest?16:box.height/2),px,line,name,align,background);
    if(*rest){
        copy_text(line,sizeof(line),rest);ui_text_title_fit(line,px,box.width,name);
        ui_text_title_background_vc(fb,x,box.y+49,px,line,name,align,background);
    }
}
static void bulk_draw_footer(uint8_t *fb){
    if(!s_bulk_selecting){ui_nav_draw(fb,1);return;}
    ui_fill_rect_fast((EpdRect){0,1096,684,120},0xe0,fb);ui_hairline(fb,1096,0,684,0xa0);
    const char *labels[]={"删除书籍","清理进度","移出书架","返回"};
    const ui_icon_t icons[]={UI_ICON_TRASH,UI_ICON_ROTATE_CW,UI_ICON_LIBRARY_BIG,UI_ICON_CHEVRON_LEFT};
    bool any=selected_count()!=0;
    for(int i=0;i<4;++i){
        EpdRect r=batch_rect(i+3);int cx=r.x+r.width/2;
        if(s_pressed_control==613+i)ui_fill_rect_fast(r,0xc0,fb);
        ui_draw_icon(fb,cx,1141,44,icons[i],i<3&&!any?0x90:UI_GRAY_BLACK);
        ui_text_box(fb,(EpdRect){r.x,1170,r.width,30},17,labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
}
static void draw_bulk(uint8_t *fb,int leaf){
    ui_clear_page(fb);ui_nav_status(fb);
    ui_text_box(fb,(EpdRect){36,80,500,76},52,"书架管理",EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,616,112,36,s_bulk_grid?UI_ICON_LIST:UI_ICON_LAYOUT_GRID,0x30);
    EpdRect search=bulk_filter_rect(2);
    ui_fill_round_rect(fb,search,18,0xf0);ui_draw_control_frame(fb,search,18,0xc0);
    ui_draw_icon(fb,66,192,28,UI_ICON_SEARCH,0x60);
    char query[65];copy_text(query,sizeof(query),s_query);fit_text(query,24,492);
    ui_text_box(fb,(EpdRect){96,168,492,48},24,s_query[0]?query:"搜索书籍",EPD_DRAW_ALIGN_LEFT,false);
    if(s_query[0])ui_draw_icon(fb,620,192,24,UI_ICON_X,0x60);
    char count[80];
    if(s_bulk_selecting)snprintf(count,sizeof(count),"共 %u 本 · 已选 %u 本",(unsigned)s_visible_count,(unsigned)selected_count());
    else snprintf(count,sizeof(count),"共 %u 本",(unsigned)s_visible_count);
    ui_text_box(fb,(EpdRect){36,244,204,46},21,count,EPD_DRAW_ALIGN_LEFT,false);
    const char *order=shelf_order_mode()==0?"名称":shelf_order_mode()==1?"最近阅读":"添加时间";
    char label[80];snprintf(label,sizeof(label),"书架排序：%s",order);
    EpdRect sort=bulk_filter_rect(1);
    if(s_pressed_control==111)ui_draw_pressed_round_rect(fb,sort,UI_BTN_RADIUS);
    else ui_draw_choice_round_rect(fb,sort,UI_BTN_RADIUS,false);
    ui_text_box(fb,(EpdRect){264,248,246,38},24,label,EPD_DRAW_ALIGN_CENTER,false);
    ui_draw_icon(fb,526,267,23,shelf_order_desc()?UI_ICON_CHEVRON_DOWN:UI_ICON_CHEVRON_UP,UI_GRAY_BLACK);
    draw_control(fb,ui_book_manage_select(),s_bulk_selecting?"取消":"选择",113);
    ui_hairline(fb,310,36,612,0xdc);
    for(int row=0;row<shelf_rows();++row){
        int index=leaf*shelf_rows()+row;if(index>=s_visible_count)break;
        const shelf_entry_t *item=&s_shelf[index];EpdRect r=row_rect(row),image=shelf_cover_image(row);
        bool selected=s_bulk_selecting&&item->selected;
        uint8_t background=selected?0xee:UI_GRAY_WHITE;
        if(selected)ui_fill_round_rect(fb,r,10,background);
        ui_fill_rect_fast(image,UI_GRAY_WHITE,fb);
        if(s_covers[row].index==index&&s_covers[row].gray){
            book_crop_t crop=book_cover_crop(s_covers[row].width,s_covers[row].height,image.width,image.height);
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){
                int sx=crop.x+x*crop.width/image.width,sy=crop.y+y*crop.height/image.height;
                uint8_t tone=ui_contrast_gray(s_covers[row].gray[sy*s_covers[row].width+sx]);
                epd_draw_pixel(image.x+x,image.y+y,ui_image_dither_gray(tone,image.x+x,image.y+y),fb);
            }
        }else ui_draw_icon(fb,image.x+image.width/2,image.y+image.height/2,30,UI_ICON_BOOK_OPEN,0x70);
        ui_draw_control_frame(fb,image,0,0x50);
        EpdRect title=s_bulk_grid?(EpdRect){r.x+10,r.y+212,172,64}:(EpdRect){154,r.y+12,442,64};
        bulk_draw_title(fb,item->name,title,s_bulk_grid?22:28,s_bulk_grid,background);
        char progress[48],detail[192];
        snprintf(progress,sizeof(progress),"%s %u%%",item->has_progress?"已读":"未读",item->pct);
        if(!s_bulk_grid&&item->author[0])snprintf(detail,sizeof(detail),"%s · %s",item->author,progress);
        else copy_text(detail,sizeof(detail),progress);
        EpdRect meta=s_bulk_grid?(EpdRect){r.x+10,r.y+280,172,28}:(EpdRect){154,r.y+88,442,30};
        ui_text_box(fb,meta,s_bulk_grid?18:20,detail,s_bulk_grid?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT,false);
        if(s_bulk_selecting){
            int mx=s_bulk_grid?r.x+170:628,my=r.y+(s_bulk_grid?18:65);
            if(s_bulk_grid)ui_fill_round_rect(fb,(EpdRect){mx-19,my-19,38,38},19,UI_GRAY_WHITE);
            epd_draw_circle(mx,my,15,0x60,fb);if(selected)epd_fill_circle(mx,my,5,UI_GRAY_BLACK,fb);
        }
        if(!s_bulk_grid)ui_hairline(fb,r.y+r.height-1,154,494,0xdc);
    }
    if(!s_visible_count)ui_text_box(fb,(EpdRect){52,450,580,100},26,s_message[0]?s_message:"当前筛选没有图书",EPD_DRAW_ALIGN_CENTER,false);
    char page[64];snprintf(page,sizeof(page),"第 %d 页 · 共 %d 页",leaf+1,leaves());
    int pressed=s_pressed_control>=100&&s_pressed_control<=103?s_pressed_control-100:UI_PAGER_NONE;
    ui_pager_draw_at(fb,page,leaf,leaves(),false,pressed,1006);
    if(s_batch_message[0]||s_message[0])ui_text_box(fb,(EpdRect){36,978,612,24},18,s_batch_message[0]?s_batch_message:s_message,EPD_DRAW_ALIGN_CENTER,false);
    bulk_draw_footer(fb);draw_batch_confirmation(fb);if(s_sort_menu)draw_bulk_sort(fb);
}
