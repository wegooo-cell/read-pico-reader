/* SPDX-License-Identifier: Apache-2.0
 * 中文：小纸 Pico 网页安装器；先读 0x8000 分区表，兼容布局更新四件套，外来布局强制整机重装，写后复核 SHA。
 * English: Pico web installer; read the 0x8000 partition table first, update four parts on compatible layouts, force a full reinstall on foreign layouts, and verify SHA after writing.
 */
import {require,equal,classifyDeviceTable,planInstall,validatePicoManifest,validatePicoBundle,validateOfficialTable,installationParts,verifyWrittenParts,verifyPartitionTable,stageError,PICO_FAILURE_HINT} from './pico-flash-core.js?v=pico-partitions-20261123';
let active=false;
function connectionMessage(error){
  if(error.name==='NotFoundError')return '已取消选择，尚未写入。';
  if(error.name==='NetworkError'||error.name==='InvalidStateError')return '串口无法打开或已断开，尚未写入。请关闭其他刷机页及串口监视器，重新进入 BOOT 后再连接。';
  if(/Failed to connect with the device/.test(error.message))return 'Pico 下载握手失败，尚未写入。请在设备「设置 → 升级和恢复 → BOOT 刷机」进入下载模式后再连接。';
  return error.message;
}
async function open(snapshot){
  if(active)return;active=true;
  const dialog=document.createElement('dialog');dialog.className='metalio-dialog pico-dialog';
  dialog.setAttribute('aria-labelledby','pico-dialog-title');
  dialog.innerHTML=`<span class="card-number">PICO / ESP32-S3</span><h2 id="pico-dialog-title">安装 kiikoread</h2><p data-version></p><p>支持直接从当前 Pico 固件刷入，无需先恢复官方固件。官方分区默认保留机内设置；检测到非官方分区表时，需要整机重装。</p><p>在墨水屏上进入「设置 → 升级和恢复 → BOOT 刷机」，再选择下载串口。连接校验通过后自动刷入，请保持 USB 连接。</p><label><input type="checkbox" data-full-reinstall>清除机内设置（默认保留，TF 卡文件保留）</label><p data-status role="status" aria-live="polite">尚未连接设备</p><progress max="100" value="0" aria-label="安装进度"></progress><details><summary>连接与校验记录</summary><pre data-log></pre></details><div class="dialog-actions"><button type="button" class="button primary" data-connect>连接并刷入</button><button type="button" class="button secondary" data-close>关闭</button></div>`;
  document.body.appendChild(dialog);dialog.showModal();
  const q=selector=>dialog.querySelector(selector), status=q('[data-status]'), log=q('[data-log]'), progress=q('progress');
  const connect=q('[data-connect]'),close=q('[data-close]'),full=q('[data-full-reinstall]');
  q('[data-version]').textContent=snapshot.manifest.board==='pico'||snapshot.manifest.project==='Read_Pico'?`小纸 Pico / ESP32-S3 / ${snapshot.manifest.version}。此包不适用于 Metalio。`:`小纸 Pico / ESP32-S3 / ${snapshot.manifest.version}。`;
  let loader,transport,bundle,plan,protectedBefore,checking=false,writing=false,complete=false,writeStarted=false;
  const say=text=>{status.textContent=text;};
  const append=text=>{log.textContent=(log.textContent+String(text)+'\n').slice(-18000);log.scrollTop=log.scrollHeight;};
  async function read(address,size,label){
    const started=performance.now();let last=0;
    say(`${label}（0/${size} 字节）…`);append(`${label}: 0x${address.toString(16)}, ${size} 字节`);
    const bytes=await loader.readFlash(address,size,(_chunk,done,total)=>{
      const now=performance.now();if(now-last<200&&done!==total)return;last=now;
      say(`${label}：${Math.floor(done/total*100)}%（${done}/${total} 字节）`);
      if(checking)progress.value=done/total*100;
    });
    append(`${label}：读取完成，${Math.round(performance.now()-started)} ms`);return bytes;
  }
  async function disconnect(){
    const previous=transport;transport=loader=null;if(!previous)return;
    let timer;
    try{
      // 驱动写入阻塞时仍让错误提示和关闭按钮可用；不复用未释放的串口。
      // Keep error/close controls responsive if a driver write is stuck; never reuse the locked transport.
      await Promise.race([previous.disconnect(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('串口未能释放，请关闭此刷机标签页后重试。')),2000);})]);
    }catch(error){append(error.message);}finally{clearTimeout(timer);}
  }
  function lock(on){connect.disabled=close.disabled=full.disabled=on;}
  async function cleanup(){if(checking||writing)return;await disconnect();bundle?.dispose();dialog.remove();active=false;}
  close.addEventListener('click',cleanup);
  dialog.addEventListener('cancel',event=>{event.preventDefault();cleanup();});
  connect.addEventListener('click',async()=>{
    if(checking||writing||complete)return;
    checking=true;lock(true);plan=null;writeStarted=false;
    try{
      // 串口选择由本次点击直接触发，下载等待不会丢失用户手势。 / Request the port directly from this click, before awaiting downloads.
      const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});
      await disconnect();say('正在下载并校验所选固件…');
      validatePicoManifest(snapshot.manifest,snapshot.files);
      bundle?.dispose();bundle=await window.KiikoreadFlasher.prepare(snapshot.manifest,snapshot.manifestURL,snapshot.files,'pico');
      validatePicoBundle(snapshot.manifest,bundle,snapshot.files);
      const official=bundle.parts.find(part=>part.name==='partitions.bin');
      require(official,'刷机包缺少分区表。');
      const officialEntries=validateOfficialTable(official.data);
      append(`目标分区表：${officialEntries.map(entry=>`${entry.name}@0x${entry.offset.toString(16)}/${entry.size.toString(16)}`).join(', ')}`);
      const {ESPLoader,Transport}=await import('./vendor/metalio/esptool-js-0.7.0.js');
      transport=new Transport(port);
      loader=new ESPLoader({transport,baudrate:115200,romBaudrate:115200,serialOptions:{bufferSize:65536},terminal:{clean(){},write:append,writeLine:append}});
      say('正在复位并识别小纸 Pico（ESP32-S3，最长 30 秒）…');
      append('连接方式：Espressif USB Serial/JTAG 下载复位；尚未写入 Flash。');
      await loader.main('default_reset');
      require(loader.chip?.CHIP_NAME==='ESP32-S3'&&loader.chip?.IMAGE_CHIP_ID===9,'未连接到小纸 Pico（ESP32-S3），尚未写入。');
      require(!loader.secureDownloadMode,'此设备处于安全下载模式，不能使用此安装包。');
      const security=await loader.getSecurityInfo();
      const flags=security.parsedFlags||{};
      require(security.chipId===9&&!flags.SECURE_BOOT_EN&&!flags.SECURE_DOWNLOAD_ENABLE&&((Number(security.flashCryptCnt||0).toString(2).match(/1/g)||[]).length%2===0),'此设备已启用安全启动或 Flash 加密，不能使用此安装包。');
      const capacity=await loader.detectFlashSize();
      require(loader.flashSizeBytes(capacity)>=0x1000000,'Flash 容量不足 16 MB。');
      await loader.runStub();
      require(loader.IS_STUB,'设备端下载程序未启动，尚未写入。');
      append('Flash 初始化：Espressif ESP32-S3 下载程序已运行；尚未写入。');
      protectedBefore=await read(0x8000,0x1000,'读取设备分区表');
      const classification=classifyDeviceTable(protectedBefore,official.data);
      plan=planInstall(classification);
      append(`设备分区表：${classification.mode==='official'?'官方布局':classification.mode==='blank'?'空白布局':'非官方布局'}${classification.foreignNames.length?`（外来分区：${classification.foreignNames.join('、')}）`:''}`);
      append(classification.message);
      if(plan.requiresConfirmation&&!full.checked)throw stageError('分区表检查',`${plan.message} 请勾选「清除机内设置」后再连接。`);
      if(plan.mode==='update')say('设备分区表与官方布局一致，将保留机内设置与存储分区。');
      else say('检测到非官方分区表，需要整机重装；将擦除全部 Flash 后写入四件套。');
      checking=false;await flashPlan();
    }catch(error){
      append(error.stack||error.message);
      say(`${connectionMessage(error)}${writeStarted&&!complete?` ${PICO_FAILURE_HINT}`:''}`);
      await disconnect();
    }finally{checking=false;lock(false);}
  });
  async function flashPlan(){
    if(!plan||!loader||writing||complete)return;
    writing=true;lock(true);let imageVerified=false;
    try{
      require(!plan.requiresConfirmation||full.checked,'整机重装需要明确勾选「清除机内设置」。');
      // 再次核对检查后设备未变；写入中不更改已绑定的版本。 / Recheck the device and retain the clicked release throughout writing.
      require(equal(await read(0x8000,0x1000,'复核设备分区表'),protectedBefore),'检查后设备分区表发生变化，请重新连接。');
      const parts=installationParts(bundle,plan);
      append(`Write ${snapshot.manifest.version}: ${plan.mode==='full'?'整机重装（擦除全部 Flash）':'更新四件套（保留机内设置）'}`);
      say(plan.mode==='full'?'正在整机重装，请保持页面开启和 USB 连接…':'正在写入四件套，请保持页面开启和 USB 连接…');
      writeStarted=true;
      await loader.writeFlash({fileArray:parts,flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:plan.fullErase,compress:true,reportProgress:(index,written,total)=>{progress.value=Math.floor((index+written/total)/parts.length*70);}});
      say('正在核对写入后的分区表…');
      const after=await read(0x8000,0x1000,'读取写入后的分区表');
      verifyPartitionTable(after,officialPart().data);
      append('分区表复核通过：标签、偏移、大小与官方布局一致。');
      progress.value=75;
      const expected=bundle.parts.map(part=>({name:part.name,offset:part.offset,size:part.data.length,sha256:part.sha256})).sort((left,right)=>left.offset-right.offset);
      require(expected.every(part=>/^[a-f0-9]{64}$/.test(part.sha256||'')),'刷机包缺少写入后校验所需的 SHA-256。');
      say('正在回读四件套并核对 SHA-256…');
      await verifyWrittenParts(expected,(address,size)=>read(address,size,`回读 0x${address.toString(16)}`),undefined,(part,index)=>{append(`SHA-256 校验通过：${part.name} @0x${part.offset.toString(16)}`);progress.value=75+Math.floor((index+1)/expected.length*25);});
      imageVerified=true;complete=true;progress.value=100;
      say('分区表与四件套 SHA-256 校验通过，正在重启…');
      try{await loader.after('hard_reset');}catch(error){append(`Reset: ${error.message}`);}
      say(`安装及设备端校验完成。请查看设备是否已进入 ${snapshot.manifest.version}；若停在 BOOT，请短按复位。`);connect.hidden=true;
    }catch(error){
      const stage=error.stage||(writeStarted?'写入阶段':'设备检查');
      const message=error.stage?error.message:`${stage}：${error.message}`;
      append(error.stack||message);
      const hint=writeStarted&&!complete?` ${PICO_FAILURE_HINT}`:'';
      say(`${imageVerified?'固件已校验，但收尾未完成':'不能确认安装成功'}：${message}。${hint}${writeStarted&&!complete?'请保留操作记录。':'尚未写入设备。'}`);
      connect.hidden=false;plan=null;
    }finally{writing=false;lock(false);await disconnect();}
  }
  function officialPart(){return bundle.parts.find(part=>part.name==='partitions.bin');}
}
window.PicoInstaller=Object.freeze({open});window.dispatchEvent(new Event('pico-installer-ready'));
