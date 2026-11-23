/* SPDX-License-Identifier: Apache-2.0
 * 中文：小纸 Pico 官方分区表与四件套校验；只解析清单和读回数据，不访问硬件。
 * English: Validate the official Pico layout and four-part bundle; parse manifests and read-back data without hardware access.
 */
export const PICO_FLASH_SIZE=0x1000000;
// 与 boards/pico/partitions.csv 逐项一致（标签、类型、偏移、大小）。
// Matches boards/pico/partitions.csv entry by entry (label, type, offset and size).
export const PICO_LAYOUT=Object.freeze([
  Object.freeze({name:'nvs',type:1,subtype:2,offset:0x9000,size:0x5000}),
  Object.freeze({name:'phy_init',type:1,subtype:1,offset:0xe000,size:0x1000}),
  Object.freeze({name:'ota_0',type:0,subtype:16,offset:0x10000,size:0x400000}),
  Object.freeze({name:'storage',type:1,subtype:0x81,offset:0x410000,size:0x500000}),
  Object.freeze({name:'ota_1',type:0,subtype:17,offset:0x910000,size:0x400000}),
  Object.freeze({name:'otadata',type:1,subtype:0,offset:0xd10000,size:0x2000})
]);
export const PICO_PARTS=Object.freeze([
  Object.freeze({name:'bootloader.bin',offset:0,max:0x6000}),
  Object.freeze({name:'partitions.bin',offset:0x8000,max:0x1000}),
  Object.freeze({name:'firmware.bin',offset:0x10000,max:0x400000}),
  Object.freeze({name:'ota_data_initial.bin',offset:0xd10000,max:0x2000})
]);
export const PICO_FOREIGN_MESSAGE='检测到非官方分区表，需要整机重装';
export const PICO_FAILURE_HINT='分区表或应用可能不完整，设备可能无法开机。请保持 USB 连接，重新连接后执行整机重装；不要断电或拔出设备。';
export function require(condition,message){if(!condition)throw Error(message);}
export function equal(a,b){return a.length===b.length&&a.every((value,index)=>value===b[index]);}
export function stageError(stage,message,cause){
  const error=Error(`${stage}：${message}`);
  error.stage=stage;
  if(cause)error.cause=cause;
  return error;
}
function asBytes(data){
  if(ArrayBuffer.isView(data)&&data.BYTES_PER_ELEMENT===1)return new Uint8Array(data.buffer,data.byteOffset,data.byteLength);
  if(Object.prototype.toString.call(data)==='[object ArrayBuffer]')return new Uint8Array(data);
  throw Error('分区表数据类型无效。');
}
export async function digestHex(data){
  const digest=await crypto.subtle.digest('SHA-256',asBytes(data));
  return Array.from(new Uint8Array(digest),byte=>byte.toString(16).padStart(2,'0')).join('');
}
// 设备读回的一个分区扇区通常为 0xC00 表加空白；结束标记之后不接受其他内容。
// A read-back partition sector is normally the 0xC00 table plus padding; nothing may follow the end marker.
export function parsePartitionTable(data){
  const bytes=asBytes(data);
  require(bytes.length>=32,'分区表不完整。');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const entries=[];
  let ended=false;
  for(let at=0;at+32<=bytes.length;at+=32){
    const magic=view.getUint16(at,true);
    if(magic===0xebeb){
      for(let tail=at+32;tail<bytes.length;tail++)require(bytes[tail]===255,'分区表结束标记后仍有内容。');
      ended=true;break;
    }
    require(magic===0x50aa,`分区表第 ${entries.length+1} 项标记异常。`);
    const nameBytes=bytes.subarray(at+12,at+28),zero=nameBytes.indexOf(0);
    const name=new TextDecoder().decode(nameBytes.subarray(0,zero<0?16:zero));
    require(name.length>0&&/^[\x20-\x7e]+$/.test(name),`分区表第 ${entries.length+1} 项标签无效。`);
    entries.push({name,type:bytes[at+2],subtype:bytes[at+3],offset:view.getUint32(at+4,true),size:view.getUint32(at+8,true),flags:view.getUint32(at+28,true)});
  }
  require(ended,'分区表缺少结束标记。');
  return entries;
}
export function validateOfficialTable(data){
  const entries=parsePartitionTable(data);
  require(entries.length===PICO_LAYOUT.length,`官方分区表条目数量为 ${entries.length}，应为 ${PICO_LAYOUT.length}。`);
  entries.forEach((entry,index)=>{
    const expected=PICO_LAYOUT[index];
    require(entry.name===expected.name&&entry.type===expected.type&&entry.subtype===expected.subtype&&entry.offset===expected.offset&&entry.size===expected.size&&entry.flags===0,`官方分区表第 ${index+1} 项与 boards/pico/partitions.csv 不一致。`);
  });
  return entries;
}
export function tableBytesMatch(deviceData,officialData){
  const device=asBytes(deviceData),official=asBytes(officialData);
  if(device.length<official.length)return false;
  for(let index=0;index<official.length;index++)if(device[index]!==official[index])return false;
  return true;
}
export function isBlankTable(data){const bytes=asBytes(data);return bytes.length>0&&bytes.every(value=>value===255);}
export function partitionTableMatches(deviceData,officialData){
  try{
    const actual=parsePartitionTable(deviceData),expected=validateOfficialTable(officialData);
    if(actual.length!==expected.length)return false;
    return actual.every((entry,index)=>entry.name===expected[index].name&&entry.type===expected[index].type&&entry.subtype===expected[index].subtype&&entry.offset===expected[index].offset&&entry.size===expected[index].size&&entry.flags===expected[index].flags);
  }catch{return false;}
}
// 中文：官方布局只更新四件套；空白或外来布局必须整机擦除后重装。 / English: Official layouts update four parts; blank or foreign layouts require a full erase and reinstall.
export function classifyDeviceTable(deviceData,officialData){
  const device=asBytes(deviceData);
  const officialEntries=validateOfficialTable(officialData);
  const result={mode:'foreign',official:false,blank:false,entries:[],officialEntries,foreignNames:[],missingNames:[],mismatches:[],bytesMatch:false,message:''};
  if(isBlankTable(device))return {...result,blank:true,mode:'blank',message:'设备没有可识别的官方分区表，需要整机重装。'};
  let entries;
  try{entries=parsePartitionTable(device);}catch(error){
    return {...result,parseError:error.message,message:`无法识别设备分区表（${error.message}），${PICO_FOREIGN_MESSAGE}。`};
  }
  result.entries=entries;
  result.bytesMatch=tableBytesMatch(device,officialData);
  const names=new Set(entries.map(entry=>entry.name));
  for(const expected of PICO_LAYOUT){
    if(!names.has(expected.name))result.missingNames.push(expected.name);
  }
  for(const entry of entries)if(!PICO_LAYOUT.some(expected=>expected.name===entry.name)&&!result.foreignNames.includes(entry.name))result.foreignNames.push(entry.name);
  const fields=['name','type','subtype','offset','size','flags'];
  for(let index=0;index<Math.min(entries.length,officialEntries.length);index++){
    for(const field of fields)if(entries[index][field]!==officialEntries[index][field])result.mismatches.push({name:entries[index].name,field,expected:officialEntries[index][field],actual:entries[index][field]});
  }
  if(entries.length!==officialEntries.length)result.mismatches.push({name:'',field:'count',expected:officialEntries.length,actual:entries.length});
  if(result.mismatches.length===0&&result.foreignNames.length===0&&result.missingNames.length===0){
    result.mode='official';result.official=true;result.message='设备分区表与官方布局一致。';
    return result;
  }
  const details=[];
  if(result.foreignNames.length)details.push(`外来分区：${result.foreignNames.join('、')}`);
  if(result.missingNames.length)details.push(`缺少分区：${result.missingNames.join('、')}`);
  for(const item of result.mismatches.slice(0,6))details.push(item.field==='count'?`条目数 ${item.actual}≠${item.expected}`:`${item.name||'未知'}.${item.field} ${item.actual}≠${item.expected}`);
  result.message=`${PICO_FOREIGN_MESSAGE}（${details.join('；')||'布局不一致'}）。`;
  return result;
}
export function planInstall(classification){
  require(classification&&['official','blank','foreign'].includes(classification.mode),'设备分区表检查结果无效。');
  if(classification.mode==='official')return {mode:'update',fullErase:false,requiresConfirmation:false,message:'设备分区表与官方布局一致：更新四件套，保留机内设置与存储分区。'};
  const reason=classification.mode==='blank'?'设备没有官方分区表':'检测到非官方分区表';
  return {mode:'full',fullErase:true,requiresConfirmation:true,message:`${reason}，需要整机重装：擦除全部 Flash 后写入四件套，不能只更新应用。`};
}
export function validatePicoManifest(manifest,files){
  require(manifest&&typeof manifest==='object','刷机清单无效。');
  require(/^\d+\.\d+\.\d+(?:-rc\d+)?$/.test(manifest.version),'刷机清单版本无效。');
  if(manifest.board!==undefined)require(manifest.board==='pico','刷机清单不属于小纸 Pico。');
  if(manifest.project!==undefined)require(manifest.project==='Read_Pico','刷机清单项目不是小纸 Pico。');
  require(manifest.new_install_prompt_erase===true,'刷机清单未声明安装前擦除确认。');
  require(Array.isArray(manifest.builds)&&manifest.builds.length===1,'刷机清单构建数量无效。');
  const build=manifest.builds[0];
  require(build&&build.chipFamily==='ESP32-S3','刷机清单芯片不是 ESP32-S3。');
  require(Array.isArray(build.parts)&&build.parts.length===4,'刷机清单必须包含四件套。');
  const seen=new Set();
  for(const part of build.parts){
    require(part&&Number.isSafeInteger(part.offset),'刷机清单地址无效。');
    const spec=PICO_PARTS.find(item=>item.offset===part.offset);
    require(spec,`刷机地址 0x${part.offset.toString(16)} 不属于小纸 Pico。`);
    require(!seen.has(part.offset),'刷机清单包含重复地址。');
    seen.add(part.offset);
    const info=part.sha256&&Number.isSafeInteger(part.size)?part:files?.[spec.name];
    require(info&&/^[a-f0-9]{64}$/.test(info.sha256||''),`${spec.name} 缺少 SHA-256。`);
    require(Number.isSafeInteger(info.size)&&info.size>0&&info.size<=spec.max,`${spec.name} 大小无效。`);
    if(spec.name==='partitions.bin')require(info.size===0xc00,'分区表大小必须为 0xC00。');
    if(spec.name==='ota_data_initial.bin')require(info.size===0x2000,'启动记录大小必须为 0x2000。');
  }
  require(seen.size===4,'刷机清单缺少四件套。');
  return build.parts;
}
export function validatePicoBundle(manifest,bundle,files){
  const declared=validatePicoManifest(manifest,files);
  require(bundle&&Array.isArray(bundle.parts)&&bundle.parts.length===4,'刷机包缺少四件套。');
  for(const spec of PICO_PARTS){
    const part=bundle.parts.find(item=>item.name===spec.name);
    require(part&&ArrayBuffer.isView(part.data)&&part.data.length>0,`刷机包缺少 ${spec.name}。`);
    const expected=declared.find(item=>item.offset===spec.offset);
    const info=expected.sha256&&Number.isSafeInteger(expected.size)?expected:files?.[spec.name];
    require(info&&/^[a-f0-9]{64}$/.test(info.sha256||''),`${spec.name} 缺少 SHA-256。`);
    require(part.offset===spec.offset,`${spec.name} 地址不符。`);
    require(part.data.length===info.size,`${spec.name} 大小与清单不符。`);
    require(part.sha256===info.sha256,`${spec.name} SHA-256 与清单不符。`);
  }
  return true;
}
export function installationParts(bundle,plan){
  require(plan&&typeof plan.fullErase==='boolean','安装计划无效。');
  require(plan.mode==='update'||plan.fullErase===true,'非官方分区表必须整机擦除，不能只更新应用。');
  const parts=[];
  for(const spec of PICO_PARTS){
    const part=(bundle?.parts??[]).find(item=>item.name===spec.name);
    require(part&&ArrayBuffer.isView(part.data)&&part.data.length>0,`刷机包缺少 ${spec.name}。`);
    require(part.offset===spec.offset,`${spec.name} 地址不符。`);
    require(part.data.length<=spec.max,`${spec.name} 超过可写入范围。`);
    parts.push({data:part.data,address:part.offset});
  }
  require(parts.length===4,'整机安装必须写入四件套。');
  return parts.sort((left,right)=>left.address-right.address);
}
export async function verifyPartSha(part,read,digest=digestHex){
  require(part&&/^[a-f0-9]{64}$/.test(part.sha256||''),'写入后校验缺少 SHA-256。');
  let data;
  try{data=await read(part.offset,part.size);}catch(error){throw stageError('写入后校验',`${part.name} 回读失败（地址 0x${part.offset.toString(16)}）：${error.message}`,error);}
  if(!(ArrayBuffer.isView(data)&&data.BYTES_PER_ELEMENT===1&&data.length===part.size))throw stageError('写入后校验',`${part.name} 回读长度不符（地址 0x${part.offset.toString(16)}，期望 ${part.size} 字节）。`);
  const actual=await digest(data);
  if(actual!==part.sha256)throw stageError('写入后校验',`${part.name} SHA-256 校验失败（地址 0x${part.offset.toString(16)}，期望 ${part.sha256.slice(0,12)}…，实际 ${String(actual).slice(0,12)}…）。`);
  return actual;
}
export async function verifyWrittenParts(parts,read,digest=digestHex,onProgress=()=>{}){
  require(Array.isArray(parts)&&parts.length===4,'写入后校验必须覆盖四件套。');
  for(let index=0;index<parts.length;index++){
    const part=parts[index];
    require(part&&typeof part.name==='string'&&Number.isSafeInteger(part.offset)&&Number.isSafeInteger(part.size)&&part.size>0,'写入后校验参数无效。');
    const actual=await verifyPartSha(part,read,digest);
    onProgress(part,index,actual);
  }
  return true;
}
export function verifyPartitionTable(deviceData,officialData){
  const classification=classifyDeviceTable(deviceData,officialData);
  if(!classification.official)throw stageError('分区表复核',`${classification.message}（地址 0x8000）。`);
  require(tableBytesMatch(deviceData,officialData),'分区表内容与已校验的 partitions.bin 不一致（地址 0x8000）。');
  return classification.entries;
}
