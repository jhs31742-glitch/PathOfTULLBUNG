(() => {
'use strict';
const root=document.getElementById('ft-group'),canvas=document.getElementById('ft-canvas');
let current=null,token=0,fontReady=false,bounds=null,lastFrame=null,lastSerial=0,hideTimer=null;
let faces=[];
const finite=n=>typeof n==='number'&&Number.isFinite(n);
const integer=n=>Number.isSafeInteger(n)&&n>=0;
const hide=()=>{root.style.visibility='hidden';root.setAttribute('aria-hidden','true');};
function receipt(kind,p){try{if(typeof window.tullFTTransportReceipt==='function')window.tullFTTransportReceipt(JSON.stringify({kind,session:p.session,revision:p.revision,...(kind==='frame'?{serial:p.serial}:{})}));}catch{}}
function failed(p){hide();token++;fontReady=false;bounds=null;lastFrame=null;clearFonts();receipt('font-failed',p);}
function equal(p){return current&&p.session===current.session&&p.revision===current.revision;}
function clearFonts(){for(const face of faces)try{document.fonts.delete(face);}catch{}faces=[];}
async function loadFonts(p,mine){
  if(!document.fonts||typeof document.fonts.load!=='function'||typeof FontFace!=='function')throw Error('font-capability');
  const catalog=new Map();
  for(const f of p.catalog?.fonts||[]){
    if(typeof f.id!=='string'||typeof f.url!=='string'||!['truetype','opentype'].includes(f.format))continue;
    if(!f.id.startsWith('user/')||f.id.split('/').includes('..')||!f.url.startsWith('assets/fonts/user/')||f.url.split('/').some(segment=>segment==='..'||segment==='.')||/[\\\x00-\x1f]/.test(f.url))continue;
    catalog.set(f.id,f);
  }
  const families=new Map();
  const ids=new Set(p.style.blocks.map(b=>b.fontId==='system'?p.style.defaultFont:b.fontId));
  await Promise.all([...ids].map(async id=>{
    if(id==='system'||id==='system-fixed'){
      const family='"Malgun Gothic",sans-serif';
      await document.fonts.load('17px "Malgun Gothic"');
      if(!document.fonts.check('17px "Malgun Gothic"'))throw Error('system-font-unavailable');
      families.set(id,family);return;
    }
    const f=catalog.get(id);if(!f)throw Error('font-not-in-native-catalog');
    // Source-verified Prisma 1.4.1 ViewManager::Create prepends file:///views/.
    // This exact packaged main-view route is still RUNTIME_UNVERIFIED on installed DLL.
    const url=new URL(f.url,'file:///views/pathoftullbung_debug/').href;
    if(!url.startsWith('file:///views/pathoftullbung_debug/assets/fonts/user/'))throw Error('font-route');
    const family='POTFT_'+mine+'_'+families.size+'_'+[...ids].indexOf(id);
    const face=new FontFace(family,'url("'+url.replace(/"/g,'%22')+'") format("'+f.format+'")');
    await face.load();
    if(mine!==token||!equal(p))return;
    document.fonts.add(face);faces.push(face);families.set(id,'"'+family+'"');
  }));
  if(mine!==token||!equal(p))return;
  for(const b of p.style.blocks){const el=canvas.children[b.slot-1]?.firstChild;if(el)el.style.fontFamily=families.get(b.fontId==='system'?p.style.defaultFont:b.fontId);}
  fontReady=true;measure();
  // Font completion is not permission to resurrect a previous frame.
  lastFrame=null;hide();
}
function measure(){
  root.style.transform='none';canvas.style.transform='none';
  const base=canvas.getBoundingClientRect();
  let x=Infinity,y=Infinity,right=-Infinity,bottom=-Infinity,count=0;
  for(const block of canvas.children){
    if(block.hidden)continue;
    const el=block.firstChild,r=el.getBoundingClientRect(),pad=Number(el.dataset.stroke)||0;
    x=Math.min(x,r.left-base.left-pad);y=Math.min(y,r.top-base.top-pad);
    right=Math.max(right,r.right-base.left+pad);bottom=Math.max(bottom,r.bottom-base.top+pad);count++;
  }
  bounds=count&&[x,y,right,bottom].every(finite)&&right>x&&bottom>y?{x,y,w:right-x,h:bottom-y}:null;
}
// Bound display of legacy long strings without rewriting title data.
function displayText(text){
  let units=0,prefix='';
  for(const ch of text.normalize('NFC')){
    units+=ch.codePointAt(0)<128?1:2;
    if(units>32)return prefix+'…';
    prefix+=ch;
  }
  return text;
}
function applyState(p){
  if(!integer(p.session)||!integer(p.revision))return;
  if(current&&(p.session<current.session||(p.session===current.session&&p.revision<=current.revision)))return;
  hide();if(hideTimer!==null)clearTimeout(hideTimer);hideTimer=null;
  current=p;token++;fontReady=false;bounds=null;lastFrame=null;lastSerial=0;clearFonts();canvas.replaceChildren();
  if(!p.enabled)return;
  const s=p.style;
  if(!s||!Array.isArray(s.blocks)||s.blocks.length!==6||!Array.isArray(p.blocks)||p.blocks.length!==6||!finite(s.hostWidth)||!finite(s.hostHeight))throw Error('state-shape');
  canvas.style.width=s.hostWidth+'px';canvas.style.height=s.hostHeight+'px';
  for(let slot=1;slot<=6;slot++){
    const b=s.blocks.find(x=>x.slot===slot),t=p.blocks.find(x=>x.slot===slot);if(!b||!t||typeof t.text!=='string')throw Error('slot');
    const block=document.createElement('div'),el=document.createElement('span');block.className='ft-block';el.className='ft-text';
    block.dataset.slot=String(slot);block.hidden=!t.active||!t.text.trim();
    Object.assign(block.style,{left:b.x+'%',top:b.y+'%',width:b.w+'%',height:b.h+'%',zIndex:String(slot)});
    el.textContent=displayText(t.text);
    const base=Math.max(12,Math.min(34,Math.round(17*Math.sqrt(Math.max(1,b.w*b.h)/(28*38))*10)/10));
    const width=b.outlineWidth,diagonal=Math.round(width*0.75*100)/100;
    Object.assign(el.style,{fontSize:(Math.round(base*(b.fontScale/100)*10)/10)+'px',opacity:String(b.opacity/100),
      transform:'translate(-50%,-50%) rotate('+b.rotationDeg+'deg)',color:s.fill,
      webkitTextFillColor:s.fill,webkitTextStroke:width+'px '+s.outline,
      textShadow:width?[-width+'px 0',width+'px 0','0 '+(-width)+'px','0 '+width+'px',
        -diagonal+'px '+(-diagonal)+'px',diagonal+'px '+(-diagonal)+'px',-diagonal+'px '+diagonal+'px',diagonal+'px '+diagonal+'px'].map(v=>v+' '+s.outline).join(','):'none'});
    el.dataset.stroke=String(width);el.dataset.outlineZero=width===0?'1':'0';block.append(el);canvas.append(block);
  }
  const mine=token;void loadFonts(p,mine).catch(()=>{if(mine===token&&equal(p))failed(p);});
}
function applyFrame(p){
  if(!equal(p)||!integer(p.serial)||p.serial<=lastSerial)return;
  lastSerial=p.serial;lastFrame=p;hide();
  if(hideTimer!==null)clearTimeout(hideTimer);
  hideTimer=setTimeout(()=>{hide();lastFrame=null;},300); // One timer; stopped producer never leaves stale text.
  if(!current.enabled||!p.visible||!fontReady||!bounds||!finite(p.width)||!finite(p.height))return;
  const points=(p.points||[]).filter(v=>Array.isArray(v)&&v.length===2&&v.every(finite));
  if(points.length<2||points.length>5)return;
  const xs=points.map(v=>v[0]).sort((a,b)=>a-b),ys=points.map(v=>v[1]);
  if(Math.max(...xs)<0||Math.min(...xs)>1||Math.max(...ys)<0||Math.min(...ys)>1)return;
  // R2 PlaceComposite: median body X, highest anchor Y, one transformed six-block union.
  const pad=12,gap=Math.max(56,Math.round(p.height/9));
  const bottom=Math.min(p.height-pad,Math.min(...ys)*p.height-gap);
  const availableW=p.width-pad*2,availableH=bottom-pad;
  const scale=Math.min(1,availableW/bounds.w,availableH/bounds.h);if(!finite(scale)||scale<=0)return;
  const n=xs.length,median=n%2?xs[Math.floor(n/2)]:(xs[n/2-1]+xs[n/2])*0.5;
  const w=bounds.w*scale,h=bounds.h*scale;
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  const x=clamp(median*p.width-w*0.5,pad,p.width-pad-w),y=bottom-h;
  canvas.style.transform='translate('+(-bounds.x)+'px,'+(-bounds.y)+'px)';
  root.style.transform='translate('+x+'px,'+y+'px) scale('+scale+')';
  root.style.visibility='visible';root.setAttribute('aria-hidden','false');
}
window.tullFTReceive=raw=>{
  let p;try{p=JSON.parse(raw);if(p.kind==='state')applyState(p);else if(p.kind==='frame')applyFrame(p);}
  catch{hide();if(p&&equal(p))failed(p);}
  finally{if(p&&(p.kind==='state'||p.kind==='frame'))receipt(p.kind,p);}
};
window.addEventListener('resize',()=>{hide();if(fontReady)measure();lastFrame=null;});
hide();
// Prisma binds native JS listeners after DOM ready. Announce readiness only once bound.
let readyAttempts=0;
function announceReady(){
  const callback=window.tullFTTransportReceipt;
  if(typeof callback==='function'){
    try{callback('{"kind":"ready"}');return;}catch{}
  }
  if(++readyAttempts<100)setTimeout(announceReady,50);
}
announceReady();
})();