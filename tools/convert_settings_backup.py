#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Export checked historical interchange formats1..25 without modifying the full original backup."""
import argparse
import json
import os
from pathlib import Path
import struct

MAX_BYTES = 4 * 1024 * 1024
BASE_SIZE = 829  # Frozen byte-only v1: magic,17 flags,160+160+288+96+96 paths,seal.
SIZES = {'extension':7,'profile':453,'keys':7,'hold':5,'adjust':5,'weight':5,
         'wifi':103,'background':292,'home':6,'bookmark':292,'controls':17,'shelf':6,'guide':5,'covers':5}
FNV = 2166136261

def fingerprint(data, value=FNV):
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

def seal(data, initial=FNV):
    return data + struct.pack('<I',fingerprint(data,initial))

def history_records(data):
    if not data.startswith(b'RPHIST1\0'):
        raise ValueError('阅读资料格式无效')
    offset=8; records=[]; checksum=FNV
    while True:
        head=data[offset:offset+20];offset+=20
        if len(head)!=20:
            raise ValueError('阅读资料未完整保存')
        if head[0]==255:
            if head[1:] != b'\0'*19:raise ValueError('阅读资料结束标志损坏')
            break
        size=int.from_bytes(head[18:20],'little')
        if head[0]>5 or not 0<size<=4096 or b'\0' not in head[2:18] or len(records)==512:
            raise ValueError('阅读资料条目超出兼容范围')
        value=data[offset:offset+size];offset+=size
        if len(value)!=size:raise ValueError('阅读资料条目不完整')
        checksum=fingerprint(value,fingerprint(head,checksum));records.append((head,value))
    if len(data)-offset!=6 or struct.unpack('<HI',data[offset:])!=(len(records),checksum):
        raise ValueError('阅读资料校验失败')
    return records

def write_history(records):
    result=bytearray(b'RPHIST1\0');checksum=FNV
    for head,value in records:
        result.extend(head);result.extend(value);checksum=fingerprint(value,fingerprint(head,checksum))
    result.extend(b'\xff'+b'\0'*19);result.extend(struct.pack('<HI',len(records),checksum))
    return bytes(result)

def parse(data):
    if not BASE_SIZE<=len(data)<=MAX_BYTES or data[:7]!=b'PICOSET':raise ValueError('配置格式或大小无效')
    code=data[7];version=code-48 if 49<=code<=57 else code-65+10 if 65<=code<=83 else 0
    if not 20<=version<=28:raise ValueError('此工具仅转换第20至28版；更早备份由新版固件直接恢复')
    base=data[:BASE_SIZE]
    if fingerprint(base[:-4])!=int.from_bytes(base[-4:],'little'):raise ValueError('配置主体校验失败')
    fields={};offset=BASE_SIZE
    for name,size in SIZES.items():
        if (name=='controls' and version<24) or (name=='shelf' and version<25) or (name=='guide' and version<26) or (name=='covers' and version<28):continue
        field=data[offset:offset+size];offset+=size
        if len(field)!=size:raise ValueError('配置字段缺失: '+name)
        if name=='extension': initial=fingerprint(base)
        elif name=='profile': initial=fingerprint(fields['extension'][:3],fingerprint(base))
        else: initial=FNV
        if fingerprint(field[:-4],initial)!=int.from_bytes(field[-4:],'little'):
            raise ValueError('配置字段校验失败: '+name)
        fields[name]=field
    return version,base,fields,history_records(data[offset:])

def convert(data,target=25):
    version,base,fields,records=parse(data)
    if not 1<=target<=25 or target>version:raise ValueError('请选择不高于原备份的兼容格式1至25')
    head=bytearray(base[:-4]);head[7]=48+target if target<10 else 65+target-10
    flags=memoryview(head)[8:25]
    if version>=27:
        if flags[13]>200 or flags[15]>200 or flags[15]%10:
            raise ValueError('排版范围编码无效')
        flags[5]=max(18,min(90,flags[5]))
        flags[13]=max(110,min(150,flags[13]+100))
        percent=int(flags[15])-100
        flags[15]=min((0,5,10,15,20,25,30,40,50,75,100),key=lambda stop:abs(stop-percent))
    if flags[8]>(1 if target<20 else 2):flags[8]=0
    if flags[7]==20:flags[7]=15
    flags[10]&=7 if target>=23 else 3 if target>=22 else 1
    if target<21:flags[12]%=3
    if target<20:
        pixels=int(flags[11])-20
        flags[11]=max(0,min(20,pixels+4)) if target==19 else max(0,min(4,round((pixels+4)/2)))
        flags[15]=min(75,((flags[15]+12)//25)*25)
        if flags[16]>3:flags[16]=2
    if target<18 and flags[4]>2:flags[4]=0
    flags[14]=min(flags[14],48)
    del flags
    base=seal(bytes(head));out=bytearray(base)
    raw_extension=bytearray(fields['extension'][:3])
    raw_extension[2]&=23 if target>=9 else 7 if target==8 else 1
    if target==2:out.extend(seal(bytes(raw_extension[:1]),fingerprint(base)))
    elif target==3:out.extend(seal(bytes(raw_extension[:2]),fingerprint(base)))
    elif target>=4:out.extend(seal(bytes(raw_extension),fingerprint(base)))
    if target>=5:
        profile=bytearray(fields['profile'][:-4])
        if target<5:profile[-1]&=1
        out.extend(seal(bytes(profile),fingerprint(raw_extension,fingerprint(base))))
    minimum={'keys':10,'hold':11,'adjust':12,'weight':13,'wifi':7,'background':14,'home':16,'bookmark':18,'controls':24,'shelf':25}
    for name in ('keys','hold','adjust','weight','wifi','background','home','bookmark','controls','shelf'):
        if target<minimum[name]:continue
        value=fields[name]
        if name=='weight' and target<17 and value[0]>=128:
            value=seal(bytes([max(0,min(4,int((value[0]-160)/16)+2))]))
        out.extend(value)
    selected=[(h,v) for h,v in records if not (h[0]==1 and
        (target<25 and h[2:18].split(b'\0')[0] in (b'reading_ms',b'position') or
         target<18 and h[2:18].split(b'\0')[0]==b'heat32'))]
    if target>=6:out.extend(write_history(selected))
    else:selected=[]
    result=bytes(out)
    if target>=20:parse(result)
    return result,{'source_format':version,'target_format':target,'history_records':len(selected),
                   'unsupported_history_records_skipped':len(records)-len(selected)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--format',type=int,choices=range(1,26),default=25)
    args=parser.parse_args()
    if args.source.resolve()==args.output.resolve():parser.error('请使用新文件名，保留原完整备份')
    if args.source.stat().st_size>MAX_BYTES:parser.error('配置文件超过大小限制')
    try:data,report=convert(args.source.read_bytes(),args.format)
    except ValueError as error:parser.error(str(error))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('xb') as file:
        os.chmod(args.output,0o600);file.write(data);file.flush();os.fsync(file.fileno())
    print(json.dumps(report,ensure_ascii=False))
if __name__=='__main__':main()
