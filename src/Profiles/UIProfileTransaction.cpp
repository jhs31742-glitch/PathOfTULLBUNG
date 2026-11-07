// A5 derived integration: normal translation units, A2-owned fixed INI facade.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ProfileFileIO.h"
#include "FixedIniTransaction.h"
#include "ProfileValidation.h"
#include "UIProfileTransaction.h"
#include "RuntimeBoundary.h"
#include "UISettings.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include "TitleSystem.h"
#include <bcrypt.h>
#include <sstream>
#include <map>
#include <set>
#include <memory>
#include <cmath>
#include <iostream>
#include <iomanip>
namespace TULLBUNG::UIProfiles::Detail {
namespace io=TULLBUNG::ProfileFileIO;
namespace ui=TULLBUNG::UISettings;
namespace ts=TULLBUNG::TitleSystem;
using Fields=std::map<std::string,std::string>;
std::string Sha(const std::string& s){
 BCRYPT_ALG_HANDLE a=nullptr; BCRYPT_HASH_HANDLE h=nullptr; DWORD cb=0,n=0; std::string out;
 if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("sha-open");
 BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&n),sizeof(n),&cb,0);
 std::vector<unsigned char> obj(n),digest(32);
 bool ok=BCryptCreateHash(a,&h,obj.data(),n,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())),static_cast<ULONG>(s.size()),0)>=0;
 if(ok)ok=BCryptFinishHash(h,digest.data(),32,0)>=0;
 if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);
 if(!ok)throw std::runtime_error("sha-failed");
 for(auto b:digest){out+="0123456789ABCDEF"[b>>4];out+="0123456789ABCDEF"[b&15];}return out;
}
std::string Hex(const std::string& s){std::string r;for(unsigned char c:s){r+="0123456789abcdef"[c>>4];r+="0123456789abcdef"[c&15];}return r;}
std::string Unhex(const std::string& s){if(s.size()%2||s.size()>2000000)throw std::runtime_error("hex-size");std::string r;auto d=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("hex-digit");};for(size_t i=0;i<s.size();i+=2)r+=static_cast<char>((d(s[i])<<4)|d(s[i+1]));return r;}
std::string Num(float v){std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v;return o.str();}
Fields Values(const ui::Settings& s){
 Fields f{{"themeId",s.themeId},{"uiBackgroundOpacity",Num(s.uiBackgroundOpacity)},{"hudMaster",s.hud.masterVisible?"1":"0"}};
 auto hud=[&](const char* p,const ui::HudElementSettings& h){std::string k=p;f[k+"Visible"]=h.visible?"1":"0";f[k+"X"]=Num(h.x);f[k+"Y"]=Num(h.y);f[k+"Scale"]=Num(h.scale);f[k+"Opacity"]=Num(h.opacity);};
 hud("hudVoid",s.hud.voidHud);hud("hudComposure",s.hud.composureHud);hud("hudQuiet",s.hud.quietProcessingHud);return f;
}
Fields All(const ui::Settings& s){auto f=Values(s);f["schemaVersion"]=std::to_string(s.schemaVersion);f["developerMode"]=s.developerMode?"1":"0";f["menuKey"]=std::to_string(s.menuHotkey.keyCode);f["menuShift"]=s.menuHotkey.shift?"1":"0";f["menuCtrl"]=s.menuHotkey.ctrl?"1":"0";f["menuAlt"]=s.menuHotkey.alt?"1":"0";return f;}
std::string Serialize(const ui::Settings& s){std::string r;for(auto& [k,v]:All(s))r+=k+"="+v+"\n";return r;}
ui::Settings Parse(const std::string& raw){ui::Settings s{};std::istringstream in(raw);std::string line;while(std::getline(in,line)){auto v=ui::TransactionCodec::Trim(line);if(v.empty()||v[0]=='#'||v[0]==';')continue;auto p=v.find('=');if(p!=v.npos)ui::TransactionCodec::ApplyKeyValue(s,v.substr(0,p),v.substr(p+1));}if(s.schemaVersion!=ui::kSchemaVersion)throw std::runtime_error("ini-schema");ui::TransactionCodec::Validate(s);return s;}
std::string Patch(const std::string& raw,const Fields& updates){
 std::set<std::string> seen;std::string out;size_t p=0;
 while(p<raw.size()){auto e=raw.find('\n',p);auto end=e==raw.npos?raw.size():e+1;auto line=raw.substr(p,end-p);auto v=ui::TransactionCodec::Trim(line);auto eq=v.find('=');std::string key=eq==v.npos?"":std::string(ui::TransactionCodec::Trim(v.substr(0,eq)));
 if(!v.empty()&&v[0]!='#'&&v[0]!=';'&&updates.contains(key)){if(!seen.insert(key).second)throw std::runtime_error("duplicate-ini-key");out+=key+"="+updates.at(key)+(line.ends_with("\r\n")?"\r\n":line.ends_with("\n")?"\n":"");}else out+=line;p=end;}
 for(auto& [k,v]:updates)if(!seen.contains(k)){if(!out.empty()&&out.back()!='\n')out+='\n';out+=k+"="+v+"\n";}return out;
}
struct ProtectedState {ts::PersistedState persisted;std::vector<uint16_t> acquired,batch;std::array<bool,20> derived;std::array<bool,20> present,favorite;
 bool operator==(const ProtectedState& o)const{if(persisted.nickname!=o.persisted.nickname||persisted.favoriteTitleIDs!=o.persisted.favoriteTitleIDs||acquired!=o.acquired||batch!=o.batch||derived!=o.derived||present!=o.present||favorite!=o.favorite)return false;for(size_t i=0;i<6;i++){const auto& a=persisted.slots[i];const auto& b=o.persisted.slots[i];if(a.active!=b.active||a.role!=b.role||a.selectedTitleID!=b.selectedTitleID||a.selectedSegmentIndex!=b.selectedSegmentIndex||a.autoEnabled!=b.autoEnabled)return false;}return true;}};
