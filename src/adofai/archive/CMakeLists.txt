# ADOFAI::archive —— .adofai.xz / .adofai.zst 的解压实现（4 MB 滑窗流式 + 整份解压）。
#
# 依赖倒置：core/level/ByteSource.hpp 定义接口与容器识别，本模块实现解压并在
# adofai::archive::install() 里把后端注册给 core。所以本 target 是**可选**的：
#   * 不链它 → 明文照常，压缩容器给出明确错误（那是 core 自己的行为，不需要任何桩）；
#   * 链它并在启动时调一次 install() → 压缩容器可用。
#
# 镜像路径：上游在仓库根的 archive/（ADOCAO 的 app/ 也这么引），所以这里就是
# src/adofai/archive/ —— 与 ADOCAO 逐字一致，已经不再有"唯一一处路径不一致"。
add_library(adofai_archive STATIC
    LevelArchive.cpp)

# 用 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>（相对）而不是 FULL_*：配置期间
# 第三方子项目会把 CMAKE_INSTALL_PREFIX 临时指到丢弃前缀，FULL_* 会把那个路径烘进导出文件。
target_include_directories(adofai_archive
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# 单向依赖 core（archive 的公共头 include "core/level/ByteSource.hpp"）。
target_link_libraries(adofai_archive PUBLIC adofai_core)

# lzma / zstd 挂在这里，而且现在是**真正的 PRIVATE**（上游 dc89c14 把 ArchiveStream 的三方
# 状态做成了 pimpl）：LevelArchive.hpp 里已经没有任何 lzma/zstd 类型，直接 include 它的
# 构建内使用方（tests/level_parse_test.cpp）不再需要压缩库的头，消费者链 ADOFAI::archive
# 也不必配压缩库的头/库路径。
#
# 写进 $<BUILD_INTERFACE:>：liblzma / libzstd_static 是构建树里的 target，不属于本库的
# 导出集（带进 install(EXPORT) 会让消费者拿到解析不了的名字）；PRIVATE 的链接项本来也不会
# 被导出，所以装出去之后消费者既不需要它们、也不该去找它们。
if(ADOFAI_LIB_BUNDLE_DEPS)
    target_link_libraries(adofai_archive PRIVATE
        "$<BUILD_INTERFACE:liblzma>" "$<BUILD_INTERFACE:libzstd_static>")
    target_compile_definitions(adofai_archive PUBLIC ADOFAI_LIB_BUNDLE_DEPS=1)
else()
    find_package(LibLZMA REQUIRED)
    find_package(zstd REQUIRED)
    target_link_libraries(adofai_archive PRIVATE "$<BUILD_INTERFACE:LibLZMA::LibLZMA>")
    if(TARGET zstd::libzstd_static)
        target_link_libraries(adofai_archive PRIVATE "$<BUILD_INTERFACE:zstd::libzstd_static>")
    elseif(TARGET zstd::libzstd_shared)
        target_link_libraries(adofai_archive PRIVATE "$<BUILD_INTERFACE:zstd::libzstd_shared>")
    else()
        target_link_libraries(adofai_archive PRIVATE "$<BUILD_INTERFACE:zstd::libzstd>")
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_archive PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_archive PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去叫 ADOFAI::archive。
set_target_properties(adofai_archive PROPERTIES EXPORT_NAME archive)

add_library(ADOFAI::archive ALIAS adofai_archive)
