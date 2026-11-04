#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：保留原cmap，追加普通书籍符号，并剔除stb未使用的布局表和替代字形。
English: Preserve the original cmap, add book symbols and remove unused OpenType layout data.
"""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def read_codepoints(path):
 return {int(line.split('#',1)[0].strip(),16) for line in path.read_text().splitlines() if line.split('#',1)[0].strip()}
def main():
 from fontTools import version
 from fontTools.ttLib import TTFont
 from fontTools.subset import Options,Subsetter
 from fontTools.varLib.instancer import instantiateVariableFont
 p=argparse.ArgumentParser();p.add_argument('--src',type=Path,required=True);p.add_argument('--extra',type=Path,default=ROOT/'main/font/reader_extra_outlines.txt');p.add_argument('--out',type=Path,default=ROOT/'boards/pico/src/main/assets/builtin.ttf');a=p.parse_args()
 baseline=TTFont(ROOT/'main/assets/builtin.ttf');original=set(baseline.getBestCmap());baseline.close();extras=read_codepoints(a.extra);wanted=original|extras
 f=TTFont(a.src,recalcTimestamp=False)
 if 'fvar' in f:f=instantiateVariableFont(f,{'wght':500},inplace=True)
 assert f['OS/2'].usWeightClass==500 and wanted<=set(f.getBestCmap())
 # 本渲染器按Unicode取字，不执行GSUB/GPOS或竖排度量表。/ This renderer uses Unicode glyphs, not OpenType shaping/vertical metrics.
 for tag in ['BASE','GDEF','GPOS','GSUB','STAT','vhea','vmtx','gasp','prep','cvt ','fpgm']:
  if tag in f:del f[tag]
 o=Options();o.layout_features=[];o.layout_closure=False;o.notdef_outline=True;o.recommended_glyphs=True;o.name_IDs=['*'];o.name_legacy=True
 sub=Subsetter(options=o);sub.populate(unicodes=wanted);sub.subset(f);assert set(f.getBestCmap())==wanted
 for index,value in [(1,'Noto Sans SC'),(2,'Medium'),(4,'Noto Sans SC Medium'),(6,'NotoSansSC-Medium')]:f['name'].setName(value,index,3,1,0x409)
 a.out.parent.mkdir(parents=True,exist_ok=True);f.save(a.out);f.close()
 print(json.dumps(dict(codepoints=len(wanted),preserved=len(original),added=len(extras-original),bytes=a.out.stat().st_size,sha256=hashlib.sha256(a.out.read_bytes()).hexdigest(),fonttools=version)))
if __name__=='__main__':main()
