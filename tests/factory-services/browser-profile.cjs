// Exercise the real local portal script, including optional company submission.
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
  const values={name:'Synthetic Attendee',company:'Lab <R&D> “2026”',github:'example',x:'',linkedin:''};
  for(const [key,value] of Object.entries(values))element(key).value=value;
  saveOk=false;
  await element('form').listeners.submit({preventDefault(){}});await settle();
  assert.equal(requests.at(-1).path,'/save');
  assert.deepEqual(requests.at(-1).parsed,{...values,ssid:'',password:'',image:'auto',imageToken:''});
  assert.equal(element('form').hidden,false);
  assert.equal(element('save').disabled,false);
  assert.equal(element('company').value,values.company,'Failed saves keep the user’s edit');
  assert.match(element('status').textContent,/Invalid company/);
  saveOk=true;element('company').value='';element('remove').checked=true;
  await element('form').listeners.submit({preventDefault(){}});await settle();
  assert.equal(requests.at(-1).parsed.company,'','Blank company is explicit removal, not omission');
  assert.equal(requests.at(-1).parsed.image,'remove');
  assert.equal(element('form').hidden,true);
  assert.equal(requests.filter(request=>request.path==='/clock').length,1,'Profile edits do not repeat clock synchronization');
  console.log('Native portal profile script: company submission, explicit clearing, image choice and failed-save edit retention passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
