#include "ProfileService.h"
#include "ProfileValidation.h"
#include "ProfileFileIO.h"
#include "UIProfileTransaction.h"
#include "RuntimeBoundary.h"
#include "Prisma/FloatingTitleMinimal.h"
#include "TitleRuntime.h"
#include "Persistence.h"
#include <Windows.h>
#include <ShlObj.h>
#include <Psapi.h>
#include <set>
#include <sstream>
#include <iomanip>
#include <chrono>
namespace TULLBUNG::UIProfiles {
namespace {
using namespace POT::Precode;
// Construct shared C++ save boundary before active so its destructor can safely
// access the boundary and mutex during process shutdown. No SKSE/game API here.
std::unique_ptr<Transaction> active=[](){InitializeSaveTitleBoundary();return std::unique_ptr<Transaction>{};}();std::string activeID,activeHash;std::set<std::string> used;bool recovery=false;bool available=false;
std::string NewSession(){static uint64_t serial=0;return Hash(std::to_string(GetCurrentProcessId())+":"+std::to_string(GetTickCount64())+":"+std::to_string(++serial));}
// Session-only receipts survive View recreation and game Save/Load boundaries.
// No ESS/co-save record or Native persistent schema is introduced.
struct FileReceipt {std::string id,op,name,before,after;int status=0;};
std::unique_ptr<FileReceipt> fileReceipt, fileAcknowledged;std::set<std::string> fileUsed;std::string fileSession;
std::string FileHash(const std::filesystem::path& root,const std::string& name){auto r=ProfileFileIO::Read(root,name);if(r.status==ProfileFileIO::Status::Ok)return Hash(r.data);if(r.status==ProfileFileIO::Status::NotFound||(r.status==ProfileFileIO::Status::InvalidRoot&&!std::filesystem::exists(root)))return "ABSENT";return "UNREADABLE";}
DiagnosticSink diagnosticSink=nullptr;
std::string session; // Created at a request/session boundary, never during DLL static initialization.
std::string Q(std::string_view s){std::string r="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){r+='\\';r+=static_cast<char>(c);}else if(c<32){r+="\\u00";r+="0123456789abcdef"[c>>4];r+="0123456789abcdef"[c&15];}else r+=static_cast<char>(c);}return r+'"';}
std::string N(double n){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<n;return s.str();}
std::filesystem::path ProfileRoot(){
#ifdef TULL_A5_ISOLATED_QA
return std::filesystem::current_path()/"Profiles";
#endif
PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Documents,KF_FLAG_DEFAULT,nullptr,&value)))throw std::runtime_error("documents-folder");std::filesystem::path root(value);CoTaskMemFree(value);return root/L"My Games"/L"Skyrim Special Edition"/L"PathOfTULLBUNG"/L"UIProfiles";}
// Fixed owner only: MO2 can expose a virtual path even for a handle's final name.
// A read-only mapping identifies the actual backing file; A2 then validates and
// pins every physical parent, file identity and reparse restriction as before.
std::filesystem::path IniOwnerRoot(){
 auto virtualRoot=std::filesystem::absolute("Data/SKSE/Plugins");
 if(!GetModuleHandleW(L"usvfs_x64.dll"))return virtualRoot;
 auto ancestor=virtualRoot.root_path();for(const auto& part:virtualRoot.relative_path()){ancestor/=part;const DWORD attributes=GetFileAttributesW(ancestor.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("ini-physical-owner-parent");}
 const auto target=virtualRoot/L"PathOfTULLBUNG_UI.ini";
 HANDLE file=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("ini-physical-owner-open");
 struct Close{HANDLE h;~Close(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} fileGuard{file};
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
 if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||!GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>1000000)throw std::runtime_error("ini-physical-owner-file");
 HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);if(!mapping)throw std::runtime_error("ini-physical-owner-mapping");Close mappingGuard{mapping};
 const void* view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,1);if(!view)throw std::runtime_error("ini-physical-owner-view");
 struct Unmap{const void* p;~Unmap(){UnmapViewOfFile(p);}} viewGuard{view};
 std::wstring device(32768,L'\0');DWORD n=GetMappedFileNameW(GetCurrentProcess(),const_cast<void*>(view),device.data(),static_cast<DWORD>(device.size()));if(!n||n>=device.size())throw std::runtime_error("ini-physical-owner-name");device.resize(n);
 for(wchar_t drive=L'A';drive<=L'Z';++drive){wchar_t name[]{drive,L':',0};wchar_t prefix[32768]{};if(!QueryDosDeviceW(name,prefix,32768))continue;const std::wstring volume(prefix);if(device.size()>volume.size()&&device[volume.size()]==L'\\'&&CompareStringOrdinal(device.data(),static_cast<int>(volume.size()),volume.data(),static_cast<int>(volume.size()),TRUE)==CSTR_EQUAL){std::filesystem::path physical=std::wstring(name)+device.substr(volume.size());if(physical.filename()!=L"PathOfTULLBUNG_UI.ini")throw std::runtime_error("ini-physical-owner-filename");if(diagnosticSink){const auto owner=physical.u8string();try{diagnosticSink("INI owner | source=MO2_mapped_file | physical="+std::string(reinterpret_cast<const char*>(owner.data()),owner.size()));}catch(...){}}return physical.parent_path();}}
 throw std::runtime_error("ini-physical-owner-volume");
}
std::string NativeJSON(const UISettings::Settings& s=UISettings::Get()){std::string r="{\"themeId\":"+Q(s.themeId)+",\"uiBackgroundOpacity\":"+N(s.uiBackgroundOpacity)+",\"hudMaster\":"+(s.hud.masterVisible?"true":"false");auto h=[&](const char* name,const UISettings::HudElementSettings& e){std::string k=name;r+=","+Q(k+"Visible")+":"+(e.visible?"true":"false")+","+Q(k+"X")+":"+N(e.x)+","+Q(k+"Y")+":"+N(e.y)+","+Q(k+"Scale")+":"+N(e.scale)+","+Q(k+"Opacity")+":"+N(e.opacity);};h("hudVoid",s.hud.voidHud);h("hudComposure",s.hud.composureHud);h("hudQuiet",s.hud.quietProcessingHud);return r+"}";}
std::string TitleJSON(){const auto& t=TitleRuntime::GetRuntime();auto records=t.MakeCustomTitleRecords();std::string r="{\"customTitles\":[";for(size_t i=0;i<20;i++){const auto& x=records[i];if(i)r+=",";r+="{\"id\":"+std::to_string(1000+i)+",\"code\":"+Q("C"+std::string(i<10?"0":"")+std::to_string(i))+",\"present\":"+(x.present?"true":"false")+",\"favorite\":"+(x.favorite?"true":"false")+",\"before\":"+Q(x.before)+",\"after\":"+Q(x.after)+"}";}r+="],\"slots\":[";for(size_t i=0;i<6;i++){if(i)r+=",";r+="{\"titleID\":"+std::to_string(t.Slots()[i].selectedTitleID)+"}";}r+="],\"favoriteTitleIDs\":[";bool first=true;for(auto id:t.FavoriteTitleIDs()){if(!first)r+=",";first=false;r+=std::to_string(id);}return r+"]}";}
std::string GetString(const Json& j,const char* k,size_t max){const auto& v=j.at(k);if(v.type!=Json::String||v.s.size()>max)throw std::runtime_error("packet-string");return v.s;}
std::string ReceiptJSON(const FileReceipt* receipt=nullptr){if(!receipt)receipt=fileReceipt.get();if(!receipt)return "null";auto& f=*receipt;return "{\"requestId\":"+Q(f.id)+",\"operation\":"+Q(f.op)+",\"filename\":"+Q(f.name)+",\"beforeHash\":"+Q(f.before)+",\"afterHash\":"+Q(f.after)+",\"status\":"+std::to_string(f.status)+"}";}
bool FileVerified(const FileReceipt* receipt){if(!receipt||receipt->status==9||receipt->status==10)return false;return FileHash(ProfileRoot(),receipt->name)==(receipt->status==0?receipt->after:receipt->before); }
bool ID(const std::string& id){return !id.empty()&&id.size()<=64&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';});}
}
void SetDiagnosticSink(DiagnosticSink sink){std::lock_guard lock(RuntimeMutex());diagnosticSink=sink;}
bool ImportBusy(){return recovery||(active&&active->State()!="ABORTED"&&!active->Finalized());}
bool Busy(){std::lock_guard lock(RuntimeMutex());return Persistence::IsSaveSafetyHeld()||ImportBusy();}
void SetAvailable(bool value){std::lock_guard lock(RuntimeMutex());available=value;if(!value)InvalidateSession();}
void InvalidateSession() noexcept {try{std::lock_guard lock(RuntimeMutex());if(active&&Busy()){if(active->State()=="PREPARED")(void)active->Abort(activeID,activeHash);else if(!active->Finalized())recovery=true;}active.reset();activeID.clear();activeHash.clear();used.clear();session=NewSession();}catch(...){recovery=true;available=false;session.clear();}}
std::string HandleRequest(const std::string& packet){std::lock_guard lock(RuntimeMutex());std::string request,operation,hash,body,state="ABORTED",error;int status=0;try{
 if(session.empty())session=NewSession();if(fileSession.empty())fileSession=NewSession();
 auto j=ParsePacket(packet);if(j.type!=Json::Object)throw std::runtime_error("packet-object");request=GetString(j,"requestId",64);operation=GetString(j,"operation",16);if(!ID(request))throw std::runtime_error("request-id");
 const std::set<std::string> keys{"requestId","operation","session","planHash","normalizedRaw","wire","filename","raw","expectedHash","fileSession"};for(auto& [k,v]:j.o)if(!keys.contains(k))throw std::runtime_error("packet-property");
 if(operation=="hello"){state="READY";body=",\"saveSafetyHold\":"+std::string(Persistence::IsSaveSafetyHeld()?"true":"false")+",\"snapshot\":{\"native\":"+NativeJSON()+",\"titleState\":"+TitleJSON()+"},\"fileSession\":"+Q(fileSession)+",\"filePending\":"+ReceiptJSON()+",\"fileAcknowledged\":"+(fileAcknowledged?ReceiptJSON(fileAcknowledged.get()):"null")+",\"importPending\":"+(ImportBusy()?"{\"requestId\":"+Q(activeID)+",\"planHash\":"+Q(activeHash)+",\"state\":"+Q(recovery?"RECOVERY_REQUIRED":active->State())+"}":"null");}
 else if(operation=="fileQuery"||operation=="fileAck"){
  // ACK is idempotent within the file session. Keep one verified terminal receipt
  // so a lost ACK response or failed journal removal remains queryable.
  auto receipt=fileReceipt&&fileReceipt->id==request?fileReceipt.get():fileAcknowledged&&fileAcknowledged->id==request?fileAcknowledged.get():nullptr;
  if(GetString(j,"fileSession",64)!=fileSession||!receipt){state="FILE_RECOVERY_REQUIRED";error="file-receipt-identity";}
  else {const bool verified=FileVerified(receipt);status=receipt->status;state=verified?(status==0?"FILE_COMMITTED":"FILE_ABORTED"):"FILE_RECOVERY_REQUIRED";body=",\"fileReceipt\":"+ReceiptJSON(receipt);if(operation=="fileAck"&&verified&&receipt==fileReceipt.get())fileAcknowledged=std::move(fileReceipt);}
 } else if(GetString(j,"session",64)!=session){state="RESULT_UNKNOWN";error="session-mismatch";}
 else if(operation=="diagnostic"){auto diagnostic=ParsePacket(GetString(j,"raw",2048));if(diagnostic.type!=Json::Object)throw std::runtime_error("diagnostic-shape");std::string line="Browser";for(auto& [key,value]:diagnostic.o){if(key!="action"&&key!="reason"&&key!="stage"&&key!="nativeState"&&key!="requestId"&&key!="blockedCause")throw std::runtime_error("diagnostic-key");if(value.type!=Json::String||value.s.size()>256||!std::all_of(value.s.begin(),value.s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':'||c=='|'||c=='/';}))throw std::runtime_error("diagnostic-value");line+=" | "+key+"="+value.s;}if(diagnosticSink)try{diagnosticSink(line);}catch(...){}state="READY";}
 else if(operation=="snapshot"){state="READY";body=",\"snapshot\":{\"native\":"+NativeJSON()+",\"titleState\":"+TitleJSON()+"}";}
 else if(operation=="folder"){auto value=ProfileRoot().u8string();std::string folder(reinterpret_cast<const char*>(value.data()),value.size());state="READY";body=",\"folder\":"+Q(folder);}
 else if(operation=="defaults"){state="READY";body=",\"nativeDefaults\":"+NativeJSON(UISettings::Settings{});}
 else if(operation=="hash"){hash=Hash(GetString(j,"raw",2000001));state="READY";}
 else if(operation=="list"){auto root=ProfileRoot();auto result=ProfileFileIO::List(root);if(result.status==ProfileFileIO::Status::InvalidRoot&&!std::filesystem::exists(root)){body=",\"files\":[]";}else{status=static_cast<int>(result.status);body=",\"files\":[";for(size_t i=0;i<result.names.size();i++){if(i)body+=",";body+=Q(result.names[i]);}body+="]";}state="READY";}
 else if(operation=="fileInfo"){auto name=GetString(j,"filename",53);if(!ProfileFileIO::ValidName(name))throw std::runtime_error("managed-profile-filename");auto r=ProfileFileIO::Read(ProfileRoot(),name);status=static_cast<int>(r.status);if(status==0)body=",\"fileHash\":"+Q(Hash(r.data));state="READY";}
 else if(operation=="store"||operation=="remove"||operation=="copy"){
  if(Busy())throw std::runtime_error("import-busy");
  if(fileReceipt||fileUsed.contains(request)||fileUsed.size()>=4096)throw std::runtime_error("file-receipt-pending-or-request-reused");
  auto name=GetString(j,"filename",53),expected=GetString(j,"expectedHash",64);
  if(!ProfileFileIO::ValidName(name)||(operation=="copy"?name.starts_with("p_"):!name.starts_with("p_")))throw std::runtime_error("managed-profile-filename");
  auto root=ProfileRoot();auto prior=ProfileFileIO::Read(root,name);bool fresh=expected=="ABSENT";
  if(fresh){if((operation!="store"&&operation!="copy")||prior.status!=ProfileFileIO::Status::NotFound&&!(prior.status==ProfileFileIO::Status::InvalidRoot&&!std::filesystem::exists(root)))throw std::runtime_error("profile-already-exists");}
  else{if(expected.size()!=64||prior.status!=ProfileFileIO::Status::Ok||Hash(prior.data)!=expected)throw std::runtime_error("profile-source-drift");}
  std::string raw;if(operation=="store"||operation=="copy"){raw=GetString(j,"raw",1000000);CheckProfile(raw,true);}
  fileUsed.insert(request);fileReceipt=std::make_unique<FileReceipt>(FileReceipt{request,operation,name,fresh?"ABSENT":Hash(prior.data),operation=="remove"?"ABSENT":Hash(raw),10});
  if(operation=="store"||operation=="copy")status=static_cast<int>(ProfileFileIO::WriteIf(root,name,raw,prior.data,fresh));
  else status=static_cast<int>(ProfileFileIO::RemoveIf(root,name,prior.data));
  fileReceipt->status=status;
  state="READY";
 }
 else if(operation=="read"||operation=="write"){auto name=GetString(j,"filename",53);auto root=ProfileRoot();if(operation=="read"){auto r=ProfileFileIO::Read(root,name);status=static_cast<int>(r.status);if(status==0){CheckProfile(r.data);body=",\"raw\":"+Q(r.data);}}else{auto raw=GetString(j,"raw",1000000);CheckProfile(raw,true);if(name.starts_with("p_"))throw std::runtime_error("managed-profile-requires-compare-write");if(Busy()||fileReceipt)throw std::runtime_error("import-busy");status=static_cast<int>(ProfileFileIO::Write(root,name,raw));}state="READY";}
 else{hash=GetString(j,"planHash",64);if(operation=="prepare"){
  if(!available||Busy()||used.contains(request)||used.size()>=4096)throw std::runtime_error("busy-or-request-reused");auto raw=GetString(j,"normalizedRaw",1000000),wire=GetString(j,"wire",1000000);used.insert(request);activeID=request;activeHash=hash;active=std::make_unique<Transaction>(TitleRuntime::GetRuntime());
  active->Prepare(IniOwnerRoot(),request,hash,raw,wire);state=active->State();error=active->Error();
 }else if(!active||request!=activeID||hash!=activeHash){state="RESULT_UNKNOWN";error="request-identity";}
 else if(operation=="commit"){state=active->Commit(request,hash);error=active->Error();}
 else if(operation=="abort"){state=active->Abort(request,hash);error=active->Error();}
 else if(operation=="query"){state=active->Query(request,hash);error=active->Error();}
 else if(operation=="finalize"){
  const bool alreadyFinalized=active->Finalized();
  if(active->Finalize(request,hash)&&!alreadyFinalized)FloatingTitleMinimal::InvalidateForImport();
  state=active->State();error=active->Error();
 }
 else throw std::runtime_error("operation");
 }
 }catch(const std::exception& e){error=e.what();status=6;if(operation=="prepare"){if(recovery||(active&&active->State()=="RECOVERY_REQUIRED"))state="RECOVERY_REQUIRED";else if(used.contains(request))state="RESULT_UNKNOWN";}}
 if(diagnosticSink&&(status!=0||state=="RECOVERY_REQUIRED"||operation=="prepare"||operation=="commit"||operation=="finalize"||operation=="abort"))try{diagnosticSink("Native | operation="+operation+" | requestId="+request+" | state="+state+" | status="+std::to_string(status)+" | reason="+error+" | win32="+std::to_string(active&&request==activeID?active->OSError():0));}catch(...){}
 if(operation=="store"||operation=="remove"||operation=="copy")body+=",\"fileReceipt\":"+ReceiptJSON();
 if(state=="COMMITTED"||state=="ABORTED")body+=",\"snapshot\":{\"native\":"+NativeJSON()+",\"titleState\":"+TitleJSON()+"}";
 return "{\"requestId\":"+Q(request)+",\"operation\":"+Q(operation)+",\"session\":"+Q(session)+",\"planHash\":"+Q(hash)+",\"state\":"+Q(state)+",\"status\":"+std::to_string(status)+",\"error\":"+Q(error)+body+"}";
}
}
