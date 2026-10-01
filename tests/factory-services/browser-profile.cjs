// Exercise the real setup script: one social account, optional company, the
// Other (URL) wording, and edit retention after a failed save.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync(process.argv[2],'utf8');
const elements=new Map();
function element(id){
  if(!elements.has(id))elements.set(id,{disabled:false,hidden:false,textContent:'',value:'',listeners:{},addEventListener(name,callback){this.listeners[name]=callback;}});
  return elements.get(id);
}
const requests=[];
let saveOk=true;
const context=vm.createContext({Date,Math,Error,Intl,setTimeout,clearTimeout,AbortController,
  document:{getElementById:element},
  fetch:async(path,options)=>{
    requests.push({path,...options,parsed:JSON.parse(options.body)});
    return {ok:path!=='/save'||saveOk,json:async()=>({ok:true,valid:true,message:saveOk?'Saved on your badge.':'Invalid company.'})};
  }
});
const settle=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
  vm.runInContext(source,context);await settle();
  assert.equal(element('handleLabel').textContent,'Username');
  element('network').value='url';element('network').listeners.change();
  assert.match(element('handleLabel').textContent,/Profile link/);
  assert.match(element('networkHint').textContent,/recognizes/);
  element('network').value='github';element('network').listeners.change();
  assert.match(element('networkHint').textContent,/gets its photo/);
  const values={name:'Synthetic Attendee',company:'Lab <R&D> “2026”'};
  for(const [key,value] of Object.entries(values))element(key).value=value;
  element('handle').value='example';
  saveOk=false;
  await element('form').listeners.submit({preventDefault(){}});await settle();
  assert.equal(requests.at(-1).path,'/save');
  assert.deepEqual(requests.at(-1).parsed,{...values,network:'github',handle:'example',ssid:'init() attendee',password:''},'No photo fields are sent');
  assert.equal(element('form').hidden,false);
  assert.equal(element('save').disabled,false);
  assert.equal(element('company').value,values.company,'Failed saves keep the user’s edit');
  assert.match(element('status').textContent,/Invalid company/);
  saveOk=true;element('company').value='';
  element('network').value='url';element('handle').value='bsky.app/profile/chan.dev';
  await element('form').listeners.submit({preventDefault(){}});await settle();
  const last=requests.at(-1).parsed;
  assert.equal(last.company,'','Blank company is explicit removal, not omission');
  assert.deepEqual([last.network,last.handle],['url','bsky.app/profile/chan.dev']);
  assert.equal(element('form').hidden,true);
  assert(!requests.some(r=>r.path==='/image'),'Photos are never uploaded from the phone');
  assert.equal(requests.filter(request=>request.path==='/clock').length,1,'Profile edits do not repeat clock synchronization');
  console.log('Native portal profile script: one social account, Other link wording, company submission/clearing and failed-save edit retention passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
