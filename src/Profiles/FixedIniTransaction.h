#pragma once
#include <filesystem>
#include <memory>
#include <string>
namespace TULLBUNG::ProfileFileIO {
class FixedIniTransaction {
 struct Impl;std::unique_ptr<Impl> p;
 public:FixedIniTransaction();~FixedIniTransaction();
 bool Open(const std::filesystem::path& root);bool Existed()const;const std::string& Before()const;
 void SetAfter(std::string);const std::string& After()const;bool OriginalMatches();std::string Current();
 bool Stage(bool partial=false);bool Replace(bool inject=false);bool Restore();bool Clean();unsigned long Error()const;
};}
