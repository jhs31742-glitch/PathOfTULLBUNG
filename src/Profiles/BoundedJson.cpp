#define NOMINMAX
#include "BoundedJson.h"
#include <Windows.h>
#include <bcrypt.h>
#include <cstdlib>
#include <locale.h>
#include <cmath>
#include <cstdint>
namespace POT::Precode {
namespace {void Require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}}
bool Utf8(std::string_view s){return s.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0)>0;}
std::size_t Utf16Length(std::string_view s){Require(s.size()<=1000000&&Utf8(s),"utf8");return s.empty()?0:static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0));}
std::string Unhex(std::string_view s){Require(s.size()%2==0&&s.size()<=2000000,"hex-size");auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return static_cast<unsigned>(c-'0');if(c>='a'&&c<='f')return static_cast<unsigned>(c-'a'+10);if(c>='A'&&c<='F')return static_cast<unsigned>(c-'A'+10);throw std::runtime_error("hex-digit");};std::string r;for(size_t i=0;i<s.size();i+=2)r+=static_cast<char>((digit(s[i])<<4)|digit(s[i+1]));return r;}
namespace {
struct NumericLocale {
 _locale_t value=_create_locale(LC_NUMERIC,"C");
 NumericLocale(){Require(value!=nullptr,"numeric-locale");}
 ~NumericLocale(){_free_locale(value);}
 NumericLocale(const NumericLocale&)=delete;
 NumericLocale& operator=(const NumericLocale&)=delete;
};
struct Parser {
 std::string_view text;size_t pos=0,nodes=0;NumericLocale locale;
 char peek()const{return pos<text.size()?text[pos]:'\0';}
 char take(){Require(pos<text.size(),"json-eof");return text[pos++];}
 void ws(){while(peek()==' '||peek()=='\t'||peek()=='\r'||peek()=='\n')++pos;}
 unsigned hex4(){unsigned n=0;for(int i=0;i<4;i++){auto c=take();n<<=4;if(c>='0'&&c<='9')n+=static_cast<unsigned>(c-'0');else if(c>='a'&&c<='f')n+=static_cast<unsigned>(c-'a'+10);else if(c>='A'&&c<='F')n+=static_cast<unsigned>(c-'A'+10);else throw std::runtime_error("json-unicode");}return n;}
 static void encode(std::string& s,unsigned c){if(c<128)s+=static_cast<char>(c);else if(c<2048){s+=static_cast<char>(0xC0|(c>>6));s+=static_cast<char>(0x80|(c&63));}else if(c<65536){s+=static_cast<char>(0xE0|(c>>12));s+=static_cast<char>(0x80|((c>>6)&63));s+=static_cast<char>(0x80|(c&63));}else{s+=static_cast<char>(0xF0|(c>>18));s+=static_cast<char>(0x80|((c>>12)&63));s+=static_cast<char>(0x80|((c>>6)&63));s+=static_cast<char>(0x80|(c&63));}}
 std::string string(){Require(take()=='"',"json-string");std::string s;for(;;){unsigned char c=static_cast<unsigned char>(take());if(c=='"')break;Require(c>=32,"json-control");if(c!='\\'){s+=static_cast<char>(c);continue;}char e=take();switch(e){case '"':case '\\':case '/':s+=e;break;case 'b':s+='\b';break;case 'f':s+='\f';break;case 'n':s+='\n';break;case 'r':s+='\r';break;case 't':s+='\t';break;case 'u':{unsigned u=hex4();if(u>=0xD800&&u<=0xDBFF){Require(take()=='\\'&&take()=='u',"json-surrogate");auto low=hex4();Require(low>=0xDC00&&low<=0xDFFF,"json-surrogate");u=0x10000+((u-0xD800)<<10)+(low-0xDC00);}else Require(!(u>=0xDC00&&u<=0xDFFF),"json-surrogate");encode(s,u);break;}default:throw std::runtime_error("json-escape");}}Require(Utf8(s),"json-utf8");return s;}
 Json value(unsigned depth){ws();Require(depth<=32&&++nodes<=100000,"json-depth-or-nodes");Json j;
 if(peek()=='{'){take();j.type=Json::Object;ws();if(peek()=='}'){take();return j;}for(;;){ws();auto key=string();Require(key!="__proto__"&&key!="constructor"&&key!="prototype","forbidden-property");ws();Require(take()==':',"json-colon");auto v=value(depth+1);Require(j.o.emplace(key,std::move(v)).second,"duplicate-key");ws();auto c=take();if(c=='}')return j;Require(c==',',"json-comma");}}
 if(peek()=='['){take();j.type=Json::Array;ws();if(peek()==']'){take();return j;}for(;;){j.a.push_back(value(depth+1));ws();auto c=take();if(c==']')return j;Require(c==',',"json-comma");}}
 if(peek()=='"'){j.type=Json::String;j.s=string();return j;}
 for(auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(text.substr(pos,literal.size())==literal){pos+=literal.size();j.type=literal=="null"?Json::Null:Json::Bool;j.b=literal=="true";return j;}
 const auto start=pos;if(peek()=='-')take();if(peek()=='0')take();else{Require(peek()>='1'&&peek()<='9',"json-number");while(peek()>='0'&&peek()<='9')take();}if(peek()=='.'){take();Require(peek()>='0'&&peek()<='9',"json-number");while(peek()>='0'&&peek()<='9')take();}if(peek()=='e'||peek()=='E'){take();if(peek()=='+'||peek()=='-')take();Require(peek()>='0'&&peek()<='9',"json-exponent");while(peek()>='0'&&peek()<='9')take();}
 // Grammar is validated above. A fixed C locale preserves finite JSON underflow as zero.
 std::string token(text.substr(start,pos-start));char* end=nullptr;j.n=_strtod_l(token.c_str(),&end,locale.value);Require(end==token.c_str()+token.size()&&std::isfinite(j.n),"json-number-finite");j.type=Json::Number;return j;
 }
};
}
Json ParseJson(std::string_view raw){Require(raw.size()<=1000000&&Utf8(raw),"size-or-encoding");Parser p{raw};auto v=p.value(0);p.ws();Require(p.pos==raw.size(),"json-trailing");return v;}
Json ParsePacket(std::string_view raw){Require(raw.size()<=4000000&&Utf8(raw),"packet-size-or-encoding");Parser p{raw};auto v=p.value(0);p.ws();Require(p.pos==raw.size(),"json-trailing");return v;}
std::string Hash(std::string_view s){
 Require(s.size()<=2000001,"hash-size");BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;DWORD cb=0,n=0;std::string out;
 Require(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"sha-open");
 bool ok=BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&n),sizeof(n),&cb,0)>=0;
 std::vector<unsigned char> obj(n),digest(32);if(ok)ok=BCryptCreateHash(a,&h,obj.data(),n,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())),static_cast<ULONG>(s.size()),0)>=0;
 if(ok)ok=BCryptFinishHash(h,digest.data(),32,0)>=0;
 if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);Require(ok,"sha-failed");for(auto b:digest){out+="0123456789ABCDEF"[b>>4];out+="0123456789ABCDEF"[b&15];}return out;
}
}
