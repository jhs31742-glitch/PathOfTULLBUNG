(() => {
'use strict';
const KEY='tull.ui.profileFilePending.v1',need=(v,m)=>{if(!v)throw Error(m);};
function create(d){
 let pending=null,raw=d.storage.getItem(KEY),fault='',busy=false,authority=null;
 try{if(raw!==null){pending=JSON.parse(raw);need(pending&&Object.keys(pending).length===5&&pending.version===1&&/^[A-Za-z0-9_-]{1,64}$/.test(pending.id)&&/^[A-F0-9]{64}$/.test(pending.session)&&['store','remove','copy'].includes(pending.operation)&&d.validName(pending.filename),'file-journal-shape');fault='FILE_RESULT_UNKNOWN';}}catch{fault='FILE_RECOVERY_REQUIRED';}
 const state=()=>({blocked:!!pending||!!fault||busy,state:fault||'IDLE',pending:pending&&{...pending}});
 function clear(){need(d.storage.getItem(KEY)===raw,'file-journal-ownership');d.storage.removeItem(KEY);need(d.storage.getItem(KEY)===null,'file-journal-clear');raw=null;pending=null;fault='';}
 function hello(r){authority=r.fileSession;if(r.filePending){if(!pending&&!fault){pending={version:1,id:r.filePending.requestId,session:authority,operation:r.filePending.operation,filename:r.filePending.filename};fault='FILE_RESULT_UNKNOWN';}else if(pending&&(pending.id!==r.filePending.requestId||pending.session!==authority))fault='FILE_RECOVERY_REQUIRED';}else if(pending){const ack=r.fileAcknowledged;fault=ack&&pending.id===ack.requestId&&pending.session===authority&&pending.operation===ack.operation&&pending.filename===ack.filename?'FILE_RESULT_UNKNOWN':'FILE_RECOVERY_REQUIRED';}}
 async function confirm(){
  need(pending&&pending.session===authority,'file-session-or-journal-recovery-required');
  const q=await d.rpc('fileQuery',pending.id,{fileSession:pending.session});
  need(q.requestId===pending.id&&['FILE_COMMITTED','FILE_ABORTED'].includes(q.state)&&q.fileReceipt?.filename===pending.filename&&q.fileReceipt?.operation===pending.operation,'file-query-recovery-required');
  const a=await d.rpc('fileAck',pending.id,{fileSession:pending.session});need(a.state===q.state,'file-ack-recovery-required');
  const result={ok:true,state:q.state,receipt:q.fileReceipt};clear();return result;
 }
 async function invoke(operation,filename,fields){
  if(state().blocked)throw Error('file-operation-blocked:'+state().state);busy=true;
  try{await d.ready();need(!pending&&!fault,'file-operation-blocked');const id=d.id();pending={version:1,id,session:authority,operation,filename};raw=JSON.stringify(pending);d.storage.setItem(KEY,raw);need(d.storage.getItem(KEY)===raw,'file-journal-readback');
   const r=await d.rpc(operation,id,{filename,...fields});
   if(!r.fileReceipt){need(['READY','ABORTED'].includes(r.state)&&r.status!==0,'file-result-unknown');clear();throw Error('native-status:'+r.status+':'+r.error);}
   const verified=await confirm();need(verified.state==='FILE_COMMITTED'&&r.status===0,'native-status:'+r.status);return r;
  }catch(e){if(pending){fault=/recovery|ownership|journal/i.test(e.message)?'FILE_RECOVERY_REQUIRED':'FILE_RESULT_UNKNOWN';}throw e;}finally{busy=false;}
 }
 async function resolve(){if(busy)return {ok:false,state:state().state,reason:'operation-in-progress'};busy=true;try{await d.ready();return await confirm();}catch(e){fault=/recovery|session|ownership|journal/i.test(e.message)?'FILE_RECOVERY_REQUIRED':'FILE_RESULT_UNKNOWN';return {ok:false,state:fault,reason:e.message};}finally{busy=false;}}
 return Object.freeze({hello,state,invoke,resolve});
}
window.TULLProfileFileGuardA5=Object.freeze({create,key:KEY});
})();
