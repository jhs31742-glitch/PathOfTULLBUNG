#pragma once
// Reuses the existing QA15 BLD1 v2 wire contract, not a new record or generic migration engine.
#include "BuildState.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace TULLBUNG::VersionedBuildRecord {
inline constexpr std::uint32_t kType=0x31444C42,kV1=1,kV2=2;
inline constexpr std::size_t kV1Length=590,kV2Length=639;
using State=BuildState::PersistentState;using Retained=BuildState::RetainedQAState;
struct Encoded {std::uint32_t version=1,length=590;std::array<std::uint8_t,639> bytes{};};
inline bool ValidateExtension(Retained& r) noexcept {
 auto& x=r.extension;if(x[0]>1)return false;std::uint16_t spent=0;
 for(std::size_t i=0;i<16;i++){auto a=x[1+i],e=x[17+i],p=x[33+i];if(a>1||e>1||(!a&&(e||p)))return false;
  if(i>=6){if(a||e||p)return false;continue;}if(a&&(!x[0]||p!=(i==5?3:2)))return false;spent=static_cast<std::uint16_t>(spent+p);
 }
 if(x[6])for(std::size_t i=0;i<5;i++)if(!x[1+i])return false;
 r.spent=spent;return true;
}
inline bool Decode(std::uint32_t version,std::span<const std::byte> bytes,State& output,Retained& retained) noexcept {
 const bool v1=version==kV1&&bytes.size()==kV1Length,v2=version==kV2&&bytes.size()==kV2Length;if(!v1&&!v2)return false;
 const auto* b=reinterpret_cast<const std::uint8_t*>(bytes.data());State s{};Retained r{};
 s.highestPlayerLevelSeen=static_cast<std::uint16_t>(b[0]|(static_cast<std::uint16_t>(b[1])<<8));
 const std::size_t classes=v1?12:13,nodes=v1?192:208,acquired=2+classes,enabled=acquired+nodes,paid=enabled+nodes;
 std::copy_n(b+2,(std::min)(classes,s.acquiredClasses.size()),s.acquiredClasses.begin());
 std::copy_n(b+acquired,(std::min)(nodes,s.acquiredNodes.size()),s.acquiredNodes.begin());
 std::copy_n(b+enabled,(std::min)(nodes,s.enabledNodes.size()),s.enabledNodes.begin());
 std::copy_n(b+paid,(std::min)(nodes,s.paidCost.size()),s.paidCost.begin());
 if(v2){r.version2=true;r.extension[0]=b[14];std::copy_n(b+15+192,16,r.extension.begin()+1);std::copy_n(b+223+192,16,r.extension.begin()+17);std::copy_n(b+431+192,16,r.extension.begin()+33);if(!ValidateExtension(r))return false;}
 output=s;retained=r;return true;
}
inline Encoded Encode(const State& s,const Retained& retained) noexcept {
 Encoded out;bool v2=retained.version2;
 if constexpr(BuildState::kQA15ExtensionEnabled){if(s.acquiredClasses[12])v2=true;for(std::size_t i=192;i<208;i++)if(s.acquiredNodes[i]||s.enabledNodes[i]||s.paidCost[i])v2=true;}
 out.version=v2?2:1;out.length=v2?639:590;auto& b=out.bytes;b[0]=static_cast<std::uint8_t>(s.highestPlayerLevelSeen&255);b[1]=static_cast<std::uint8_t>(s.highestPlayerLevelSeen>>8);
 const std::size_t classes=v2?13:12,nodes=v2?208:192,acquired=2+classes,enabled=acquired+nodes,paid=enabled+nodes;
 std::copy_n(s.acquiredClasses.begin(),(std::min)(classes,s.acquiredClasses.size()),b.begin()+2);
 std::copy_n(s.acquiredNodes.begin(),(std::min)(nodes,s.acquiredNodes.size()),b.begin()+acquired);
 std::copy_n(s.enabledNodes.begin(),(std::min)(nodes,s.enabledNodes.size()),b.begin()+enabled);
 std::copy_n(s.paidCost.begin(),(std::min)(nodes,s.paidCost.size()),b.begin()+paid);
 if constexpr(!BuildState::kQA15ExtensionEnabled){if(v2){b[14]=retained.extension[0];std::copy_n(retained.extension.begin()+1,16,b.begin()+15+192);std::copy_n(retained.extension.begin()+17,16,b.begin()+223+192);std::copy_n(retained.extension.begin()+33,16,b.begin()+431+192);}}
 return out;
}
}
