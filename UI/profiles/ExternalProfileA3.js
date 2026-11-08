(() => {
'use strict';
if(window.TULLExternalProfileA3) return;
const core=window.TULLUIBrowserBackupR2, contract=window.TULLUIExternalProfileContractR1;
const legacy=window.TULLUIExternalImportPlanR1;
const MAX=1000000, FORMAT='TULLBUNG_UI_ONLY_PROFILE';
const has=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
const obj=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
const own=(o,allowed,required=allowed)=>obj(o)&&Object.keys(o).every(k=>allowed.includes(k))&&required.every(k=>has(o,k));
const fail=reason=>({ok:false,reason,applied:false});
const requireThat=(v,s)=>{if(!v)throw new Error(s);};
const clone=x=>JSON.parse(JSON.stringify(x));
const number=(x,a,b)=>typeof x==='number'&&Number.isFinite(x)&&x>=a&&x<=b;
const integer=(x,a,b)=>Number.isInteger(x)&&number(x,a,b);
const bytes=s=>{
 if(typeof s!=='string')return -1;
 let n=0;
 for(let i=0;i<s.length;i++){const c=s.charCodeAt(i);
  if(c<=127)n++;else if(c<=2047)n+=2;
  else if(c>=0xD800&&c<=0xDBFF){const d=s.charCodeAt(++i);if(!(d>=0xDC00&&d<=0xDFFF))return -1;n+=4;}
  else if(c>=0xDC00&&c<=0xDFFF)return -1;else n+=3;
 }return n;
};
function parseStrict(raw){
 requireThat(bytes(raw)>=0&&bytes(raw)<=MAX,'size-or-encoding');
 let value;try{value=JSON.parse(raw);}catch{throw Error('json-syntax');}let i=0,nodes=0;
 const ws=()=>{while(/\s/.test(raw[i]||'')&&i<raw.length)i++;};
 const text=()=>{const start=i++;while(i<raw.length){const c=raw[i++];if(c==='\\')i++;else if(c==='"')break;}return JSON.parse(raw.slice(start,i));};
 function scan(depth){
  requireThat(depth<=32&&++nodes<=100000,'depth-or-nodes');ws();
  if(raw[i]==='{'){i++;ws();const seen=new Set();if(raw[i]==='}'){i++;return;}
   for(;;){ws();const k=text();requireThat(!seen.has(k),'duplicate-key');
    requireThat(!['__proto__','constructor','prototype'].includes(k),'forbidden-property');seen.add(k);ws();i++;scan(depth+1);ws();if(raw[i++]==='}')break;}
  }else if(raw[i]==='['){i++;ws();if(raw[i]===']'){i++;return;}for(;;){scan(depth+1);ws();if(raw[i++]===']')break;}}
  else if(raw[i]==='"'){const s=text();requireThat(bytes(s)>=0,'surrogate');}
  else {while(i<raw.length&&!/[,\]}\s]/.test(raw[i]))i++;}
 }
 scan(0);ws();requireThat(i===raw.length,'trailing-json');return value;
}
const keys=core.keys(), nativeKeys=contract.nativeKeys();
const blocks=Array.from({length:6},(_,i)=>'BLOCK_TITLE_'+String(i+1).padStart(2,'0'));
const colorKeys=['surface','text','muted','accent','border'];
const hex=x=>typeof x==='string'&&/^#[0-9a-f]{6}$/i.test(x);
const colorSet=x=>own(x,colorKeys)&&colorKeys.every(k=>hex(x[k]));
const overlap=(a,b)=>a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
function noOverlap(rects){return rects.every((r,i)=>rects.slice(i+1).every(s=>!overlap(r,s)));}
function fontId(s){
 if(s==='system')return true;
 if(typeof s!=='string'||!s.startsWith('user/')||s.length>512||bytes(s)<0||/[\x00-\x1f\x7f\\:<>"|?*]/.test(s))return false;
 const p=s.slice(5).split('/');
 return p.length<=9&&p.every(x=>x&&x!=='.'&&x!=='..')&&/\.(ttf|otf|woff|woff2)$/i.test(p[p.length-1]);
}
function rect(r,columns,rows,minW=1,minH=1){
 return own(r,['x','y','w','h'])&&integer(r.x,0,columns)&&integer(r.y,0,rows)&&
 integer(r.w,minW,columns)&&integer(r.h,minH,rows)&&r.x+r.w<=columns&&r.y+r.h<=rows;
}
function presentation(p){
 return own(p,['fontId','fontScale','opacity','rotationDeg','outlineWidth'],['fontId','fontScale','opacity','rotationDeg'])&&
 fontId(p.fontId)&&integer(p.fontScale,50,400)&&integer(p.opacity,5,100)&&integer(p.rotationDeg,-180,180)&&
 (!has(p,'outlineWidth')||integer(p.outlineWidth,0,8));
}
function gridConfig(g){
 if(!own(g,['columns','rows','zones'])||!integer(g.columns,4,24)||!integer(g.rows,6,40)||!own(g.zones,['TITLE','SUMMARY','NODE']))return false;
 const z=g.zones;
 return ['TITLE','SUMMARY','NODE'].every(k=>own(z[k],['start','end'])&&integer(z[k].start,0,g.rows-1)&&integer(z[k].end,1,g.rows)&&z[k].start<z[k].end)&&
 z.TITLE.end<=z.SUMMARY.start&&z.SUMMARY.end<=z.NODE.start&&z.NODE.end===g.rows;
}
function central(v,dormant=false){
 if(!own(v,['version','global','classes','grid'],['version','global','classes'])||v.version!==1||!obj(v.global)||!obj(v.classes))return false;
 if(has(v,'grid')&&!gridConfig(v.grid))return false;
 const columns=v.grid?.columns||24,rows=v.grid?.rows||40;
 const globals=['title','stat.highestLevel','stat.earnedJP','stat.spentJP','stat.availableJP'];
 const validEntry=(key,r,isNode)=>{
  if(!rect(r,columns,rows)||(isNode&&r.w!==r.h))return false;
  if(!v.grid||dormant)return true;
  const zone=v.grid.zones[isNode?'NODE':key==='title'?'TITLE':'SUMMARY'];
  return r.y>=zone.start&&r.y+r.h<=zone.end;
 };
 if(!Object.keys(v.global).every(k=>globals.includes(k)&&validEntry(k,v.global[k],false))||!noOverlap(Object.values(v.global)))return false;
 if(Object.keys(v.classes).length>64)return false;
 return Object.keys(v.classes).every(c=>{
  if(!/^(0|[1-9]\d{0,4})$/.test(c)||Number(c)>65535||!obj(v.classes[c])||Object.keys(v.classes[c]).length>256)return false;
  return Object.keys(v.classes[c]).every(k=>new RegExp('^node\\.'+c+'\\.(0|[1-9]\\d{0,4})$').test(k)&&validEntry(k,v.classes[c][k],true))&&
  noOverlap([...(dormant?[]:Object.values(v.global)),...Object.values(v.classes[c])]);
 });
}
function browserValue(key,raw){
 if(raw===null)return true;
 if(!keys.includes(key)||!core.valid(key,raw))return false;
 if(key==='tull.hudEditor.selectedPreset.v1')return /^[123]$/.test(raw);
 let v;try{v=parseStrict(raw);}catch(_){return false;}
 if(key==='tull.ui.customPalette.v1')
  return own(v,['version','active','mabinogi','selected','working','slots'])&&v.version===1&&
   typeof v.active==='boolean'&&typeof v.mabinogi==='boolean'&&integer(v.selected,1,3)&&colorSet(v.working)&&
   Array.isArray(v.slots)&&v.slots.length===3&&v.slots.every(s=>s===null||colorSet(s));
 if(key==='tull.ui.shared.colors.v1')return own(v,['version','colors'])&&v.version===1&&Array.isArray(v.colors)&&v.colors.length<=24&&v.colors.every(c=>c===null||hex(c));
 if(key==='tull.title.lettering.overrides.v1')return own(v,['version','fill','outline'])&&v.version===1&&['fill','outline'].every(k=>v[k]===null||hex(v[k]));
 if(key==='tull.font.uiTitle.v1')return own(v,['version','ui','title'])&&v.version===1&&fontId(v.ui)&&fontId(v.title);
 if(key.startsWith('tull.preview.authoring.'))return central(v,key.includes('.preset.'));
 if(key.startsWith('tull.titleGrid.')){
  const preset=key.includes('.preset.');
  return own(v,['version','geometry','presentation','activeMask'],preset?['version','geometry','activeMask']:['version','geometry'])&&v.version===1&&
   (!has(v,'activeMask')||preset&&integer(v.activeMask,0,63))&&own(v.geometry,blocks)&&blocks.every(b=>rect(v.geometry[b],100,100,4,6))&&
   (!has(v,'presentation')||own(v.presentation,blocks)&&blocks.every(b=>presentation(v.presentation[b])));
 }
 if(key.startsWith('tull.hudEditor.preset.'))
  return own(v,['version','masterVisible','hud'])&&v.version===1&&typeof v.masterVisible==='boolean'&&own(v.hud,['void','composure','quiet'])&&
   ['void','composure','quiet'].every(k=>{
    const h=v.hud[k];return own(h,['visible','x','y','scale','opacity'])&&typeof h.visible==='boolean'&&number(h.x,0,1)&&number(h.y,0,1)&&number(h.scale,.5,2)&&number(h.opacity,0,1);
   });
 return false;
}
function nativeValue(k,v){
 if(k==='themeId')return ['default','dark-gold'].includes(v);
 if(k==='uiBackgroundOpacity')return number(v,0,1);
 if(k==='hudMaster'||k.endsWith('Visible'))return typeof v==='boolean';
 return nativeKeys.includes(k)&&number(v,k.endsWith('Scale')?.5:0,k.endsWith('Scale')?2:1);
}
function record(t){
 if(!own(t,['code','present','before','after'],['code','present'])||typeof t.code!=='string'||!/^C(0\d|1\d)$/.test(t.code)||typeof t.present!=='boolean')return false;
 if(!t.present)return ['before','after'].every(k=>!has(t,k)||t[k]==='');
 if(!has(t,'before')||!has(t,'after'))return false;
 if(!['before','after'].every(k=>typeof t[k]==='string'&&bytes(t[k])>=0&&bytes(t[k])<=1024&&!/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(t[k])))return false;
 return Number(t.code.slice(1))<10?/\S/.test(t.before)&&t.after==='':/\S/.test(t.before)||/\S/.test(t.after);
}
function input(raw){
 const v=parseStrict(raw);
 requireThat(own(v,['format','schema','browser','native','customTitles'],['format','schema'])&&v.format===FORMAT&&v.schema===1,'profile-shape');
 if(has(v,'browser')){
  requireThat(own(v.browser,['schema','values'])&&v.browser.schema===2&&obj(v.browser.values),'browser-shape');
  for(const k of Object.keys(v.browser.values))requireThat(keys.includes(k)&&browserValue(k,v.browser.values[k]),'browser-field:'+k);
 }
 if(has(v,'native')){
  requireThat(obj(v.native),'native-shape');
  for(const k of Object.keys(v.native))requireThat(nativeKeys.includes(k)&&nativeValue(k,v.native[k]),'native-field:'+k);
 }
 if(has(v,'customTitles')){
  requireThat(Array.isArray(v.customTitles)&&v.customTitles.length<=20,'title-shape');
  const seen=new Set();
  for(const t of v.customTitles){requireThat(record(t),'title-field');requireThat(!seen.has(t.code),'duplicate-title');seen.add(t.code);}
 }
 return v;
}
// Dormant presets are bounded stored editor slots, not an active DOM layout.
// Preserve valid slot bytes including legacy zone crossings; working layouts stay strict.
// This path never validates untrusted incoming fields or relocates any rectangle.
function compatibleCurrent(current){
 const safe=clone(current),excluded=[];
 for(const k of keys)if(/^tull\.preview\.authoring\.preset\.[123]\.v1$/.test(k)&&safe.browser[k]!==null&&!browserValue(k,safe.browser[k])&&core.valid(k,safe.browser[k])){
  parseStrict(safe.browser[k]);excluded.push(k);safe.browser[k]=null;
 }
 return {safe,excluded};
}
function exportFull(current){
 try{
  requireThat(obj(current)&&obj(current.browser)&&obj(current.native)&&obj(current.titleState),'snapshot-missing');
  const compatible=compatibleCurrent(current);requireThat(compatible.excluded.length===0,'current-preset-invalid');current=compatible.safe;
  requireThat(own(current.browser,keys)&&keys.every(k=>browserValue(k,current.browser[k])),'current-browser-invalid');
  const out=contract.exportBundle(current.browser,current.native,current.titleState);
  requireThat(out.ok,'current-contract-invalid');input(out.raw);
  // Reuse the original current title/slot/favorite checks without applying anything.
  requireThat(legacy.plan(out.raw,current).ok,'current-plan-invalid');
  return {ok:true,raw:out.raw,format:FORMAT,schema:1,warnings:compatible.excluded.map(k=>'legacy-preset-excluded-local-preserved:'+k)};
 }catch(e){return fail(e.message);}
}
function prepare(raw,current,slotDefaults={},replaceTitles=false){
 try{
  const incoming=input(raw),base=exportFull(current);requireThat(base.ok,base.reason);
  const full=incoming.browser&&own(incoming.browser.values,keys)&&incoming.native&&own(incoming.native,nativeKeys)&&incoming.customTitles?.length===20;
  const normalized=JSON.parse(base.raw),warnings=[...(base.warnings||[])];
  if(incoming.browser)for(const k of Object.keys(incoming.browser.values)){
   if(incoming.browser.values[k]===null){if(full){const fallback=typeof slotDefaults==='function'?slotDefaults(k):slotDefaults[k];requireThat(browserValue(k,fallback)&&fallback!==null,'empty-value-default-unavailable:'+k);normalized.browser.values[k]=fallback;warnings.push('empty-value-default:'+k);}else warnings.push('browser-null-preserved:'+k);continue;}
   normalized.browser.values[k]=incoming.browser.values[k];
  }
  if(incoming.native)for(const k of Object.keys(incoming.native))normalized.native[k]=incoming.native[k];
  for(const t of incoming.customTitles||[]){
   const i=Number(t.code.slice(1));
   if(!replaceTitles&&!t.present)continue;
   if(!replaceTitles)requireThat(current.titleState.customTitles[i].present,'derived-acquisition-change:'+t.code);
   normalized.customTitles[i]={code:t.code,present:t.present,before:t.before??'',after:t.after??''};
  }
  const normalizedRaw=JSON.stringify(normalized);requireThat(bytes(normalizedRaw)<=MAX,'normalized-too-large');
  requireThat(contract.parseBundle(normalizedRaw).ok,'normalized-contract-invalid');
  // The Native replacement plan owns title changes; legacy computes settings writes only.
  const settingsRaw=replaceTitles?JSON.stringify({...normalized,customTitles:JSON.parse(base.raw).customTitles}):normalizedRaw;
  const plan=legacy.plan(settingsRaw,current);requireThat(plan.ok,plan.reason);
  const excluded=compatibleCurrent(current).excluded;
  plan.browserWrites=plan.browserWrites.filter(w=>!excluded.includes(w.key)||incoming.browser?.values[w.key]!=null);plan.counts.browser=plan.browserWrites.length;
  requireThat(plan.titleWrites.every(t=>t.operation==='update'&&t.before.present&&t.after.present),'nonappearance-title-operation');
  const fontRefs=new Set();
  for(const key of keys){
   const stored=normalized.browser.values[key];if(stored===null)continue;
   if(key==='tull.font.uiTitle.v1'){const f=JSON.parse(stored);fontRefs.add(f.ui);fontRefs.add(f.title);}
   if(key.startsWith('tull.titleGrid.')){const p=JSON.parse(stored).presentation;for(const v of Object.values(p||{}))fontRefs.add(v.fontId);}
  }
  return {...plan,replaceTitles,applied:false,normalization:'partial-preserve-v1',normalizedRaw,warnings,
   fontTransfer:{references:[...fontRefs].filter(x=>x!=='system'),assetBytesCopied:0},
   preserved:{...plan.preserved,titleFavorites:!replaceTitles,titleSelections:!replaceTitles,titlePresence:!replaceTitles,derivedAcquisition:!replaceTitles},requiresA4:true};
 }catch(e){return fail(e.message);}
}
// Explicit built-in recovery only: supplied defaults replace Browser values, including null.
// The current raw Browser bytes remain the transaction rollback snapshot; they are not parsed as a valid target.
function prepareDefaults(raw,current){
 try {
  const incoming=input(raw);requireThat(obj(current)&&obj(current.browser)&&own(current.browser,keys),'snapshot-missing');
  requireThat(incoming.browser&&own(incoming.browser.values,keys)&&incoming.native&&own(incoming.native,nativeKeys),'defaults-incomplete');
  requireThat(Array.isArray(incoming.customTitles)&&incoming.customTitles.length===20&&incoming.customTitles.every(t=>t.present===false),'defaults-title-mutation');
  const safe={...current,browser:Object.fromEntries(keys.map(k=>[k,null]))};
  const baseline=contract.exportBundle(safe.browser,safe.native,safe.titleState);requireThat(baseline.ok,'current-native-title-invalid');
  const normalized=JSON.parse(baseline.raw);normalized.browser=clone(incoming.browser);normalized.native=clone(incoming.native);
  // Default resets working appearance, retaining the user's stored editor slots.
  for(const k of keys)if(k.includes('.preset.')&&browserValue(k,current.browser[k]))normalized.browser.values[k]=current.browser[k];
  const normalizedRaw=JSON.stringify(normalized);input(normalizedRaw);requireThat(contract.parseBundle(normalizedRaw).ok,'defaults-contract');
  const plan=legacy.plan(normalizedRaw,safe);requireThat(plan.ok&&plan.titleWrites.length===0,'defaults-protected-state');
  const excluded=compatibleCurrent(current).excluded;
  const browserWrites=keys.filter(k=>!excluded.includes(k)&&current.browser[k]!==normalized.browser.values[k]).map(key=>({key,before:current.browser[key],after:normalized.browser.values[key]}));
  return {...plan,browserWrites,counts:{...plan.counts,browser:browserWrites.length},normalizedRaw,normalization:'explicit-builtin-default-recovery',warnings:keys.filter(k=>!browserValue(k,current.browser[k])).map(k=>'invalid-current-replaced:'+k),fontTransfer:{references:[],assetBytesCopied:0},preserved:{...plan.preserved,titlePresence:true,titleText:true,derivedAcquisition:true},requiresA4:true};
 }catch(e){return fail(e.message);}
}
function validName(s){
 if(typeof s!=='string'||!/^[A-Za-z0-9_-]{1,48}\.json$/.test(s))return false;
 return !/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(s.slice(0,-5));
}
const requestId=s=>typeof s==='string'&&/^[A-Za-z0-9_-]{1,64}$/.test(s);
function create(transport){
 requireThat(obj(transport)&&typeof transport.invoke==='function','transport-missing');
 async function invoke(operation,id,name,raw){
  if(!requestId(id))return fail('request-id');
  if(operation!=='list'&&!validName(name))return fail('filename');
  try{
   const req={operation,requestId:id};if(operation!=='list')req.filename=name;if(operation==='write')req.raw=raw;
   const reply=await transport.invoke(Object.freeze(req));
   requireThat(obj(reply)&&reply.requestId===id&&reply.operation===operation,'native-response-mismatch');
   requireThat(Number.isInteger(reply.status)&&reply.status>=0&&reply.status<=10,'native-status');
   if(reply.status!==0)return {...fail('native-status:'+reply.status),nativeStatus:reply.status,nativeState:reply.state,nativeReason:reply.error||'',requestId:id};
   return {ok:true,reply};
  }catch(e){return fail('native-response:'+e.message);}
 }
 return Object.freeze({
  async list(id){const r=await invoke('list',id);if(!r.ok)return r;
   const files=r.reply.files;
   if(!Array.isArray(files)||files.length>256||!files.every(validName)||new Set(files).size!==files.length)return fail('native-list');
   return {ok:true,files:[...files].sort(),requestId:id};},
  async exportFile(name,current,id){const out=exportFull(current);if(!out.ok)return out;
   const r=await invoke('write',id,name,out.raw);return r.ok?{ok:true,requestId:id,filename:name,exported:true,settingsApplied:false}:r;},
  async readFile(name,id){const r=await invoke('read',id,name);if(!r.ok)return r;
   try{input(r.reply.raw);return {ok:true,requestId:id,raw:r.reply.raw,validation:'syntax-and-supplied-fields'};}
   catch(e){return fail(e.message);}},
  async importPlan(name,current,id){const r=await this.readFile(name,id);if(!r.ok)return r;return prepare(r.raw,current,transport.defaultBrowser);}
 });
}
window.TULLExternalProfileA3=Object.freeze({exportFull,prepare,prepareDefaults,inspect:raw=>{try{return {ok:true,value:input(raw)};}catch(e){return fail(e.message);}},create,validName,validBrowserValue:browserValue});
})();