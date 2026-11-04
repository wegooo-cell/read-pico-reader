/* 中文：S31 专用网页安装，先验证再写入；与 Pico 安装器隔离。 / English: Verify-before-write S31 installation isolated from the Pico installer. */
import {require,equal,partitionTable,selectedSlot} from './metalio-flash-core.js';
import {connectS31,prepareS31Flash,connectionMessage} from './metalio-connection.js?v=s31-direct-20261024';
import {readS31Flash,verifyS31Flash} from './metalio-flash-reader.js?v=s31-direct-20261024';
let active=false;
async function open(snapshot){
  if(active)return;active=true;
  const dialog=document.createElement('dialog');dialog.className='metalio-dialog';
  dialog.setAttribute('aria-labelledby','metalio-dialog-title');
  dialog.innerHTML=`<span class="card-number">METALIO / ESP32-S31</span><h2 id="metalio-dialog-title">安装 kiikoread</h2><p data-version></p><p>支持直接从当前 S31 固件刷入，无需先恢复官方固件。兼容分区默认保留机内设置。</p><p>在墨水屏上进入「设置 → BOOT 刷机」，再选择下载串口。连接校验通过后自动刷入，请保持 USB 连接。</p><label><input type="checkbox" data-clear>清除机内设置（默认保留，TF 卡文件保留）</label><p data-status role="status" aria-live="polite">尚未连接设备</p><progress max="100" value="0" aria-label="安装进度"></progress><details><summary>连接与校验记录</summary><pre data-log></pre></details><div class="dialog-actions"><button type="button" class="button primary" data-connect>连接并刷入</button><button type="button" class="button secondary" data-close>关闭</button></div>`;
  document.body.appendChild(dialog);dialog.showModal();
  const q=s=>dialog.querySelector(s), status=q('[data-status]'), log=q('[data-log]'), progress=q('progress');
  const connect=q('[data-connect]'),close=q('[data-close]');
  const clear=q('[data-clear]');
  q('[data-version]').textContent=`Metalio EINK 4 Plus V1.5 / ${snapshot.manifest.version}。此包不适用于小纸 Pico。`;
  let loader,transport,bundle,plan,protectedBefore,checking=false,writing=false,complete=false;
  const say=text=>{status.textContent=text;};
  const append=text=>{log.textContent=(log.textContent+String(text)+'\n').slice(-18000);log.scrollTop=log.scrollHeight;};
  async function read(address,size,label){
    const started=performance.now();let last=0;
    say(`${label}（0/${size} 字节）…`);append(`${label}: 0x${address.toString(16)}, ${size} 字节`);
    const bytes=await readS31Flash(loader,address,size,(done,total)=>{
      const now=performance.now();if(now-last<200&&done!==total)return;last=now;
      say(`${label}：${Math.floor(done/total*100)}%（${done}/${total} 字节）`);
      if(checking)progress.value=done/total*100;
    });
    append(`${label}：读取及 MD5 校验通过，${Math.round(performance.now()-started)} ms`);return bytes;
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
  function lock(on){connect.disabled=close.disabled=clear.disabled=on;}
  async function cleanup(){if(checking||writing)return;await disconnect();bundle?.dispose();dialog.remove();active=false;}
  close.addEventListener('click',cleanup);
  dialog.addEventListener('cancel',event=>{event.preventDefault();cleanup();});
  connect.addEventListener('click',async()=>{
    if(checking||writing||complete)return;
    checking=true;lock(true);plan=null;
    try{
      // 串口选择由本次点击直接触发，下载等待不会丢失用户手势。 / Request the port directly from this click, before awaiting downloads.
      const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a,usbProductId:0x1001}]});
      await disconnect();say('正在下载并校验所选固件…');
      bundle?.dispose();bundle=await window.KiikoreadFlasher.prepare(snapshot.manifest,snapshot.manifestURL,snapshot.files,'metalio');
      const {ESPLoader}=await import('./vendor/metalio/esptool-js-0.7.0.js');
      const {S31Transport}=await import('./metalio-transport.js?v=s31-direct-20261024');
      transport=new S31Transport(port);loader=new ESPLoader({transport,baudrate:115200,romBaudrate:115200,serialOptions:{bufferSize:65536},terminal:{clean(){},write:append,writeLine:append}});
      say('正在复位 USB JTAG 下载口并识别 S31（最长 30 秒）…');
      append('连接方式：Espressif USB JTAG 下载复位（usb_reset）；尚未写入 Flash。');
      await connectS31(loader);
      const security=await loader.getSecurityInfo();
      require(security.chipId===32&&!security.parsedFlags.SECURE_BOOT_EN&&!security.parsedFlags.SECURE_DOWNLOAD_ENABLE&&((security.flashCryptCnt.toString(2).match(/1/g)||[]).length%2===0),'此设备已启用安全启动或 Flash 加密，不能使用此安装包。');
      const capacity=await loader.detectFlashSize();require(loader.flashSizeBytes(capacity)>=0x1000000,'Flash 容量不足 16 MB。');
      await prepareS31Flash(loader,loader.flashSizeBytes(capacity));
      append('Flash 初始化：已按本地 esptool 设置 16 MB / 64 KiB 块 / 4 KiB 扇区 / 256 字节页；尚未写入。');
      const mac=await loader.chip.readMac(loader);append(`S31 MAC: ${mac} / ${capacity}`);
      append('读取方式：512 字节逐包确认，单包在途；每次完整读取核验 MD5。');
      const table=bundle.parts.find(p=>p.name==='partitions.bin');partitionTable(table.data);
      say('正在核对分区与启动位置…');
      protectedBefore=await read(0x8000,0x8000,'读取分区与启动记录');
      const expected=new Uint8Array(4096).fill(255);expected.set(table.data);
      const compatible=equal(protectedBefore.slice(0,4096),expected);
      if(!compatible){
        require(clear.checked,'首次安装的分区不同，需要勾选清除机内设置后再连接；TF 文件保留。');
        plan={full:true,mac};say(`已识别 S31 ${mac}。将重新安装基础包并清除机内设置。`);
      }else{
        const slot=selectedSlot(protectedBefore.slice(0x5000,0x7000));
        const app=bundle.parts.find(p=>p.name==='firmware.bin');require(Math.ceil(app.data.length/4096)*4096<=slot.size,'固件超过启动槽容量。');
        // 用户要求不检查当前固件品牌，按已确认的启动槽直接更新。 / Update the confirmed boot slot without inspecting the current firmware brand.
        plan={full:false,slot,mac};say(`已连接 S31，正在刷入 ${snapshot.manifest.version}。`);
      }
      append(`不创建刷前备份；机内设置：${clear.checked?'清除':'保留'}，TF 文件保留。`);
      checking=false;await flashPlan();
    }catch(error){append(error.stack||error.message);say(connectionMessage(error));await disconnect();}
    finally{checking=false;lock(false);}
  });
  async function flashPlan(){
    if(!plan||!loader||writing||complete)return;
    writing=true;lock(true);let imageVerified=false;
    try{
      require(!plan.full||clear.checked,'首次安装需要明确勾选清除机内设置。');
      // 再次核对检查后设备未变；写入中不更改已绑定的版本。 / Recheck the device and retain the clicked release throughout writing.
      require(await loader.chip.readMac(loader)===plan.mac,'设备身份发生变化。');
      require(equal(await read(0x8000,0x8000,'复核分区与启动记录'),protectedBefore),'检查后启动记录发生变化，请重新连接。');
      const app=bundle.parts.find(p=>p.name==='firmware.bin');
      const parts=plan.full?bundle.parts.map(p=>({data:p.data,address:p.offset})):[{data:app.data,address:plan.slot.address}];
      parts.sort((a,b)=>a.address-b.address);
      say('正在写入，请保持页面开启和 USB 连接…');append(`Write ${snapshot.manifest.version}: ${plan.full?'full install':`0x${plan.slot.address.toString(16)}`}`);
      await loader.writeFlash({fileArray:parts,flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true,reportProgress:(index,written,total)=>{progress.value=Math.floor((index+written/total)/parts.length*75);}});
      for(let i=0;i<parts.length;i++){
        say(`正在设备端校验 ${i+1}/${parts.length}…`);
        const md5=await verifyS31Flash(loader,parts[i].address,parts[i].data);
        append(`设备端 MD5 校验通过：0x${parts[i].address.toString(16)}，${parts[i].data.length} 字节，${md5}`);progress.value=75+Math.floor((i+1)/parts.length*20);
      }
      imageVerified=true;
      if(clear.checked){
        say('固件已校验，正在清除机内设置…');
        await loader.writeFlash({fileArray:[{address:0x9000,data:new Uint8Array(0x4000).fill(255)}],flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true});
      }
      const after=await read(0x8000,0x8000,'核对数据保护区域');
      if(!plan.full){
        require(equal(after.slice(0,4096),protectedBefore.slice(0,4096))&&equal(after.slice(0x5000),protectedBefore.slice(0x5000)),'保护区校验失败，请保留记录。');
        require(clear.checked?after.slice(0x1000,0x5000).every(v=>v===255):equal(after.slice(0x1000,0x5000),protectedBefore.slice(0x1000,0x5000)),'机内设置校验失败。');
      }else{
        const table=new Uint8Array(4096).fill(255);table.set(bundle.parts.find(p=>p.name==='partitions.bin').data);
        require(equal(after.slice(0,4096),table)&&equal(after.slice(0x5000,0x7000),bundle.parts.find(p=>p.name==='ota_data_initial.bin').data),'基础包分区或启动记录校验失败。');
        require(equal(after.slice(0x7000),protectedBefore.slice(0x7000)),'基础包安装影响了保护范围。');
        require(after.slice(0x1000,0x5000).every(v=>v===255),'清除设置校验失败。');
      }
      complete=true;progress.value=100;say('固件与数据保护校验通过，正在重启…');
      // 按 Espressif S31 继承的 watchdog_reset 顺序复位，不写 eFuse。 / Follow Espressif S31's inherited watchdog_reset sequence; never write eFuses.
      try{await loader.writeReg(0x20801018,0x50d83aa1);await loader.writeReg(0x20801004,2000);await loader.writeReg(0x20801000,0xd0000102);await loader.writeReg(0x20801018,0);}catch(error){append(`Reset: ${error.message}`);}
      say(`安装及设备端校验完成。请查看设备是否已进入 ${snapshot.manifest.version}；若停在 BOOT，请短按复位。`);connect.hidden=true;
    }catch(error){append(error.stack||error.message);say(`${imageVerified?'固件已校验，但收尾未完成':'操作未完成，不能确认写入成功'}：${error.message}。请保留操作记录。`);connect.hidden=false;plan=null;}
    finally{writing=false;lock(false);await disconnect();}
  }
}
window.MetalioInstaller=Object.freeze({open});window.dispatchEvent(new Event('metalio-installer-ready'));
