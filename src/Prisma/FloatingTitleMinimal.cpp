#include "PCH.h"
#include "Prisma/FloatingTitleMinimal.h"
#include "Prisma/PrismaBridge.h"
#include "Profiles/BoundedJson.h"
#include "Profiles/ProfileService.h"
#include "Profiles/ProfileFileIO.h"
#include <ShlObj.h>
#include "Profiles/RuntimeBoundary.h"
#include "TitleRuntime.h"
#include "RE/H/HUDMenu.h"
#include "RE/G/GViewport.h"
#include "RE/S/State.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace TULLBUNG::FloatingTitleMinimal {
namespace {
using Json = POT::Precode::Json;
constexpr std::uint64_t kMaxSerial=9007199254740991ULL;
constexpr char kViewPath[]="pathoftullbung_debug/floating_title/index.html";
struct State {
    std::recursive_mutex mutex;
    std::atomic<bool> dirty{true};
    std::atomic<unsigned> callbacks{0};
    PRISMA_UI_API::IVPrismaUI1* api{};
    StatusSink status{};
    PrismaView view{};
    bool attempted{},dom{},rendererReady{},ready{},enabled{},styleReady{},pending{true},inflight{},suspended{};
    std::uint64_t session{1},revision{1},styleRevision{},serial{},lastEngineFrame{};
    std::uint64_t flightSession{},flightRevision{},flightSerial{};
    bool flightIsFrame{},flightVisible{};
    std::uint32_t frameThread{};
    HWND timerWindow{};
    bool frameThreadRejected{}, restorePending{};
    unsigned frameDiagnostics{},viewportDiagnostics{};
    std::chrono::steady_clock::time_point sentAt{};
    std::array<bool,6> active{};
    std::array<std::string,6> text{};
    std::string pendingOn;
    std::chrono::steady_clock::time_point pendingOnUntil{};
    std::string style{"null"},fonts{R"({"fonts":[]})"};
    const char* reason{"off"};
};
// Intentional process lifetime: callbacks/tasks never capture a destructible owner.
// No FreeLibrary/hot unload, engine cleanup in static destructors or dynamic rebind.
State& S(){static auto* value=new State;return *value;}
std::string Q(std::string_view value){
    std::string out="\"";
    for(unsigned char ch:value){switch(ch){
    case '\\':out+="\\\\";break;case '"':out+="\\\"";break;
    case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;
    default:if(ch<32){constexpr char hex[]="0123456789abcdef";out+="\\u00";out+=hex[ch>>4];out+=hex[ch&15];}else out+=static_cast<char>(ch);
    }}return out+'"';
}
void Require(bool ok){if(!ok)throw std::runtime_error("ft-packet-invalid");}
std::uint64_t U(const Json& value){
    Require(value.type==Json::Number&&value.n>=0&&value.n<=static_cast<double>(kMaxSerial)&&std::floor(value.n)==value.n);
    return static_cast<std::uint64_t>(value.n);
}
double N(const Json& value,double low,double high){
    Require(value.type==Json::Number&&std::isfinite(value.n)&&std::floor(value.n)==value.n&&value.n>=low&&value.n<=high);return value.n;
}
bool Color(const Json& v){
    if(v.type!=Json::String||v.s.size()!=7||v.s[0]!='#')return false;
    for(std::size_t i=1;i<7;++i)if(!((v.s[i]>='0'&&v.s[i]<='9')||(v.s[i]>='a'&&v.s[i]<='f')||(v.s[i]>='A'&&v.s[i]<='F')))return false;
    return true;
}
void Hide(State& s){if(s.api&&s.view&&s.api->IsValid(s.view))s.api->Hide(s.view);}
void Status(State& s,std::uint64_t request=0,const char* result=nullptr){
    if(!s.status)return;
    const auto payload=std::string(R"({"kind":"ft-status","session":)")+std::to_string(s.session)+
        R"(,"enabled":)"+(s.enabled?"true":"false")+R"(,"driverBound":)"+(s.frameThread?"true":"false")+
        R"(,"reason":)"+Q(result?result:s.reason)+
        (request?R"(,"request":)"+std::to_string(request):std::string{})+"}";
    s.status(payload.c_str());
}
void Stop(State& s,const char* reason){
    s.pendingOn.clear();
    s.enabled=false;s.reason=reason;s.pending=true;Hide(s);
}
bool Advance(State& s,std::uint64_t& n){
    if(n==kMaxSerial){Stop(s,"serial-limit");return false;}++n;return true;
}
void FlushState(State& s){
    if(!s.ready||s.restorePending||!s.api||!s.view||!s.dom||!s.rendererReady||!s.pending||s.inflight||!s.api->IsValid(s.view))return;
    std::string payload=R"({"kind":"state","session":)"+std::to_string(s.session)+R"(,"revision":)"+std::to_string(s.revision)+
        R"(,"enabled":)"+(s.ready&&s.enabled&&s.styleReady?"true":"false")+R"(,"style":)"+s.style+R"(,"catalog":)"+s.fonts+R"(,"blocks":[)";
    for(std::size_t i=0;i<6;++i){if(i)payload+=',';payload+=R"({"slot":)"+std::to_string(i+1)+
        R"(,"active":)"+(s.active[i]?"true":"false")+R"(,"text":)"+Q(s.text[i])+"}";}
    payload+="]}";
    // Exactly one state OR frame is outstanding, with one latest pending state.
    s.inflight=true;s.flightIsFrame=false;s.flightSession=s.session;s.flightRevision=s.revision;s.flightSerial=0;
    s.sentAt=std::chrono::steady_clock::now();s.pending=false;
    s.api->InteropCall(s.view,"tullFTReceive",payload.c_str());
}
void Changed(State& s,std::uint64_t request=0){
    Hide(s);s.pending=true;Advance(s,s.revision);FlushState(s);Status(s,request);
}
void Capture(State& s){
    auto* player=RE::PlayerCharacter::GetSingleton();
    auto& runtime=TitleRuntime::GetRuntime();
    const char* name=player?player->GetName():nullptr;
    const std::string characterName=name?name:"";
    const auto& slots=runtime.Slots();
    // Values only; no changes to title acquisition, AUTO, selection or writers.
    for(std::size_t i=0;i<6;++i){s.active[i]=slots[i].active;s.text[i]=runtime.ResolveSlotText(i+1,characterName);}
    s.dirty.store(false);
}
void OwnerTask(std::function<void()> work) noexcept {
    try{
        auto& s=S();
        if(s.callbacks.fetch_add(1)>=4){s.callbacks.fetch_sub(1);return;}
        auto* tasks=SKSE::GetTaskInterface();if(!tasks){s.callbacks.fetch_sub(1);return;}
        try{tasks->AddTask([work=std::move(work)](){
            auto& state=S();
            struct Done{State& s;~Done(){s.callbacks.fetch_sub(1);}} done{state};
            try{std::lock_guard profileLock(UIProfiles::RuntimeMutex());std::lock_guard lock(state.mutex);work();}
            catch(...){try{std::lock_guard lock(state.mutex);Stop(state,"callback-failed");Status(state);}catch(...){}}
        });}catch(...){s.callbacks.fetch_sub(1);}
    }catch(...){}
}
void PumpMainThreadActivation() noexcept;
void RestorePreference() noexcept;

std::uint32_t VerifiedMainThread() {
    auto* main=RE::Main::GetSingleton();
    const auto current=GetCurrentThreadId();
    return main&&main->threadID==current?current:0;
}

UINT_PTR FrameTimerID() {
    return reinterpret_cast<UINT_PTR>(&S());
}

void CALLBACK FrameTimer(HWND window,UINT,UINT_PTR id,DWORD) noexcept {
    try{
        if(id!=FrameTimerID()||!VerifiedMainThread())return;
        auto& s=S();
        {
            std::lock_guard lock(s.mutex);
            if(window!=s.timerWindow||!s.ready)return;
        }
        RestorePreference();
        PumpMainThreadActivation();
        {
            std::lock_guard lock(s.mutex);
            if(!s.enabled||!s.rendererReady)return;
        }
        auto* ui=RE::UI::GetSingleton();
        auto* graphics=RE::BSGraphics::State::GetSingleton();
        auto hud=ui?ui->GetMenu<RE::HUDMenu>():RE::GPtr<RE::HUDMenu>{};
        if(!graphics||!hud||!hud->uiMovie){SuspendForMenu();return;}
        const auto frame=graphics->frameCount;
        {std::lock_guard lock(s.mutex);if(!frame||frame<=s.lastEngineFrame)return;}
        RE::GViewport viewport{};
        hud->uiMovie->GetViewport(&viewport);
        if(viewport.bufferWidth<=24||viewport.bufferHeight<=24||viewport.left!=0||viewport.top!=0||
            viewport.width!=viewport.bufferWidth||viewport.height!=viewport.bufferHeight){
            SuspendForMenu();
            return;
        }
        OnVerifiedGameFrame({frame,static_cast<float>(viewport.bufferWidth),static_cast<float>(viewport.bufferHeight)});
    }catch(...){try{auto& s=S();std::lock_guard lock(s.mutex);Stop(s,"frame-failed");Status(s);}catch(...){}}
}

void StopFrameTimer(State& s) {
    if(!s.timerWindow)return;
    ::KillTimer(s.timerWindow,FrameTimerID());
    s.timerWindow=nullptr;
}

void StartFrameTimer(State& s) {
    if(REL::Module::get().version()!=SKSE::RUNTIME_SSE_1_5_97)return;
    const auto thread=VerifiedMainThread();
    if(!thread)return;
    const auto window=reinterpret_cast<HWND>(RE::Main::GetSingleton()->wnd);
    DWORD process{};
    if(!window||::GetWindowThreadProcessId(window,&process)!=thread||process!=::GetCurrentProcessId())return;
    if(s.timerWindow==window)return;
    StopFrameTimer(s);
    if(!::SetTimer(window,FrameTimerID(),16,FrameTimer))return;
    s.timerWindow=window;
    logger::info("[FTMIN] Game window frame timer armed | Main={} | CAMERA_ORDER=RUNTIME_UNVERIFIED",thread);
}

void Receipt(const char* argument) noexcept {
    try{
        if(!argument)return;
        const auto length=strnlen_s(argument,257);if(length>256)return;
        std::string owned(argument,length); // Prisma pointer never outlives this call.
        OwnerTask([owned=std::move(owned)](){
            auto& s=S();const auto j=POT::Precode::ParseJson(owned);
            const auto& kind=j.at("kind");Require(kind.type==Json::String);
            if(kind.s=="ready"){
                if(j.o.size()==1&&s.view&&s.api&&s.api->IsValid(s.view)&&!s.rendererReady){
                    s.rendererReady=true;
                    FlushState(s);
                }
                return;
            }
            const auto session=U(j.at("session")),revision=U(j.at("revision"));
            if(kind.s=="font-failed"){
                if(session==s.session&&revision==s.revision){Stop(s,"font-failed");Status(s);}return;
            }
            const bool frame=kind.s=="frame";Require(frame||kind.s=="state");
            const auto serial=frame?U(j.at("serial")):0;
            if(!s.inflight||session!=s.flightSession||revision!=s.flightRevision||
                frame!=s.flightIsFrame||serial!=s.flightSerial)return;
            // A superseded receipt releases transport capacity only, never display permission.
            s.inflight=false;
            if(frame&&s.flightVisible&&!s.suspended&&!s.frameThreadRejected&&session==s.session&&revision==s.revision&&s.ready&&s.enabled&&s.styleReady&&s.api&&s.api->IsValid(s.view))
                s.api->Show(s.view);
            FlushState(s);
        });
    }catch(...){}
}
void DomReady(PrismaView view) noexcept {
    OwnerTask([view](){auto& s=S();if(!view||view!=s.view||!s.api||!s.api->IsValid(view))return;
        s.dom=true;Hide(s);s.pending=true;FlushState(s);Status(s);});
}
void EnsureView(State& s){
    if(!s.api){Stop(s,"api-unavailable");return;} if(s.attempted){if(!s.view||!s.api->IsValid(s.view))Stop(s,"view-unavailable");return;}
    s.attempted=true; // Single creation attempt; no stale callback / ID rebind architecture.
    s.view=s.api->CreateView(kViewPath,DomReady);
    if(!s.view){Stop(s,"view-create-failed");return;}
    Hide(s);
    s.api->RegisterJSListener(s.view,"tullFTTransportReceipt",Receipt);
}
void ValidateStyle(const Json& j,State& s,bool activation=false,bool restoring=false){
    Require(j.type==Json::Object&&j.o.size()==(activation?9u:8u));
    const auto session=U(j.at("session"));
    const auto seq=U(j.at("styleRevision"));
    Require(restoring ? session>0&&seq>0 : session==s.session&&seq>s.styleRevision);
    if(activation)Require(U(j.at("request"))>0);
    N(j.at("hostWidth"),1,16384);N(j.at("hostHeight"),1,16384);
    const auto& defaultFont=j.at("defaultFont");
    Require(defaultFont.type==Json::String&&!defaultFont.s.empty()&&defaultFont.s.size()<=2048&&defaultFont.s.find('\0')==std::string::npos);
    Require(Color(j.at("fill"))&&Color(j.at("outline")));
    const auto& blocks=j.at("blocks");Require(blocks.type==Json::Array&&blocks.a.size()==6);
    std::array<bool,6> seen{};
    for(const auto& b:blocks.a){
        Require(b.type==Json::Object&&b.o.size()==10);
        const auto slot=U(b.at("slot"));Require(slot>=1&&slot<=6&&!seen[slot-1]);seen[slot-1]=true;
        const auto x=N(b.at("x"),0,100),y=N(b.at("y"),0,100),w=N(b.at("w"),4,100),h=N(b.at("h"),6,100);
        Require(x+w<=100&&y+h<=100);
        N(b.at("fontScale"),50,400);N(b.at("opacity"),0,100);N(b.at("rotationDeg"),-180,180);N(b.at("outlineWidth"),0,8);
        const auto& font=b.at("fontId");
        Require(font.type==Json::String&&!font.s.empty()&&font.s.size()<=2048&&font.s.find('\0')==std::string::npos);
        for(unsigned char ch:font.s)Require(ch>=32&&ch!=127);
    }
    if(!restoring)s.styleRevision=seq;
}

constexpr char kPreferenceFile[]="FloatingTitleDev09.json";

std::filesystem::path PreferenceRoot()
{
    PWSTR raw=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Documents,KF_FLAG_DEFAULT,nullptr,&raw)))
        throw std::runtime_error("ft-documents-folder");
    std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> documents(raw,&CoTaskMemFree);
    return std::filesystem::path(documents.get()) / L"My Games" /
        L"Skyrim Special Edition" / L"PathOfTULLBUNG" / L"Preferences";
}

