#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include "TitleSystem.h"
namespace TULLBUNG::UIProfiles {
// One coherent save view; not a new disk record or TitleRuntime owner.
struct SaveTitleSnapshot {TitleSystem::PersistedState title;TitleSystem::CustomTitleRecords custom;};
void InitializeSaveTitleBoundary();
SaveTitleSnapshot CaptureSaveTitles(const TitleSystem::Runtime&);
void ResetSaveTitleSnapshotForLoad() noexcept;
struct Faults {std::string at;bool diskRecovery=false,memoryRecovery=false,titleRecovery=false;};
class Transaction {struct Impl;std::unique_ptr<Impl> p;public:
 explicit Transaction(TitleSystem::Runtime&);~Transaction();
 bool Prepare(const std::filesystem::path&,const std::string&,const std::string&,const std::string&,const std::string&);
 std::string Commit(const std::string&,const std::string&);std::string Abort(const std::string&,const std::string&);std::string Query(const std::string&,const std::string&);bool Finalize(const std::string&,const std::string&);
 unsigned long OSError()const;std::string State()const;std::string Error()const;bool Finalized()const;void Inject(Faults);std::string DiskPreSha()const;std::string DiskPostSha();
};}
