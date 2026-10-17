# ADOFAI::archive —— 谱面容器：`.adofai.xz` / `.adofai.zst` 解压（4 MB 滑窗流式 + 整份解压），
# 以及 `.adocao` 二进制容器（列编码器 + 读写方）。
#
# 依赖倒置：core/level/ByteSource.hpp 定义接口与容器识别，本模块实现解压并在
# adofai::archive::install() 里把后端注册给 core。所以本 target 是**可选**的：
#   * 不链它 → 明文照常，容器给出明确错误（那是 core 自己的行为，不需要任何桩）；
#   * 链它并在启动时调一次 install() → 容器可用。
#
# 镜像路径：上游在仓库根的 archive/（ADOCAO 的 app/ 也这么引），所以这里就是
# src/adofai/archive/ —— 与 ADOCAO 逐字一致，已经不再有"唯一一处路径不一致"。
add_library(adofai_archive STATIC
    LevelArchive.cpp
    # `.adocao` 的列编码器（字典/常量/差分/位打包/原始）。**不依赖** lzma/zstd：
    # 压缩只是列流之后的可选一步，设计核心是"每列按取值基数选编码"。
    AdocaoColumns.cpp
    # v3 的分块列容器（4096 一块、块内自选编码）。与 Columns 不同，它用 **zstd 帧** ——
    # 帧是 seek 的单位，必须能单独解压（archive 的 PRIVATE 依赖里有 zstd）。
    AdocaoBlocked.cpp
    # `.adocao` 写入方（pack）：依赖 core 的 LevelData（archive → core 是允许的方向）
    AdocaoWriter.cpp
    # `.adocao` 读取方（unpack）：填 LevelData 但不做收尾（收尾与两条 JSON 路径共用）
    AdocaoReader.cpp
    # v3 的 `Derived` 段：每 4096 层的续算快照。要建一次 Timeline 才拿得到 timeSec/bpm
    # —— 用权威实现，不重抄状态机（所以它额外依赖 core/timeline，archive 本来就 PUBLIC 依赖 core）。
    AdocaoCheckpoints.cpp
    # 每砖结构窗口化：从 checkpoint + 一块 angleData + 这一块的事件重算窗口内的
    # Tile/tileBPMs/tileStartTimes（算式与浮点类型必须与生产路径逐字一致）
    AdocaoWindow.cpp
    # v3 判据统计（哨兵掩码 + 差分基数），给 pack 的编码选择当依据
    AdocaoDiagnostics.cpp
    # Re_ADOJAS 的 `.ilytx`（= xz(tar(ilybin))）：解 ilybin 的核心二进制
    Ilybin.cpp
    #   …… 再把它翻译成等价的 .adofai 明文，交回 core 那条唯一路径（不建第二条解析链）
    Ilytx.cpp)

# 写入方会把"生成这份文件的源码来自哪个 commit"写进文件头做溯源（读取方根本不看它，
# 它只作排查/可复现断言）。上游那边取的是 `git rev-parse HEAD`，但**库这边的 HEAD 是库自己的
# 提交**，而这个字段要回答的是"哪份上游源码生成了它" —— 所以用 SYNCED_AT 里的上游 commit
# （顶层已把它读进 ADOFAI_LIB_ADOCAO_COMMIT）。拿不到就写全 0 并标 unknown，不影响可读性。
if(ADOFAI_LIB_ADOCAO_COMMIT AND NOT ADOFAI_LIB_ADOCAO_COMMIT STREQUAL "unknown")
    target_compile_definitions(adofai_archive PRIVATE
        ADOCAO_GIT_COMMIT="${ADOFAI_LIB_ADOCAO_COMMIT}")
endif()

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
#
# 接线本体在 cmake/ADOFAICompression.cmake —— ADOFAI::export 也要链同一份（静态库不传递依赖），
# 两条分支抄两遍必然漂移。
adofai_link_compression(adofai_archive)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_archive PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_archive PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去叫 ADOFAI::archive。
set_target_properties(adofai_archive PROPERTIES EXPORT_NAME archive)

add_library(ADOFAI::archive ALIAS adofai_archive)