bool SavePreference(const std::string& raw) noexcept
{
    try {
        if(ProfileFileIO::Write(PreferenceRoot(),kPreferenceFile,raw)==ProfileFileIO::Status::Ok)
            return true;
    } catch(...) {}
    logger::warn("[FTMIN] Preference write failed");
    return false;
}

void RestorePreference() noexcept
{
    try {
        std::lock_guard profileLock(UIProfiles::RuntimeMutex());
        auto& s=S();
        std::lock_guard lock(s.mutex);
        if(!s.restorePending||!s.ready||UIProfiles::Busy())return;
        s.restorePending=false;
        const auto saved=ProfileFileIO::Read(PreferenceRoot(),kPreferenceFile);
        if(saved.status==ProfileFileIO::Status::NotFound||saved.status==ProfileFileIO::Status::InvalidRoot){
            Changed(s);
            return;
        }
        Require(saved.status==ProfileFileIO::Status::Ok&&saved.data.size()<=65536);
        if(saved.data=="{}"){Changed(s);return;}
        const auto style=POT::Precode::ParseJson(saved.data);
        ValidateStyle(style,s,style.has("request"),true);
        if(!BindVerifiedFrameThread(VerifiedMainThread()))return;
        s.style=saved.data;
        s.styleReady=true;
        Capture(s);
        s.enabled=true;
        s.reason="awaiting-frame";
        EnsureView(s);
        Changed(s);
        logger::info("[FTMIN] Preference restored | Enabled={}",s.enabled);
    } catch(...) {
        auto& s=S();
        std::lock_guard lock(s.mutex);
        Stop(s,"preference-read-failed");
        Status(s);
        logger::warn("[FTMIN] Preference read or validation failed");
    }
}

