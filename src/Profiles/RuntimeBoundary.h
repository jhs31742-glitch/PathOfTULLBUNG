#pragma once
#include <mutex>
namespace TULLBUNG::UIProfiles {inline std::recursive_mutex& RuntimeMutex(){static std::recursive_mutex value;return value;}}
