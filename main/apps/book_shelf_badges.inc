// 中文：角标在成品封面缓存之后实时绘制；只用一个1792B字形遮罩，不新增整帧或TF文件。
// English: Paint badges live after cached artwork, using one1792-byte glyph mask, without another frame or SD file.
static uint8_t s_shelf_read_mask[64*28];
static bool s_shelf_read_ready;
static int s_shelf_read_x,s_shelf_read_y,s_shelf_read_w,s_shelf_read_h;
static bool shelf_read_mask_prepare(void){
    if(s_shelf_read_ready)return s_shelf_read_w>0;
    s_shelf_read_ready=true;memset(s_shelf_read_mask,0,sizeof(s_shelf_read_mask));
    s_shelf_read_x=64;s_shelf_read_y=28;s_shelf_read_w=s_shelf_read_h=0;
    if(!ttf_text_mask_px(s_shelf_read_mask,64,28,0,ttf_ascender_px(18),18,"已读"))return false;
    int right=-1,bottom=-1;
    for(int y=0;y<28;++y)for(int x=0;x<64;++x)if(s_shelf_read_mask[y*64+x]){
        if(x<s_shelf_read_x)s_shelf_read_x=x;
        if(y<s_shelf_read_y)s_shelf_read_y=y;
        if(x>right)right=x;
        if(y>bottom)bottom=y;
    }
    s_shelf_read_w=right-s_shelf_read_x+1;s_shelf_read_h=bottom-s_shelf_read_y+1;return s_shelf_read_w>0;
}
static void draw_cover_progress(uint8_t*fb,EpdRect image,unsigned progress){
    int style=shelf_display_style();
    if(!app_settings_shelf_progress_badges()||!progress||(style!=1&&style!=2&&style!=6))return;
    if(progress>=100&&image.width>=66&&image.height>=66&&shelf_read_mask_prepare()){
        int left=image.x+image.width-66;
        for(int y=0;y<66;++y)for(int x=0;x<66;++x){
            if(x-y<8||x-y>42)continue;
            epd_draw_pixel(left+x,image.y+y,UI_GRAY_BLACK,fb);
            int sx=((x-44+y-19)*181)/256+s_shelf_read_w/2;
            int sy=((-x+44+y-19)*181)/256+s_shelf_read_h/2;
            if(sx>=0&&sx<s_shelf_read_w&&sy>=0&&sy<s_shelf_read_h){
                uint8_t alpha=s_shelf_read_mask[(s_shelf_read_y+sy)*64+s_shelf_read_x+sx];
                if(alpha)epd_draw_pixel(left+x,image.y+y,alpha,fb);
            }
        }
    }else{
        char text[12];if(progress>=100)copy_text(text,sizeof(text),"已读");else snprintf(text,sizeof(text),"%u%%",progress);
        int width=ui_text_fixed_width_px(26,text)+18;
        if(width>image.width-10)width=image.width-10;
        EpdRect r={image.x+image.width-width-5,image.y+5,width,36};
        ui_fill_round_rect(fb,r,18,UI_GRAY_BLACK);ui_text_fixed_vc(fb,r.x+r.width/2,r.y+18,26,text,EPD_DRAW_ALIGN_CENTER,true);
    }
}