bool Finite(const RE::NiPoint3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
void Configure(PRISMA_UI_API::IVPrismaUI1* api,StatusSink sink) noexcept {
    try{auto& s=S();std::lock_guard lock(s.mutex);if(!s.api)s.api=api;else if(s.api!=api){Stop(s,"api-replaced");return;}s.status=sink;}catch(...){}
}
void MarkTitleDirty() noexcept {try{S().dirty.store(true);}catch(...){}}
void RefreshTitles() noexcept {
    try{std::lock_guard profileLock(UIProfiles::RuntimeMutex());auto& s=S();std::lock_guard lock(s.mutex);
        if(!s.ready)return;
        if(!VerifiedMainThread()){s.dirty.store(true);return;}
        const auto priorText=s.text;const auto priorActive=s.active;Capture(s);
        if(priorText!=s.text||priorActive!=s.active)Changed(s);else FlushState(s);
    }catch(...){try{auto& s=S();std::lock_guard lock(s.mutex);Stop(s,"snapshot-failed");}catch(...){}}
}
void SetGameReady(bool ready) noexcept {
    try{std::lock_guard profileLock(UIProfiles::RuntimeMutex());auto& s=S();std::lock_guard lock(s.mutex);
        s.ready=ready;
        s.restorePending=ready;
        if(!ready){
            if(VerifiedMainThread())StopFrameTimer(s);
            Stop(s,"session-reset");
            Advance(s,s.session);
            // Discard the previous save's renderer receipt ownership.
            s.inflight=false;
            s.suspended=true;
            s.styleReady=false;
            s.style="null";
            s.styleRevision=0;
            s.lastEngineFrame=0;
            s.frameDiagnostics=0;
        }else if(VerifiedMainThread()){
            StartFrameTimer(s);
            Capture(s);
        }else{
            s.dirty.store(true);
        }
    }catch(...){}
}
void InvalidateForImport() noexcept {
    try {
        auto& s=S();
        std::lock_guard lock(s.mutex);
        const bool cleared=SavePreference("{}");
        s.restorePending=false;
        Stop(s,cleared?"import":"preference-write-failed");
        Advance(s,s.session);
        s.styleReady=false;
        s.style="null";
        s.styleRevision=0;
        Changed(s);
    } catch(...) {}
}
void SuspendForMenu() noexcept {
    try{auto& s=S();std::lock_guard lock(s.mutex);if(!s.suspended){s.suspended=true;Hide(s);}}catch(...){}
}
void SetFontCatalog(std::string_view raw) noexcept {
    try{const auto j=POT::Precode::ParseJson(raw);Require(j.at("fonts").type==Json::Array);
        auto& s=S();std::lock_guard lock(s.mutex);
        if(s.fonts!=raw){s.fonts.assign(raw);Changed(s);}
    }catch(...){try{auto& s=S();std::lock_guard lock(s.mutex);Stop(s,"catalog-failed");Status(s);}catch(...){}}
}
bool HandleCommand(std::string_view command) noexcept {
    if(!command.starts_with("ft."))return false;
    const auto traceOn=[isOn=command.starts_with("ft.on:")](const char* result,std::uint64_t session) noexcept {
        static std::atomic<unsigned> count{};
        if(!isOn||count.fetch_add(1,std::memory_order_relaxed)>=8)return;
        try{logger::info("[FTTRACE] on-result | Result={} | Session={} | Thread={}",result,session,GetCurrentThreadId());}catch(...){}
    };
    std::uint64_t request=0;
    try{
        std::lock_guard profileLock(UIProfiles::RuntimeMutex());auto& s=S();std::lock_guard lock(s.mutex);
        if(command=="ft.off"){
            s.restorePending=false;
            const bool cleared=SavePreference("{}");
            Stop(s,cleared?"off":"preference-write-failed");
            Changed(s);
            return true;
        }
        if(command=="ft.state"){Status(s);return true;}
        const bool busy=UIProfiles::Busy();
        if(command.starts_with("ft.on:")){
            const auto packet=POT::Precode::ParseJson(command.substr(6));
            request=U(packet.at("request"));Require(request!=0);
            const auto& style=packet;
            const auto reject=[&](const char* reason){traceOn(reason,s.session);Status(s,request,reason);};
            if(U(style.at("session"))!=s.session){reject("session-mismatch");return true;}
            if(busy||!s.ready){reject(busy?"profile-busy":"game-not-ready");return true;}
            const auto revision=U(style.at("styleRevision"));
            if(revision<=s.styleRevision){reject("style-stale");return true;}
            if(REL::Module::get().version()!=SKSE::RUNTIME_SSE_1_5_97){reject("frame-runtime-unsupported");return true;}
            if(!s.timerWindow){reject("frame-main-thread-unavailable");return true;}
            const auto thread=VerifiedMainThread();
            if(!thread){
                s.pendingOn.assign(command);
                // Expire before the UI releases its pending request at 2500 ms.
                s.pendingOnUntil=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                return true;
            }
            s.pendingOn.clear();
            if(!BindVerifiedFrameThread(thread)){reject("frame-main-thread-unavailable");return true;}
            const auto wire=command.substr(6);
            ValidateStyle(style,s,true);
            s.style.assign(wire);s.styleReady=true;
            s.frameThreadRejected=false;
            Capture(s);s.enabled=true;s.reason="awaiting-frame";
            s.restorePending=false;
            EnsureView(s);
            if(s.enabled&&!SavePreference(s.style))Stop(s,"preference-write-failed");
            traceOn(s.reason,s.session);Changed(s,request);return true;
        }
        if(busy||!s.ready){Status(s);return true;}
        if(command.starts_with("ft.style:")){
            const auto raw=command.substr(9);Require(raw.size()<=65536);
            const auto j=POT::Precode::ParseJson(raw);if(U(j.at("session"))!=s.session||U(j.at("styleRevision"))<=s.styleRevision)return true;ValidateStyle(j,s);
            if(s.enabled&&!SavePreference(std::string(raw))){
                Status(s,0,"preference-write-failed");
                return true;
            }
            s.style.assign(raw);s.styleReady=true;s.dirty.store(true);Changed(s);return true;
        }
        Status(s);
    }catch(...){try{auto& s=S();std::lock_guard lock(s.mutex);traceOn("command-rejected",s.session);Stop(s,"command-rejected");Status(s,request);}catch(...){}}
    return true;
}
namespace {
void PumpMainThreadActivation() noexcept {
    try{
        auto& s=S();
        {std::lock_guard lock(s.mutex);if(s.pendingOn.empty())return;}
        if(!VerifiedMainThread())return;
        std::lock_guard profileLock(UIProfiles::RuntimeMutex());
        std::lock_guard lock(s.mutex);
        if(s.pendingOn.empty())return;
        auto command=std::exchange(s.pendingOn,{});
        if(std::chrono::steady_clock::now()>s.pendingOnUntil){
            const auto packet=POT::Precode::ParseJson(std::string_view(command).substr(6));
            Status(s,U(packet.at("request")),"frame-main-thread-unavailable");
            return;
        }
        HandleCommand(command);
    }catch(...){}
}
} // namespace
bool BindVerifiedFrameThread(std::uint32_t threadID) noexcept {
    // Binding requires the SDK engine main ID and current thread to agree.
    try{auto* main=RE::Main::GetSingleton();if(!main||!threadID||main->threadID!=threadID||GetCurrentThreadId()!=threadID)return false;
        auto& s=S();std::lock_guard lock(s.mutex);if(s.frameThread&&s.frameThread!=threadID)return false;if(s.frameThread==threadID)return true;
        s.frameThread=threadID;s.reason="awaiting-frame";logger::info("[FTMIN] Frame binding | Thread={} | RUNTIME_UNVERIFIED",threadID);Status(s);return true;
    }catch(...){return false;}
}
void OnVerifiedGameFrame(const FrameContext& context) noexcept {
    try{
        auto& s=S();
        // Recheck engine main ID before Actor/3D/NiCamera access, even for a direct caller.
        {std::lock_guard lock(s.mutex);if(!s.frameThread||GetCurrentThreadId()!=s.frameThread)return;}
        std::lock_guard profileLock(UIProfiles::RuntimeMutex());std::lock_guard lock(s.mutex);
        if(!s.ready||!s.enabled||!s.styleReady||!s.dom||!s.rendererReady||!s.api||s.frameThreadRejected)return;
        auto* main=RE::Main::GetSingleton();if(!main||main->threadID!=s.frameThread){Stop(s,"frame-main-thread-changed");Status(s);return;}
        if(PrismaBridge::IsVisible()){SuspendForMenu();return;} // Continuous main-UI gate after frame connection.
        if(s.inflight&&std::chrono::steady_clock::now()-s.sentAt>std::chrono::seconds(2)){
            s.inflight=false;
            logger::warn("[FTMIN] Renderer receipt timeout | Session={} | FlightSession={}",s.session,s.flightSession);
            Stop(s,"transport-timeout");Status(s);return;
        }
        if(s.dirty.load()){Capture(s);Changed(s);}
        if(!context.engineFrame||context.engineFrame<=s.lastEngineFrame)return;
        s.lastEngineFrame=context.engineFrame;
        if(!std::isfinite(context.width)||!std::isfinite(context.height)||context.width<=24||context.height<=24)return;
        auto* ui=RE::UI::GetSingleton();auto* player=RE::PlayerCharacter::GetSingleton();auto* pc=RE::PlayerCamera::GetSingleton();
        bool visible=ui&&ui->IsShowingMenus()&&!ui->IsApplicationMenuOpen()&&player&&pc&&!pc->IsInFirstPerson()&&!UIProfiles::Busy();
        if(visible)for(const auto& menu:ui->menuStack)if(menu&&menu->OnStack()&&!menu->AlwaysOpen()){visible=false;break;}
        // No stale coordinates survive menu, POV, missing 3D, bad depth or projection failure.
        std::array<std::array<float,2>,5> points{};std::size_t count=0;
        auto* camera=visible?RE::Main::WorldRootCamera():nullptr;
        auto* root=visible?player->Get3D():nullptr;
        visible=visible&&camera&&root;
        // Candidate skeleton names; runtime node/pose validation is still required.
        constexpr std::array<const char*,5> names{"NPC Head [Head]","NPC Spine2 [Spn2]","NPC L Clavicle [LClv]","NPC R Clavicle [RClv]","NPC Pelvis [Pelv]"};
        bool head=false;
        if(visible)for(std::size_t i=0;i<names.size();++i){
            auto* node=root->GetObjectByName(RE::BSFixedString(names[i]));if(!node||!Finite(node->world.translate))continue;
            float x{},y{},z{};
            if(!camera->WorldPtToScreenPt3(node->world.translate,x,y,z,1e-5F)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||z<=0)continue;
            if(x<-2||x>3||y<-2||y>3)continue;
            points[count++]={x,1.0F-y};if(i==0)head=true;
        }
        visible=visible&&head&&count>=2;const bool wasSuspended=s.suspended;s.suspended=!visible; // No fabricated actor-height/skeleton fallback.
        if(!visible&&!wasSuspended)Hide(s);
        if(s.frameDiagnostics<4){++s.frameDiagnostics;auto* graphics=RE::BSGraphics::State::GetSingleton();
            logger::info("[FTMIN] Frame sample | Main={} | Current={} | EngineFrame={} | Viewport={}x{} | Camera={} | Actor3D={} | Anchors={} | Visible={} | InsideFrame={} | CAMERA_ORDER=RUNTIME_UNVERIFIED",
                main->threadID,GetCurrentThreadId(),context.engineFrame,context.width,context.height,camera!=nullptr,root!=nullptr,count,visible,graphics&&graphics->insideFrame);}
        if(s.inflight||s.pending){FlushState(s);return;}
        if(!Advance(s,s.serial))return;
        std::string payload=R"({"kind":"frame","session":)"+std::to_string(s.session)+R"(,"revision":)"+std::to_string(s.revision)+
            R"(,"serial":)"+std::to_string(s.serial)+R"(,"visible":)"+(visible?"true":"false")+R"(,"width":)"+std::to_string(context.width)+
            R"(,"height":)"+std::to_string(context.height)+R"(,"points":[)";
        for(std::size_t i=0;i<count;++i){if(i)payload+=',';payload+="["+std::to_string(points[i][0])+","+std::to_string(points[i][1])+"]";}
        payload+="]}";s.inflight=true;s.flightIsFrame=true;s.flightSession=s.session;s.flightRevision=s.revision;s.flightSerial=s.serial;s.sentAt=std::chrono::steady_clock::now();
        s.flightVisible=visible;
        if(!s.api->IsValid(s.view)){Stop(s,"view-invalid");Status(s);return;}
        s.api->InteropCall(s.view,"tullFTReceive",payload.c_str());
    }catch(...){try{auto& s=S();std::lock_guard lock(s.mutex);Stop(s,"frame-failed");Status(s);}catch(...){}}
}
} // namespace TULLBUNG::FloatingTitleMinimal
