(() => {
'use strict';
const clone=x=>JSON.parse(JSON.stringify(x)), same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const need=(v,m)=>{if(!v)throw new Error(m);};
const hex=s=>Array.from(new TextEncoder().encode(s),v=>v.toString(16).padStart(2,'0')).join('');
const scalar=v=>typeof v==='boolean'?(v?'1':'0'):String(v);
function wireFor(plan){return plan.nativeWrites.map(w=>w.key+'='+hex(scalar(w.before))+':'+hex(scalar(w.after))).join('\n')+(plan.nativeWrites.length?'\n':'')+'--TITLES--\n'+plan.titleWrites.map(t=>t.id+':'+hex(t.before.before)+':'+hex(t.before.after)+':'+hex(t.after.before)+':'+hex(t.after.after)+'\n').join('');}
function create({a3,keys,storage,owners,native,sha256,journal}){
 need(a3&&Array.isArray(keys)&&keys.length===16&&new Set(keys).size===16,'contracts');
 for(const m of ['capture','currentGrid','preflight','apply','verify','restore','verifySnapshot'])need(typeof owners[m]==='function','owner-adapter:'+m);
 for(const m of ['prepare','commit','query','abort','finalize'])need(typeof native[m]==='function','native-adapter:'+m);
 let state='IDLE',pending=null,sequence=0,executing=false,reservation=null;const usedIds=new Set();
 const status=()=>({state,blocked:executing||reservation!==null||pending!==null,requestId:pending?.id||reservation?.id,planHash:pending?.hash,domValidation:pending?.dom||'NOT_RUN'});
 const nativeDetails=()=>({nativeState:pending?.lastNativeReply?.state||'NOT_REQUESTED',nativeStatus:pending?.lastNativeReply?.status,nativeReason:pending?.lastNativeReply?.error||''});
 const unknown=(why,recovery=false)=>{state=recovery?'RECOVERY_REQUIRED':'RESULT_UNKNOWN';return {ok:false,...status(),...nativeDetails(),reason:why};};
 function accept(reply){need(reply&&reply.requestId===pending.id&&reply.planHash===pending.hash,'response-identity');need(['PREPARED','COMMITTED','ABORTED','RECOVERY_REQUIRED'].includes(reply.state),'response-state');pending.lastNativeReply=reply;return reply.state;}
 const snapshot=()=>keys.map(key=>({key,raw:storage.getItem(key)}));
 function checkStorage(rows){for(const r of rows)need(storage.getItem(r.key)===r.raw,'browser-readback:'+r.key);}
 async function restoreBrowser(){
  if(pending.lastNativeReply?.snapshot)await owners.applyNativeMirror(pending.lastNativeReply.snapshot);
  let failed=false;for(const w of [...pending.plan.browserWrites].reverse()){try{const r=pending.browser.find(x=>x.key===w.key),actual=storage.getItem(w.key);need(actual===w.after||actual===r.raw,'browser-rollback-ownership');r.raw===null?storage.removeItem(r.key):storage.setItem(r.key,r.raw);}catch{failed=true;}}
  if(failed)return unknown('browser-rollback-ownership-or-write',true);
  try{await owners.restore(clone(pending.memory));checkStorage(pending.browser);need(await owners.verifySnapshot(clone(pending.memory)),'owner-rollback-readback');}catch{failed=true;}
  if(failed)return unknown('browser-rollback-unverified',true);
  try{journal?.clear();}catch{return unknown('browser-journal-clear',true);}
  state='ABORTED';const result={ok:false,...status(),blocked:false,...nativeDetails(),reason:pending.reason||pending.lastNativeReply?.error||'native-aborted',restored:true};pending=null;return result;
 }
 async function abortKnown(reason){pending.reason=reason;try{const s=accept(await native.abort(pending.id,pending.hash));if(s==='ABORTED')return await restoreBrowser();if(s==='RECOVERY_REQUIRED')return unknown('native-recovery-required',true);return unknown('abort-result-unresolved');}catch(e){return unknown('abort-response:'+e.message);}}
 async function finishCommitted(){
  try{
   await owners.applyNativeMirror(pending.lastNativeReply?.snapshot);
   checkStorage(pending.expected);need(await owners.verify(clone(pending.plan),clone(pending.ownerPlan)),'owner-final-verification');
  }catch(e){return abortKnown('browser-final-verification:'+e.message);}
  try{const s=accept(await native.finalize(pending.id,pending.hash));if(s==='RECOVERY_REQUIRED')return unknown('native-finalize-recovery',true);need(s==='COMMITTED','native-finalize-state');}
  catch(e){return unknown('finalize-response:'+e.message);}
  try{journal?.clear();}catch{return unknown('browser-journal-clear',true);}
  state='COMMITTED';const result={ok:true,...status(),blocked:false,isolated:true,domValidation:pending.dom,gameRuntimeVerified:false};pending=null;return result;
 }
 async function resolveReserved(){
  if(!pending)return {ok:false,...status(),reason:'no-pending-request'};
  if(state==='RECOVERY_REQUIRED')return unknown('manual-recovery-required',true);
  try{const s=accept(await native.query(pending.id,pending.hash));
   if(s==='COMMITTED')return finishCommitted();if(s==='ABORTED')return restoreBrowser();if(s==='RECOVERY_REQUIRED')return unknown('native-recovery-required',true);
   // PREPARED proves commit did not complete. Query never resends commit.
   return abortKnown('query-prepared-abort');
  }catch(e){return unknown('query-response:'+e.message);}
 }
 async function applyReserved(raw,current,id,builtinDefaults=false){
  let plan,ownerPlan,browser,memory,wire,hash;
  try{
   browser=snapshot();need(keys.every(k=>Object.prototype.hasOwnProperty.call(current.browser,k)),'current-browser-keys');
   for(const r of browser)need(r.raw===current.browser[r.key],'browser-snapshot-conflict');
   plan=builtinDefaults?a3.prepareDefaults(raw,current):a3.prepare(raw,current);need(plan.ok,plan.reason);
   // The stored document can omit Grid. Ask the actual owner; never invent a default.
   const adjusted=JSON.parse(plan.normalizedRaw),gridReferences={};
   for(const [key,value] of Object.entries(adjusted.browser.values))if(key.startsWith('tull.preview.authoring.')&&value!==null){
    const v=JSON.parse(value);if(!Object.prototype.hasOwnProperty.call(v,'grid')){const grid=await owners.currentGrid(key);need(grid&&typeof grid==='object','current-owner-grid-unavailable');v.grid=clone(grid);const updated=JSON.stringify(v);need(a3.validBrowserValue(key,updated),'current-grid-rejects-coordinates');gridReferences[key]=clone(grid);}
   }
   memory=await owners.capture();need(memory!==undefined&&memory!==null,'owner-memory-snapshot');memory=clone(memory);
   ownerPlan=await owners.preflight(clone(plan),clone(gridReferences));need(ownerPlan?.ok&&ownerPlan.geometryUnchanged===true,'owner-grid-relocation-or-rejection');
   ownerPlan={...ownerPlan,currentGrids:clone(gridReferences)};
   wire=wireFor(plan);hash=await sha256(plan.normalizedRaw+'\n'+wire);need(typeof hash==='string'&&/^[A-F0-9]{64}$/.test(hash),'plan-hash');
  }catch(e){
   // No Native request or Browser write has been issued. Verify read-only owner boundaries before releasing.
   try{if(browser)checkStorage(browser);if(memory!==undefined)need(await owners.verifySnapshot(clone(memory)),'validation-owner-side-effect');}
   catch(check){return unknown('validation-side-effect-or-unverified:'+check.message,true);}
   state=reservation.previousState;
   return {ok:false,...status(),reason:'validation:'+e.message,mutated:false};
  }
  pending={id,hash,plan:clone(plan),ownerPlan:clone(ownerPlan),browser,memory,wire,dom:ownerPlan.domVerified===true?'VERIFIED_BY_ADAPTER':'NOT_RUN',sequence:++sequence,expected:browser.map(r=>{const w=plan.browserWrites.find(w=>w.key===r.key);return {key:r.key,raw:w?w.after:r.raw};})};state='PREPARING';
  try{journal?.save(id,hash);const s=accept(await native.prepare(id,hash,plan.normalizedRaw,wire));if(s==='ABORTED'){const rejected=nativeDetails();journal?.clear();state='ABORTED';pending=null;return {ok:false,...status(),...rejected,reason:rejected.nativeReason||'native-prepare-rejected',mutated:false};}if(s==='RECOVERY_REQUIRED')return unknown('native-prepare-recovery',true);need(s==='PREPARED','prepare-state');}
  catch(e){return unknown('prepare-response:'+e.message);}
  state='PREPARED';
  try{
   // Only acknowledged PREPARED reaches a Browser write.
   checkStorage(browser);
   for(const w of plan.browserWrites){need(keys.includes(w.key),'forbidden-browser-key');w.after===null?storage.removeItem(w.key):storage.setItem(w.key,w.after);}
   checkStorage(pending.expected);await owners.apply(clone(plan),clone(ownerPlan));need(await owners.verify(clone(plan),clone(ownerPlan)),'owner-apply-readback');
  }catch(e){return abortKnown('browser-apply:'+e.message);}
  state='BROWSER_APPLIED';
  try{const s=accept(await native.commit(id,hash));if(s==='ABORTED')return restoreBrowser();if(s==='RECOVERY_REQUIRED')return unknown('native-commit-recovery',true);need(s==='COMMITTED','commit-state');return finishCommitted();}
  catch(e){return unknown('commit-response:'+e.message);}
 }
 // Public admissions reserve synchronously before any external callback or await.
 async function executeReserved(action){
  executing=true;let result;
  try{result=await action();}
  catch(e){result=unknown('unexpected-operation:'+e.message,!pending);}
  finally{
   executing=false;
   // A completed call releases its execution lock; unresolved transaction ownership stays reserved.
   if(!pending&&state!=='RECOVERY_REQUIRED')reservation=null;
  }
  return {...result,blocked:status().blocked};
 }
 async function apply(raw,current,id,builtinDefaults=false){
  if(executing||reservation||pending)return {ok:false,...status(),reason:'import-blocked',mutated:false};
  if(usedIds.has(id))return {ok:false,...status(),reason:'request-id-reused',mutated:false};
  if(typeof id!=='string'||!/^[A-Za-z0-9_-]{1,64}$/.test(id))return {ok:false,...status(),reason:'request-id',mutated:false};
  reservation={id,previousState:state};usedIds.add(id);state='VALIDATING';
  return executeReserved(()=>applyReserved(raw,current,id,builtinDefaults));
 }
 async function resolve(){
  if(executing)return {ok:false,...status(),reason:'operation-in-progress'};
  if(!pending)return {ok:false,...status(),reason:state==='RECOVERY_REQUIRED'?'manual-recovery-required':'no-pending-request'};
  return executeReserved(resolveReserved);
 }
 return Object.freeze({apply,applyDefaults:(raw,current,id)=>apply(raw,current,id,true),resolve,status});
}
window.TULLProfileTransactionA4=Object.freeze({create,wireFor});
})();
