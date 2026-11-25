#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run actual ESP settings/history/progress together against all versioned backup formats."""
from pathlib import Path
import importlib.util,json,subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('convert',R/'tools/convert_settings_backup.py');codec=importlib.util.module_from_spec(spec);spec.loader.exec_module(codec)
with tempfile.TemporaryDirectory(prefix='restore-integration-') as folder:
 root=Path(folder);(root/'ui_locale_format.h').write_text('#pragma once\n')
 view=R/'.kiiko-source-view.json'
 boards=[int(json.loads(view.read_text())['board']=='metalio47')] if view.exists() else [0,1]
 for board in boards:
  p=root/str(board);p.mkdir();card=p/'card';card.mkdir()
  source=R/('main/settings.c' if view.exists() or board else 'boards/pico/src/main/settings.c')
  cmd=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-DESP_PLATFORM',f'-DKIIKO_BOARD_METALIO47={board}','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',f'-DSETTINGS_SOURCE="{source}"',f'-DAPP_SETTINGS_BACKUP_ROOT="{card}"',f'-DBOOK_PROGRESS_SD_ROOT="{card}/progress-store"',f'-DBOOK_PROGRESS_DIR="{p}/internal"','-I'+str(root),'-I'+str(R/'tools/settings_backup_stubs'),'-I'+str(R/'main'),'-I'+str(R/'main/book'),'-I'+str(R/'components/read_pico_transfer/include'),str(R/'tools/settings_restore_integration_host_test.c'),str(R/'main/book/book_progress.c'),str(R/'main/book/book_history_backup.c'),'-o',str(p/'test')]
  subprocess.run(cmd,check=True,cwd=R)
  native=p/'v29.backup';subprocess.run([str(p/'test'),'export',str(native)],check=True)
  data=native.read_bytes();files=[native]
  for v in range(1,26):
   target=p/f'v{v}.backup';target.write_bytes(codec.convert(data,v)[0]);files.append(target)
  # Native v26/v27 share the older prefix; v28 appends a separately sealed low-ghost choice.
  ghost_at=codec.BASE_SIZE+sum(size for name,size in codec.SIZES.items() if name not in ('lowghost','extra'))
  for version in [26,27,28]:
   old=bytearray(data[:ghost_at]+(codec.seal(bytes([data[ghost_at+4]])) if version==28 else b'')+data[ghost_at+12:]);base=bytearray(old[:825]);base[7]=ord('A')+version-10
   if version==26:base[13]=90;base[21]=150;base[23]=0
   old[:829]=codec.seal(base);ext=old[829:836];old[832:836]=codec.seal(ext[:3],codec.fingerprint(old[:829]))[-4:]
   profile=old[836:1289];old[1285:1289]=codec.seal(profile[:-4],codec.fingerprint(old[829:832],codec.fingerprint(old[:829])))[-4:]
   target=p/f'v{version}.backup';target.write_bytes(old);files.append(target)
  old=bytearray((p/'v28.backup').read_bytes())
  margin=bytes([data[ghost_at+6],data[ghost_at+6]^255])
  margin_file=p/'v28-margin.backup';margin_file.write_bytes(old[:ghost_at]+margin+old[ghost_at+5:]);files.append(margin_file)
  if len(sys.argv)>2:files.extend(sorted(Path(sys.argv[2]).glob('*.backup')))
  # Keep generated test inputs separate from card data; create a fresh card for the check process.
  import shutil
  shutil.rmtree(card);card.mkdir()
  subprocess.run([str(p/'test'),'check',*(str(f) for f in files)],check=True,cwd=R)
  if len(sys.argv)>=2:
   out=Path(sys.argv[1])/str(board);out.mkdir(parents=True,exist_ok=True)
   for f in files:shutil.copy2(f,out/f.name)
