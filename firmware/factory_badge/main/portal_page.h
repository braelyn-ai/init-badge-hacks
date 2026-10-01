#pragma once
// Local-only portal retained from the Arduino conference UI. No external assets.
static constexpr char BADGE_PORTAL_HTML[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>init() badge setup</title><style>body{font:17px system-ui;background:#101114;color:#fff;max-width:460px;margin:25px auto;padding:24px}h1{font-size:32px}label{display:block;margin:22px 0 7px}input,select,button{box-sizing:border-box;width:100%;padding:14px;border-radius:10px;border:1px solid #666;font:inherit}input,select{background:#24262b;color:#fff}button{margin-top:20px;background:#fff;color:#111;font-weight:650}button.secondary{background:#24262b;color:#fff}p{color:#ccc;line-height:1.5}.row{display:flex;gap:10px;align-items:center}.row input{width:auto}#status{min-height:3em;white-space:pre-wrap}small{color:#bbb}button:disabled{opacity:.5}#photoPreview{display:block;width:160px;height:160px;margin:14px 0;border:1px solid #666}#photoPreview[hidden]{display:none}a{color:#bdb5ff}</style></head><body><p>init() / local badge setup</p><h1>Set up your badge</h1><p>Profile details stay on this badge. No account or sign-in is needed. Blank social accounts stay available as placeholders.</p><section aria-labelledby="clockHeading"><h2 id="clockHeading">Clock</h2><p id="clockStatus" role="status" aria-live="polite">Syncing the clock from this browser…</p><button id="clockRetry" type="button" class="secondary">Sync clock again</button><small>Clock updates are kept even if you cancel profile edits. Reopen setup after a timezone or daylight-saving change.</small></section><section aria-labelledby="wifiHeading"><h2 id="wifiHeading">Event Wi-Fi</h2><p>The event network is built in. Change it only if you need to; it is saved with Save badge. Wi-Fi stays off except while the badge downloads your photo.</p><label for="wifiSsid">Network name</label><input id="wifiSsid" maxlength="32" autocomplete="off" value="{{WIFI_SSID}}"><label for="wifiPassword">Password (blank keeps the saved one; open Wi-Fi needs none)</label><input id="wifiPassword" type="password" maxlength="63" autocomplete="off"><button id="wifiForget" type="button" class="secondary">Use event Wi-Fi</button><p id="wifiStatus" role="status" aria-live="polite">{{WIFI_STATUS}}</p></section><form id="form"><label for="name">Display name</label><input id="name" maxlength="120" autocomplete="off" value="{{NAME}}"><label for="company">Company (optional)</label><input id="company" maxlength="120" autocomplete="organization" value="{{COMPANY}}"><label for="github">GitHub handle or profile URL</label><input id="github" maxlength="180" autocomplete="off" value="{{GITHUB}}"><label for="x">X / Twitter handle or profile URL</label><input id="x" maxlength="180" autocomplete="off" value="{{X}}"><label for="linkedin">LinkedIn handle or /in/ profile URL</label><input id="linkedin" maxlength="180" autocomplete="off" value="{{LINKEDIN}}"><label for="photoSource">Photo</label><select id="photoSource"><option value="auto" selected>Automatic: my GitHub, X or LinkedIn photo if I have none</option><option value="github">Use my GitHub photo</option><option value="x">Use my X photo</option><option value="linkedin">Use my LinkedIn photo</option></select><small>The badge downloads the photo over Wi-Fi after setup closes, then turns Wi-Fi off.</small><label for="photo">Or upload a photo</label><input id="photo" type="file" accept="image/jpeg,image/png,image/webp,image/heic,image/heif"><canvas id="photoPreview" width="160" height="160" hidden role="img" aria-label="Square crop of your selected photo"></canvas><p id="photoState" role="status" aria-live="polite">{{PHOTO}}</p><button id="discardPhoto" type="button" class="secondary" hidden>Cancel photo change</button><small>Choose a photo saved on this phone. You will see its square crop before saving. JPEG, PNG, WebP and supported HEIC photos are converted locally; nothing is sent until Save badge.</small><p><small>Photo chooser not opening? Stay on the badge Wi-Fi, then open <a href="http://192.168.4.1/">http://192.168.4.1</a> in Safari or Chrome.</small></p><label class="row"><input id="remove" type="checkbox">Remove saved photo</label><button id="save" type="submit">Save badge</button><small>Saves your profile, photo choice and Wi-Fi together.</small><button id="cancel" type="button" class="secondary">Cancel setup</button></form><p id="status" role="status" aria-live="polite"></p><script>
'use strict';
const nonce='{{NONCE}}';
const $=id=>document.getElementById(id);
const originalPhotoMessage=$('photoState').textContent;
const browserHelp='If this Wi-Fi sign-in window cannot open photos, stay on the badge Wi-Fi and open http://192.168.4.1 in Safari or Chrome.';
let busy=false,preparing=false,photoVersion=0,selectedFile=null,preparedPhoto=null,photoFailure='';
function state(message){$('status').textContent=message}
function controls(){
  $('save').disabled=busy||preparing||!!photoFailure;
  for(const id of ['cancel','clockRetry','name','company','github','x','linkedin','photoSource','photo','remove','discardPhoto','wifiSsid','wifiPassword','wifiForget'])$(id).disabled=busy;
}
function lock(value){busy=value;controls()}
async function request(path,body,type='application/json'){
  const controller=typeof AbortController==='function'?new AbortController():null;
  let timer;
  const timeoutMessage='The badge did not respond. Stay connected to its Wi-Fi and reopen setup if needed.';
  const timeout=new Promise((resolve,reject)=>{timer=setTimeout(()=>{
    reject(Error(timeoutMessage));if(controller)controller.abort();
  },15000)});
  try{
    return await Promise.race([timeout,(async()=>{
      const options={method:'POST',headers:{'Content-Type':type,'X-Conference-Nonce':nonce},body:type==='application/json'?JSON.stringify(body):body};
      if(controller)options.signal=controller.signal;
      const r=await fetch(path,options);
      let result;
      try{result=await r.json()}catch{throw Error('The badge returned an unreadable response. Reopen its setup page.')}
      if(!result||typeof result!=='object')throw Error('The badge returned an unreadable response. Reopen its setup page.');
      if(!r.ok)throw Error(result.message||'The badge could not complete this request.');
      return result;
    })()]);
  }catch(error){if(error.name==='AbortError')throw Error(timeoutMessage);if(error.name==='TypeError')throw Error('Could not reach the badge. Stay connected to its Wi-Fi and reopen setup if needed.');throw error}
  finally{clearTimeout(timer)}
}
function browserClockPayload(now=new Date()){
  const payload={epoch:Math.floor(now.getTime()/1000),offset_minutes:-now.getTimezoneOffset()};
  try{const zone=Intl.DateTimeFormat().resolvedOptions().timeZone;if(typeof zone==='string'&&/^[A-Za-z0-9_+\/-]{1,64}$/.test(zone))payload.timezone=zone}catch{}
  return payload;
}
async function syncClock(){
  if(busy)return;lock(true);$('clockStatus').textContent='Syncing the clock from this browser…';
  try{
    const result=await request('/clock',browserClockPayload());
    if(result.ok!==true||result.valid!==true)throw Error(result.message||'Clock verification failed.');
    $('clockStatus').textContent=result.message||'Clock synchronized.';
  }catch(error){$('clockStatus').textContent='Clock sync could not be confirmed. '+(error.message||'Check the connection and retry.')}
  finally{lock(false)}
}
function readPhoto(file){
  return new Promise((resolve,reject)=>{
    if(typeof FileReader!=='function'){reject(Error('Photo reading is unavailable. '+browserHelp));return}
    const reader=new FileReader();let done=false;
    const timer=setTimeout(()=>{finish(Error('Reading this photo took too long. Choose a smaller local photo.'));try{reader.abort()}catch{}},10000);
    function finish(error,value){if(done)return;done=true;clearTimeout(timer);reader.onload=reader.onerror=reader.onabort=null;error?reject(error):resolve(value)}
    reader.onload=()=>typeof reader.result==='string'?finish(null,reader.result):finish(Error('The photo could not be read.'));
    reader.onerror=()=>finish(Error('The photo could not be read. Choose a photo saved on this phone.'));
    reader.onabort=()=>finish(Error('Photo reading was cancelled.'));
    try{reader.readAsDataURL(file)}catch(error){finish(error)}
  });
}
function loadPhoto(source,releaseSource=()=>{}){
  return new Promise((resolve,reject)=>{
    const img=new Image();let done=false;
    function release(){img.onload=img.onerror=null;img.src='';releaseSource()}
    const timer=setTimeout(()=>fail(Error('Opening this photo took too long.')),10000);
    function fail(error){if(done)return;done=true;clearTimeout(timer);release();reject(error)}
    img.onload=()=>{if(done)return;done=true;clearTimeout(timer);img.onload=img.onerror=null;resolve({image:img,release})};
    img.onerror=()=>fail(Error('This browser cannot open that photo.'));
    try{img.src=source}catch(error){fail(error)}
  });
}
async function decodePhoto(file){
  // Keep the source alive through drawing. Some embedded browser contexts do
  // not load blob URLs reliably; FileReader is the bounded native fallback.
  if(typeof URL!=='undefined'&&typeof URL.createObjectURL==='function'){
    let url;
    try{url=URL.createObjectURL(file);return await loadPhoto(url,()=>URL.revokeObjectURL(url))}catch{}
  }
  try{return await loadPhoto(await readPhoto(file))}
  catch{throw Error('This photo could not be opened. Try a JPEG or a screenshot saved on this phone. '+browserHelp)}
}
function dataUrlJpeg(canvas,quality){
  const data=canvas.toDataURL('image/jpeg',quality);
  if(!data.startsWith('data:image/jpeg;base64,'))throw Error('This browser cannot prepare JPEG photos. '+browserHelp);
  const decoded=atob(data.slice(data.indexOf(',')+1));
  const bytes=new Uint8Array(decoded.length);
  for(let i=0;i<decoded.length;i++)bytes[i]=decoded.charCodeAt(i);
  return new Blob([bytes],{type:'image/jpeg'});
}
function jpegBlob(canvas,quality){
  return new Promise((resolve,reject)=>{
    let done=false;
    const timer=setTimeout(fallback,4000);
    function finish(error,blob){if(done)return;done=true;clearTimeout(timer);error?reject(error):resolve(blob)}
    function fallback(){if(done)return;try{const blob=dataUrlJpeg(canvas,quality);if(!blob.size)throw Error('The prepared photo is empty.');finish(null,blob)}catch(error){finish(error)}}
    if(typeof canvas.toBlob!=='function'){fallback();return}
    try{canvas.toBlob(blob=>{if(done)return;if(blob&&blob.size&&blob.type==='image/jpeg')finish(null,blob);else fallback()},'image/jpeg',quality)}catch{fallback()}
  });
}
async function photo(file,isCurrent=()=>true){
  if(!file.size)throw Error('This photo is empty. Choose a photo saved on this phone.');
  if(file.size>10*1024*1024)throw Error('Choose a photo smaller than 10 MB, or a screenshot.');
  const decoded=await decodePhoto(file),canvas=document.createElement('canvas');
  try{
    if(!isCurrent())return null;
    const img=decoded.image,w=img.naturalWidth||img.width,h=img.naturalHeight||img.height;
    if(!w||!h)throw Error('This photo is empty.');
    if(w*h>32*1024*1024)throw Error('This photo is too large to prepare here. Choose a smaller version or a screenshot.');
    const side=Math.min(w,h),left=(w-side)/2,top=(h-side)/2;
    for(const edge of [...new Set([Math.min(512,side),Math.min(320,side),Math.min(160,side)])]){
      canvas.width=canvas.height=edge;
      const ctx=canvas.getContext('2d');
      if(!ctx)throw Error('Photo preparation is unavailable. '+browserHelp);
      ctx.fillStyle='#000';ctx.fillRect(0,0,edge,edge);
      ctx.drawImage(img,left,top,side,side,0,0,edge,edge);
      for(const quality of [.88,.70,.50]){
        const blob=await jpegBlob(canvas,quality);
        if(!isCurrent())return null;
        if(blob.size<=128*1024){
          const preview=$('photoPreview'),previewContext=preview.getContext('2d');
          if(!previewContext)throw Error('The photo preview is unavailable. '+browserHelp);
          previewContext.drawImage(canvas,0,0,preview.width,preview.height);
          return blob;
        }
      }
    }
    throw Error('This photo is too detailed. Try a smaller photo or a screenshot.');
  }finally{decoded.release();canvas.width=canvas.height=0}
}
const photoSources={github:'GitHub',x:'X',linkedin:'LinkedIn'};
function choosePhotoSource(){
  const source=$('photoSource').value;
  ++photoVersion;selectedFile=null;preparedPhoto=null;photoFailure='';preparing=false;
  $('photo').value='';$('remove').checked=false;$('photoPreview').hidden=true;$('discardPhoto').hidden=true;
  $('photoState').textContent=photoSources[source]?'After Save badge, the badge will get your '+photoSources[source]+' photo.':originalPhotoMessage;controls();
}
function discardPhoto(){
  ++photoVersion;selectedFile=null;preparedPhoto=null;photoFailure='';preparing=false;$('photoSource').value='auto';
  $('photo').value='';$('remove').checked=false;$('photoPreview').hidden=true;$('discardPhoto').hidden=true;
  $('photoState').textContent=originalPhotoMessage;controls();
}
async function selectPhoto(){
  if(busy)return;
  const file=$('photo').files[0];
  if(!file){discardPhoto();return}
  const version=++photoVersion;
  selectedFile=file;preparedPhoto=null;photoFailure='';preparing=true;$('photoSource').value='auto';
  $('remove').checked=false;$('photoPreview').hidden=true;$('discardPhoto').hidden=false;
  $('photoState').textContent='Preparing a square photo preview on this phone…';controls();
  try{
    const blob=await photo(file,()=>version===photoVersion);
    if(version!==photoVersion)return;
    preparedPhoto=blob;$('photoPreview').hidden=false;
    $('photoState').textContent='Photo ready. Review this square crop, then Save badge to keep it.';
  }catch(error){
    if(version!==photoVersion)return;
    photoFailure=error.message||'The photo could not be prepared. '+browserHelp;
    $('photoState').textContent=photoFailure;
  }finally{if(version===photoVersion){preparing=false;controls()}}
}
function finish(message){
  ++photoVersion;preparedPhoto=null;selectedFile=null;preparing=false;
  state(message);$('form').hidden=true;$('clockRetry').disabled=true;
}
$('photoSource').addEventListener('change',choosePhotoSource);
$('photo').addEventListener('change',selectPhoto);
$('discardPhoto').addEventListener('click',discardPhoto);
$('remove').addEventListener('change',()=>{
  ++photoVersion;selectedFile=null;preparedPhoto=null;photoFailure='';preparing=false;$('photoSource').value='auto';
  $('photo').value='';$('photoPreview').hidden=true;$('discardPhoto').hidden=!$('remove').checked;
  $('photoState').textContent=$('remove').checked?'The saved photo will be removed when you Save badge.':originalPhotoMessage;
  controls();
});
$('form').addEventListener('submit',async event=>{
  event.preventDefault();if(busy)return;
  if(preparing){state('Wait for the photo preview before saving.');return}
  if(photoFailure){state(photoFailure);return}
  const fields={name:$('name').value,company:$('company').value,github:$('github').value,x:$('x').value,linkedin:$('linkedin').value};
  fields.ssid=$('wifiSsid').value;fields.password=$('wifiPassword').value;
  const source=$('photoSource').value||'auto';
  if(photoSources[source]&&!fields[source].trim()){state('Add your '+photoSources[source]+' handle to use its photo.');return}
  lock(true);state('Saving…');
  try{
    let image=$('remove').checked?'remove':source,imageToken='';
    if(selectedFile&&!$('remove').checked){
      if(!preparedPhoto)throw Error('Choose the photo again and wait for its preview.');
      state('Sending your prepared photo to the badge…');
      const result=await request('/image',preparedPhoto,'image/jpeg');
      if(typeof result.imageToken!=='string'||!/^[a-f0-9]{16}$/.test(result.imageToken))throw Error('The badge did not confirm the photo upload. Try Save badge again.');
      image='staged';imageToken=result.imageToken;
    }
    state('Saving your badge…');
    const result=await request('/save',{...fields,image,imageToken});finish(result.message);
  }catch(error){state(error.message+' If the connection was lost, check your badge or reopen setup to confirm what was saved.');lock(false)}
});
$('cancel').addEventListener('click',async()=>{
  if(busy)return;lock(true);
  try{const result=await request('/cancel',{});finish(result.message)}
  catch(error){state(error.message+' You can also press either badge pusher to leave setup.');lock(false)}
});
$('wifiForget').addEventListener('click',()=>{
  if(busy)return;$('wifiSsid').value='init() attendee';$('wifiPassword').value='';
  $('wifiStatus').textContent='The badge will use the event Wi-Fi after Save badge.';
});
$('clockRetry').addEventListener('click',syncClock);
void syncClock();
</script></body></html>)HTML";
