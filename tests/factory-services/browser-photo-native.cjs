// Optional integration check using an already-installed Playwright and Chrome.
// Real native image/canvas APIs, synthetic images and a loopback-only server.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const http=require('node:http');
const zlib=require('node:zlib');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'../..');
const nonce='0123456789abcdef0123456789abcdef';
const header=fs.readFileSync(path.join(root,'firmware/factory_badge/main/portal_page.h'),'utf8');
const html=header.match(/R"HTML\((.*)\)HTML";/s)[1]
  .replaceAll('{{NONCE}}',nonce).replaceAll('{{PHOTO}}','No saved photo.')
  .replaceAll(/\{\{(?:NAME|COMPANY|GITHUB|X|LINKEDIN)\}\}/g,'');

// Minimal lossless synthetic RGB PNG: outer vertical strips red/blue and a green
// centered square. A correct centered crop contains green, with no outer strip.
function png(){
  function crc32(bytes){let c=0xffffffff;for(const b of bytes){c^=b;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
  function chunk(name,data){const type=Buffer.from(name),size=Buffer.alloc(4),crc=Buffer.alloc(4);size.writeUInt32BE(data.length);crc.writeUInt32BE(crc32(Buffer.concat([type,data])));return Buffer.concat([size,type,data,crc]);}
  const w=1200,h=800,scan=Buffer.alloc(h*(w*3+1));
  for(let y=0;y<h;y++)for(let x=0;x<w;x++)scan[y*(w*3+1)+1+x*3+(x<200?0:x>=1000?2:1)]=255;
  const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(w);ihdr.writeUInt32BE(h,4);ihdr[8]=8;ihdr[9]=2;
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',zlib.deflateSync(scan)),chunk('IEND',Buffer.alloc(0))]);
}

(async()=>{
  const requests=[];
  const server=http.createServer(async(req,res)=>{
    if(req.method==='GET'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end(html);return;}
    const chunks=[];for await(const chunk of req)chunks.push(chunk);
    const body=Buffer.concat(chunks);assert.equal(req.headers['x-conference-nonce'],nonce);
    const record={path:req.url,headers:req.headers,body};
    if(req.headers['content-type']==='application/json')record.parsed=JSON.parse(body);
    requests.push(record);res.writeHead(200,{'Content-Type':'application/json'});
    res.end(JSON.stringify({ok:true,valid:true,message:'Saved on your badge.',imageToken:'0123456789abcdef'}));
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try{
    browser=await chromium.launch({headless:true,...(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{channel:'chrome'})});
    const address=`http://127.0.0.1:${server.address().port}`;
    for(const mode of ['native','file-reader','data-url']){
      const context=await browser.newContext({viewport:{width:390,height:844}});
      if(mode==='file-reader')await context.addInitScript(()=>{URL.createObjectURL=()=>{throw Error('Synthetic unsupported blob URL');};});
      if(mode==='data-url')await context.addInitScript(()=>{HTMLCanvasElement.prototype.toBlob=undefined;});
      const page=await context.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
      await page.goto(address);await page.waitForFunction(()=>!document.getElementById('save').disabled);
      await page.locator('#name').fill('Synthetic Attendee');await page.locator('#company').fill('Synthetic Lab');
      const before=requests.length;
      await page.locator('#photoSource').selectOption('upload');
      await page.locator('#photo').setInputFiles({name:'synthetic.png',mimeType:'image/png',buffer:png()});
      await page.waitForFunction(()=>document.getElementById('photoState').textContent.startsWith('Photo ready.'));
      assert.equal(requests.length,before,'Preparation and preview never upload');
      const preview=await page.locator('#photoPreview').evaluate(canvas=>({hidden:canvas.hidden,pixel:Array.from(canvas.getContext('2d').getImageData(80,80,1,1).data)}));
      assert.equal(preview.hidden,false);assert.deepEqual(preview.pixel,[0,255,0,255]);
      await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('form').hidden);
      const written=requests.slice(before);assert.deepEqual(written.map(r=>r.path),['/image','/save']);
      assert.equal(written[0].headers['content-type'],'image/jpeg');assert(written[0].body.length>0&&written[0].body.length<=128*1024);
      assert.equal(written[0].body.readUInt16BE(0),0xffd8,'Native encoder emits a JPEG');
      assert.equal(written[0].headers['content-length'],String(written[0].body.length),'HTTP upload has the bounded length the badge accepts');
      const decoded=await page.evaluate(async bytes=>{
        const image=await createImageBitmap(new Blob([new Uint8Array(bytes)],{type:'image/jpeg'}));
        const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;
        const ctx=canvas.getContext('2d');ctx.drawImage(image,0,0);
        const result={width:image.width,height:image.height,pixel:Array.from(ctx.getImageData(0,0,1,1).data)};image.close();return result;
      },Array.from(written[0].body));
      assert.deepEqual([decoded.width,decoded.height],[512,512]);
      assert(decoded.pixel[0]<5&&decoded.pixel[1]>250&&decoded.pixel[2]<5,'Encoded square excludes the outer colored strips');
      assert.deepEqual(written[1].parsed,{name:'Synthetic Attendee',company:'Synthetic Lab',network:'github',handle:'',ssid:'init() attendee',password:'',image:'staged',imageToken:'0123456789abcdef'});
      assert.deepEqual(errors,[]);await context.close();
    }
    {
      const context=await browser.newContext();const page=await context.newPage();
      await page.goto(address);await page.waitForFunction(()=>!document.getElementById('save').disabled);
      await page.locator('#name').fill('Keep this edit');const before=requests.length;
      await page.locator('#photoSource').selectOption('upload');
      await page.locator('#photo').setInputFiles({name:'invalid.png',mimeType:'image/png',buffer:Buffer.from('not an image')});
      await page.waitForFunction(()=>document.getElementById('photoState').textContent.includes('could not be opened'));
      assert.equal(await page.locator('#save').isDisabled(),true);assert.equal(requests.length,before);
      await page.locator('#discardPhoto').click();await page.locator('#photoSource').selectOption('keep');await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('form').hidden);
      assert.deepEqual(requests.slice(before).map(r=>r.path),['/save']);
      assert.equal(requests.at(-1).parsed.name,'Keep this edit');assert.equal(requests.at(-1).parsed.image,'keep');await context.close();
    }
    console.log(`Native ${await browser.version()}: PNG→centered JPEG preview/upload via normal, FileReader and toDataURL paths; malformed-photo rejection and preserved draft passed`);
  }finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
