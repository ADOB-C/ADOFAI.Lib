cmake_minimum_required(VERSION 3.20)

project(ADOFAI.Lib
    VERSION 0.1.0
    DESCRIPTION "Reusable ADOFAI level parsing / timeline / hitsound / tile rendering library"
    HOMEPAGE_URL "https://github.com/CHT-1192/ADOFAI.Lib"
    LANGUAGES CXX)

# ── Options ──────────────────────────────────────────────────────────────────
# 默认值按 PLAN.md §6 的表。RENDER 默认 OFF 是一条承诺，见 §5：
# OFF 时不 find_package(OpenGL/glfw)、公共头里不出现 GL 类型、装出来的 target 里没有 ADOFAI::render。
option(ADOFAI_LIB_ARCHIVE      "xz / zstd 谱面容器（ADOFAI::archive）"          ON)
option(ADOFAI_LIB_AUDIO        "打拍音合成 + 解码（ADOFAI::audio，P4）"          ON)
option(ADOFAI_LIB_AUDIO_DEVICE "miniaudio 设备播放（ADOFAI::audio_device）"      OFF)
option(ADOFAI_LIB_RENDER       "砖块渲染（ADOFAI::render，P5）—— 默认 OFF"       OFF)
option(ADOFAI_LIB_ASSET_ZIP    "资产 zip 支持（私有链 miniz）；OFF = 只走磁盘"   ON)
option(ADOFAI_LIB_BUNDLE_DEPS  "内嵌 lzma/zstd；OFF = 用系统库"                  ON)
option(ADOFAI_LIB_BUILD_TESTS  "构建本库自己的测试"                              ${PROJECT_IS_TOP_LEVEL})
option(ADOFAI_LIB_BUILD_EXAMPLES "构建 examples/"                               ${PROJECT_IS_TOP_LEVEL})
option(ADOFAI_LIB_INSTALL      "生成 install / export 规则"                      ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 版本头（库自己的文件，生成到构建目录）。
# 对齐到的 ADOCAO commit 从 SYNCED_AT 里读出来，免得同一个事实写在两个地方。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SYNCED_AT" _synced_lines
     REGEX "^ADOCAO_COMMIT_FULL=")
if(_synced_lines)
    string(REGEX REPLACE "^ADOCAO_COMMIT_FULL=" "" ADOFAI_LIB_ADOCAO_COMMIT "${_synced_lines}")
    list(GET ADOFAI_LIB_ADOCAO_COMMIT 0 ADOFAI_LIB_ADOCAO_COMMIT)
else()
    set(ADOFAI_LIB_ADOCAO_COMMIT "unknown")
    message(WARNING "SYNCED_AT 里没有 ADOCAO_COMMIT_FULL —— 对齐记录缺失")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/adofai/version.hpp"
    @ONLY)

