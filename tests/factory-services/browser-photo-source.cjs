// The setup page's GitHub/X/LinkedIn photo choice: handle required, no upload,
// and exclusive with a local photo or removal.
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
(async()=>{
  vm.runInContext(source,context);await settle();
  element('name').value='Synthetic Attendee';
  element('remove').checked=true;
  element('photoSource').value='x';element('photoSource').listeners.change();
  assert.equal(element('remove').checked,false,'Choosing a network photo cancels removal');
  assert.match(element('photoState').textContent,/X photo/);
  await submit();
  assert.equal(requests.filter(r=>r.path==='/save').length,0,'A network photo needs its handle');
  assert.match(element('status').textContent,/Add your X handle/);
  element('x').value='@example';
  await submit();
  const save=requests.at(-1);
  assert.equal(save.path,'/save');
  assert.equal(save.parsed.image,'x');assert.equal(save.parsed.imageToken,'');
  assert.equal(requests.filter(r=>r.path==='/image').length,0,'Network photos are never uploaded from the phone');
  assert.equal(element('photoSource').disabled,true,'Locked while saving');
  console.log('Portal photo source: handle requirement, network choice without upload and exclusive removal passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
