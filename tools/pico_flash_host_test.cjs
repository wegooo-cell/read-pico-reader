/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行实际 Pico 分区表和安装器，覆盖官方布局、外来布局、SHA/偏移、整机重装与失败回滚提示。
 * English: Run the actual Pico partition table and installer against official/foreign layouts, SHA/offset validation, full reinstall and failure hints.
 */
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const {webcrypto,createHash}=require('node:crypto');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const root=path.resolve(__dirname,'..');
const coreSource=fs.readFileSync(path.join(root,'flash/pico-flash-core.js'),'utf8').replace(/export /g,'');
const installerSource=fs.readFileSync(path.join(root,'flash/pico-installer.js'),'utf8')
  .replace(/^import .*;\r?\n/gm,'')
  .replace(/await import\('[^']*esptool-js[^']*'\)/g,'await Promise.resolve({ESPLoader:FakeLoader,Transport:FakeTransport})');
const SUBTYPES={nvs:2,phy:1,ota_0:16,fat:0x81,ota_1:17,ota:0};
function layout(){
  return fs.readFileSync(path.join(root,'boards/pico/partitions.csv'),'utf8').split(/\r?\n/).filter(line=>line&&!line.startsWith('#')).map(line=>{
    const [name,kind,subtype,offset,size]=line.split(',').map(value=>value.trim());
    return {name,type:kind==='app'?0:1,subtype:SUBTYPES[subtype],offset:Number(offset),size:Number(size),flags:0};
  });
}
function table(entries){
  const data=new Uint8Array(0xc00).fill(255),view=new DataView(data.buffer);
  entries.forEach((entry,index)=>{
    const at=index*32;view.setUint16(at,0x50aa,true);data[at+2]=entry.type;data[at+3]=entry.subtype;
    view.setUint32(at+4,entry.offset,true);view.setUint32(at+8,entry.size,true);
    data.fill(0,at+12,at+28);data.set(new TextEncoder().encode(entry.name),at+12);view.setUint32(at+28,entry.flags||0,true);
  });
  const end=entries.length*32;
  view.setUint16(end,0xebeb,true);
  // 官方表在结束项里写入前 192 字节的 MD5；读回校验忽略该值，SHA 仍按完整表核对。
  // The official table stores the MD5 of the entries in its end item; read-back validation ignores it while SHA covers the full table.
  data.set(createHash('md5').update(data.subarray(0,end)).digest(),end+16);
  return data;
}
const officialLayout=layout();
const officialTable=table(officialLayout);
const order=[['bootloader.bin',0],['partitions.bin',0x8000],['firmware.bin',0x10000],['ota_data_initial.bin',0xd10000]];
function bundleFor(partitions=officialTable){
  const data={
    'bootloader.bin':new Uint8Array(512).fill(0x11),
    'partitions.bin':partitions,
    'firmware.bin':new Uint8Array(1024).fill(0x22),
    'ota_data_initial.bin':new Uint8Array(0x2000).fill(255)
  };
  return order.map(([name,offset])=>({name,offset,data:data[name],sha256:sha(data[name])}));
}
function manifestFor(bundle){
  return {name:'kiikoread',version:'0.3.3-rc110',board:'pico',project:'Read_Pico',new_install_prompt_erase:true,
    builds:[{chipFamily:'ESP32-S3',parts:bundle.map(part=>({path:part.name,offset:part.offset,size:part.data.length,sha256:part.sha256}))}]};
}
function newCore(){
  const context=vm.createContext({crypto:webcrypto,console,TextDecoder,TextEncoder});
  vm.runInContext(coreSource,context);
  return context;
}
function foreignLayout(){
  return [
    {name:'nvs',type:1,subtype:2,offset:0x9000,size:0x5000,flags:0},
    {name:'otadata',type:1,subtype:0,offset:0xe000,size:0x2000,flags:0},
    {name:'app0',type:0,subtype:16,offset:0x10000,size:0x400000,flags:0},
    {name:'font',type:1,subtype:0x82,offset:0x742000,size:0x100000,flags:0},
    {name:'coredump',type:1,subtype:3,offset:0x842000,size:0x10000,flags:0},
    {name:'crashlog',type:1,subtype:0x83,offset:0x843000,size:0x10000,flags:0},
    {name:'spiffs',type:1,subtype:0x82,offset:0xf6a000,size:0x90000,flags:0}
  ];
}
function setupInstaller(deviceTable,fail={}){
  const bundle=bundleFor(),manifest=manifestFor(bundle),writes=[],reads=[],requests={ports:0,prepares:0},fields=new Map();
  const memory=new Uint8Array(0x1000000).fill(255);memory.set(deviceTable,0x8000);
  let dialog;
  function element(){return {checked:false,disabled:false,hidden:false,textContent:'',listeners:{},addEventListener(name,callback){this.listeners[name]=callback;}};}
  class FakeTransport{async disconnect(){}}
  class FakeLoader{
    constructor(){this.chip={CHIP_NAME:'ESP32-S3',IMAGE_CHIP_ID:9,readMac:async()=>'test-pico'};this.IS_STUB=false;}
    async main(){this.IS_STUB=true;}
    async getSecurityInfo(){return {chipId:9,parsedFlags:{},flashCryptCnt:0};}
    async detectFlashSize(){return '16MB';}
    flashSizeBytes(){return 0x1000000;}
    async runStub(){this.IS_STUB=true;}
    async readFlash(address,size,progress){reads.push([address,size]);if(progress)progress(size,size);return new Uint8Array(memory.subarray(address,address+size));}
    async writeFlash(options){writes.push(options);if(fail.write)throw Error('模拟写入失败');if(options.eraseAll)memory.fill(255);for(const part of options.fileArray)memory.set(part.data,part.address);if(fail.verify){const firmware=options.fileArray.find(part=>part.address===0x10000);memory[0x10000]^=1;}}
    async after(){}
  }
  const context=vm.createContext({
    crypto:webcrypto,console,TextDecoder,TextEncoder,performance,setTimeout,clearTimeout,Promise,Event,Error,
    navigator:{serial:{requestPort:async()=>{requests.ports++;return {};}}},
    document:{createElement(){
      dialog={...element(),innerHTML:'',setAttribute(){},showModal(){},remove(){},querySelector(selector){if(!fields.has(selector))fields.set(selector,element());return fields.get(selector);}};
      return dialog;
    },body:{appendChild(){}}},
    window:{dispatchEvent(){},KiikoreadFlasher:{prepare:async()=>{requests.prepares++;return {parts:bundle,dispose(){}};}}},
    FakeLoader,FakeTransport
  });
  vm.runInContext(coreSource,context);
  vm.runInContext(installerSource,context);
  return {context,fields,writes,reads,requests,manifest,bundle,bundleParts:bundle};
}
function field(installer,selector){return installer.fields.get(selector);}
(async()=>{
  const core=newCore();
  const bundle=bundleFor(),manifest=manifestFor(bundle);
  // 官方布局：分区表逐项、SHA 与四件套地址；同时核对当前发布清单。
  const published=JSON.parse(fs.readFileSync(path.join(root,'flash/manifest.json'),'utf8'));
  assert.equal(core.validatePicoManifest(published).length,4);
  assert.equal(sha(officialTable),published.builds[0].parts.find(part=>part.offset===0x8000).sha256);
  const officialEntries=core.parsePartitionTable(officialTable);
  assert.equal(officialEntries.length,6);
  assert.deepEqual(JSON.parse(JSON.stringify(officialEntries)),officialLayout);
  assert.equal(sha(officialTable),manifest.builds[0].parts.find(part=>part.offset===0x8000).sha256);
  assert.equal(core.validateOfficialTable(officialTable).length,6);
  assert.equal(core.partitionTableMatches(officialTable,officialTable),true);
  const officialResult=core.classifyDeviceTable(officialTable,officialTable);
  assert.equal(officialResult.mode,'official');assert.equal(officialResult.official,true);
  const updatePlan=core.planInstall(officialResult);
  assert.equal(updatePlan.mode,'update');assert.equal(updatePlan.fullErase,false);assert.equal(updatePlan.requiresConfirmation,false);
  const updateParts=core.installationParts({parts:bundle},updatePlan);
  assert.deepEqual(Array.from(updateParts,part=>part.address),[0,0x8000,0x10000,0xd10000]);
  assert.equal(core.validatePicoBundle(manifest,{parts:bundle}),true);
  // 外来布局：列出外来标签并强制整机重装，绝不退化为只更新应用。
  const foreignTable=table(foreignLayout());
  const foreignResult=core.classifyDeviceTable(foreignTable,officialTable);
  assert.equal(foreignResult.mode,'foreign');assert.equal(foreignResult.official,false);
  for(const name of ['app0','font','spiffs','coredump','crashlog'])assert(foreignResult.foreignNames.includes(name),`missing foreign label ${name}`);
  assert.match(foreignResult.message,/检测到非官方分区表，需要整机重装/);
  const fullPlan=core.planInstall(foreignResult);
  assert.equal(fullPlan.mode,'full');assert.equal(fullPlan.fullErase,true);assert.equal(fullPlan.requiresConfirmation,true);
  assert.match(fullPlan.message,/检测到非官方分区表，需要整机重装/);
  assert.match(fullPlan.message,/擦除全部 Flash/);assert.match(fullPlan.message,/不能只更新应用/);
  const fullParts=core.installationParts({parts:bundle},fullPlan);
  assert.equal(fullParts.length,4,'foreign layouts must write all four parts');
  assert.deepEqual(Array.from(fullParts,part=>part.address),[0,0x8000,0x10000,0xd10000]);
  assert.equal(core.partitionTableMatches(foreignTable,officialTable),false);
  assert.throws(()=>core.validateOfficialTable(foreignTable),/不一致|条目数量/);
  // 空白设备、偏移/标签/数量不符都不得当作官方布局。
  const blank=new Uint8Array(0x1000).fill(255);
  const blankResult=core.classifyDeviceTable(blank,officialTable);
  assert.equal(blankResult.mode,'blank');assert.equal(core.planInstall(blankResult).fullErase,true);
  const shifted=officialLayout.map((entry,index)=>index===2?{...entry,offset:0x20000}:entry);
  const shiftedResult=core.classifyDeviceTable(table(shifted),officialTable);
  assert.equal(shiftedResult.mode,'foreign');assert(shiftedResult.mismatches.some(item=>item.field==='offset'));
  const renamed=officialLayout.map((entry,index)=>index===2?{...entry,name:'app0'}:entry);
  const renamedResult=core.classifyDeviceTable(table(renamed),officialTable);
  assert.equal(renamedResult.mode,'foreign');assert(renamedResult.foreignNames.includes('app0'));assert(renamedResult.missingNames.includes('ota_0'));
  const extra=[...officialLayout,{name:'spiffs',type:1,subtype:0x82,offset:0xf6a000,size:0x90000,flags:0}];
  const extraResult=core.classifyDeviceTable(table(extra),officialTable);
  assert.equal(extraResult.mode,'foreign');assert(extraResult.mismatches.some(item=>item.field==='count'));
  const broken=officialTable.slice();broken[0]=0;broken[1]=0;
  const brokenResult=core.classifyDeviceTable(broken,officialTable);
  assert.equal(brokenResult.mode,'foreign');assert(brokenResult.parseError);
  // 清单偏移、分区表大小、启动记录大小和四件套 SHA 必须一致。
  const badOffset=JSON.parse(JSON.stringify(manifest));badOffset.builds[0].parts.find(part=>part.offset===0x10000).offset=0x510000;
  assert.throws(()=>core.validatePicoManifest(badOffset),/地址/);
  const badParts=JSON.parse(JSON.stringify(manifest));badParts.builds[0].parts.pop();
  assert.throws(()=>core.validatePicoManifest(badParts),/四件套/);
  const badChip=JSON.parse(JSON.stringify(manifest));badChip.builds[0].chipFamily='ESP32-S31';
  assert.throws(()=>core.validatePicoManifest(badChip),/芯片/);
  const badVersion=JSON.parse(JSON.stringify(manifest));badVersion.version='rc110';
  assert.throws(()=>core.validatePicoManifest(badVersion),/版本/);
  const badTableSize=JSON.parse(JSON.stringify(manifest));badTableSize.builds[0].parts.find(part=>part.offset===0x8000).size=0x1000;
  assert.throws(()=>core.validatePicoManifest(badTableSize),/分区表大小/);
  const badOtaSize=JSON.parse(JSON.stringify(manifest));badOtaSize.builds[0].parts.find(part=>part.offset===0xd10000).size=0x1000;
  assert.throws(()=>core.validatePicoManifest(badOtaSize),/启动记录大小/);
  const badSha=JSON.parse(JSON.stringify(manifest));badSha.builds[0].parts.find(part=>part.offset===0x8000).sha256='0'.repeat(64);
  assert.throws(()=>core.validatePicoBundle(badSha,{parts:bundle}),/SHA-256 与清单不符/);
  // 写后回读：正确数据通过，篡改或长度不足必须报出具体阶段。
  const expected=bundle.map(part=>({name:part.name,offset:part.offset,size:part.data.length,sha256:part.sha256}));
  const readExpected=async(address,size)=>{const part=bundle.find(item=>item.offset===address);return new Uint8Array(part.data.subarray(0,size));};
  assert.equal(await core.verifyWrittenParts(expected,readExpected),true);
  assert.equal(core.verifyPartitionTable(officialTable,officialTable).length,6);
  const corrupt=officialTable.slice();corrupt[192+32]=0;
  assert.throws(()=>core.verifyPartitionTable(corrupt,officialTable),/分区表复核/);
  const badRead=async(address,size)=>{const part=bundle.find(item=>item.offset===address);const data=new Uint8Array(part.data.subarray(0,size));if(address===0x10000)data[0]^=1;return data;};
  await assert.rejects(core.verifyWrittenParts(expected,badRead),error=>error.stage==='写入后校验'&&/firmware\.bin SHA-256 校验失败/.test(error.message));
  const shortRead=async(address,size)=>new Uint8Array(size-1);
  await assert.rejects(core.verifyWrittenParts(expected,shortRead),error=>error.stage==='写入后校验'&&/回读长度不符/.test(error.message));

  // 安装器：官方布局写四件套且不整片擦除。
  const officialRun=setupInstaller(officialTable);
  await officialRun.context.window.PicoInstaller.open({manifest:officialRun.manifest,manifestURL:'https://local.invalid/manifest.json',files:undefined});
  assert.equal(officialRun.requests.ports,0);
  await field(officialRun,'[data-connect]').listeners.click();
  assert.equal(officialRun.requests.ports,1);assert.equal(officialRun.requests.prepares,1);
  assert.equal(officialRun.writes.length,1);assert.equal(officialRun.writes[0].eraseAll,false);
  assert.equal(officialRun.writes[0].fileArray.length,4);
  assert.deepEqual(Array.from(officialRun.writes[0].fileArray,part=>part.address),[0,0x8000,0x10000,0xd10000]);
  assert.match(field(officialRun,'[data-status]').textContent,/安装及设备端校验完成/);
  assert.equal(field(officialRun,'[data-connect]').hidden,true);
  // 外来布局：未勾选整机重装不写入；勾选后整片擦除并写四件套。
  const foreignRun=setupInstaller(table(foreignLayout()));
  await foreignRun.context.window.PicoInstaller.open({manifest:foreignRun.manifest,manifestURL:'https://local.invalid/manifest.json',files:undefined});
  await field(foreignRun,'[data-connect]').listeners.click();
  assert.equal(foreignRun.writes.length,0);assert.equal(foreignRun.requests.prepares,1);
  assert.match(field(foreignRun,'[data-status]').textContent,/检测到非官方分区表，需要整机重装/);
  assert.match(field(foreignRun,'[data-status]').textContent,/请勾选「清除机内设置」/);
  field(foreignRun,'[data-full-reinstall]').checked=true;
  await field(foreignRun,'[data-connect]').listeners.click();
  assert.equal(foreignRun.writes.length,1);assert.equal(foreignRun.writes[0].eraseAll,true);
  assert.equal(foreignRun.writes[0].fileArray.length,4);
  assert.deepEqual(Array.from(foreignRun.writes[0].fileArray,part=>part.address),[0,0x8000,0x10000,0xd10000]);
  assert.match(field(foreignRun,'[data-status]').textContent,/安装及设备端校验完成/);
  // 写入失败与写后回读失败：不静默成功，提示整机重装且不要断电。
  const writeFail=setupInstaller(officialTable,{write:true});
  await writeFail.context.window.PicoInstaller.open({manifest:writeFail.manifest,manifestURL:'https://local.invalid/manifest.json',files:undefined});
  await field(writeFail,'[data-connect]').listeners.click();
  const writeStatus=field(writeFail,'[data-status]').textContent;
  assert.match(writeStatus,/不能确认安装成功/);assert.match(writeStatus,/写入阶段：模拟写入失败/);
  assert.match(writeStatus,/重新连接后执行整机重装/);assert.match(writeStatus,/不要断电/);
  const verifyFail=setupInstaller(officialTable,{verify:true});
  await verifyFail.context.window.PicoInstaller.open({manifest:verifyFail.manifest,manifestURL:'https://local.invalid/manifest.json',files:undefined});
  await field(verifyFail,'[data-connect]').listeners.click();
  const verifyStatus=field(verifyFail,'[data-status]').textContent;
  assert.match(verifyStatus,/不能确认安装成功/);assert.match(verifyStatus,/SHA-256 校验失败/);
  assert.match(verifyStatus,/重新连接后执行整机重装/);assert.match(verifyStatus,/不要断电/);
  assert.doesNotMatch(verifyStatus,/安装及设备端校验完成/);
  console.log('PASS: official layout and SHA, foreign/blank/mismatch rejection, forced full reinstall, four-part writes, post-write SHA checks and failure hints');
})().catch(error=>{console.error(error);process.exitCode=1;});
