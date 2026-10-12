# ADOFAI::export —— 写入方：`.adofai`（明文 JSON）/ `.adofai.xz` / `.adofai.zst` / `.adocao`。
#
# **这是库自有的模块，不在单向镜像的白名单里**（镜像的是 `src/adofai/{core,archive,audio,
# render,glad}`）。ADOCAO 本体刻意不做导出，所以这段代码没有上游对应物 —— 代价与护栏写在
# MIRROR.md，护栏本身是 tests/export_roundtrip_test.cpp。
#
# 依赖方向：export → core（LevelData）以及 export → archive（压缩库 + `.adocao` 打包器）。
# 与 archive → core 一样是"实现方依赖接口方"的方向，core 侧仍然不知道任何容器/压缩的存在。
add_library(adofai_export STATIC
    # LevelData → JSON（右逆契约与"不承诺什么"都写在头里）
    LevelJsonWriter.cpp
    # xz / zstd 整帧编码（头里无三方类型，照 archive/LevelArchive.hpp 的口径）
    LevelEncode.cpp
    # 格式枚举 + 调度 + 原子落盘
    LevelExport.cpp)

target_include_directories(adofai_export
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# archive 是导出集里的 target，直接当普通 PUBLIC 依赖链。
# core 不在这里重复列：archive 已经 PUBLIC 依赖它，transitive 就会带过来；两个都列会让
# 链接行出现两份同一个 .a（ld 会报 "ignoring duplicate libraries"，examples/headless 踩过）。
if(ADOFAI_LIB_ENABLE_ARCHIVE)
    target_link_libraries(adofai_export PUBLIC adofai_archive)
    # 压缩库：与 archive 共用一份接线（静态库不传递依赖，所以 export 必须自己再链一次）。
    adofai_link_compression(adofai_export)
    # `.adocao` / xz / zst 三条路都在这个宏后面（LevelExport.cpp / LevelEncode.cpp）。
    target_compile_definitions(adofai_export PRIVATE ADOFAI_LIB_EXPORT_HAVE_ARCHIVE=1)
else()
    target_link_libraries(adofai_export PUBLIC adofai_core)
    message(STATUS "  export: ARCHIVE=OFF -> 只写明文 .adofai（.xz/.zst/.adocao 会明确报不可用）")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_export PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_export PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去叫 ADOFAI::export。
set_target_properties(adofai_export PROPERTIES EXPORT_NAME export)

add_library(ADOFAI::export ALIAS adofai_export)