# 镜像进来的源码一律经 include 根 src/adofai 引用（#include "core/level/LevelData.hpp"）；
# 装出去之后同一个根变成 include/adofai，所以消费者的 #include 一字不改。
set(ADOFAI_LIB_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/adofai")

# ── Dependencies ─────────────────────────────────────────────────────────────
# 只走 FetchContent，钉 ADOCAO 用的同一批版本（ADOCAO 的 CMakeLists.txt）。
# 离线或想用本地已有的源码时：-DFETCHCONTENT_SOURCE_DIR_<大写的名字>=<路径>
#
# **已知问题：lzma / zstd / miniz 的 install 规则会污染消费者前缀**（PLAN.md §1 要求它们
# "私有内嵌、不装头"）。实测过的机制，都不可行：
#   * 覆盖 install()：CMake 4.4 没有 cmake_language(GET_COMMAND/SET_COMMAND)，而
#     `cmake_language(CALL install ...)` 会命中同名 function（递归）；而且 function(install)
#     是**全局**的 —— 一旦定义，连我们自己的 install 规则也一起失效（实测：先遮蔽再调用，
#     规则数为 0）。子目录/兄弟目录都救不了它。
#   * 把第三方的前缀指到丢弃位置：父项目的 install 脚本在 include 子项目脚本前会用自己的
#     CMAKE_INSTALL_PREFIX（来自 cmake --install --prefix）覆盖，烘进去的路径被顶掉 ——
#     实测与改动前完全一致。
#   * `EXCLUDE_FROM_ALL` 掉上游 CLI：install 规则仍会引用它们 → `cmake --install` 直接失败。
# **可行方向（本轮试到一半，被一个硬问题挡住，未落地）**：把第三方换成
# `ExternalProject_Add` + 独立前缀。实测它确实能让我们的 install 树干净（只剩
# adofai/ + glm/，上游 CLI 也不再装进来）；但**静态库不传递依赖** ——
# `libadofai_archive.a` 引用 `ZSTD_*` / `lzma_*`，而外部依赖不装进消费者前缀，
# 于是消费者链接直接报未定义符号。要落地得配合"把 lzma/zstd 的目标文件合并进我们自己的
# .a"（或导出私有的依赖 target 并处理可重定位），那是另一块工作。
# 详见 MIRROR.md 的 P3 残留第 3 条。
include(FetchContent)

# rapidjson 没有可用的 CMake 构建，用 Populate；压掉 CMP0169 的弃用警告。
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# glm —— 公共依赖：PlaybackClock.hpp / PositionSolver.hpp 的公共头就带 glm::dvec2。
# 它的头出现在我们装出去的公共头里，所以**必须真的装进消费者前缀**，find_dependency(glm)
# 才找得到 —— 它是这几个依赖里唯一应该进前缀的（也正是它留着的理由）。
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
# glm 自带一整套测试，我们不要；但它的 install 规则要打开（它默认只在 glm 自己是顶层
# 项目时生成，作为子项目用默认是关的）。
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# RapidJSON —— 也是公共依赖：LevelData.hpp 里有 <rapidjson/document.h>
FetchContent_Declare(rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()
# rapidjson 没有可用的 CMake 构建，自己造一个 header-only target（FetchContent 的
# <name>_SOURCE_DIR 只在顶层作用域可见，子目录得靠这个 target 传下去）。
if(NOT TARGET rapidjson)
    add_library(rapidjson INTERFACE)
    target_include_directories(rapidjson INTERFACE
        "$<BUILD_INTERFACE:${rapidjson_SOURCE_DIR}/include>")
endif()

if(ADOFAI_LIB_ARCHIVE AND ADOFAI_LIB_BUNDLE_DEPS)
    # liblzma 自带 19 个 ctest 用例，我们只跑自己的
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(XZ_NLS OFF CACHE BOOL "" FORCE)          # 不要 i18n / gettext
    FetchContent_Declare(lzma
        GIT_REPOSITORY https://github.com/tukaani-project/xz.git
        GIT_TAG v5.6.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(lzma)
    if(TARGET liblzma)
        # liblzma 只给自己加了 PRIVATE include（它预设消费者用 find_package），
        # 作为子项目用要替它把公开头文件目录挂出来。
        target_include_directories(liblzma INTERFACE
            $<BUILD_INTERFACE:${lzma_SOURCE_DIR}/src/liblzma/api>)
    endif()

    # zstd 的 CMakeLists 在 build/cmake/ 下（根目录没有），所以手工 Populate + add_subdirectory。
    # 目标名是 libzstd_static。
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(zstd)
    if(NOT zstd_POPULATED)
        FetchContent_Populate(zstd)
        add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
    endif()
endif()

if(ADOFAI_LIB_ASSET_ZIP)
    # miniz —— 只给 core 私有链（zip 资产）
    FetchContent_Declare(miniz
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 3.0.2
        GIT_SHALLOW TRUE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(miniz)
    target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
endif()
# OFF 时什么都不做：上游 dc89c14 给 core/util/DataFile.cpp 的 zip 那段加了
# ADOCAO_HAVE_MINIZ 条件编译（库这边对应 ADOFAI_LIB_ASSET_ZIP），于是 zip 支持真的可选 ——
# 关掉后 readDataFile() 直接走 dataDir / 搜索根 / 直接路径，行为等于"zipName 为空"。
# 宏由 src/adofai/core/CMakeLists.txt 定义。

# stb / miniaudio —— 只服务可选的设备播放（ADOFAI::audio_device，默认 OFF）。
# 默认构建**完全不拉它们**：合成/导出打拍音是纯逻辑，不需要设备（见 src/adofai/audio/CMakeLists.txt）。
if(ADOFAI_LIB_AUDIO AND ADOFAI_LIB_AUDIO_DEVICE)
    # stb：只要 stb_vorbis.c（miniaudio 的 OGG 解码后端）
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(stb)
    if(NOT stb_POPULATED)
        FetchContent_Populate(stb)
    endif()

    # miniaudio（单头）
    set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
    set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
    set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
    set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # 别让 miniaudio 去探测我们不要的后端（SDL2 / Steam Audio）
    set(SDL2_LIBRARY "SDL2_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
    set(STEAMAUDIO_LIBRARY "STEAMAUDIO_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.22
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(miniaudio)
endif()

# ── Modules ──────────────────────────────────────────────────────────────────
# archive 是**可选**模块（上游 2026-10 的依赖倒置）：core 只认 core/level/ByteSource.hpp
# 那套接口，压缩能力由 archive 模块通过 adofai::archive::install() 注册进来。
# ADOFAI_LIB_ARCHIVE=OFF 就是不编那个目录 —— 不需要任何桩，core 自己会给压缩谱明确报错。
# audio 已搬（P4）：ADOFAI::audio = 合成/导出（纯逻辑）；ADOFAI::audio_device = 设备播放（默认 OFF）。
# render(P5) 还没搬，选项先留着。
set(ADOFAI_LIB_ENABLE_CORE ON)
set(ADOFAI_LIB_ENABLE_ARCHIVE ${ADOFAI_LIB_ARCHIVE})
set(ADOFAI_LIB_ENABLE_AUDIO ${ADOFAI_LIB_AUDIO})

add_subdirectory(src)

if(ADOFAI_LIB_BUILD_EXAMPLES)
    add_subdirectory(examples/headless)
endif()

if(ADOFAI_LIB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install / export ─────────────────────────────────────────────────────────
if(ADOFAI_LIB_INSTALL)
    # 各 OFF 的模块不生成 target —— 消费者 link 它是编译期报错（PLAN.md §5 的口径）。
    set(_adofai_install_targets adofai_core)
    if(ADOFAI_LIB_ENABLE_ARCHIVE)
        list(APPEND _adofai_install_targets adofai_archive)
    endif()
    if(ADOFAI_LIB_ENABLE_AUDIO)
        list(APPEND _adofai_install_targets adofai_audio)
        if(ADOFAI_LIB_AUDIO_DEVICE)
            list(APPEND _adofai_install_targets adofai_audio_device)
        endif()
    endif()
    install(TARGETS ${_adofai_install_targets}
        EXPORT ADOFAILibTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai)

    # 公共头：装出去的布局与源码树同一个 include 根（include/adofai/...）
    install(DIRECTORY "${ADOFAI_LIB_SRC_ROOT}/core/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai/core
        FILES_MATCHING PATTERN "*.hpp")
    if(ADOFAI_LIB_ENABLE_ARCHIVE)
        install(DIRECTORY "${ADOFAI_LIB_SRC_ROOT}/archive/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai/archive
            FILES_MATCHING PATTERN "*.hpp")
    endif()
    if(ADOFAI_LIB_ENABLE_AUDIO)
        # 逐个装而不是 install(DIRECTORY)：AudioEngine.hpp 只对链了 audio_device 的人有意义
        # （它虽然不 include miniaudio —— 只前置声明 ma_device / ma_decoder —— 但那个类是设备
        # 模块的内容）。HitsoundManager.hpp 才是纯合成/导出的公共头。
        install(FILES "${ADOFAI_LIB_SRC_ROOT}/audio/HitsoundManager.hpp"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai/audio)
        if(ADOFAI_LIB_AUDIO_DEVICE)
            install(FILES "${ADOFAI_LIB_SRC_ROOT}/audio/AudioEngine.hpp"
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai/audio)
        endif()
    endif()

    # 生成的版本头（#include <adofai/version.hpp>）
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/adofai/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adofai)

    install(EXPORT ADOFAILibTargets
        NAMESPACE ADOFAI::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ADOFAI.Lib)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ADOFAI.LibConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ADOFAI.LibConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ADOFAI.Lib)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ADOFAI.LibConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ADOFAI.LibConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ADOFAI.LibConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ADOFAI.Lib)
endif()

message(STATUS "ADOFAI.Lib ${PROJECT_VERSION}: core=ON archive=${ADOFAI_LIB_ARCHIVE} "
               "audio=${ADOFAI_LIB_AUDIO} audio_device=${ADOFAI_LIB_AUDIO_DEVICE} "
               "render=${ADOFAI_LIB_RENDER}(未搬) "
               "asset_zip=${ADOFAI_LIB_ASSET_ZIP} bundle_deps=${ADOFAI_LIB_BUNDLE_DEPS}")
