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