ProtectedState Protected(const ts::Runtime& t){ProtectedState p;p.persisted=t.MakePersistedState();p.acquired=t.AcquiredTitleIDs();p.batch.assign(t.LastAcquisitionBatch().begin(),t.LastAcquisitionBatch().end());auto c=t.MakeCustomTitleRecords();for(size_t i=0;i<20;i++){p.derived[i]=t.IsAcquired(static_cast<uint16_t>(1000+i));p.present[i]=c[i].present;p.favorite[i]=c[i].favorite;}return p;}
struct FullTitles {ProtectedState protectedState;ts::CustomTitleRecords records;bool operator==(const FullTitles& o)const{if(!(protectedState==o.protectedState))return false;for(size_t i=0;i<20;i++){const auto& a=records[i];const auto& b=o.records[i];if(a.present!=b.present||a.favorite!=b.favorite||a.before!=b.before||a.after!=b.after)return false;}return true;}};
FullTitles Titles(const ts::Runtime& t){return {Protected(t),t.MakeCustomTitleRecords()};}
// A read-only copy of the existing A4 rollback snapshot, retained across lost
// responses/destruction until verified finalization/abort or a new load boundary.
struct SaveViewState {std::shared_ptr<const FullTitles> titles;const void* owner=nullptr;};
SaveViewState& SaveState(){static SaveViewState state;return state;}
struct TitleEdit{uint16_t id;std::string before,after;};
struct Faults{std::string at;bool diskRecovery=false,memoryRecovery=false,titleRecovery=false;};
class Transaction {
 io::FixedIniTransaction disk; ui::Settings old{},next{}; ts::Runtime& titles;
 ts::CustomTitleRecords oldTitles{};ProtectedState oldProtected;FullTitles oldFullTitles;std::vector<TitleEdit> edits;
 Fields writes;bool dirty=false,retained=false;
 bool OriginalDisk(){return disk.OriginalMatches();}
 bool SnapshotMatches(){return All(ui::Get())==All(old)&&Titles(titles)==oldFullTitles&&OriginalDisk();}
 bool TargetMatches(){auto data=disk.Current();if(data!=disk.After()||All(Parse(data))!=All(next)||All(ui::Get())!=All(next)||Protected(titles)!=oldProtected)return false;for(auto& e:edits){auto* t=titles.GetCustomTitle(e.id);if(!t||t->before!=e.before||t->after!=e.after)return false;}auto actual=titles.MakeCustomTitleRecords();for(size_t i=0;i<20;i++){auto expected=oldTitles[i];for(const auto& e:edits)if(e.id==1000+i){expected.before=e.before;expected.after=e.after;}if(actual[i].present!=expected.present||actual[i].favorite!=expected.favorite||actual[i].before!=expected.before||actual[i].after!=expected.after)return false;}return true;}
 public:
 FullTitles SaveBaseline()const{return oldFullTitles;}
 bool finalized=false;
 std::string state="IDLE",error,id,hash;DWORD osError=0;Faults fault;
 explicit Transaction(ts::Runtime& t):titles(t){}
 ~Transaction()=default; // no blind recovery/cleanup after loss of session authority
 bool Prepare(const std::filesystem::path& fixedRoot,const std::string& request,const std::string& planHash,const std::string& normalized,const std::string& wire){
  if(state!="IDLE"){error="busy";return false;}
  id=request;hash=planHash;
  try{
   if(id.empty()||id.size()>64||!std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';})||hash!=Sha(normalized+"\n"+wire))throw std::runtime_error("identity-or-plan-hash");
   if(normalized.size()>1000000||wire.size()>1000000)throw std::runtime_error("plan-size");
   old=ui::Get();next=old;oldTitles=titles.MakeCustomTitleRecords();oldProtected=Protected(titles);oldFullTitles=Titles(titles);
   auto typed=POT::Precode::ValidatePlan(normalized,wire,planHash,{old,oldTitles});
   std::string trusted;for(const auto& change:typed.native){auto scalar=[](const POT::Precode::Scalar& v){return std::visit([](const auto& x)->std::string{using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,std::string>)return x;else if constexpr(std::is_same_v<T,bool>)return x?"1":"0";else return Num(x);},v);};trusted+=change.key+"="+Hex(scalar(change.before))+":"+Hex(scalar(change.after))+"\n";}
   trusted+="--TITLES--\n";for(const auto& t:typed.titles)trusted+=std::to_string(t.id)+":"+Hex(t.beforePrefix)+":"+Hex(t.beforeSuffix)+":"+Hex(t.afterPrefix)+":"+Hex(t.afterSuffix)+"\n";
   std::istringstream in(trusted);std::string line;bool section=false;std::set<uint16_t> ids;
   auto allowed=Values(old);
   while(std::getline(in,line)){
    if(line=="--TITLES--"){if(section)throw std::runtime_error("wire-section");section=true;continue;}
    if(!section){auto p=line.find('=');if(p==line.npos)throw std::runtime_error("wire-native");auto k=line.substr(0,p),encoded=line.substr(p+1);auto colon=encoded.find(':');if(colon==encoded.npos)throw std::runtime_error("native-before-missing");auto previous=Unhex(encoded.substr(0,colon)),v=Unhex(encoded.substr(colon+1));auto baseline=old;ui::TransactionCodec::ApplyKeyValue(baseline,k,previous);if(Values(baseline)!=Values(old))throw std::runtime_error("native-before-conflict");if(!allowed.contains(k)||writes.contains(k))throw std::runtime_error("native-forbidden-or-duplicate");
     if(k=="themeId"){if(v!="default"&&v!="dark-gold")throw std::runtime_error("theme-value");}
     else if(k=="hudMaster"||k.ends_with("Visible")){if(v!="0"&&v!="1")throw std::runtime_error("bool-value");}
     else{float n; if(!ui::TransactionCodec::ParseFloat(v,n)||!std::isfinite(n)||n<(k.ends_with("Scale")?.5F:0.F)||n>(k.ends_with("Scale")?2.F:1.F))throw std::runtime_error("float-range");}
     writes[k]=v;ui::TransactionCodec::ApplyKeyValue(next,k,v);
    }else{
     auto p=line.find(':'),q=line.find(':',p==line.npos?0:p+1);if(p==line.npos||q==line.npos)throw std::runtime_error("title-wire");unsigned value=0;auto n=line.substr(0,p);auto parsed=std::from_chars(n.data(),n.data()+n.size(),value);if(parsed.ec!=std::errc{}||parsed.ptr!=n.data()+n.size()||value<1000||value>1019)throw std::runtime_error("title-id");auto tid=static_cast<uint16_t>(value);if(!ids.insert(tid).second||!titles.GetCustomTitle(tid))throw std::runtime_error("title-create-or-duplicate");auto rest=line.substr(q+1);auto r=rest.find(':'),z=rest.find(':',r==rest.npos?0:r+1);if(r==rest.npos||z==rest.npos)throw std::runtime_error("title-before-missing");auto prevBefore=Unhex(line.substr(p+1,q-p-1)),prevAfter=Unhex(rest.substr(0,r));auto* current=titles.GetCustomTitle(tid);if(current->before!=prevBefore||current->after!=prevAfter)throw std::runtime_error("title-before-conflict");auto b=Unhex(rest.substr(r+1,z-r-1)),a=Unhex(rest.substr(z+1));if(!io::ValidUtf8(b)||!io::ValidUtf8(a)||std::any_of(b.begin(),b.end(),[](unsigned char c){return (c<32&&c!=9&&c!=10&&c!=13)||c==127;})||std::any_of(a.begin(),a.end(),[](unsigned char c){return (c<32&&c!=9&&c!=10&&c!=13)||c==127;}))throw std::runtime_error("title-encoding");auto probe=titles;if(!probe.UpsertCustomTitle(tid,b,a)||Protected(probe)!=oldProtected)throw std::runtime_error("title-invalid");edits.push_back({tid,b,a});
    }
   }
   if(!section)throw std::runtime_error("wire-section-missing");
   auto checked=next;ui::TransactionCodec::Validate(checked);if(All(checked)!=All(next))throw std::runtime_error("owner-would-normalize");
   if(!disk.Open(fixedRoot))throw std::runtime_error("ini-root-or-read");
   if(disk.Existed()&&All(Parse(disk.Before()))!=All(old))throw std::runtime_error("ini-memory-baseline-diverged");
   disk.SetAfter(disk.Existed()?Patch(disk.Before(),writes):Serialize(next));
   if(All(Parse(disk.After()))!=All(next))throw std::runtime_error("candidate-decode-mismatch");
   state="PREPARED";return true;
  }catch(const std::exception& e){error=e.what();state="ABORTED";return false;}
 }
 bool Match(const std::string& request,const std::string& plan){return request==id&&plan==hash;}
 std::string Abort(const std::string& request,const std::string& plan){if(!Match(request,plan))return "RESULT_UNKNOWN";if(finalized)return state;if(state=="ABORTED")return state;if(state!="PREPARED"&&state!="COMMITTED")return state;
  bool t=true,m=true,d=true;
  if(dirty){for(auto it=edits.rbegin();it!=edits.rend();++it){const auto& v=oldTitles[it->id-1000];const auto* current=titles.GetCustomTitle(it->id);if(fault.titleRecovery||Protected(titles)!=oldProtected||!current||!((current->before==it->before&&current->after==it->after)||(current->before==v.before&&current->after==v.after))||!titles.UpsertCustomTitle(it->id,v.before,v.after))t=false;}
   if(fault.memoryRecovery||(All(ui::Get())!=All(next)&&All(ui::Get())!=All(old)))m=false;else m=ui::Apply(old,false);
   d=!fault.diskRecovery&&disk.Restore();
  }
  bool verified=t&&m&&d&&SnapshotMatches();if(verified&&!disk.Clean())verified=false;
  state=verified?"ABORTED":"RECOVERY_REQUIRED";if(!verified)error+="|rollback-unverified";return state;
 }
 std::string Commit(const std::string& request,const std::string& plan){
  if(!Match(request,plan))return "RESULT_UNKNOWN";
  if(state!="PREPARED")return state; // repeated commit never reapplies
  try{
   if(!SnapshotMatches()){error="snapshot-conflict";state=disk.Clean()?"ABORTED":"RECOVERY_REQUIRED";return state;}
   if(fault.at=="write"){(void)disk.Stage(true);throw std::runtime_error("injected-partial-temp-write");}
   if(!disk.Stage())throw std::runtime_error("temp-or-backup-write");
   if(fault.at=="temp-verify")throw std::runtime_error("injected-temp-verify");
   if(!SnapshotMatches())throw std::runtime_error("pre-replace-conflict");
   dirty=true;bool ok=disk.Replace(fault.at=="replace");osError=disk.Error();
   if(!ok)throw std::runtime_error(fault.at=="replace"?"injected-replace":"os-replace");
   if(fault.at=="post-replace")throw std::runtime_error("injected-post-replace");
   // Exercise a failure after actual partial mutation, not a pre-call-only mock.
   if(!ui::Apply(next,false))throw std::runtime_error("memory-apply");
   if(fault.at=="memory")throw std::runtime_error("injected-memory-after-apply");
   for(auto& e:edits){if(!titles.GetCustomTitle(e.id)||!titles.UpsertCustomTitle(e.id,e.before,e.after))throw std::runtime_error("title-update");if(fault.at=="title")throw std::runtime_error("injected-title-after-update");}
   if(fault.at=="verify"||!TargetMatches())throw std::runtime_error("post-readback");
   retained=true;state="COMMITTED";return state;
  }catch(const std::exception& e){error=e.what();if(!dirty){if(!disk.Clean()){state="RECOVERY_REQUIRED";return state;}}return Abort(request,plan);}
 }
 std::string Query(const std::string& request,const std::string& plan){return Match(request,plan)?state:"RESULT_UNKNOWN";}
 bool Finalize(const std::string& request,const std::string& plan){if(!Match(request,plan)||state!="COMMITTED")return false;if(!TargetMatches()||!disk.Clean()){state="RECOVERY_REQUIRED";error="finalize-verification-or-cleanup";return false;}retained=false;finalized=true;return true;}
 std::string DiskPreSha()const{return Sha(disk.Before());}std::string DiskPostSha(){auto value=disk.Current();return value.empty()?"ABSENT":Sha(value);}
};
} // A4

