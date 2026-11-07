#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
namespace TULLBUNG::ProfileFileIO {
inline constexpr std::size_t kMaxBytes = 1000000;
// Original values 0..7 and existing call signatures are retained.
enum class Status { Ok, InvalidName, InvalidData, InvalidRoot, NotFound, TooLarge, IoError, VerifyFailed,
                    ReplaceFailed, RollbackFailed, CleanupFailed };
struct ReadResult { Status status{Status::IoError}; std::string data; };
struct ListResult { Status status{Status::IoError}; std::vector<std::string> names; };
[[nodiscard]] bool ValidName(const std::string& name) noexcept;
[[nodiscard]] bool ValidUtf8(const std::string& text) noexcept;
[[nodiscard]] ReadResult Read(const std::filesystem::path& root, const std::string& name) noexcept;
[[nodiscard]] Status Write(const std::filesystem::path& root, const std::string& name, const std::string& data, bool injectBeforeCommit=false) noexcept;
[[nodiscard]] Status WriteIf(const std::filesystem::path&,const std::string&,const std::string&,const std::string& expectedRaw,bool createNew) noexcept;
[[nodiscard]] Status RemoveIf(const std::filesystem::path&,const std::string&,const std::string& expectedRaw) noexcept;
[[nodiscard]] ListResult List(const std::filesystem::path& root) noexcept;
#ifdef TULL_PROFILE_IO_TESTING
enum class Fault { None, TempWrite, TempVerify, BeforeCommit, AfterCommit, PostVerify, Rollback, Cleanup, ReplaceUnchanged, ReplaceMissing, ReplaceInstalled };
enum class Point { TempName, BackupName, BeforeReplace, AfterReplace, BeforeRollback };
using TestHook = void(*)(Point, const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&);
void SetFault(Fault) noexcept;
void SetHook(TestHook) noexcept;
unsigned long LastWin32Error() noexcept;
#endif
}
