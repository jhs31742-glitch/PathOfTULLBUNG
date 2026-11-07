(() => {
'use strict';
const stable=x=>Array.isArray(x)?x.map(stable):x&&typeof x==='object'?Object.fromEntries(Object.keys(x).sort().map(k=>[k,stable(x[k])])):x;
const owners=window.TULLProfileOwners,clone=x=>JSON.parse(JSON.stringify(x)),same=(a,b)=>JSON.stringify(stable(a))===JSON.stringify(stable(b));
const keys=window.TULLUIBrowserBackupR2.keys(),baseA3=window.TULLExternalProfileA3;
const emptySlotDefault=key=>{const name=key==='tull.ui.customPalette.v1'?'palette':key==='tull.ui.shared.colors.v1'?'colors':key==='tull.title.lettering.overrides.v1'?'lettering':key==='tull.font.uiTitle.v1'?'font':key.startsWith('tull.preview.authoring.')?'center':key.startsWith('tull.titleGrid.')?'title':'hud';const owner=owners[name];return key==='tull.hudEditor.selectedPreset.v1'?owner.defaults().selectedPreset:JSON.stringify(key.includes('.preset.')?(owner.presetDefaults?.()||owner.defaults()):owner.defaults());};
const a3={...baseA3,prepare:(raw,current)=>baseA3.prepare(raw,current,emptySlotDefault)};
const configs=[['palette',k=>k==='tull.ui.customPalette.v1'],['colors',k=>k==='tull.ui.shared.colors.v1'],['lettering',k=>k==='tull.title.lettering.overrides.v1'],['font',k=>k==='tull.font.uiTitle.v1'],['center',k=>k.startsWith('tull.preview.authoring.')],['title',k=>k.startsWith('tull.titleGrid.')],['hud',k=>k.startsWith('tull.hudEditor.')]];
const importKey='tull.ui.profileImportPending.v1';let importRawMarker=localStorage.getItem(importKey),importMarker=null;
if(importRawMarker!==null){try{importMarker=JSON.parse(importRawMarker);if(!/^[A-Za-z0-9_-]{1,64}$/.test(importMarker.requestId)||!/^[A-F0-9]{64}$/.test(importMarker.planHash))throw Error('marker');}catch{importMarker={state:'RECOVERY_REQUIRED'};}}
let sequence=0,session=null,actionBusy=false,uiBlocked=false,mirror=null,foreignImport=importMarker;const replies=new Map();let lastTransactionReply=null,uiEpoch=0;
const id=()=>('A5_'+Date.now()+'_'+(++sequence));
window.tullProfileReceive=payload=>{let r;try{r=typeof payload==='string'?JSON.parse(payload):payload;}catch{return;}const key=r?.requestId+'|'+r?.operation,wait=replies.get(key);if(!wait)return;replies.delete(key);clearTimeout(wait.timer);if(wait.session&&!['fileQuery','fileAck'].includes(r.operation)&&r.session!==wait.session){wait.reject(Error('session-mismatch'));return;}wait.resolve(r);};
async function rpc(operation,requestId,fields={}){
 if(typeof window.tullProfileCommand!=='function')throw Error('native-profile-bridge-unavailable');
 if(operation==='prepare')window.TULLFTMinimalClient?.beginImport?.();
 const packet={operation,requestId,...fields};if(operation!=='hello')packet.session=session;
 const wire=JSON.stringify(packet);if(new TextEncoder().encode(wire).length>4000000)throw Error('profile-packet-too-large');
 const key=requestId+'|'+operation;if(replies.has(key))throw Error('duplicate-rpc');
 const response=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{replies.delete(key);reject(Error('native-response-timeout'));},10000);replies.set(key,{resolve,reject,timer,session:operation==='hello'?null:session});try{window.tullProfileCommand(wire);}catch(e){clearTimeout(timer);replies.delete(key);reject(e);}});
 if(['prepare','commit','abort','finalize','query'].includes(operation))lastTransactionReply=response;
 if(operation==='hello'){if(!/^[A-F0-9]{64}$/.test(response.session))throw Error('session');session=response.session;fileGuard.hello(response);foreignImport=response.importPending||importMarker||null;}
 return response;
}
const fileGuard=window.TULLProfileFileGuardA5.create({storage:localStorage,validName:a3.validName,rpc,ready:()=>ready(),id});
async function ready(){if(!session)await rpc('hello',id());}
async function current(){await ready();const r=await rpc('snapshot',id());if(r.state!=='READY'||r.status!==0||!r.snapshot)throw Error('native-snapshot');return {browser:Object.fromEntries(keys.map(k=>[k,localStorage.getItem(k)])),native:r.snapshot.native,titleState:r.snapshot.titleState};}
const changed=(plan,name)=>plan.browserWrites.filter(w=>configs.find(x=>x[0]===name)[1](w.key));
const ownerAPI={
 async capture(){const s={};for(const [name] of configs){if(!owners[name])throw Error('owner-missing:'+name);s[name]=clone(owners[name].capture());}return s;},
 async currentGrid(){return clone(owners.center.currentGrid());},
 async preflight(plan,grids){for(const [name] of configs){if(owners[name].busy())throw Error('owner-editor-busy:'+name);for(const w of changed(plan,name)){if(w.after!==null&&!(name==='center'&&w.key.includes('.preset.')))owners[name].preflight?.(w.after);}}const warnings=[...plan.warnings];const missing=owners.font.missing(plan.fontTransfer.references);for(const ref of missing)warnings.push('font-file-missing:'+ref);return {ok:true,geometryUnchanged:true,domVerified:false,warnings};},
 async apply(plan){for(const [name] of configs)if(changed(plan,name).length)owners[name].apply();},
 async applyNativeMirror(snapshot){if(!snapshot?.native)throw Error('native-mirror-missing');mirror=clone(snapshot);if(typeof settingsState!=='undefined'&&settingsState)Object.assign(settingsState,snapshot.native);applySettingsVisuals();if(typeof titleState!=='undefined'&&titleState?.customTitles){const records=snapshot.titleState.customTitles;for(let i=0;i<20;i++){if(titleState.customTitles[i])Object.assign(titleState.customTitles[i],{before:records[i].before,after:records[i].after});}}window.TULLTitleAuthoringProductionR1?.refresh();},
 async verify(plan){for(const [name] of configs)for(const w of changed(plan,name)){const o=owners[name],m=o.capture();if(name==='palette'&&(!same(m,o.expected(w.after))||!o.verify()))return false;if(name==='colors'&&!same(m.savedColors,o.expected(w.after)))return false;if(name==='lettering'&&(!same(m,o.expected(w.after))||!o.verify()))return false;if(name==='font'&&(!same(m.state,o.expected(w.after))||!o.verify()))return false;if((name==='center'||name==='title')&&w.key.includes('.working.')&&!o.verify(w.after))return false;if(name==='hud'&&!o.verify())return false;}return true;},
 async restore(snapshot){let failed=false;for(const [name] of [...configs].reverse())try{owners[name].restore(clone(snapshot[name]));}catch{failed=true;}if(failed)throw Error('owner-restore');},
 async verifySnapshot(snapshot){for(const [name] of configs)if(!same(owners[name].capture(),snapshot[name]))return false;return true;}
};
const native={};for(const op of ['prepare','commit','abort','query','finalize'])native[op]=async(requestId,planHash,normalizedRaw,wire)=>{await ready();const fields={planHash};if(op==='prepare')Object.assign(fields,{normalizedRaw,wire});return rpc(op,requestId,fields);};
const sha256=async value=>{if(!window.crypto?.subtle){await ready();const reply=await rpc('hash',id(),{raw:value});if(reply.state!=='READY'||reply.status!==0||!/^[A-F0-9]{64}$/.test(reply.planHash))throw Error('native-sha256-unavailable');return reply.planHash;}const hash=await window.crypto.subtle.digest('SHA-256',new TextEncoder().encode(value));return [...new Uint8Array(hash)].map(x=>x.toString(16).padStart(2,'0')).join('').toUpperCase();};
const importJournal={save(requestId,planHash){if(localStorage.getItem(importKey)!==importRawMarker)throw Error('import-marker-ownership');importMarker={requestId,planHash,session};importRawMarker=JSON.stringify(importMarker);localStorage.setItem(importKey,importRawMarker);if(localStorage.getItem(importKey)!==importRawMarker)throw Error('import-marker-readback');},clear(){if(localStorage.getItem(importKey)!==importRawMarker)throw Error('import-marker-ownership');localStorage.removeItem(importKey);if(localStorage.getItem(importKey)!==null)throw Error('import-marker-clear');importRawMarker=null;importMarker=null;}};
const tx=window.TULLProfileTransactionA4.create({a3,keys,storage:localStorage,owners:ownerAPI,native,sha256,journal:importJournal});
const file=a3.create({defaultBrowser:emptySlotDefault,invoke:async request=>{await ready();const {operation,requestId,...fields}=request;return rpc(operation,requestId,fields);}});
const checkedReply=async(operation,fields)=>{const r=await rpc(operation,id(),fields);if(r.state==='RESULT_UNKNOWN')throw Error('native-result-unknown');if(r.state!=='READY'||r.status!==0)throw Error('native-status:'+r.status+':'+(r.error||r.state));return r;};
const manager=window.TULLProfileManagerA5.create({storage:localStorage,a3,keys,nativeKeys:window.TULLUIExternalProfileContractR1.nativeKeys(),owners,sha256,current,blocked:()=>tx.status().blocked||!!foreignImport,fileBlocked:()=>fileGuard.state().blocked,fileState:()=>fileGuard.state().state,resolveFiles:()=>fileGuard.resolve(),
 nativeDefaults:async()=>{await ready();return (await checkedReply('defaults',{})).nativeDefaults;},
 list:async()=>{const r=await file.list(id());if(!r.ok)throw Error(r.reason);return r.files;},
 read:async filename=>{const r=await file.readFile(filename,id());if(!r.ok){const e=Error(r.nativeReason||r.reason);e.profileResult=r;throw e;}return r.raw;},
 infoHash:async filename=>(await checkedReply('fileInfo',{filename})).fileHash,
 store:(filename,raw,expectedHash)=>fileGuard.invoke('store',filename,{raw,expectedHash}),remove:(filename,expectedHash)=>fileGuard.invoke('remove',filename,{expectedHash}),exportCopy:(filename,raw,expectedHash='ABSENT')=>fileGuard.invoke('copy',filename,{raw,expectedHash}),
 apply:(raw,c,builtin)=>builtin?tx.applyDefaults(raw,c,id()):tx.apply(raw,c,id()),resolve:()=>tx.resolve()});
