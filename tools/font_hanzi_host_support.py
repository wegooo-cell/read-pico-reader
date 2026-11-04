# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Link the shipped Han decoder and bytes into native-font host regressions."""
from pathlib import Path

def font_hanzi_host_sources(root: Path, work: Path) -> list[str]:
    name='ui-hanzi.bin'
    symbol='_binary_ui_hanzi_bin'
    data=(root/'main/assets'/name).read_bytes()
    p=work/'reader-hanzi-assets.c'
    p.write_text('#include <stdint.h>\nconst uint8_t reader_hanzi[] asm("'+symbol+'_start")={'+','.join(map(str,data))+'};\n'+f'asm(".globl {symbol}_end\\n.set {symbol}_end, {symbol}_start + {len(data)}");\n')
    header=work/'miniz.h'
    old=header.read_text() if header.exists() else '#pragma once\n'
    if 'tinfl_decompressor' not in old:
        header.write_text(old+'''\n#include <stddef.h>
#include <zlib.h>
typedef struct {int state;} tinfl_decompressor;
typedef int tinfl_status;
#define TINFL_STATUS_DONE 0
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(s) ((s)->state=0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor *s,const unsigned char *in,size_t *n,unsigned char *base,unsigned char *out,size_t *m,int flags){
 (void)s;(void)base;(void)flags;uLongf z=*m;int result=uncompress(out,&z,in,*n);*m=z;return result==Z_OK?0:-1;
}
''')
    return [str(root/'main/font/ui_hanzi.c'),str(p),'-lz']

# Existing cmap-removal fixture shared by title and body fallback regressions.
import struct
def subset_without_xing(raw):
 b=bytearray(raw);u16=lambda at:struct.unpack_from('>H',b,at)[0];u32=lambda at:struct.unpack_from('>I',b,at)[0]
 records={bytes(b[12+16*i:16+16*i]):12+16*i for i in range(u16(4))}
 record=records[b'cmap'];base=u32(record+8);mapping={}
 for i in range(u16(base+2)):
  at=base+4+8*i;platform,encoding,offset=struct.unpack_from('>HHI',b,at);o=base+offset
  if platform not in (0,3):continue
  if u16(o)==12:
   for j in range(u32(o+12)):
    first,last,gid=struct.unpack_from('>III',b,o+16+j*12)
    for cp in range(first,last+1):
     if gid+cp-first:mapping[cp]=gid+cp-first
  elif u16(o)==4:
   n=u16(o+6)//2;ends=o+14;starts=ends+n*2+2;deltas=starts+n*2;ranges=deltas+n*2
   for j in range(n):
    first,last,delta,distance=u16(starts+j*2),u16(ends+j*2),u16(deltas+j*2),u16(ranges+j*2)
    for cp in range(first,last+1):
     if cp==65535:continue
     gid=u16(ranges+j*2+distance+2*(cp-first)) if distance else cp
     gid=(gid+delta)&65535 if gid or not distance else 0
     if gid:mapping[cp]=gid
 assert mapping.pop(ord('行'),None)
 groups=b''.join(struct.pack('>III',cp,cp,gid) for cp,gid in sorted(mapping.items()))
 fmt=struct.pack('>HHIII',12,0,16+len(groups),0,len(mapping))+groups
 cmap=struct.pack('>HHHHI',0,1,3,10,12)+fmt
 while len(b)%4:b.append(0)
 offset=len(b);b.extend(cmap)
 def checksum(data):
  data=bytes(data)+b'\0'*((-len(data))%4)
  return sum(struct.unpack('>'+str(len(data)//4)+'I',data))&0xffffffff
 struct.pack_into('>III',b,record+4,checksum(cmap),offset,len(cmap))
 head=u32(records[b'head']+8);struct.pack_into('>I',b,head+8,0)
 struct.pack_into('>I',b,head+8,(0xb1b0afba-checksum(b))&0xffffffff)
 return b

def font_codepoints(raw: bytes) -> set[int]:
    """Read Unicode format4/12 coverage without a fontTools runtime dependency."""
    import struct
    u16=lambda p:struct.unpack_from('>H',raw,p)[0]
    u32=lambda p:struct.unpack_from('>I',raw,p)[0]
    tables={raw[12+i*16:16+i*16]:u32(20+i*16) for i in range(u16(4))}
    base=tables[b'cmap'];result=set()
    for i in range(u16(base+2)):
        platform,encoding,offset=struct.unpack_from('>HHI',raw,base+4+i*8);at=base+offset
        if platform not in (0,3):continue
        if u16(at)==12:
            for j in range(u32(at+12)):
                first,last,gid=struct.unpack_from('>III',raw,at+16+j*12)
                if gid:result.update(range(first,last+1))
        elif u16(at)==4:
            n=u16(at+6)//2;ends=at+14;starts=ends+n*2+2;deltas=starts+n*2;ranges=deltas+n*2
            for j in range(n):
                first,last,delta,distance=u16(starts+j*2),u16(ends+j*2),u16(deltas+j*2),u16(ranges+j*2)
                for cp in range(first,last+1):
                    if cp==65535:continue
                    gid=u16(ranges+j*2+distance+2*(cp-first)) if distance else cp
                    if (gid or not distance) and (gid+delta)&65535:result.add(cp)
    return result
