# 本库自己的测试。
#
# P2 有：headless 冒烟 + WAV 结构 + 压缩容器两向，以及从 ADOCAO 镜像来的解析对拍测试。
# 还没搬：tile_geometry / tile_expansion / geom_probe（都要 render，随 P5）。

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

# ── headless 示例的三条 ───────────────────────────────────────────────────

# 先按测试的方式把 WAV 生成出来（也保证例子在任何构建里被真的跑过一次）。
add_test(NAME headless_run
    COMMAND adofai_headless
            "${CMAKE_CURRENT_SOURCE_DIR}/data/smoke_level.adofai"
            "${CMAKE_CURRENT_BINARY_DIR}/smoke_out.wav")

find_package(Python3 COMPONENTS Interpreter QUIET)
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
