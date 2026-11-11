#!/usr/bin/env python3
"""Exhaustive original-to-packed dictionary equivalence and existing search behavior.

SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile,os,sys
ROOT=Path(__file__).resolve().parents[1]
from generate_search_packed import generate
folder=ROOT/'components/read_pico_search'
assert (folder/'search_packed.h').read_text()==generate(folder/'search_table.h')
board=os.environ.get('KIIKO_TEST_BOARD','pico');metalio=int(board=='metalio47')
unit=r'''
#include <assert.h>
#include <stdio.h>
#include PRODUCTION_SOURCE
int main(void) {
    const size_t count=sizeof(search_codepoints)/sizeof(search_codepoints[0]);
    assert(count==41923&&SEARCH_CODEPOINT_COUNT==count);
    for(size_t i=0;i<count;++i) {
        assert(search_codepoint_at(i)==search_codepoints[i]);
        assert(search_group_at(i)==search_groups[i]);
    }
    // Independent linear oracle covers every Unicode scalar, gaps and both edges.
    size_t next=0;
    for(uint32_t cp=0;cp<=0x10ffff;++cp) {
        int expected=-1;
        if(next<count&&search_codepoints[next]==cp)expected=(int)search_groups[next++];
        assert(lookup(cp)==expected);
    }
    assert(next==count&&lookup(0xffffffff)==-1);
    puts("PASS: all41923 codepoint/reading-group pairs exact; all1114112 Unicode positions and missing-key boundaries match original dictionary");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-search-packed-') as directory:
    work=Path(directory);(work/'test.c').write_text(unit)
    common=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-DKIIKO_BOARD_METALIO47='+str(metalio),'-I'+str(folder/'include')]
    subprocess.run(common+['-DPRODUCTION_SOURCE="'+str(folder/'read_pico_search.c')+'"',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
    subprocess.run(common+[str(ROOT/'tools/search_host_test.c'),str(folder/'read_pico_search.c'),'-o',str(work/'search')],check=True)
    subprocess.run([str(work/'search')],check=True)
print('PASS: reproducible packed index and original literal/pinyin/initial/T9/polyphonic/mixed/IME behavior; '+board)
