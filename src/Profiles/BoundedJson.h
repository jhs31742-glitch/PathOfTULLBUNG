#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace POT::Precode {
struct Json {
 enum Type { Null,Bool,Number,String,Object,Array } type=Null;
 bool b=false;double n=0;std::string s;std::map<std::string,Json> o;std::vector<Json> a;
 const Json& at(std::string_view key)const {auto i=o.find(std::string(key));if(type!=Object||i==o.end())throw std::runtime_error("missing-field:"+std::string(key));return i->second;}
 bool has(std::string_view key)const{return type==Object&&o.contains(std::string(key));}
};
Json ParseJson(std::string_view raw);
bool Utf8(std::string_view raw);
std::size_t Utf16Length(std::string_view raw);
std::string Unhex(std::string_view raw);
std::string Hash(std::string_view raw);
}

namespace POT::Precode {Json ParsePacket(std::string_view raw);}