const resolveTransaction=async()=>{if(foreignImport){if(!foreignImport.requestId||!foreignImport.planHash)return {ok:false,state:'RECOVERY_REQUIRED',reason:'이전 Import 추적 정보 손상 · 수동 복구 확인 필요'};const r=await rpc('query',foreignImport.requestId,{planHash:foreignImport.planHash});return {ok:false,state:'RECOVERY_REQUIRED',reason:'View 재생성 전 요청: '+r.state+' · 이전 Browser 메모리/DOM 원복 근거 없음. 재커밋과 자동 확정을 차단합니다.'};}return manager.state().pendingTarget?manager.resolve():tx.resolve();};
// Normal UI exposes four tasks; diagnostics remain available through TULLProfilesA5.
function failure(action,r){
 const t=tx.status(),native=lastTransactionReply;const clean=x=>String(x||'NONE').replace(/[^A-Za-z0-9_.:|/-]/g,'_').slice(0,256);
 const entry={action:clean(action),reason:clean(r.nativeReason||r.reason||'unclassified-error'),stage:clean(/^(prepare|commit|finalize|abort|query)-response/.exec(r.reason||'')?.[1]||((r.reason||'').startsWith('validation:')?'validation':native?.operation||'manager-or-file-validation')),nativeState:clean(r.nativeState||native?.state||'NOT_REQUESTED'),requestId:clean(r.requestId||t.requestId||native?.requestId||'NOT_REQUESTED'),blockedCause:clean(t.blocked?'import:'+t.state:fileGuard.state().blocked?'file:'+fileGuard.state().state:'NONE')};
 try{localStorage.setItem('tull.ui.profileLastFailure.v1',JSON.stringify({version:1,...entry}));}catch{}
 void ready().then(()=>rpc('diagnostic',id(),{raw:JSON.stringify(entry)})).catch(()=>{});
}
const resultKey='tull.ui.profileLastResult.v1';let message='',lastUI={};
try{const saved=JSON.parse(localStorage.getItem(resultKey)||'null');if(saved?.version===1&&typeof saved.message==='string'&&saved.message.length<=160){message=saved.message;lastUI=saved;}}catch{}
function show(text){message=text;try{localStorage.setItem(resultKey,JSON.stringify({version:1,message:text,operation,saveMode,exportMode}));}catch{}const node=document.querySelector('#s20-external-profile [data-status]');if(node)node.textContent=text;}
function friendly(r,success='불러오기 완료'){
 if(r.ok&&r.fileError)return '파일 확인 필요';
 if(r.ok&&r.metaError)return '목록 정보 확인 필요';
 if(r.ok)return r.metadataSaved===false||r.trackingError?'설정 완료 — 목록 표시 확인 필요':success;
 if(r.state==='RECOVERY_REQUIRED')return '설정 복구 확인 필요';
 if(r.state==='RESULT_UNKNOWN')return '불러오기 결과 확인 필요';
 if(r.state?.startsWith('FILE_')||fileGuard.state().blocked)return '파일 확인 필요';
 if(/profile-already-exists/.test(r.reason||''))return '같은 이름의 파일이 있습니다. 덮어쓰기를 선택하십시오.';
 if(/builtin-read-only/.test(r.reason||''))return '기본값은 덮어쓸 수 없습니다. 새 이름으로 저장하십시오.';
 if(/export-selection/.test(r.reason||''))return '덮어쓸 JSON 파일을 먼저 선택하십시오.';
 if(/import-filename/.test(r.reason||''))return '불러올 JSON 파일을 먼저 선택하십시오.';
 if(/profile-not-found|profile-selection/.test(r.reason||''))return '저장된 설정을 먼저 선택하십시오.';
 if(/export-name-too-long/.test(r.reason||''))return '파일 이름을 줄여 주십시오 (한글 약 11자 이내).';
 if(/export-filename|filename/.test(r.reason||''))return '파일 이름을 확인하십시오 — 경로 문자는 사용할 수 없습니다.';
 if(/display-name/.test(r.reason||''))return '저장할 이름을 입력하십시오.';
 if(/operation-in-progress|owner-editor-busy/.test(r.reason||''))return '편집 또는 파일 확인이 끝난 뒤 다시 시도하십시오.';
 if(/central-.*unavailable/.test(r.reason||''))return '홈 화면이 준비된 뒤 다시 시도하십시오.';
 if(/central-|grid|current-preset-invalid/.test(r.reason||''))return '중앙 배치 프리셋 확인 필요 — 기존 설정 유지';
 if(/browser-field|json|profile-shape|size-or-encoding/.test(r.reason||''))return '설정 파일의 형식을 확인하십시오 — 기존 설정 유지';
 if(/ini-|post-readback|snapshot-conflict/.test(r.nativeReason||r.reason||''))return '설정 파일 적용 실패 — 기존 설정 유지';
 if(/font-file-missing/.test(r.reason||''))return '글꼴 파일 확인 필요';
 if(/derived-acquisition-change|absent-title-creation/.test(r.reason||''))return '기존 사용자 칭호 확인 필요 — 기존 설정 유지';
 return '실패 — 기존 설정 유지';
}
function showResult(r){show(friendly(r));}
function block(value){uiBlocked=value;let cover=document.getElementById('s20-profile-busy');if(!cover){cover=document.createElement('div');cover.id='s20-profile-busy';cover.style.cssText='position:fixed;inset:0;z-index:2147483000;background:rgba(0,0,0,.25);pointer-events:auto';cover.innerHTML='<div style="margin:20vh auto;padding:24px;max-width:520px;background:var(--paper);color:var(--ink)"><p data-label></p><button type="button" data-query>다시 확인</button></div>';cover.querySelector('[data-query]').onclick=()=>void perform(async()=>{const r=await resolveTransaction();showResult(r);return r;},true);document.body.append(cover);}cover.hidden=!value;cover.querySelector('[data-label]').textContent=(tx.status().blocked||foreignImport)?'설정 결과 확인 필요':'설정 처리 중';cover.querySelector('[data-query]').hidden=actionBusy;}
function controlsBusy(){const c=document.getElementById('s20-external-profile');if(c)for(const b of c.querySelectorAll('button'))b.disabled=actionBusy;}
async function perform(action,resolving=false,kind=operation){if(actionBusy)return {ok:false,reason:'operation-in-progress'};if((tx.status().blocked||foreignImport)&&!resolving){const r={ok:false,state:tx.status().state,reason:'import-blocked'};failure(kind,r);show(friendly(r));return r;}actionBusy=true;uiEpoch++;lastTransactionReply=null;show('처리 중');controlsBusy();try{const r=await action();if(r?.ok===false)failure(kind,r);else if(message==='처리 중')show('');return r;}catch(e){const r={ok:false,reason:e.message};failure(kind,r);show(friendly(r));return r;}finally{actionBusy=false;controlsBusy();block(tx.status().blocked||!!foreignImport);window.TULLFTMinimalClient?.endImport?.(!tx.status().blocked&&!foreignImport);}}
async function importRaw(raw){return perform(async()=>{block(true);const c=await current(),plan=a3.prepare(raw,c);if(!plan.ok)throw Error(plan.reason);const r=await tx.apply(raw,c,id());showResult(r);if(r.ok&&owners.font.missing(plan.fontTransfer.references).length)show('불러오기 완료 — 글꼴 파일 없음, 기본 글꼴 표시');return r;},false,'import_raw');}
let operation=['save','load','export','import'].includes(lastUI.operation)?lastUI.operation:'save',saveMode=lastUI.saveMode==='existing'?'existing':'new',exportMode=lastUI.exportMode==='existing'?'existing':'new';
const cardNode=()=>document.getElementById('s20-external-profile');
function fillProfiles(){const card=cardNode();if(!card)return;const s=manager.state();for(const select of card.querySelectorAll('[data-profiles]')){const prev=select.value;select.replaceChildren(...s.profiles.filter(p=>select.hasAttribute('data-users')?!p.builtin:true).map(p=>{const o=document.createElement('option');o.value=p.id;o.textContent=p.builtin?'기본값':p.displayName;return o;}));if([...select.options].some(o=>o.value===prev))select.value=prev;else if([...select.options].some(o=>o.value===s.selected))select.value=s.selected;}paint();}
function paint(){const card=cardNode();if(!card)return;for(const section of card.querySelectorAll('[data-section]'))section.hidden=section.dataset.section!==operation;for(const b of card.querySelectorAll('[data-task]'))b.setAttribute('aria-pressed',String(b.dataset.task===operation));card.querySelector('[data-save-new]').hidden=saveMode!=='new';card.querySelector('[data-save-existing]').hidden=saveMode!=='existing';card.querySelector('[data-export-new]').hidden=exportMode!=='new';card.querySelector('[data-export-existing]').hidden=exportMode!=='existing';card.querySelector('[data-file-recheck]').hidden=!fileGuard.state().blocked;}
async function refreshFiles(){const r=await file.list(id());if(!r.ok)throw Error(r.reason);const card=cardNode();if(card)for(const select of card.querySelectorAll('[data-files]')){const prior=select.value;select.replaceChildren(...r.files.filter(name=>!name.startsWith('p_')).map(name=>{const o=document.createElement('option');o.value=name;o.textContent=window.TULLProfileManagerA5.displayFilename(name);return o;}));if(r.files.includes(prior))select.value=prior;}return r;}
async function recoverFiles(){if(!fileGuard.state().blocked)return {ok:true};const r=await manager.resolveFiles();show(r.ok?(r.state==='FILE_COMMITTED'?(r.receipt.operation==='copy'?'내보내기 완료':'저장 완료'):'실패 — 기존 파일 유지'):'파일 확인 필요');paint();return r;}
async function profileAction(fn,success,kind=operation){const r=await perform(async()=>{block(true);if(fileGuard.state().blocked)await recoverFiles();const r=await fn();show(friendly(r,success));if(r.ok&&success==='내보내기 완료'&&r.filename)show('내보내기 완료 — '+r.filename);if(r.ok&&success==='불러오기 완료'){const c=await current(),out=a3.exportFull(c),p=out.ok?a3.prepare(out.raw,c):null;if(p?.ok&&owners.font.missing(p.fontTransfer.references).length)show('불러오기 완료 — 글꼴 파일 없음, 기본 글꼴 표시');}return r;},false,kind);fillProfiles();return r;}
function mount(){if(currentTab!=='settings'||!window.TULLUIBrowserBackupPanelR1.eligible()||cardNode())return;
 if(!document.getElementById('s20-profile-manager-style')){const st=document.createElement('style');st.id='s20-profile-manager-style';st.textContent='#s20-external-profile{display:grid;gap:12px}#s20-external-profile [hidden]{display:none!important}#s20-external-profile .pm-actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}#s20-external-profile .pm-row{display:flex;gap:8px;flex-wrap:wrap;align-items:end}#s20-external-profile label{display:grid;gap:6px;flex:1;min-width:160px}#s20-external-profile button,#s20-external-profile input,#s20-external-profile select{font:inherit;color:var(--ink);background:var(--card-bg);border:1px solid var(--card-line);padding:8px;min-height:36px;max-width:100%;box-sizing:border-box}#s20-external-profile [data-task][aria-pressed=true]{border-color:var(--gold);box-shadow:inset 0 -2px var(--gold)}#s20-external-profile section{display:grid;gap:10px}#s20-external-profile p{margin:0}';document.head.append(st);}
 const card=document.createElement('div');card.id='s20-external-profile';card.className='card';card.innerHTML=
 '<div class="cardTitle">전체 꾸미기 설정</div><div class="pm-actions" role="group" aria-label="전체 꾸미기 설정 작업"><button data-task="save">전체 설정 저장하기</button><button data-task="load">전체 설정 불러오기</button><button data-task="export">전체 설정 외부로 내보내기</button><button data-task="import">전체 설정 외부파일 불러오기</button></div>'+
 '<section data-section="save"><label>저장 방법<select data-save-mode><option value="new">새 이름으로 저장</option><option value="existing">기존 저장 덮어쓰기</option></select></label><div data-save-new class="pm-row"><label>저장 이름<input data-display data-profile-display maxlength="80"></label><button data-save-as>저장하기</button></div><div data-save-existing class="pm-row"><label>저장된 설정<select data-profiles data-users data-save-target></select></label><button data-save-refresh>새로고침</button><button data-save>덮어쓰기</button></div></section>'+
 '<section data-section="load"><div class="pm-row"><label>저장된 설정<select data-profiles data-load-target></select></label><button data-refresh>새로고침</button><button data-apply>불러오기 실행</button></div></section>'+
 '<section data-section="export"><label>내보내기 방법<select data-export-mode><option value="new">새 JSON 파일</option><option value="existing">기존 JSON 덮어쓰기</option></select></label><div data-export-new class="pm-row"><label>JSON 파일명 (비우면 자동 이름)<input data-name data-profile-display placeholder="예: 내 꾸미기" maxlength="80"></label></div><div data-export-existing class="pm-row"><label>기존 JSON 파일<select data-files data-export-target></select></label><button data-export-refresh>새로고침</button></div><button data-export>내보내기 실행</button><p class="small">현재 적용된 꾸미기 설정을 문서의 UIProfiles 폴더에 저장합니다. 글꼴 파일은 포함하지 않습니다.</p></section>'+
 '<section data-section="import"><div class="pm-row"><label>JSON 파일<select data-files data-import-target></select></label><button data-list>새로고침</button><button data-import>불러오기 실행</button></div><p class="small">UIProfiles 폴더에 복사한 JSON을 선택하십시오. 검증 후 적용하며 게임 진행은 유지합니다.</p></section>'+
 '<p data-status role="status"></p><button data-file-recheck hidden>파일 다시 확인</button>';
 const panel=document.getElementById('centerPanel');panel.insertBefore(card,[...panel.children].find(n=>n.classList.contains('card'))||null);window.TULLImeControls?.attach(card);card.querySelector('[data-status]').textContent=message;
 for(const b of card.querySelectorAll('[data-task]'))b.onclick=()=>{operation=b.dataset.task;uiEpoch++;show('');paint();if(operation==='import'||operation==='export')void perform(refreshFiles);};
 card.querySelector('[data-save-mode]').value=saveMode;card.querySelector('[data-save-mode]').onchange=e=>{saveMode=e.target.value;show('');paint();};card.querySelector('[data-export-mode]').value=exportMode;card.querySelector('[data-export-mode]').onchange=e=>{exportMode=e.target.value;show('');paint();if(exportMode==='existing')void perform(refreshFiles);};
 for(const attr of ['data-refresh','data-save-refresh'])card.querySelector('['+attr+']').onclick=()=>void profileAction(()=>manager.refresh(),'목록 갱신 완료');
 card.querySelector('[data-save-as]').onclick=()=>void profileAction(()=>manager.saveAs(card.querySelector('[data-display]').value),'저장 완료','save_new');
 card.querySelector('[data-save]').onclick=()=>void profileAction(()=>{const id=card.querySelector('[data-save-target]').value;const r=manager.select(id);return r.ok?manager.save():r;},'저장 완료','save_overwrite');
 card.querySelector('[data-apply]').onclick=()=>void profileAction(()=>{const r=manager.select(card.querySelector('[data-load-target]').value);return r.ok?manager.apply():r;},'불러오기 완료',card.querySelector('[data-load-target]').value==='@default'?'load_default':'load_saved');
 for(const attr of ['data-list','data-export-refresh'])card.querySelector('['+attr+']').onclick=()=>void perform(async()=>{await refreshFiles();show('목록 갱신 완료');return {ok:true};});
 card.querySelector('[data-export]').onclick=()=>void profileAction(()=>{const name=exportMode==='existing'?card.querySelector('[data-export-target]').value:card.querySelector('[data-name]').value;return manager.exportCurrent(name,exportMode==='existing');},'내보내기 완료','export_'+exportMode);
 card.querySelector('[data-import]').onclick=()=>void profileAction(()=>manager.applyFile(card.querySelector('[data-import-target]').value),'불러오기 완료','import_file');
 card.querySelector('[data-file-recheck]').onclick=()=>void perform(recoverFiles,true);
 if(!actionBusy){actionBusy=true;controlsBusy();const mountEpoch=uiEpoch;void (async()=>{try{await ready();if(fileGuard.state().blocked)await recoverFiles();const listed=await manager.refresh();if((listed.fileError||listed.metaError)&&uiEpoch===mountEpoch)show('파일 확인 필요');fillProfiles();}catch{if(uiEpoch===mountEpoch)show('파일 확인 필요');}finally{actionBusy=false;controlsBusy();block(tx.status().blocked||!!foreignImport);}})();}fillProfiles();
}
const oldRender=renderSettings;renderSettings=function(...args){const r=oldRender.apply(this,args);mount();return r;};
// Freeze other UI writers while a transaction owns the snapshot; do not alter BrowserBackupR2 implementation.
for(const event of ['click','change','input','pointerdown','keydown'])window.addEventListener(event,e=>{if(uiBlocked&&!e.target?.closest?.('#s20-profile-busy')){e.preventDefault();e.stopImmediatePropagation();}},true);
window.TULLProfilesA5=Object.freeze({importRaw,resolve:()=>perform(async()=>{const r=await resolveTransaction();showResult(r);return r;},true),status:()=>foreignImport?{state:'RECOVERY_REQUIRED',blocked:true,...foreignImport}:tx.status(),fileStatus:fileGuard.state,uiBusy:()=>actionBusy,owners:ownerAPI,current,file,rpc,manager});mount();
})();
