# ADOFAI::core —— 解析（明文 + 容器识别接口）+ 时间线/解算 + 工具。
#
# 文件列表 = 模块边界（PLAN.md §2 / §3 白名单里的 core/level、core/timeline、core/util）。
# 刻意不含 core/map/**（map / 出图不进库），也**不含任何压缩库** —— 2026-10 上游把 archive
# 改成依赖倒置（core/level/ByteSource.hpp 是接口，archive 模块注册后端），所以：
#   * core 独立可编、可装、可测；公共头里没有 <lzma.h>/<zstd.h>；
#   * 只想解析明文谱的消费者链 core 就够，不用背压缩库；
#   * 压缩谱要额外链 ADOFAI::archive 并在启动时调 adofai::archive::install()。
add_library(adofai_core STATIC
    level/ByteSource.cpp
    level/JsonCleaner.cpp
    level/LevelData.cpp
    level/LevelPath.cpp
    timeline/Timeline.cpp
    timeline/PositionSolver.cpp
    timeline/PlaybackClock.cpp
    util/AssetPaths.cpp
    util/DataFile.cpp
    util/Logger.cpp
    util/Progress.cpp
    util/ThreadPool.cpp)

# 公共依赖：glm 的 dvec2 出现在 PlaybackClock.hpp / PositionSolver.hpp。
# 它跟着本库一起装，消费者由 cmake/ADOFAI.LibConfig.cmake.in 的 find_dependency(glm) 接上。
#
# rapidjson 是另一回事：LevelData.hpp 里只有一句 #include <rapidjson/document.h>，**没有任何
# rapidjson 类型出现在公共接口里**（全库仅此一处 include）。它没有可用的 CMake config
# （source 树里那份 RapidJSONConfig.cmake.in 缺只有它自己构建时才会生成的
# RapidJSON-targets.cmake），所以消费者得自己给 include 目录 —— 见 README 的"公共依赖"。
#
# 两个都写进 $<BUILD_INTERFACE:>：它们是构建树里的 target，不属于本库的导出集，
# 带进 install(EXPORT) 会让消费者拿到解析不了的名字。
target_link_libraries(adofai_core
    PUBLIC  "$<BUILD_INTERFACE:glm::glm>"
            "$<BUILD_INTERFACE:rapidjson>")
target_include_directories(adofai_core
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# 资产 zip：私有链 miniz，只在 core 内部可见。
# miniz 只被 core/util/DataFile.cpp 用到（而且那段在上游 dc89c14 之后是可条件编译的），
# 所以它不进 INTERFACE、不进导出集 —— 消费者前缀里不会出现 miniz 的头或 config。
if(ADOFAI_LIB_ASSET_ZIP)
    target_compile_definitions(adofai_core PRIVATE ADOFAI_HAVE_MINIZ=1)
    target_link_libraries(adofai_core PRIVATE "$<BUILD_INTERFACE:miniz>")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_core PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去的名字要正好是 ADOFAI::core（NAMESPACE ADOFAI:: + EXPORT_NAME core）。
set_target_properties(adofai_core PROPERTIES EXPORT_NAME core)

add_library(ADOFAI::core ALIAS adofai_core)
