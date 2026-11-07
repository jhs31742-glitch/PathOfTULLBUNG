#include "ProfileFileIO.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <utility>
#include <mutex>
#ifndef _WIN32
#error This isolated A2 candidate is Windows-only; no GNU or non-Windows security claim.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace TULLBUNG::ProfileFileIO {
namespace fs = std::filesystem;
namespace {
std::atomic<unsigned long long> g_serial{0};
std::mutex g_writer;
thread_local DWORD g_error=0;
#ifdef TULL_PROFILE_IO_TESTING
thread_local Fault g_fault=Fault::None;
thread_local TestHook g_hook=nullptr;
bool FaultAt(Fault f) { return g_fault==f; }
void Hook(Point p,const fs::path& t,const fs::path& x={},const fs::path& b={}) { if(g_hook) g_hook(p,t,x,b); }
#else
enum class Fault { None, TempWrite, TempVerify, BeforeCommit, AfterCommit, PostVerify, Rollback, Cleanup, ReplaceUnchanged, ReplaceMissing, ReplaceInstalled };
enum class Point { TempName, BackupName, BeforeReplace, AfterReplace, BeforeRollback };
bool FaultAt(Fault) { return false; }
void Hook(Point,const fs::path&,const fs::path& ={},const fs::path& ={}) {}
#endif
std::string Upper(std::string s) {
    for(auto& c:s) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    Handle()=default;
    explicit Handle(HANDLE v):h(v){}
    ~Handle(){ if(h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(Handle&& v) noexcept:h(std::exchange(v.h,INVALID_HANDLE_VALUE)){}
    Handle& operator=(Handle&& v) noexcept { if(this!=&v){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=std::exchange(v.h,INVALID_HANDLE_VALUE);}return *this; }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    explicit operator bool()const{return h!=INVALID_HANDLE_VALUE;}
    void close(){if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}}
};
struct Identity {
    DWORD volume=0,high=0,low=0;
    bool operator==(const Identity& o)const{return volume==o.volume&&high==o.high&&low==o.low;}
};
Identity Id(const BY_HANDLE_FILE_INFORMATION& i){return {i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow};}
bool SamePath(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
std::wstring Final(HANDLE h) {
    std::wstring s(32768,L'\0');
    DWORD n=GetFinalPathNameByHandleW(h,s.data(),static_cast<DWORD>(s.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!n||n>=s.size())return {};
    s.resize(n);return s;
}
struct RootGuard {
    fs::path path;
    std::vector<Handle> held;
    bool open(const fs::path& input,bool create) {
        if(!input.is_absolute()||input.root_name().wstring().size()!=2||input.root_name().wstring()[1]!=L':')return false;
        const auto absolute=input.lexically_normal();
        fs::path at=absolute.root_path();
        auto pin=[&](const fs::path& dir,bool volumeRoot) {
            Handle h(CreateFileW(dir.c_str(),FILE_READ_ATTRIBUTES,
                volumeRoot?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
            if(!h){g_error=GetLastError();return false;}
            BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(h.h,&info)||
               !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            const auto real=Final(h.h);
            if(real.empty()||!SamePath(real,L"\\\\?\\"+dir.wstring()))return false;
            held.push_back(std::move(h));return true;
        };
        if(!pin(at,true))return false;
        for(const auto& part:absolute.relative_path()) {
            at/=part;
            DWORD a=GetFileAttributesW(at.c_str());
            if(a==INVALID_FILE_ATTRIBUTES) {
                DWORD e=GetLastError();
                if(!create||(e!=ERROR_PATH_NOT_FOUND&&e!=ERROR_FILE_NOT_FOUND))return false;
                if(!CreateDirectoryW(at.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            }
            if(!pin(at,false))return false;
        }
        const auto real=Final(held.back().h);
        if(real.empty())return false;
        path=fs::path(real);
        return true;
    }
};
Handle OpenChecked(const RootGuard& root,const fs::path& file,DWORD access,DWORD sharing,Identity* identity=nullptr) {
    g_error=0;
    Handle h(CreateFileW(file.c_str(),access,sharing,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!h){g_error=GetLastError();return {};}
    BY_HANDLE_FILE_INFORMATION info{};
    if(GetFileType(h.h)!=FILE_TYPE_DISK||!GetFileInformationByHandle(h.h,&info)||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1){g_error=ERROR_INVALID_DATA;return {};}
    if(!SamePath(Final(h.h),file.wstring())||!SamePath(file.parent_path().wstring(),root.path.wstring())){g_error=ERROR_INVALID_DATA;return {};}
    if(identity)*identity=Id(info);
    return h;
}
Status ReadRaw(HANDLE h,std::string& out) {
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(h,&size)||size.QuadPart<0)return Status::IoError;
    if(static_cast<unsigned long long>(size.QuadPart)>kMaxBytes)return Status::TooLarge;
    std::string data;data.reserve(static_cast<std::size_t>(size.QuadPart));
    char buffer[8192];
    for(;;){DWORD got=0;if(!ReadFile(h,buffer,sizeof(buffer),&got,nullptr)){g_error=GetLastError();return Status::IoError;}
        if(!got)break;if(data.size()+got>kMaxBytes)return Status::TooLarge;data.append(buffer,got);}
    out=std::move(data);return Status::Ok;
}
ReadResult ReadPath(const RootGuard& root,const fs::path& file,Identity* id=nullptr,bool validate=true) {
    auto h=OpenChecked(root,file,GENERIC_READ,FILE_SHARE_READ,id);
    if(!h){DWORD e=g_error;return {(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)?Status::NotFound:Status::IoError,{}};}
    std::string data;auto s=ReadRaw(h.h,data);
    if(s!=Status::Ok)return {s,{}};
    if(validate&&(data.empty()||!ValidUtf8(data)))return {Status::InvalidData,{}};
    return {Status::Ok,std::move(data)};
}
struct Owned { fs::path path; Identity id; bool exists=false; };
bool RemoveOwned(const RootGuard& root,Owned& f) {
    if(!f.exists)return true;
    const DWORD a=GetFileAttributesW(f.path.c_str());
    if(a==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND){f.exists=false;return true;}
    Identity actual;auto h=OpenChecked(root,f.path,DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,&actual);
    if(!h||!(actual==f.id))return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    if(!SetFileInformationByHandle(h.h,FileDispositionInfo,&disposition,sizeof(disposition))){g_error=GetLastError();return false;}
    h.close();f.exists=false;return true;
}
bool CreateOwned(const RootGuard& root,const fs::path& target,const char* extension,const std::string& data,Owned& out) {
    for(unsigned trial=0;trial<16;trial++) {
        const auto tick=std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path p=root.path/(target.filename().wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(tick)+L"."+std::to_wstring(++g_serial)+fs::path(extension).wstring());
        Hook(extension[1]=='p'?Point::TempName:Point::BackupName,target,p);
        Handle h(CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!h){DWORD e=GetLastError();if(e==ERROR_FILE_EXISTS||e==ERROR_ALREADY_EXISTS)continue;g_error=e;return false;}
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h.h,&info))return false;
        out={p,Id(info),true};
        if((info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
           info.nNumberOfLinks!=1||!SamePath(Final(h.h),p.wstring()))return false;
        DWORD written=0;
        if(extension[1]=='p'&&FaultAt(Fault::TempWrite)){
            WriteFile(h.h,data.data(),static_cast<DWORD>(data.size()/2),&written,nullptr);
            g_error=ERROR_WRITE_FAULT;return false;
        }
        if(!WriteFile(h.h,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)||written!=data.size()||!FlushFileBuffers(h.h)){g_error=GetLastError();return false;}
        LARGE_INTEGER zero{};if(!SetFilePointerEx(h.h,zero,nullptr,FILE_BEGIN))return false;
        std::string check;if(ReadRaw(h.h,check)!=Status::Ok||check!=data)return false;
        return true;
    }
    g_error=ERROR_FILE_EXISTS;return false;
}
struct WriteTransaction {
    RootGuard root;
    Owned temp,backup;
    fs::path target;
    std::string before,after,failedAfter;
    bool failedAfterKnown=false;
    Identity originalId;
    bool existed=false,apiAttempt=false;
    bool clean() {
        bool a=RemoveOwned(root,temp);
        if(!a)return false;
        return RemoveOwned(root,backup);
    }
    bool restore() {
        Hook(Point::BeforeRollback,target,temp.path,backup.path);
        if(FaultAt(Fault::Rollback))return false;
        Identity currentId;auto current=ReadPath(root,target,&currentId,false);
        if(existed) {
            if(current.status==Status::Ok&&current.data==before&&currentId==originalId)return true;
            if(current.status!=Status::NotFound&&
               !(current.status==Status::Ok&&(current.data==after||(failedAfterKnown&&current.data==failedAfter))&&currentId==temp.id))return false;
            Identity backupId;auto old=ReadPath(root,backup.path,&backupId,false);
            if(old.status!=Status::Ok||old.data!=before||!(backupId==backup.id))return false;
            DWORD flags=MOVEFILE_WRITE_THROUGH;
            if(current.status!=Status::NotFound)flags|=MOVEFILE_REPLACE_EXISTING;
            if(!MoveFileExW(backup.path.c_str(),target.c_str(),flags)){g_error=GetLastError();return false;}
            backup.exists=false;
            auto verified=ReadPath(root,target,nullptr,false);
            return verified.status==Status::Ok&&verified.data==before;
        }
        if(current.status==Status::NotFound)return true;
        if(current.status!=Status::Ok||!(current.data==after||(failedAfterKnown&&current.data==failedAfter))||!(currentId==temp.id))return false;
        Owned newFile{target,temp.id,true};
        return RemoveOwned(root,newFile)&&ReadPath(root,target).status==Status::NotFound;
    }
    Status fail(Status reason,bool recover) {
        const DWORD cause=g_error;
        if(recover&&!restore())return Status::RollbackFailed;
        if(!clean())return Status::CleanupFailed;
        g_error=cause;return reason;
    }
    void refreshAfterReplace() {
        // ReplaceFile may replace our reserved backup object with the old target.
        Identity id;auto b=ReadPath(root,backup.path,&id,false);
        if(existed&&b.status==Status::Ok&&b.data==before&&(id==originalId||id==backup.id)){backup.id=id;backup.exists=true;}
    }
};
} // internal helpers
bool ValidName(const std::string& name) noexcept {
    if(name.size()<6 || name.size()>53 || name.substr(name.size()-5)!=".json") return false;
    const std::string stem=name.substr(0,name.size()-5);
    if(stem.empty() || stem.size()>48) return false;
    for(unsigned char c:stem)
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return false;
    const auto u=Upper(stem);
    if(u=="CON"||u=="PRN"||u=="AUX"||u=="NUL") return false;
    if(u.size()==4 && ((u.substr(0,3)=="COM")||(u.substr(0,3)=="LPT")) && u[3]>='1'&&u[3]<='9') return false;
    return true;
}
bool ValidUtf8(const std::string& text) noexcept {
    const auto* p=reinterpret_cast<const unsigned char*>(text.data());
    const auto n=text.size();
    for(std::size_t i=0;i<n;) {
        const auto a=p[i++];
        if(a==0) return false;
        if(a<0x80) continue;
        unsigned len=0; unsigned cp=0;
        if(a>=0xC2&&a<=0xDF){len=1;cp=a&0x1F;}
        else if(a>=0xE0&&a<=0xEF){len=2;cp=a&0x0F;}
        else if(a>=0xF0&&a<=0xF4){len=3;cp=a&0x07;}
        else return false;
        if(i+len>n) return false;
        for(unsigned j=0;j<len;j++){const auto b=p[i++];if((b&0xC0)!=0x80)return false;cp=(cp<<6)|(b&0x3F);}
        if((len==1&&cp<0x80)||(len==2&&cp<0x800)||(len==3&&cp<0x10000)||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)) return false;
    }
    return true;
}

ReadResult Read(const fs::path& root,const std::string& name) noexcept {
    try{g_error=0;if(!ValidName(name))return {Status::InvalidName,{}};
        RootGuard guard;if(!guard.open(root,false))return {Status::InvalidRoot,{}};
        return ReadPath(guard,guard.path/name);
    }catch(...){return {Status::IoError,{}};}
}
static Status WriteGuarded(const fs::path& root,const std::string& name,const std::string& data,bool injectBeforeCommit,const std::string* expected,bool createNew) noexcept {
    WriteTransaction tx;
    std::unique_lock<std::mutex> lock(g_writer,std::defer_lock);
    try{
        lock.lock();g_error=0;
        if(!ValidName(name))return Status::InvalidName;
        if(data.empty()||data.size()>kMaxBytes||!ValidUtf8(data))return Status::InvalidData;
        if(!tx.root.open(root,true))return Status::InvalidRoot;
        tx.target=tx.root.path/name;tx.after=data;
        auto prior=ReadPath(tx.root,tx.target,&tx.originalId,false);
        tx.existed=prior.status!=Status::NotFound;
        if(tx.existed&&prior.status!=Status::Ok)return Status::IoError;
        tx.before=std::move(prior.data);
        if(expected&&(createNew?tx.existed:(!tx.existed||tx.before!=*expected)))return Status::VerifyFailed;
        if(!CreateOwned(tx.root,tx.target,".pending",data,tx.temp))return tx.fail(Status::IoError,false);
        if(FaultAt(Fault::TempWrite))return tx.fail(Status::IoError,false);
        if(FaultAt(Fault::TempVerify))return tx.fail(Status::VerifyFailed,false);
        if(tx.existed&&!CreateOwned(tx.root,tx.target,".backup",tx.before,tx.backup))return tx.fail(Status::IoError,false);
        Identity checkId;auto check=ReadPath(tx.root,tx.target,&checkId,false);
        if(tx.existed?(check.status!=Status::Ok||check.data!=tx.before||!(checkId==tx.originalId)):(check.status!=Status::NotFound))
            return tx.fail(Status::VerifyFailed,false);
        if(injectBeforeCommit||FaultAt(Fault::BeforeCommit))return tx.fail(Status::VerifyFailed,false);
        Hook(Point::BeforeReplace,tx.target,tx.temp.path,tx.backup.path);
        tx.apiAttempt=true;
        BOOL ok=FALSE;
        DWORD simulatedError=ERROR_SUCCESS;
        if(FaultAt(Fault::ReplaceUnchanged)){simulatedError=ERROR_UNABLE_TO_MOVE_REPLACEMENT;}
        else if(FaultAt(Fault::ReplaceMissing)&&tx.existed){
            if(!MoveFileExW(tx.target.c_str(),tx.backup.path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                return tx.fail(Status::IoError,true);
            simulatedError=ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        }else ok=tx.existed?ReplaceFileW(tx.target.c_str(),tx.temp.path.c_str(),tx.backup.path.c_str(),0,nullptr,nullptr):
                          MoveFileExW(tx.temp.path.c_str(),tx.target.c_str(),MOVEFILE_WRITE_THROUGH);
        DWORD replaceError=simulatedError?simulatedError:(ok?ERROR_SUCCESS:GetLastError());
        if(ok&&FaultAt(Fault::ReplaceInstalled)){ok=FALSE;replaceError=ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;}
        Hook(Point::AfterReplace,tx.target,tx.temp.path,tx.backup.path);
        tx.refreshAfterReplace();
        if(!ok){g_error=replaceError;return tx.fail(Status::ReplaceFailed,true);}
        if(FaultAt(Fault::AfterCommit)||FaultAt(Fault::PostVerify)||FaultAt(Fault::Rollback))
            return tx.fail(Status::VerifyFailed,true);
        Identity writtenId;auto written=ReadPath(tx.root,tx.target,&writtenId,false);
        if(written.status!=Status::Ok||written.data!=data){
            if(written.status==Status::Ok&&writtenId==tx.temp.id){tx.failedAfter=written.data;tx.failedAfterKnown=true;}
            return tx.fail(Status::VerifyFailed,true);
        }
        if(FaultAt(Fault::Cleanup))return tx.fail(Status::CleanupFailed,true);
        if(!tx.clean())return tx.fail(Status::CleanupFailed,true);
        return Status::Ok;
    }catch(...){try{return tx.fail(Status::IoError,tx.apiAttempt);}catch(...){return Status::RollbackFailed;}}
}
Status Write(const fs::path& root,const std::string& name,const std::string& data,bool injectBeforeCommit) noexcept {return WriteGuarded(root,name,data,injectBeforeCommit,nullptr,false);}
Status WriteIf(const fs::path& root,const std::string& name,const std::string& data,const std::string& expectedRaw,bool createNew) noexcept {return WriteGuarded(root,name,data,false,&expectedRaw,createNew);}
Status RemoveIf(const fs::path& input,const std::string& name,const std::string& expectedRaw) noexcept {
    std::lock_guard lock(g_writer);RootGuard root;Owned backup;bool removed=false;
    try {
        if(!ValidName(name))return Status::InvalidName;
        if(!root.open(input,false))return Status::InvalidRoot;
        const auto target=root.path/name;Identity identity;
        auto h=OpenChecked(root,target,GENERIC_READ|DELETE,FILE_SHARE_READ,&identity);
        if(!h)return g_error==ERROR_FILE_NOT_FOUND?Status::NotFound:Status::IoError;
        std::string before;auto status=ReadRaw(h.h,before);
        if(status!=Status::Ok)return status;
        if(before!=expectedRaw)return Status::VerifyFailed;
        if(!CreateOwned(root,target,".backup",before,backup)){h.close();return RemoveOwned(root,backup)?Status::IoError:Status::CleanupFailed;}
        if(FaultAt(Fault::BeforeCommit)){h.close();return RemoveOwned(root,backup)?Status::VerifyFailed:Status::CleanupFailed;}
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(!SetFileInformationByHandle(h.h,FileDispositionInfo,&disposition,sizeof(disposition))){g_error=GetLastError();h.close();return RemoveOwned(root,backup)?Status::IoError:Status::CleanupFailed;}
        h.close();removed=true;
        const auto after=ReadPath(root,target,nullptr,false);
        if(FaultAt(Fault::AfterCommit)||FaultAt(Fault::Rollback)||after.status!=Status::NotFound){
            if(after.status!=Status::NotFound||FaultAt(Fault::Rollback)||!MoveFileExW(backup.path.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))return Status::RollbackFailed;
            backup.exists=false;
            const auto restored=ReadPath(root,target,nullptr,false);
            return restored.status==Status::Ok&&restored.data==before?Status::VerifyFailed:Status::RollbackFailed;
        }
        return RemoveOwned(root,backup)?Status::Ok:Status::CleanupFailed;
    }catch(...){if(removed)return Status::RollbackFailed;return RemoveOwned(root,backup)?Status::IoError:Status::CleanupFailed;}
}
ListResult List(const fs::path& root) noexcept {
    try{
        g_error=0;RootGuard guard;if(!guard.open(root,false))return {Status::InvalidRoot,{}};
        WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW((guard.path/L"*").c_str(),&data);
        if(search==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?ListResult{Status::Ok,{}}:ListResult{Status::IoError,{}};
        struct FindGuard{HANDLE h;~FindGuard(){FindClose(h);}} closer{search};
        std::vector<std::string> names;
        for(;;){
            if(!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){
                const std::wstring wideName(data.cFileName);
                if(!std::all_of(wideName.begin(),wideName.end(),[](wchar_t c){return c>=0&&c<=0x7f;})){
                    if(!FindNextFileW(search,&data)){if(GetLastError()!=ERROR_NO_MORE_FILES)return {Status::IoError,{}};break;}
                    continue;
                }
                const std::string name(wideName.begin(),wideName.end());
                if(ValidName(name)){
                    auto h=OpenChecked(guard,guard.path/data.cFileName,GENERIC_READ,FILE_SHARE_READ);
                    LARGE_INTEGER size{};
                    if(h&&GetFileSizeEx(h.h,&size)&&size.QuadPart>=0&&static_cast<unsigned long long>(size.QuadPart)<=kMaxBytes){
                        names.push_back(std::move(name));if(names.size()>256)return {Status::TooLarge,{}};
                    }
                }
            }
            if(!FindNextFileW(search,&data)){if(GetLastError()!=ERROR_NO_MORE_FILES)return {Status::IoError,{}};break;}
        }
        std::sort(names.begin(),names.end());return {Status::Ok,std::move(names)};
    }catch(...){return {Status::IoError,{}};}
}
#ifdef TULL_PROFILE_IO_TESTING
void SetFault(Fault f) noexcept { g_fault=f; }
void SetHook(TestHook h) noexcept { g_hook=h; }
unsigned long LastWin32Error() noexcept { return g_error; }
#endif
}

#include "FixedIniTransaction.h"
namespace TULLBUNG::ProfileFileIO {
struct FixedIniTransaction::Impl {WriteTransaction disk;DWORD error=0;};
FixedIniTransaction::FixedIniTransaction():p(std::make_unique<Impl>()){}
FixedIniTransaction::~FixedIniTransaction()=default;
bool FixedIniTransaction::Open(const fs::path& root){auto& d=p->disk;if(!d.root.open(root,false))return false;d.target=d.root.path/L"PathOfTULLBUNG_UI.ini";auto r=ReadPath(d.root,d.target,&d.originalId,false);d.existed=r.status!=Status::NotFound;if(d.existed&&r.status!=Status::Ok)return false;d.before=r.data;return true;}
bool FixedIniTransaction::Existed()const{return p->disk.existed;}
const std::string& FixedIniTransaction::Before()const{return p->disk.before;}
void FixedIniTransaction::SetAfter(std::string v){p->disk.after=std::move(v);}
const std::string& FixedIniTransaction::After()const{return p->disk.after;}
bool FixedIniTransaction::OriginalMatches(){auto& d=p->disk;Identity id;auto r=ReadPath(d.root,d.target,&id,false);return d.existed?r.status==Status::Ok&&r.data==d.before&&id==d.originalId:r.status==Status::NotFound;}
std::string FixedIniTransaction::Current(){auto& d=p->disk;auto r=ReadPath(d.root,d.target,nullptr,false);if(r.status==Status::NotFound)return {};if(r.status!=Status::Ok)throw std::runtime_error("ini-readback");return r.data;}
bool FixedIniTransaction::Stage(bool partial){auto& d=p->disk;if(!CreateOwned(d.root,d.target,".pending",partial?d.after.substr(0,d.after.size()/2):d.after,d.temp))return false;if(partial)return false;return !d.existed||CreateOwned(d.root,d.target,".backup",d.before,d.backup);}
bool FixedIniTransaction::Replace(bool inject){auto& d=p->disk;BOOL ok=FALSE;if(!inject)ok=d.existed?ReplaceFileW(d.target.c_str(),d.temp.path.c_str(),d.backup.path.c_str(),0,nullptr,nullptr):MoveFileExW(d.temp.path.c_str(),d.target.c_str(),MOVEFILE_WRITE_THROUGH);p->error=ok||inject?0:GetLastError();d.refreshAfterReplace();return ok!=FALSE;}
bool FixedIniTransaction::Restore(){return p->disk.restore();}
bool FixedIniTransaction::Clean(){return p->disk.clean();}
unsigned long FixedIniTransaction::Error()const{return p->error;}
}
