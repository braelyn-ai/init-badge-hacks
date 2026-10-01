// Executes the actual local portal photo flow with bounded native API adapters.
// Synthetic files only. This verifies browser control flow, not phone decoding.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync(process.argv[2],'utf8');
const settle=()=>new Promise(resolve=>setImmediate(resolve));

function harness(config={}){
  const elements=new Map(),requests=[],sources=new Map(),images=[],draws=[],timers=new Map();
  let nextTimer=0,nextUrl=0,revoked=0,fallbacks=0;
  function element(id){
    if(!elements.has(id))elements.set(id,{disabled:false,hidden:false,textContent:id==='photoState'?'Saved photo is kept.':'',files:[],value:'',checked:false,width:160,height:160,listeners:{},addEventListener(event,callback){this.listeners[event]=callback;}});
    const result=elements.get(id);
    if(id==='photoPreview')result.getContext=()=>config.previewUnavailable?null:{drawImage(canvas){draws.push({preview:true,file:canvas.file,width:canvas.width,height:canvas.height});}};
    return result;
  }
  const fakeTimers={
    setTimeout(callback,delay){const id=++nextTimer;timers.set(id,{callback,delay});return id;},
    clearTimeout(id){timers.delete(id);}
  };
  class Image {
    set src(value){
      this.source=value;if(!value)return;
      const file=sources.get(value);assert(file,'Image only reads our synthetic local source');
      this.file=file;images.push(this);
      if(config.holdDecode)return;
      queueMicrotask(()=>{
        if(config.failDecode||(config.blobUnsupported&&value.startsWith('blob:'))){if(this.onerror)this.onerror();return;}
        this.naturalWidth=file.width||4000;this.naturalHeight=file.height||3000;
        if(this.onload)this.onload();
      });
    }
  }
  class FileReader {
    readAsDataURL(file){
      if(config.holdRead)return;
      queueMicrotask(()=>{
        if(config.failRead){if(this.onerror)this.onerror();return;}
        this.result='data:image/jpeg;base64,'+Buffer.from(file.id||'synthetic').toString('base64');
        sources.set(this.result,file);if(this.onload)this.onload();
      });
    }
    abort(){if(this.onabort)this.onabort();}
  }
  class Canvas {
    constructor(){this.width=this.height=0;if(config.noToBlob)this.toBlob=undefined;}
    getContext(){
      if(config.canvasUnavailable)return null;
      return {fillStyle:'',fillRect(){},drawImage:(image,...bounds)=>{
        assert(sources.has(image.source),'Source URL must stay alive through canvas drawing');
        this.file=image.file;draws.push({file:this.file,width:this.width,height:this.height,bounds});
      }};
    }
    toBlob(callback,type,quality){
      if(config.holdEncode)return;
      queueMicrotask(()=>{
        if(config.nullBlob){callback(null);return;}
        const size=config.blobSize?config.blobSize(this.width,quality):24000;
        const blob=new Blob([new Uint8Array(size)],{type:config.wrongMime?'image/png':type});
        blob.fileId=this.file.id;callback(blob);
      });
    }
    toDataURL(type){
      ++fallbacks;
      if(config.badDataUrl)return 'data:image/png;base64,AA==';
      return 'data:'+type+';base64,/9j/2Q==';
    }
  }
  const urlApi={
    createObjectURL(file){if(config.throwObjectUrl)throw Error('Blob URLs unavailable');const url='blob:synthetic-'+(++nextUrl);sources.set(url,file);return url;},
    revokeObjectURL(url){++revoked;sources.delete(url);}
  };
  const context=vm.createContext({Date,Math,Error,Intl,Blob,Uint8Array,Set,AbortController,
    ...fakeTimers,URL:urlApi,Image,FileReader,atob:value=>Buffer.from(value,'base64').toString('binary'),
    document:{getElementById:element,createElement(name){assert.equal(name,'canvas');return new Canvas();}},
    fetch:async(path,options)=>{
      const request={path,...options};if(options.headers['Content-Type']==='application/json')request.parsed=JSON.parse(options.body);requests.push(request);
      if(config.hangPath===path)return new Promise(()=>{});
      if(config.holdCancel&&path==='/cancel')await new Promise(resolve=>{config.completeCancel=resolve;});
      const failed=(path==='/image'&&config.uploadError)||(path==='/cancel'&&config.cancelError);
      return {ok:!failed,json:async()=>({ok:true,valid:true,message:failed?(path==='/cancel'?'Setup could not be cancelled.':'Photo rejected by badge.'):'Saved on your badge.',imageToken:config.invalidToken?'': '0123456789abcdef'})};
    }
  });
  vm.runInContext(source,context);
  return {
    config,elements,element,requests,images,draws,timers,context,
    get revoked(){return revoked},get fallbacks(){return fallbacks},
    // These scenarios exercise phone uploads, so start in Upload mode.
    async ready(){await settle();assert.equal(element('save').disabled,false);element('photoSource').value='upload';element('photoSource').listeners.change();return this;},
    select(file){element('photo').files=file?[file]:[];return element('photo').listeners.change();},
    save(){return element('form').listeners.submit({preventDefault(){}});},
    fire(delay){for(const [id,timer] of [...timers])if(timer.delay===delay){timers.delete(id);timer.callback();}},
    complete(id){const image=images.find(image=>image.file.id===id&&image.onload);assert(image);image.naturalWidth=image.file.width||4000;image.naturalHeight=image.file.height||3000;image.onload();}
  };
}
const file=(id='A',extra={})=>({id,size:80000,type:'image/jpeg',width:4000,height:3000,...extra});
const uploads=h=>h.requests.filter(r=>r.path==='/image');
const saves=h=>h.requests.filter(r=>r.path==='/save');

