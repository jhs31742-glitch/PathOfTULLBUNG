add_library(CommonLibSSE::CommonLibSSE STATIC IMPORTED GLOBAL)
set_target_properties(CommonLibSSE::CommonLibSSE PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS "WINVER=0x0601;_WIN32_WINNT=0x0601;\$<\$<BOOL:OFF>:REX_OPTION_INI=1>;\$<\$<BOOL:OFF>:REX_OPTION_JSON=1>;\$<\$<BOOL:OFF>:REX_OPTION_TOML=1>;\$<\$<BOOL:OFF>:SKSE_SUPPORT_XBYAK=1>;\$<\$<BOOL:ON>:ENABLE_SKYRIM_SE=1>;\$<\$<BOOL:ON>:ENABLE_SKYRIM_AE=1>;\$<\$<BOOL:OFF>:ENABLE_SKYRIM_VR=1>;\$<\$<AND:\$<BOOL:ON>,\$<BOOL:ON>>:HAS_SKYRIM_MULTI_TARGETING=1>;\$<\$<AND:\$<BOOL:ON>,\$<BOOL:OFF>>:HAS_SKYRIM_MULTI_TARGETING=1>;\$<\$<AND:\$<BOOL:ON>,\$<BOOL:OFF>>:HAS_SKYRIM_MULTI_TARGETING=1>"
  INTERFACE_COMPILE_FEATURES "cxx_std_23"
  INTERFACE_COMPILE_OPTIONS "\$<\$<CONFIG:Debug>:/Zo>;/Zc:preprocessor;\$<\$<CONFIG:Debug>:/wd4702>;/wd4005;/wd4061;/wd4068;/wd4200;/wd4201;/wd4265;/wd4266;/wd4371;/wd4514;/wd4582;/wd4583;/wd4623;/wd4625;/wd4626;/wd4710;/wd4711;/wd4820;/wd4996;/wd5026;/wd5027;/wd5045;/wd5053;/wd5204;/wd5220"
  INTERFACE_INCLUDE_DIRECTORIES "G:/SkyrimDev/PathOfTULLBUNG/lib/commonlibsse-ng/include;G:/SkyrimDev/PathOfTULLBUNG/lib/commonlibsse-ng/extern/openvr/headers"
  INTERFACE_LINK_LIBRARIES "spdlog::spdlog;Microsoft::DirectXTK;Advapi32.lib;bcrypt.lib;D3D11.lib;d3dcompiler.lib;Dbghelp.lib;DXGI.lib;Ole32.lib;Version.lib;\$<\$<BOOL:OFF>:G:/SkyrimDev/PathOfTULLBUNG/lib/commonlibsse-ng/extern/openvr/lib/win64/openvr_api.lib>"
  INTERFACE_LINK_OPTIONS "\$<\$<CONFIG:Debug>:/DEBUG:FULL>"
)
set_target_properties(CommonLibSSE::CommonLibSSE PROPERTIES IMPORTED_LOCATION "G:/SkyrimDev/PathOfTULLBUNG/build/msvc/commonlibsse-ng/CommonLibSSE.lib")