namespace TULLBUNG::UIProfiles {
struct Transaction::Impl {Detail::Transaction core;explicit Impl(TitleSystem::Runtime& t):core(t){}};
Transaction::Transaction(TitleSystem::Runtime& t):p(std::make_unique<Impl>(t)){}
Transaction::~Transaction(){std::lock_guard lock(RuntimeMutex());if(Detail::SaveState().owner==this){if(p->core.finalized||p->core.state=="ABORTED")Detail::SaveState().titles.reset();Detail::SaveState().owner=nullptr;}}
bool Transaction::Prepare(const std::filesystem::path& root,const std::string& id,const std::string& hash,const std::string& raw,const std::string& wire){std::lock_guard lock(RuntimeMutex());if(Detail::SaveState().titles&&Detail::SaveState().owner!=this){p->core.state="ABORTED";p->core.error="save-snapshot-owned-or-orphaned";return false;}bool ok=p->core.Prepare(root,id,hash,raw,wire);if(ok){try{Detail::SaveState().titles=std::make_shared<const Detail::FullTitles>(p->core.SaveBaseline());Detail::SaveState().owner=this;}catch(...){(void)p->core.Abort(id,hash);return false;}}return ok;}
std::string Transaction::Commit(const std::string& id,const std::string& hash){std::lock_guard lock(RuntimeMutex());if(!p->core.finalized&&p->core.state!="ABORTED"&&Detail::SaveState().owner!=this)return "RESULT_UNKNOWN";auto r=p->core.Commit(id,hash);if(r=="ABORTED"&&Detail::SaveState().owner==this){Detail::SaveState().titles.reset();Detail::SaveState().owner=nullptr;}return r;}
std::string Transaction::Abort(const std::string& id,const std::string& hash){std::lock_guard lock(RuntimeMutex());if(!p->core.finalized&&p->core.state!="ABORTED"&&Detail::SaveState().owner!=this)return "RESULT_UNKNOWN";auto r=p->core.Abort(id,hash);if(r=="ABORTED"&&Detail::SaveState().owner==this){Detail::SaveState().titles.reset();Detail::SaveState().owner=nullptr;}return r;}
std::string Transaction::Query(const std::string& id,const std::string& hash){std::lock_guard lock(RuntimeMutex());return p->core.Query(id,hash);}
bool Transaction::Finalize(const std::string& id,const std::string& hash){std::lock_guard lock(RuntimeMutex());if(!p->core.finalized&&Detail::SaveState().owner!=this)return false;bool ok=p->core.Finalize(id,hash);if(ok&&Detail::SaveState().owner==this){Detail::SaveState().titles.reset();Detail::SaveState().owner=nullptr;}return ok;}
void InitializeSaveTitleBoundary(){std::lock_guard lock(RuntimeMutex());(void)Detail::SaveState();}
SaveTitleSnapshot CaptureSaveTitles(const TitleSystem::Runtime& runtime){std::lock_guard lock(RuntimeMutex());if(Detail::SaveState().titles)return {Detail::SaveState().titles->protectedState.persisted,Detail::SaveState().titles->records};return {runtime.MakePersistedState(),runtime.MakeCustomTitleRecords()};}
void ResetSaveTitleSnapshotForLoad() noexcept {std::lock_guard lock(RuntimeMutex());Detail::SaveState().titles.reset();Detail::SaveState().owner=nullptr;}
std::string Transaction::State()const{return p->core.state;}std::string Transaction::Error()const{return p->core.error;}bool Transaction::Finalized()const{return p->core.finalized;}
void Transaction::Inject(Faults f){p->core.fault={f.at,f.diskRecovery,f.memoryRecovery,f.titleRecovery};}
std::string Transaction::DiskPreSha()const{return p->core.DiskPreSha();}std::string Transaction::DiskPostSha(){return p->core.DiskPostSha();}
}

namespace TULLBUNG::UIProfiles {unsigned long Transaction::OSError()const{return p->core.osError;}}
