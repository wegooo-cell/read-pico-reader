#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the real file-manager page selector with filters/list/grid/root/final pages."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
for source in [R/'main/apps/app_files.c',R/'boards/pico/src/main/apps/app_files.c']:
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static struct{bool selected;}s_files[30];static int s_folder,s_page,rows,s_batch_count;
static int root_rows(void){return 6;}static int visible_rows(void){return rows;}
static int file_visible_count(void){return 20;}
static int file_visible_index(int n){for(int i=0;i<30;++i)if(i%3&&n--==0)return i;return -1;}
'''+function('file_select_page',source)+r'''
int main(void){for(int mode=0;mode<3;++mode)for(int page=0;page<4;++page){
 s_folder=mode?0:-1;rows=mode==1?7:9;s_page=page;s_batch_count=3;bool before[30];
 for(int i=0;i<30;++i)s_files[i].selected=before[i]=(i==1||i==29);
 file_select_page();int size=mode?rows:6;assert(!s_batch_count);
 for(int i=0;i<30;++i){bool visible=false;for(int n=page*size;n<20&&n<(page+1)*size;++n)if(file_visible_index(n)==i)visible=true;assert(s_files[i].selected==(visible||before[i]));}
 file_select_page();for(int i=0;i<30;++i){bool visible=false;for(int n=page*size;n<20&&n<(page+1)*size;++n)if(file_visible_index(n)==i)visible=true;assert(s_files[i].selected==(!visible&&before[i]));}
 }
 puts("PASS: real file page selector affects only filtered current root/list/grid/last-page rows, preserves every off-page selection and toggles current-page cancellation");}
'''
 assert 'file_select_page();return APP_REDRAW_PAGE;' in source.read_text()
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'test.c').write_text(unit);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
