/* 中文：只解析 S31 分区和已确认启动记录，不访问硬件。 / English: Parse only S31 layouts and confirmed boot records without hardware access. */
export function require(condition,message){if(!condition)throw Error(message);}
export function equal(a,b){return a.length===b.length&&a.every((v,i)=>v===b[i]);}
export function partitionTable(data){
  require(data.length>=0xc00,'分区表不完整。');
  const expected=[['nvs',1,2,0x9000,0x4000],['otadata',1,0,0xd000,0x2000],['phy_init',1,1,0xf000,0x1000],['ota_0',0,16,0x10000,0x500000],['ota_1',0,17,0x510000,0x500000],['storage',1,0x81,0xa10000,0x5e0000],['coredump',1,3,0xff0000,0x10000]];
  const view=new DataView(data.buffer,data.byteOffset,data.byteLength);
  for(let i=0;i<expected.length;i++){
    const at=i*32,[name,kind,subtype,address,size]=expected[i];
    const actual=new TextDecoder().decode(data.slice(at+12,at+28)).split('\0')[0];
    require(view.getUint16(at,true)===0x50aa&&data[at+2]===kind&&data[at+3]===subtype&&view.getUint32(at+4,true)===address&&view.getUint32(at+8,true)===size&&actual===name&&view.getUint32(at+28,true)===0,'不支持的 S31 分区布局。');
  }
  require(view.getUint16(224,true)===0xebeb&&data.slice(256).every(v=>v===255),'分区表包含未知尾部。');
  return true;
}
export function otaCRC(bytes){let crc=0;for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
export function selectedSlot(data){
  require(data.length===8192,'启动记录不完整。');const records=[];let empty=0;
  for(let sector=0;sector<2;sector++){
    const record=data.slice(sector*4096,sector*4096+32);
    if(record.every(v=>v===255)){empty++;continue;}
    const v=new DataView(record.buffer),sequence=v.getUint32(0,true),state=v.getUint32(24,true);
    require(sequence>0&&sequence<0xffffffff&&v.getUint32(28,true)===otaCRC(record.slice(0,4)),'启动记录校验失败。');
    require([0,1,2,3,4,0xffffffff].includes(state),'启动状态未知。');
    require(state!==0&&state!==1,'上次更新尚未确认，请先正常启动设备再刷机。');
    if(state!==3&&state!==4)records.push({sequence,state,sector});
  }
  if(empty===2)return {address:0x10000,size:0x500000,index:0,initial:true};
  records.sort((a,b)=>b.sequence-a.sequence||a.sector-b.sector);
  require(records.length&&records[0].state===2,'没有已确认的启动槽，请先正常启动或使用 TF 升级。');
  const index=(records[0].sequence-1)%2;return {...records[0],index,address:index?0x510000:0x10000,size:0x500000};
}
