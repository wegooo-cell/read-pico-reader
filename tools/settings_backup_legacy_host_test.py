#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import io,json,subprocess,tarfile,re,sys
import argparse
parser=argparse.ArgumentParser(description="Compile frozen release readers; check compatible backup exports without changing old firmware.")
parser.add_argument('--work-dir',type=Path,required=True)
parser.add_argument('--native',type=Path,required=True)
args=parser.parse_args()
W=args.work_dir.resolve();W.mkdir(parents=True,exist_ok=True)
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'tools'))
from convert_settings_backup import convert
native=args.native.read_bytes()
rows=[('v0.3.3-rc90','pico',n) for n in range(1,13)]+[('v0.3.3-rc90','pico',12),('v0.3.3-rc91','pico',15),('v0.3.3-rc95','pico',17),('metalio-s31-v0.3.3-rc109','metalio47',20),('metalio-s31-v0.3.3-rc111','metalio47',22),('metalio-s31-v0.3.3-rc114','metalio47',25),('v0.3.3-rc104','pico',20),('v0.3.3-rc106','pico',22),('v0.3.3-rc108','pico',25)]
rows += [('v0.3.3-rc91','pico',n) for n in (13,14)] + [('v0.3.3-rc95','pico',16)] + [('v0.3.3-rc104','pico',n) for n in (18,19)] + [('v0.3.3-rc106','pico',21)] + [('v0.3.3-rc108','pico',n) for n in (23,24)]
report=[]
for tag,board,version in rows:
 folder=W/'legacy-readers'/tag
 folder.mkdir(parents=True,exist_ok=True)
 if not (folder/'main').exists():
  paths=['main','tools','boards','components/read_pico_transfer/include']
  paths=[name for name in paths if subprocess.run(['git','cat-file','-e',tag+':'+name],cwd=R,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0]
  data=subprocess.check_output(['git','archive',tag,*paths],cwd=R)
  with tarfile.open(fileobj=io.BytesIO(data)) as archive:archive.extractall(folder,filter='data')
 source=folder/('boards/pico/src/main/settings.c' if board=='pico' and (folder/'boards/pico/src/main/settings.c').exists() else 'main/settings.c')
 candidate=folder/'boards/metalio47/tests/tools/settings_backup_host_test.c'
 fixture=candidate if board=='metalio47' and candidate.exists() else folder/'tools/settings_backup_host_test.c'
 text=fixture.read_text().replace('#include "../main/settings.c"','#include '+json.dumps(str(source)))
 # The fixture's original history stub used an eight-byte marker; use a real empty RPHIST1 tail.
 pattern=r'bool book_history_backup_validate\(FILE \*file\) \{.*?\n\}'
 text,count=re.subn(pattern,'''bool book_history_backup_validate(FILE *file) {
 unsigned char bytes[34];
 return fread(bytes,1,34,file)==34&&!memcmp(bytes,"RPHIST1",8)&&bytes[8]==255&&
 bytes[28]==0&&bytes[29]==0&&bytes[30]==0xc5&&bytes[31]==0x9d&&bytes[32]==0x1c&&bytes[33]==0x81&&fgetc(file)==EOF;
}''',text,count=1,flags=re.S)
 assert count
 unit='#define main old_fixture_main\n'+text+'\n#undef main\n'
 has_counter='commit_count' in text
 unit+='''static bool review_copy(const char *from,const char *to){
 FILE *input=fopen(from,"rb"),*output=fopen(to,"wb");assert(input&&output);
 unsigned char bytes[256];size_t count;bool ok=true;
 while((count=fread(bytes,1,sizeof(bytes),input)))if(fwrite(bytes,1,count,output)!=count)ok=false;
 if(ferror(input))ok=false;fclose(input);if(fclose(output))ok=false;return ok;
}
int main(int argc,char **argv){
 assert(argc==3);remove(BACKUP_FILE);remove(BACKUP_TEMP);app_settings_init();
 assert(review_copy(argv[1],BACKUP_FILE));
 int before=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE&&commit_count==before);
 assert(review_copy(argv[2],BACKUP_FILE));
 assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==40);
 puts("PASS: frozen old firmware rejects the newer full format, restores exported compatible settings without source changes");
}'''
 if not has_counter:unit=unit.replace('int before=commit_count;','').replace('&&commit_count==before','')
 (folder/'reader_test.c').write_text(unit)
 output=folder/'reader_test'
 compiler_args=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={int(board=="metalio47")}', '-I'+str(folder/'main'),'-I'+str(folder/'tools/settings_backup_stubs'),'-I'+str(folder/'components/read_pico_transfer/include'),str(folder/'reader_test.c'),'-o',str(output)]
 log=W/f'legacy-{tag}.log'
 try:
  with log.open('w') as out:subprocess.run(compiler_args,cwd=folder,check=True,stdout=out,stderr=subprocess.STDOUT)
  (folder/'build/book-tests/settings-card').mkdir(parents=True,exist_ok=True)
  data,metadata=convert(native,version);target=folder/'compatible.backup';target.write_bytes(data)
  with log.open('a') as out:subprocess.run([str(output),str(args.native.resolve()),str(target)],cwd=folder,check=True,stdout=out,stderr=subprocess.STDOUT)
  print('PASS',tag,'format',version,flush=True);report.append({'tag':tag,'board':board,'format':version,'pass':True})
 except subprocess.CalledProcessError:
  print('FAIL',tag,log.read_text()[-2500:],flush=True);raise
(W/'legacy-reader-matrix.json').write_text(json.dumps(report,indent=2)+'\n')
