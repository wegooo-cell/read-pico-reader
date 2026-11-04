/* SPDX-License-Identifier: Apache-2.0
 * 中文：选择已发布的设备与版本，每次安装绑定独立快照。 / English: Select a published device/release and bind each installation to an independent snapshot.
 */
(() => {
  const selector=document.getElementById('firmware-choice');
  const radios=[...document.querySelectorAll('[name="flash-device"]')];
  const version=document.getElementById('firmware-version'), updated=document.getElementById('firmware-updated');
  const download=document.getElementById('bin-download'), help=document.getElementById('flash-help');
  const title=document.getElementById('flash-help-title'), hint=document.getElementById('firmware-choice-hint');
  const metalioButton=document.getElementById('metalio-installer');
  let installer=document.getElementById('flash-installer');
  const models={
    pico:{name:'小纸 Pico',manifest:'./manifest.json',catalog:'./releases.json',feed:'./update.json',family:'ESP32-S3',prefix:'Pico-update',filename:'Pico-update.bin',tag:'v',package:'kiikoread',description:'RDP-G01-W · ESP32-S3'},
    metalio:{name:'Metalio EINK 4 Plus',manifest:'./metalio/manifest.json',catalog:'./metalio/releases.json',feed:'./metalio-s31-update.json',family:'ESP32-S31',prefix:'Metalio-S31-update',filename:'Metalio-S31-update.bin',tag:'metalio-s31-v',package:'kiikoread-metalio47',description:'V1.5 · 34Pin · E0470A14-AF · ESP32-S31'}
  };
  const query=new URLSearchParams(location.search);
  let device=query.get('device')==='metalio'?'metalio':'pico';
  let request=0, catalogReady=false, manifestReady=false, serialReady=false, selected=null;
  let releases=new Map();
  function busy(){return !!document.querySelector('ewt-install-dialog,dialog.metalio-dialog[open]');}
  function syncControls(){selector.disabled=!catalogReady||busy();radios.forEach(r=>r.disabled=busy());}
  function showInstaller(){
    const ready=window.isSecureContext&&'serial' in navigator&&manifestReady;
    installer.hidden=!(ready&&serialReady&&device==='pico');
    metalioButton.hidden=!(ready&&device==='metalio'&&window.MetalioInstaller);
    help.hidden=device==='pico'?!installer.hidden:!metalioButton.hidden;
  }
  async function readJSON(url){const response=await fetch(url,{cache:'no-store'});if(!response.ok)throw Error('清单暂不可用');return response.json();}
  function hideDownloads(){
    for(const id of ['bin-download','tf-package-download','usb-package-download']){const link=document.getElementById(id);link.hidden=true;link.removeAttribute('href');}
    installer.hidden=metalioButton.hidden=true;
  }
  function packageLink(kind,manifest,choice,model){
    const link=document.getElementById(`${kind.toLowerCase()}-package-download`);
    const expected=`https://github.com/wegooo-cell/read-pico-reader/releases/download/${model.tag}${manifest.version}/${model.package}-${manifest.version}-${kind}.zip`;
    link.hidden=(manifest[`${kind.toLowerCase()}_package_url`]||choice[`${kind.toLowerCase()}_package_url`])!==expected;
    if(!link.hidden){link.href=expected;link.textContent=`下载 ${manifest.version.split('-').pop()} ${kind==='TF'?'TF 卡升级包':'全量包（USB）'} ZIP ↓`;}
  }
  async function selectRelease(){
    const current=++request, model=models[device], clickedDevice=device, choice=releases.get(selector.value);
    manifestReady=false;selected=null;hideDownloads();version.textContent=updated.textContent='读取中…';updated.removeAttribute('datetime');
    document.dispatchEvent(new CustomEvent('kiikoread:release-loading'));showInstaller();
    try{
      if(!choice)throw Error('Unknown release');
      const manifestURL=new URL(choice.manifest,document.baseURI).href;
      const manifest=await readJSON(manifestURL);
      if(current!==request)return;
      window.KiikoreadFlasher.validate(manifest,clickedDevice);
      if(choice.version&&manifest.version!==choice.version)throw Error('版本不匹配');
      const latest=choice.manifest===model.manifest;
      version.textContent=manifest.version;
      if(latest)selector.options[0].textContent=`最新版本 · ${manifest.version}`;
      const date=new Date(manifest.updated_at);
      if(Number.isFinite(date.getTime())){updated.dateTime=date.toISOString();updated.textContent=new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(date).replace(/\//g,'-');}else updated.textContent='暂未提供';
      const snapshot={device:clickedDevice,manifest:JSON.parse(JSON.stringify(manifest)),manifestURL,files:choice.files};
      selected=snapshot;
      if(clickedDevice==='pico'){
        const next=installer.cloneNode(true);next.manifest=manifestURL;next.setAttribute('manifest',manifestURL);
        next.prepareManifest=()=>window.KiikoreadFlasher.prepare(snapshot.manifest,manifestURL,snapshot.files,'pico');
        next.querySelector('button').textContent=`连接小纸 Pico，刷入 ${manifest.version.split('-').pop()}`;
        installer.replaceWith(next);installer=next;
      }else metalioButton.textContent=`连接 Metalio，刷入 ${manifest.version.split('-').pop()}`;
      download.href=new URL(latest?`./${model.prefix}-${manifest.version}.bin`:choice.bin,document.baseURI).href;
      download.download=model.filename;download.textContent=`下载 ${manifest.version.split('-').pop()} TF 卡升级 Bin ↓`;download.hidden=false;
      packageLink('TF',manifest,choice,model);packageLink('USB',manifest,choice,model);
      manifestReady=true;showInstaller();
      const url=new URL(location.href);if(clickedDevice==='pico')url.searchParams.delete('device');else url.searchParams.set('device',clickedDevice);
      if(latest)url.searchParams.delete('version');else url.searchParams.set('version',manifest.version);history.replaceState(null,'',url);
      document.dispatchEvent(new CustomEvent('kiikoread:release',{detail:{manifest,latest,device:clickedDevice,feed:model.feed}}));
    }catch(error){if(current!==request)return;version.textContent='读取失败，请重选版本或刷新';updated.textContent='暂不可用';title.textContent='所选版本暂不可用，请重试。';document.dispatchEvent(new CustomEvent('kiikoread:release-unavailable'));showInstaller();}
  }
  async function selectDevice(wantedVersion){
    const current=++request, model=models[device];catalogReady=manifestReady=false;selected=null;hideDownloads();
    radios.forEach(r=>r.checked=r.value===device);
    document.getElementById('device-description').textContent=`已选择 ${model.name}。${model.description}。以下安装与下载均对应这款设备。`;
    document.getElementById('rc88-bin-download').hidden=device!=='pico';
    document.getElementById('usb-guide').href=device==='metalio'?'./metalio/#usb':'./manual/#first-use';
    document.getElementById('tf-guide').href=device==='metalio'?'./metalio/#tf':'./manual/#update';
    document.getElementById('tf-instructions').replaceChildren(document.createTextNode('升级文件名保持 '),Object.assign(document.createElement('code'),{textContent:model.filename}),document.createTextNode('，放入 TF 卡根目录。这是应用升级包，不能代替完整 USB 包。'));
    document.getElementById('usb-instructions').textContent=device==='metalio'?'支持直接从当前 S31 固件刷入，无需先恢复官方系统。进入 BOOT 下载模式再连接，关闭其他占用串口的页面。兼容分区默认保留设置；校验后直接刷入，不做刷前备份，写后由设备核验 MD5。':'安装时保留数据：不要勾选「擦除设备」。无法连接时，先在设置中进入 BOOT 模式。';
    document.getElementById('ota-instructions').textContent=device==='metalio'?'先通过 USB 或 TF 卡安装兼容的 Metalio 基础包，再连接 Wi-Fi，在「设置 → 系统升级」检查更新。GitHub Pages 优先，官网备用；保留设置与 TF 文件。':'连接 Wi-Fi，在「设置 → 系统升级」检查更新。GitHub Pages 优先，官网作为备用来源；旧版本先按兼容说明升级基础包。';
    hint.textContent='设备、版本与刷机包一一对应。安装前会校验固件身份与完整性。';
    releases=new Map([[model.manifest,{manifest:model.manifest}]]);
    selector.replaceChildren(Object.assign(document.createElement('option'),{value:model.manifest,textContent:'最新版本 · 读取中'}));
    version.textContent=updated.textContent='读取中…';syncControls();showInstaller();
    try{
      const catalog=await readJSON(model.catalog);if(current!==request)return;
      if(catalog.schema!==1||!Array.isArray(catalog.releases))throw Error('Invalid catalog');
      for(const release of catalog.releases){
        const path=device==='pico'?`releases/${release.version}/manifest.json`:`metalio/releases/${release.version}/manifest.json`;
        if(!/^\d+\.\d+\.\d+(?:-rc\d+)?$/.test(release.version)||release.manifest!==path||release.bin!==`${model.prefix}-${release.version}.bin`||releases.has(release.manifest))throw Error('Invalid release');
        releases.set(release.manifest,release);
        selector.appendChild(Object.assign(document.createElement('option'),{value:release.manifest,textContent:`${release.version} · ${release.version===catalog.previous_version?(device==='metalio'?'上一版本':'上一正式版'):'历史版本'}`}));
      }
      const wanted=catalog.releases.find(r=>r.version===wantedVersion||r.version.split('-').pop()===wantedVersion);if(wanted)selector.value=wanted.manifest;
    }catch(error){if(current!==request)return;hint.textContent='历史列表暂不可用，可选择当前机型最新版本。';}
    if(current!==request)return;catalogReady=true;syncControls();await selectRelease();
  }
  if(location.protocol==='file:'){version.textContent=updated.textContent='请打开官网刷机页';return;}
  if(!window.isSecureContext)title.textContent='请使用 HTTPS 刷机页。';else if(!('serial' in navigator))title.textContent='请使用电脑 Chrome 或 Edge 连接串口。';
  customElements.whenDefined('esp-web-install-button').then(()=>{serialReady=true;showInstaller();});
  window.addEventListener('metalio-installer-ready',showInstaller);
  new MutationObserver(syncControls).observe(document.body,{childList:true,subtree:true,attributes:true,attributeFilter:['open']});
  radios.forEach(r=>r.addEventListener('change',()=>{if(r.checked&&!busy()){device=r.value;selectDevice();}}));
  selector.addEventListener('change',selectRelease);
  metalioButton.addEventListener('click',()=>{if(selected?.device==='metalio')window.MetalioInstaller.open(selected);});
  selectDevice(query.get('version'));
})();
