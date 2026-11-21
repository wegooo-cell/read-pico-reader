/* SPDX-License-Identifier: Apache-2.0
 * Books management follows the real folder tree; enrollment remains a separate action.
 */
static size_t books_directory_root_size(void){
    const char *root=app_settings_books_dir();size_t n=strlen(root);while(n>1&&root[n-1]=='/')--n;return n;
}
static const char *books_directory_current(void){
    const char *root=app_settings_books_dir();size_t n=books_directory_root_size();
    return s_manage_dir[0]&&!strncmp(s_manage_dir,root,n)&&(!s_manage_dir[n]||s_manage_dir[n]=='/')?s_manage_dir:root;
}
static bool books_directory_is_root(void){
    const char *path=books_directory_current();size_t n=strlen(path);while(n>1&&path[n-1]=='/')--n;
    return n==books_directory_root_size();
}
static const char *books_directory_name(void){
    if(books_directory_is_root())return "Books目录";
    const char *path=books_directory_current(),*base=strrchr(path,'/');return base?base+1:path;
}
static app_redraw_t books_directory_open(app_ctx_t *ctx,const char *path){
    const char *root=app_settings_books_dir();size_t n=books_directory_root_size();
    char target[BOOK_STORE_PATH_MAX];
    if(!path||strlen(path)>=sizeof(target)||strncmp(path,root,n)||(path[n]&&path[n]!='/'))return APP_REDRAW_NONE;
    for(const char *part=path+n;*part;){
        if(*part=='/'){++part;continue;}const char *end=strchr(part,'/');size_t size=end?(size_t)(end-part):strlen(part);
        if((size==1&&part[0]=='.')||(size==2&&part[0]=='.'&&part[1]=='.'))return APP_REDRAW_NONE;
        if(!end)break;
        part=end+1;
    }
    copy_text(target,sizeof(target),path);size_t size=strlen(target);while(size>n&&target[size-1]=='/')target[--size]=0;
    struct stat st;
    if(stat(target,&st)||!S_ISDIR(st.st_mode)){copy_text(s_batch_message,sizeof(s_batch_message),"目录不可用，请返回上一级");return APP_REDRAW_PAGE;}
    copy_text(s_manage_dir,sizeof(s_manage_dir),target);clear_selection();s_bulk_selecting=false;s_query[0]=0;
    s_count=s_visible_count=0;library_scan_again(ctx);return APP_REDRAW_PAGE;
}
static app_redraw_t books_directory_up(app_ctx_t *ctx){
    if(books_directory_is_root())return APP_REDRAW_NONE;
    char parent[BOOK_STORE_PATH_MAX];copy_text(parent,sizeof(parent),books_directory_current());
    char *slash=strrchr(parent,'/');if(!slash)return APP_REDRAW_NONE;*slash=0;
    return books_directory_open(ctx,parent);
}
