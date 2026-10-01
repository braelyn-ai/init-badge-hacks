// Photo choice follows the one social account: username required, no phone
// upload, and an upload needs a chosen file.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync(process.argv[2],'utf8');
const elements=new Map();
function element(id){
  if(!elements.has(id))elements.set(id,{disabled:false,hidden:false,textContent:'',files:[],value:'',checked:false,listeners:{},addEventListener(name,callback){this.listeners[name]=callback;}});
  return elements.get(id);
}
const requests=[];
const context=vm.createContext({Date,Math,Error,Intl,setTimeout,clearTimeout,AbortController,
  document:{getElementById:element},
  fetch:async(path,options)=>{
    requests.push({path,parsed:JSON.parse(options.body)});
    return {ok:true,json:async()=>({ok:true,valid:true,message:'Saved.'})};
  }
});
const settle=()=>new Promise(resolve=>setImmediate(resolve));
const submit=async()=>{await element('form').listeners.submit({preventDefault(){}});await settle();};
const saves=()=>requests.filter(r=>r.path==='/save');
(async()=>{
  vm.runInContext(source,context);await settle();
  element('name').value='Synthetic Attendee';
  assert.equal(element('uploadArea').hidden,true,'Upload controls stay hidden unless chosen');
  element('network').value='x';element('network').listeners.change();
  assert.match(element('photoState').textContent,/X photo/);
  await submit();
  assert.equal(saves().length,0,'A network photo needs a username');
  assert.match(element('status').textContent,/Add your X username/);
  element('photoSource').value='upload';element('photoSource').listeners.change();
  assert.equal(element('uploadArea').hidden,false);
  await submit();
  assert.equal(saves().length,0);assert.match(element('status').textContent,/Choose a photo to upload/);
  element('photoSource').value='network';element('photoSource').listeners.change();
  element('handle').value='@example';
  await submit();
  const save=saves().at(-1).parsed;
  assert.equal(save.image,'x');assert.equal(save.imageToken,'');
  assert.deepEqual([save.github,save.x,save.linkedin],['','@example','']);
  assert.equal(requests.filter(r=>r.path==='/image').length,0,'Network photos are never uploaded from the phone');
  assert.equal(element('photoSource').disabled,true,'Locked while saving');
  console.log('Portal photo source: one network, username requirement, hidden upload, network choice without upload passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
