#pragma once
// Local-only portal retained from the Arduino conference UI. No external assets.
static constexpr char BADGE_PORTAL_HTML[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>init() badge setup</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:#0b0b0e;color:#f5f2e8;font:16px/1.5 system-ui,-apple-system,"Helvetica Neue",sans-serif}
main{max-width:480px;margin:0 auto;padding:28px 20px 48px}
.eyebrow,.num,label,button{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.eyebrow{font-size:13px;letter-spacing:.1em;text-transform:uppercase;color:#96969c;margin:0 0 18px}
h1{font-size:40px;line-height:1.05;font-weight:500;letter-spacing:-.02em;margin:0 0 12px}
h2{font-size:20px;line-height:1.3;font-weight:500;margin:0}
.lede,.hint,.clock{color:#96969c;margin:4px 0 0}.hint,.clock{font-size:14px}.clock{margin:14px 0 28px}
.step{position:relative;border-top:1px solid rgba(230,234,242,.14);padding:24px 0}
.num{position:absolute;top:28px;right:0;font-size:13px;color:#96969c}h2{padding-right:40px}
label{display:block;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#96969c;margin:18px 0 6px}
input,select{width:100%;height:48px;padding:0 14px;border:1px solid rgba(230,234,242,.28);border-radius:2px;background-color:#131318;color:#f5f2e8;font:inherit}
select{-webkit-appearance:none;appearance:none;padding-right:40px;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' viewBox='0 0 12 8'%3E%3Cpath d='M1 1.5l5 5 5-5' fill='none' stroke='%2396969c' stroke-width='1.5'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 14px center}
button{display:block;width:100%;height:48px;margin-top:12px;border:1px solid rgba(230,234,242,.28);border-radius:2px;background:transparent;color:#f5f2e8;font-size:14px;letter-spacing:.1em;text-transform:uppercase}
button.primary{background:#f5f2e8;border-color:#f5f2e8;color:#0b0b0e}button:disabled{opacity:.45}
button.text{display:inline;width:auto;height:auto;margin:0 0 0 6px;padding:0;border:0;font:inherit;letter-spacing:0;text-transform:none;text-decoration:underline;color:#96969c}
.actions{border-top:1px solid rgba(230,234,242,.14);padding-top:12px}
#status{min-height:1.5em;margin:18px 0 0;white-space:pre-wrap}a{color:#f5f2e8}
</style></head><body><main><p class="eyebrow">init() / Badge setup</p><h1>Set up your badge.</h1><p class="lede">Everything stays on this badge. No account needed.</p><p class="clock"><span id="clockStatus" role="status" aria-live="polite">Syncing the clock from this browser…</span><button id="clockRetry" type="button" class="text">Sync again</button></p><form id="form"><section class="step"><span class="num">01</span><div><h2>You</h2><label for="name">Name</label><input id="name" maxlength="120" autocomplete="off" value="{{NAME}}"><label for="company">Company (optional)</label><input id="company" maxlength="120" autocomplete="organization" value="{{COMPANY}}"></div></section><section class="step"><span class="num">02</span><div><h2>Profile</h2><p class="hint" id="networkHint">Your QR code opens this profile, and the badge gets its photo.</p><label for="network">Social network</label><select id="network" data-initial="{{NETWORK}}"><option value="linkedin">LinkedIn</option><option value="x">X</option><option value="github">GitHub</option><option value="huggingface">Hugging Face</option><option value="youtube">YouTube</option><option value="url">Other (URL)</option></select><label for="handle" id="handleLabel">Username</label><input id="handle" maxlength="180" autocomplete="off" autocapitalize="none" value="{{HANDLE}}"></div></section><section class="step"><span class="num">03</span><div><h2>Wi-Fi</h2><p class="hint">Used only to download your photo, then it turns off.</p><label for="wifiChoice">Wi-Fi network</label><select id="wifiChoice" data-initial="{{WIFI_CHOICE}}"><option value="event">init() attendee</option><option value="other">Other network</option></select><div id="wifiFields" hidden><label for="wifiSsid">Network name</label><input id="wifiSsid" maxlength="32" autocomplete="off" placeholder="Phone hotspot or home Wi-Fi" value="{{WIFI_SSID}}"><label for="wifiPassword">Password</label><input id="wifiPassword" type="password" maxlength="63" autocomplete="off" placeholder="Blank keeps the saved one"></div></div></section><div class="actions"><button id="save" type="submit" class="primary">Save badge</button><button id="cancel" type="button">Cancel</button></div></form><p id="status" role="status" aria-live="polite"></p></main><script>
'use strict';
const nonce='{{NONCE}}';
const $=id=>document.getElementById(id);
let busy=false;
function state(message){$('status').textContent=message}
function controls(){
  $('save').disabled=busy;
  for(const id of ['cancel','clockRetry','name','company','network','handle','wifiChoice','wifiSsid','wifiPassword'])$(id).disabled=busy;
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
// Keep in step with social_networks.h (checked by tests/factory-services).
const networkNames={linkedin:'LinkedIn',x:'X',github:'GitHub',huggingface:'Hugging Face',youtube:'YouTube',url:'Other (URL)'};
function describeNetwork(){
  const other=$('network').value==='url';
  $('handleLabel').textContent=other?'Profile link (https://…)':'Username';
  $('networkHint').textContent=other?'Your QR code opens this link. If it is a profile the badge recognizes, it also gets that photo.'
    :'Your QR code opens this profile, and the badge gets its photo.';
}
function describeWifi(){$('wifiFields').hidden=$('wifiChoice').value!=='other'}
function finish(message){state(message);$('form').hidden=true;$('clockRetry').disabled=true}
for(const id of ['network','wifiChoice']){const value=$(id).dataset&&$(id).dataset.initial;if(value)$(id).value=value}
$('network').addEventListener('change',describeNetwork);
$('wifiChoice').addEventListener('change',describeWifi);
describeNetwork();describeWifi();
$('form').addEventListener('submit',async event=>{
  event.preventDefault();if(busy)return;
  // One social account; the badge decides whether to download its photo.
  const network=$('network').value||'linkedin',handle=$('handle').value;
  const eventWifi=$('wifiChoice').value!=='other';
  const fields={name:$('name').value,company:$('company').value,network,handle,
    ssid:eventWifi?'init() attendee':$('wifiSsid').value,password:eventWifi?'':$('wifiPassword').value};
  lock(true);state('Saving your badge…');
  try{const result=await request('/save',fields);finish(result.message)}
  catch(error){state(error.message+' If the connection was lost, check your badge or reopen setup to confirm what was saved.');lock(false)}
});
$('cancel').addEventListener('click',async()=>{
  if(busy)return;lock(true);
  try{const result=await request('/cancel',{});finish(result.message)}
  catch(error){state(error.message+' You can also press either badge pusher to leave setup.');lock(false)}
});
$('clockRetry').addEventListener('click',syncClock);
void syncClock();
</script></body></html>)HTML";