(async()=>{
  {
    const h=await harness().ready();
    h.element('name').value='Synthetic Attendee';h.element('company').value='Research';
    await h.select(file('HEIC',{type:'image/heic'}));
    assert.equal(h.element('photoPreview').hidden,false);
    assert.match(h.element('photoState').textContent,/Photo ready/);
    assert.equal(uploads(h).length,0,'Selection and preview stay local');
    assert.equal(h.revoked,1,'Source URL released after conversion');
    assert.deepEqual(h.draws[0].bounds,[500,0,3000,3000,0,0,512,512],'Preview uses the same centered square crop as storage');
    await h.save();
    assert.equal(uploads(h).length,1);assert.equal(uploads(h)[0].body.type,'image/jpeg');
    assert.equal(uploads(h)[0].headers['X-Conference-Nonce'],'0123456789abcdef0123456789abcdef');
    assert.deepEqual(saves(h)[0].parsed,{name:'Synthetic Attendee',company:'Research',network:'github',handle:'',ssid:'init() attendee',password:'',image:'staged',imageToken:'0123456789abcdef'});
    assert.equal(h.element('form').hidden,true);assert.equal(h.timers.size,0);
  }
  for(const mode of [{blobUnsupported:true},{throwObjectUrl:true}]){
    const h=await harness(mode).ready();await h.select(file());
    assert.equal(h.element('photoPreview').hidden,false,'FileReader fallback prepares preview');
    await h.save();assert.equal(uploads(h).length,1);
  }
  for(const mode of [{noToBlob:true},{nullBlob:true},{wrongMime:true}]){
    const h=await harness(mode).ready();await h.select(file());await h.save();
    assert(h.fallbacks>0);assert.equal(uploads(h)[0].body.type,'image/jpeg','Never label a PNG canvas result as JPEG');
  }
  {
    const h=await harness({holdEncode:true}).ready();const preparing=h.select(file());await settle();
    assert.equal(h.element('save').disabled,true);h.fire(4000);await preparing;
    assert.equal(h.element('save').disabled,false);assert(h.fallbacks>0,'Stalled toBlob uses bounded data URL fallback');
  }
  {
    const h=await harness({blobSize:width=>width>320?150000:64000}).ready();await h.select(file());await h.save();
    assert.equal(uploads(h)[0].body.size,64000);assert(h.draws.some(draw=>draw.width===320),'Detailed photos get another bounded resize');
  }
  for(const [mode,input] of [[{},file('large',{size:11*1024*1024})],[{},file('empty',{size:0})],[{},file('dimensions',{width:10000,height:10000})],[{failDecode:true},file()],[{blobUnsupported:true,failRead:true},file()],[{canvasUnavailable:true},file()],[{previewUnavailable:true},file()],[{wrongMime:true,badDataUrl:true},file()]]){
    const h=await harness(mode).ready();h.element('name').value='Keep my edit';await h.select(input);
    assert.equal(h.element('photoPreview').hidden,true);assert.equal(h.element('save').disabled,true);
    assert.equal(h.element('discardPhoto').hidden,false);assert(h.element('photoState').textContent.length>20);
    await h.save();assert.equal(uploads(h).length,0);assert.equal(saves(h).length,0,'Failed preparation cannot silently save without the selected photo');
    h.element('discardPhoto').listeners.click();assert.equal(h.element('save').disabled,false);
    await h.save();assert.equal(saves(h).length,0,'Upload mode without a photo never saves');
    h.element('photoSource').value='keep';h.element('photoSource').listeners.change();
    await h.save();assert.equal(saves(h)[0].parsed.image,'keep');assert.equal(saves(h)[0].parsed.name,'Keep my edit');
    assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({holdDecode:true}).ready();const preparing=h.select(file());await settle();
    h.fire(10000);await settle();h.fire(10000);await preparing;
    assert.equal(h.element('save').disabled,true);assert.match(h.element('photoState').textContent,/Safari or Chrome/);
    assert.equal(h.timers.size,0,'Stalled decoding terminates and releases timers');
  }
  {
    const h=await harness({holdDecode:true}).ready();const first=h.select(file('first')),second=h.select(file('second'));
    h.complete('second');await second;h.complete('first');await first;
    assert.equal(h.draws.filter(draw=>draw.preview).at(-1).file.id,'second','Late earlier selection cannot replace the preview');
    await h.save();assert.equal(uploads(h)[0].body.fileId,'second');assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({holdDecode:true}).ready();const preparing=h.select(file());
    h.element('photoSource').value='remove';h.element('photoSource').listeners.change();h.complete('A');await preparing;
    assert.equal(h.element('photoPreview').hidden,true);await h.save();
    assert.equal(uploads(h).length,0);assert.equal(saves(h)[0].parsed.image,'remove','Explicit removal cancels an in-flight preparation');
  }
  for(const mode of [{uploadError:true},{invalidToken:true},{hangPath:'/image'}]){
    const h=await harness(mode).ready();await h.select(file());const saving=h.save();await settle();
    if(mode.hangPath){h.fire(15000);await settle();assert.equal(uploads(h)[0].signal.aborted,true);}
    await saving;assert.equal(saves(h).length,0,'Only a confirmed image token permits profile commit');
    assert.equal(h.element('form').hidden,false);assert.equal(h.element('save').disabled,false);
    assert.equal(h.element('photoPreview').hidden,false,'Upload failure retains the prepared photo for deliberate retry');
    assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({hangPath:'/save'}).ready();await h.select(file());const saving=h.save();await settle();
    h.fire(15000);await saving;
    assert.equal(saves(h).length,1,'Uncertain save is not automatically retried');
    assert.match(h.element('status').textContent,/check your badge or reopen setup/);assert.equal(h.element('save').disabled,false);
  }
  for(const failure of ['rejected','timeout']){
    const h=await harness({holdDecode:true,...(failure==='rejected'?{cancelError:true}:{hangPath:'/cancel'})}).ready();
    const preparing=h.select(file());const cancelling=h.element('cancel').listeners.click();await settle();
    if(failure==='timeout'){h.fire(15000);await settle();}
    await cancelling;
    assert.equal(h.element('form').hidden,false);
    assert.equal(h.element('save').disabled,true,'Failed Cancel keeps Save disabled while photo preparation is pending');
    h.complete('A');await preparing;
    assert.equal(h.element('photoPreview').hidden,false);
    assert.match(h.element('photoState').textContent,/Photo ready/);
    assert.equal(h.element('save').disabled,false);
    await h.save();assert.equal(uploads(h)[0].body.fileId,'A','Failed Cancel retains the pending photo for deliberate Save');
    assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({holdDecode:true,hangPath:'/cancel'}).ready();const preparing=h.select(file());
    const cancelling=h.element('cancel').listeners.click();await settle();
    h.complete('A');await preparing;
    assert.equal(h.element('save').disabled,true,'Completed photo cannot enable Save while Cancel is pending');
    h.fire(15000);await cancelling;
    assert.equal(h.element('save').disabled,false);assert.match(h.element('photoState').textContent,/Photo ready/);
    await h.save();assert.equal(uploads(h)[0].body.fileId,'A');assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({holdDecode:true}).ready();const preparing=h.select(file());
    await h.element('cancel').listeners.click();assert.equal(h.element('form').hidden,true);
    h.complete('A');await preparing;
    assert.equal(h.draws.filter(draw=>draw.preview).length,0,'Confirmed Cancel discards a later preparation result');
    assert.equal(uploads(h).length,0);assert.equal(saves(h).length,0);assert.equal(h.timers.size,0);
  }
  {
    const h=await harness({holdDecode:true,holdCancel:true}).ready();const preparing=h.select(file());
    const cancelling=h.element('cancel').listeners.click();await settle();h.complete('A');await preparing;
    assert.equal(h.element('save').disabled,true);assert.match(h.element('photoState').textContent,/Photo ready/);
    h.config.completeCancel();await cancelling;
    assert.equal(h.element('form').hidden,true);assert.equal(uploads(h).length,0);assert.equal(saves(h).length,0);
    assert.equal(h.timers.size,0,'Confirmed Cancel also discards preparation completed while awaiting its response');
  }
  console.log('Native portal photo script: local crop/preview, HEIC path, source lifetime, FileReader/JPEG fallbacks, resize limits, stalled/failed operations, selection races, atomic upload/save gate and preserved edits passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
