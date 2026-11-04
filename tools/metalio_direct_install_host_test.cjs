/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行实际S31安装器，验证不读旧固件品牌、启动槽和数据保护。
 * English: Execute the S31 installer to verify brand-free updates, boot-slot selection and data protection.
 */
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const core=fs.readFileSync(path.join(root,'flash/metalio-flash-core.js'),'utf8').replace(/export /g,'');
const installer=fs.readFileSync(path.join(root,'flash/metalio-installer.js'),'utf8').replace(/^import .*;\n/gm,'').replace(/await import\('[^']*esptool-js[^']*'\)/g,'await Promise.resolve({ESPLoader:FakeLoader})').replace(/await import\('[^']*metalio-transport[^']*'\)/g,'await Promise.resolve({S31Transport:FakeTransport})');
function table(){
 const data=Buffer.alloc(0xc00,255),subtypes={nvs:2,ota:0,phy:1,ota_0:16,ota_1:17,fat:0x81,coredump:3};let index=0;
 for(const line of fs.readFileSync(path.join(root,'boards/metalio47/partitions.csv'),'utf8').split('\n')){
  if(!line||line.startsWith('#'))continue;const [name,kind,subtype,offset,size]=line.split(',').map(s=>s.trim()),at=index++*32;
  data.writeUInt16LE(0x50aa,at);data[at+2]=kind==='app'?0:1;data[at+3]=subtypes[subtype];data.writeUInt32LE(Number(offset),at+4);data.writeUInt32LE(Number(size),at+8);data.fill(0,at+12,at+28);data.write(name,at+12);data.writeUInt32LE(0,at+28);
 }
 data.writeUInt16LE(0xebeb,index*32);return data;
}
async function run({slot=0,chip=32,badLayout=false,badBundle=false}={}){
 const tableData=table(),protectedData=new Uint8Array(0x8000).fill(255);protectedData.set(tableData);protectedData.fill(0x63,0x1000,0x5000);
 const app=new Uint8Array(512).fill(0x31),writes=[],reads=[],fields=new Map();let dialog,ports=0,requests=0;
 function element(){return {checked:false,disabled:false,hidden:false,textContent:'',listeners:{},addEventListener(n,cb){this.listeners[n]=cb;}};}
 class FakeTransport{async disconnect(){}}
 class FakeLoader{
  constructor(){this.chip={readMac:async()=> 'test-s31'};}
  async getSecurityInfo(){return {chipId:chip,parsedFlags:{},flashCryptCnt:0};}
  async detectFlashSize(){return '16MB';}flashSizeBytes(){return 0x1000000;}
  async writeFlash(opts){writes.push(opts);assert.equal(opts.eraseAll,false);assert.equal(opts.flashSize,'keep');}
  async writeReg(){}
 }
 const context=vm.createContext({Uint8Array,DataView,TextDecoder,performance,setTimeout,clearTimeout,Promise,Event,FakeTransport,FakeLoader,
  navigator:{serial:{requestPort:async()=>{ports++;return {};}}},
  document:{createElement(){dialog={...element(),innerHTML:'',setAttribute(){},showModal(){},remove(){},querySelector(s){if(!fields.has(s))fields.set(s,element());return fields.get(s);}};return dialog;},body:{appendChild(){}}},
  window:{dispatchEvent(){},KiikoreadFlasher:{prepare:async()=>{requests++;if(badBundle)throw Error('bad bundle');return {parts:[{name:'partitions.bin',data:tableData},{name:'firmware.bin',data:app}],dispose(){}};}}},
  connectS31:async()=>{},prepareS31Flash:async()=>{},connectionMessage:e=>e.message,
  readS31Flash:async(loader,address,size)=>{reads.push([address,size]);assert.equal(address,0x8000,'Never read or inspect the current application');assert.equal(size,0x8000);const b=protectedData.slice();if(badLayout)b[0]^=1;return b;},verifyS31Flash:async()=> 'verified'});
 vm.runInContext(core,context);
 if(slot!==null){const at=0x5000,v=new DataView(protectedData.buffer);protectedData.fill(255,at,at+32);v.setUint32(at,slot+1,true);v.setUint32(at+24,2,true);v.setUint32(at+28,context.otaCRC(protectedData.slice(at,at+4)),true);}
 vm.runInContext(installer,context);await context.window.MetalioInstaller.open({manifest:{version:'test'},manifestURL:'https://local.invalid/manifest.json',files:{}});
 assert(!fields.get('[data-clear]').checked);assert.equal(ports,0);await fields.get('[data-connect]').listeners.click();
 assert.equal(ports,1);assert.equal(requests,1);
 if(badLayout||badBundle||chip!==32){assert.equal(writes.length,0);return;}
 assert.equal(writes.length,1);assert.equal(writes[0].fileArray.length,1);assert.equal(writes[0].fileArray[0].address,slot===1?0x510000:0x10000);assert.equal(writes[0].fileArray[0].data,app);assert.equal(reads.length,3);assert.match(fields.get('[data-status]').textContent,/安装及设备端校验完成/);
}
(async()=>{for(const slot of [0,1,null])await run({slot});await run({badLayout:true});await run({badBundle:true});await run({chip:9});console.log('PASS: S31 installs without reading existing firmware for both confirmed slots/initial boot; app-only writes, unchanged metadata, default data retention, model/layout/bundle rejection and no automatic serial request');})().catch(e=>{console.error(e);process.exitCode=1;});
