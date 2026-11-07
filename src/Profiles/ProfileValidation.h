#pragma once
#include "BoundedJson.h"
#include "UISettings.h"
#include "TitleSystem.h"
#include <variant>
namespace POT::Precode {
using Scalar=std::variant<std::string,bool,float>;
struct NativeChange {std::string key;Scalar before,after;};
struct TitleChange {std::uint16_t id;std::string beforePrefix,beforeSuffix,afterPrefix,afterSuffix;};
struct CurrentSnapshot {TULLBUNG::UISettings::Settings settings;TULLBUNG::TitleSystem::CustomTitleRecords titles;};
struct TypedPlan {std::vector<NativeChange> native;std::vector<TitleChange> titles;std::vector<std::string> numericNoOps;bool requiresOwnerGrid=false;std::string planHash;};
std::vector<std::string> BrowserKeys();
std::vector<std::string> NativeKeys();
Json CheckProfile(std::string_view raw,bool complete=false);
TypedPlan ValidatePlan(std::string_view normalizedRaw,std::string_view wire,std::string_view planHash,const CurrentSnapshot& current);
}
