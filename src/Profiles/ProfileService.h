#pragma once
#include <string>
namespace TULLBUNG::UIProfiles {std::string HandleRequest(const std::string& packet);using DiagnosticSink=void(*)(const std::string&);void SetDiagnosticSink(DiagnosticSink);bool Busy();void SetAvailable(bool);void InvalidateSession() noexcept;}
