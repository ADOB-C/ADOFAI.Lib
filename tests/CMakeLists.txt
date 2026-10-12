# 本库自己的测试。
#
# 解析对拍 + headless 那几条（core/archive/audio），以及 render=ON 时的三层几何验收。

# Python 检查脚本（WAV 结构 / 压缩容器 / 打拍音 / shader 一致性）都用它，所以先找一次。
find_package(Python3 COMPONENTS Interpreter QUIET)

# ── 三层几何（全部要 render；上游 PLAN.md §7 的"改几何必须三层全绿"）────────────
if(ADOFAI_LIB_ENABLE_RENDER)
    # L0 中旋五边形不变量 + **源码级调用点护栏**：把 render/TileMesh.cpp 当数据读，
    # 断言它走 TileShape::buildShape()、且没有退回 createTileMesh/createMidSpinMesh。
    # 所以它需要 TileGeometryReference（参考几何）当负向对照。
    add_executable(adofai_tile_geometry_test tile_geometry_test.cpp)
    target_link_libraries(adofai_tile_geometry_test PRIVATE adofai_tile_reference)
    # 它用叶子名 include "TileGeometry.hpp" / "TileMesh.hpp"（上游靠 -I render/ 解决）
    target_include_directories(adofai_tile_geometry_test PRIVATE
        "${ADOFAI_LIB_SRC_ROOT}/render")
    add_test(NAME tile_geometry
        COMMAND adofai_tile_geometry_test
                "${ADOFAI_LIB_SRC_ROOT}/render/TileMesh.cpp")

    # L1 CPU 逐位对拍：形状表展开 vs 参考实现（3.3e7 坐标）。TileShape.cpp 不碰 GL。
    add_executable(adofai_tile_expansion_test
        tile_expansion_test.cpp
        "${ADOFAI_LIB_SRC_ROOT}/render/TileShape.cpp")
    target_link_libraries(adofai_tile_expansion_test PRIVATE
        ADOFAI::core adofai_tile_reference)
    target_include_directories(adofai_tile_expansion_test PRIVATE
        "${ADOFAI_LIB_SRC_ROOT}/render")
    add_test(NAME tile_expansion
        COMMAND adofai_tile_expansion_test
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/angles360.adofai"
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/shapes_exhaustive.adofai")

    # L2 GPU 逐位对拍：真跑 assets/shaders/tile.vert，把局部坐标读回来与 CPU 的展开比。
    # 需要 GL 上下文；没有显示/驱动时测试自己 SKIP 退 0（headless CI 不会红）。
    add_executable(adofai_geom_probe_test
        geom_probe_test.cpp
        "${ADOFAI_LIB_SRC_ROOT}/render/TileShape.cpp")
    target_link_libraries(adofai_geom_probe_test PRIVATE ADOFAI::core ADOFAI::glad glfw)
    target_include_directories(adofai_geom_probe_test PRIVATE
        "${ADOFAI_LIB_SRC_ROOT}/render")
    # glad 是手写的最小加载器（没有 glReadPixels/glFinish），探针直接用平台 GL。
    if(WIN32)
        target_link_libraries(adofai_geom_probe_test PRIVATE opengl32)
    elseif(APPLE)
        target_link_libraries(adofai_geom_probe_test PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(adofai_geom_probe_test PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
    endif()
    add_test(NAME geom_probe
        COMMAND adofai_geom_probe_test
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/angles360.adofai"
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/shapes_exhaustive.adofai")

    # geom_probe 按 CWD 找 assets/shaders/（与 app 的资产查找同序），而 ctest 在构建目录跑
    # —— 把 shader 资产拷过去，否则它拿不到 tile.vert（那会让 L2 变成静默 SKIP）。
    add_custom_command(TARGET adofai_geom_probe_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/shaders"
                "$<TARGET_FILE_DIR:adofai_geom_probe_test>/assets/shaders"
        COMMENT "拷贝 assets/shaders 到构建目录（geom_probe 按 CWD 找它）")

    # 内嵌回退 GLSL 必须与 assets/shaders/* 逐字相同（PLAN.md §7 的铁律之一）。
    # 它只在资产缺失时才生效，所以漂移了正常构建永远不红 —— 必须机械校验。
    # 负向对照已验：改 assets 那边、或只改 Shaders.hpp 里的内嵌串，都会红并指出首个差异行。
    if(Python3_Interpreter_FOUND)
        add_test(NAME shader_fallback
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/check_shader_fallback.py"
                    "${CMAKE_SOURCE_DIR}")
    endif()
endif()

# ── 解析对拍（从 ADOCAO 镜像）──────────────────────────────────────────────
# 快路径必须与 cleanJson + RapidJSON 老路径逐位一致；对每个明文 fixture 还会在内存里压成
# xz / zstd 再加载一遍比对。注意它**直接用压缩 API 造数据**（lzma_easy_buffer_encode /
# ZSTD_compress），所以除了 ADOFAI::archive 还得自己链那两个压缩库 ——
# archive 现在把它们是 PRIVATE（pimpl 之后公共头不再需要），不会传递给测试。
# 上游的 tests/CMakeLists 也是这么显式链的。
if(ADOFAI_LIB_ENABLE_ARCHIVE)
    add_executable(adofai_level_parse_test level_parse_test.cpp)
    if(ADOFAI_LIB_BUNDLE_DEPS)
        target_link_libraries(adofai_level_parse_test PRIVATE
            ADOFAI::archive liblzma libzstd_static)
    else()
        # 系统库模式：find_package 是在 archive 子目录的作用域里调的，imported target 传不到
        # 这里，所以自己也找一遍（与上游 tests/CMakeLists 显式链压缩库等价）。
        find_package(LibLZMA REQUIRED)
        find_package(zstd REQUIRED)
        target_link_libraries(adofai_level_parse_test PRIVATE
            ADOFAI::archive LibLZMA::LibLZMA)
        if(TARGET zstd::libzstd_static)
            target_link_libraries(adofai_level_parse_test PRIVATE zstd::libzstd_static)
        elseif(TARGET zstd::libzstd_shared)
            target_link_libraries(adofai_level_parse_test PRIVATE zstd::libzstd_shared)
        else()
            target_link_libraries(adofai_level_parse_test PRIVATE zstd::libzstd)
        endif()
    endif()
    add_test(NAME level_parse_parity
        COMMAND adofai_level_parse_test "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures")
else()
    message(STATUS "ARCHIVE=OFF：跳过 level_parse_parity（它按设计要直接建压缩容器）")
endif()

# lzma_mt 内存字段的跨版本兼容（liblzma 5.4 起 `memlimit` 拆成
# memlimit_threading/memlimit_stop）：纯编译期选择 + 断言，**不依赖压缩库、不链任何 target**，
# 所以 ARCHIVE=OFF 时也照跑。它挡的是"按版本号判字段、于是 5.4/5.5 的系统库编不过"这类回归
# —— 本库的 linux-system-deps 真红过一次，而内嵌的 5.6.3 在本地永远看不出来。
add_executable(adofai_lzma_mt_compat_test lzma_mt_compat_test.cpp)
target_include_directories(adofai_lzma_mt_compat_test PRIVATE "${ADOFAI_LIB_SRC_ROOT}")
add_test(NAME lzma_mt_compat COMMAND adofai_lzma_mt_compat_test)

# `.adocao` 列编码器自测：逐位往返 + 编码选择（字典/常量/差分/位打包）+ **坏数据必须干净失败**
# （截断、count 说谎、未知 codec、越界下标）。只链 archive —— 列编码器本身不用压缩库。
if(ADOFAI_LIB_ENABLE_ARCHIVE)
    add_executable(adofai_columns_test adocao_columns_test.cpp)
    target_link_libraries(adofai_columns_test PRIVATE ADOFAI::archive)
    add_test(NAME adocao_columns COMMAND adofai_columns_test)
endif()

# 快路径必须吃下非整数 action 字段（带负向对照）。上游同名的护栏跑本体 app，库里没有 app，
# 所以这是**库自己的**等价版；它不放进 level_parse_test.cpp —— 那个是镜像文件，不许手改。
add_executable(adofai_fast_path_test check_fast_path.cpp)
target_link_libraries(adofai_fast_path_test PRIVATE ADOFAI::core)
add_test(NAME fast_path_noninteger COMMAND adofai_fast_path_test)

# ── 从上游镜像来的其它用例 ──────────────────────────────────────────────────
# 只挂**不依赖本体 app / 资产**的那些；依赖 assets/hitsounds 或 serve 当 dump 工具的
# （kick_chord / timeline_ab_dump / tl_*）库里没有对应物，见文件末尾的说明。

# `.adocao` v3 判据统计：哨兵掩码 + 差分基数（负向对照是重点）。只链 archive。
if(ADOFAI_LIB_ENABLE_ARCHIVE)
    add_executable(adofai_adocao_diagnostics_test adocao_diagnostics_test.cpp)
    target_link_libraries(adofai_adocao_diagnostics_test PRIVATE ADOFAI::archive)
    add_test(NAME adocao_diagnostics COMMAND adofai_adocao_diagnostics_test)
endif()

# 解析/加载的各种上限（core 的 CountLimits）
add_executable(adofai_limits_test limits_test.cpp)
target_link_libraries(adofai_limits_test PRIVATE ADOFAI::core)
add_test(NAME limits COMMAND adofai_limits_test)

# PositionTrack 的 justThisTile 消费侧语义（点加 vs 后缀加 + 负向对照）
add_executable(adofai_position_offset_test position_offset_test.cpp)
target_link_libraries(adofai_position_offset_test PRIVATE ADOFAI::core)
add_test(NAME position_offset COMMAND adofai_position_offset_test)

# audio-as-chart（raw-PCM 直通）与"自然弯曲"赫兹谱：都要 audio
if(ADOFAI_LIB_ENABLE_AUDIO AND ADOFAI_LIB_ENABLE_ARCHIVE)
    # 这两个用例会把 WAV 写到 CMAKE_CURRENT_BINARY_DIR/diag/ 供人耳复核。ctest 在
    # build/tests 下跑（上游从源码根跑，所以那边有 build/diag），目录得先建出来。
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/diag")

    add_executable(adofai_raw_pcm_test raw_pcm_test.cpp)
    target_link_libraries(adofai_raw_pcm_test PRIVATE ADOFAI::archive ADOFAI::audio)
    add_test(NAME raw_pcm
        COMMAND adofai_raw_pcm_test
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_wav_rate.adofai"
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_dict.adofai"
                "${CMAKE_CURRENT_BINARY_DIR}/diag/wav_rate.wav")

    add_executable(adofai_hz_grid_test hz_grid_test.cpp)
    target_link_libraries(adofai_hz_grid_test PRIVATE ADOFAI::archive ADOFAI::audio)
    add_test(NAME hz_grid
        COMMAND adofai_hz_grid_test
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_arpeggio.adofai"
                "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_duet.adofai")
endif()

# ── 导出（**库自有**模块，不在镜像白名单里）────────────────────────────────
# 这是导出模块的**漂移护栏**：它比的是 parse(原件) 与 parse(导出物) 的建模字段指纹，
# 所以上游一旦给 LevelData 加字段而导出没跟上，这里立刻红（判据来自解析器，不是我另抄一份表）。
# 详见 tests/export_roundtrip_test.cpp 顶部的说明。
if(ADOFAI_LIB_ENABLE_EXPORT)
    # 真实谱面（上游镜像进来的 diag_* / angles360 / shapes_exhaustive …）全都过一遍。
    file(GLOB _adofai_export_charts CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/charts/*.adofai")

    add_executable(adofai_export_roundtrip_test export_roundtrip_test.cpp)
    target_link_libraries(adofai_export_roundtrip_test PRIVATE ADOFAI::export)
    if(ADOFAI_LIB_ENABLE_ARCHIVE)
        # `.adocao` 的 pack 逐字节对照要直接调 archive 的 packLevel。
        target_compile_definitions(adofai_export_roundtrip_test PRIVATE
            ADOFAI_EXPORT_TEST_HAVE_ARCHIVE=1)
    endif()
    add_test(NAME export_roundtrip
        COMMAND adofai_export_roundtrip_test
                "${CMAKE_CURRENT_BINARY_DIR}/export_out"
                ${_adofai_export_charts}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
                # 解析容错夹具里**本身就是合法 JSON** 的那些（其余是故意写坏的，喂给导出没有意义）
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f16_pathdata_only.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f18b_sci_neg_numbers.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f19_all_action_types.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f23_string_escapes.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f24_positiontrack_variants.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f25_setspeed_variants.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f27_whitespace_heavy.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f28_empty_arrays.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f29_minimal.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f38_hitsound_volume_dense.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f40_negative_hitsound_volume.json")

    # 独立验收：Python 的 json / lzma（以及 zstd CLI）说了算，还有一份**另写的**建模投影。
    # 输出目录与上面那条**刻意分开**：两条用例导出的文件名集合是重叠的（都是按夹具名派生），
    # 共用目录在 `ctest -j` 下会互相踩。
    if(Python3_Interpreter_FOUND AND TARGET adofai_headless)
        if(ADOFAI_LIB_ARCHIVE)
            set(_adofai_export_expect on)
        else()
            set(_adofai_export_expect off)
        endif()
        add_test(NAME export_independent
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/check_export.py"
                    "$<TARGET_FILE:adofai_headless>"
                    "${CMAKE_CURRENT_BINARY_DIR}/export_out_py"
                    "${_adofai_export_expect}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
                    "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_dict.adofai"
                    "${CMAKE_CURRENT_SOURCE_DIR}/charts/diag_midspin.adofai"
                    "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f19_all_action_types.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f24_positiontrack_variants.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f25_setspeed_variants.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f38_hitsound_volume_dense.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/level_fixtures/f40_negative_hitsound_volume.json")
    endif()
endif()

# ── headless 示例的三条 ───────────────────────────────────────────────────

# 先按测试的方式把 WAV 生成出来（也保证例子在任何构建里被真的跑过一次）。
add_test(NAME headless_run
    COMMAND adofai_headless
            "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
            "${CMAKE_CURRENT_BINARY_DIR}/smoke_out.wav")

if(Python3_Interpreter_FOUND)
    add_test(NAME headless_wav_structure
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/check_wav.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
                "${CMAKE_CURRENT_BINARY_DIR}/smoke_out.wav"
                "1000")
    # 结构检查必须在生成之后跑。
    set_tests_properties(headless_wav_structure PROPERTIES DEPENDS headless_run)

    # 压缩容器那两向：ON 要读得动 .xz/.zst，OFF 要干净失败（不许静默当明文）。
    if(ADOFAI_LIB_ARCHIVE)
        set(_archive_expect on)
    else()
        set(_archive_expect off)
    endif()
    add_test(NAME archive_container
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/check_archive.py"
                "$<TARGET_FILE:adofai_headless>"
                "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
                "${_archive_expect}")

    # 打拍音：链了 ADOFAI::audio 时，用**现场合成**的音色 WAV 走一遍真混音
    # （不依赖 ADOCAO 的 assets/hitsounds/ —— 那不在本库白名单里）。
    if(ADOFAI_LIB_ENABLE_AUDIO)
        add_test(NAME audio_hitsound_mix
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/check_hitsound.py"
                    "$<TARGET_FILE:adofai_headless>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
                    "${CMAKE_CURRENT_BINARY_DIR}/Kick.wav"
                    "${CMAKE_CURRENT_BINARY_DIR}/hitsound_out.wav")

        # 源码级护栏：混音内核必须是饱和加法（每次相加都 clamp），且不许有 Nyquist 去重。
        # 负向对照已验：把 vqaddq_s16 换成 vaddq_s16、或插入一行去重，都会红。
        add_test(NAME audio_hitsound_rules
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/check_hitsound_rules.py"
                    "${ADOFAI_LIB_SRC_ROOT}/audio/HitsoundManager.cpp")
    endif()
else()
    message(STATUS "没找到 Python3，跳过 headless_wav_structure / archive_container / audio_hitsound_mix")
endif()
