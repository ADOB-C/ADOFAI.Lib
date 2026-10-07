# ADOFAI::audio —— 打拍音的合成与导出（纯逻辑，不需要设备、也不需要 miniaudio）。
#
# 文件列表 = 模块边界（PLAN.md §2 的 audio/）。拆法依据（实测）：
#   * HitsoundManager.cpp 只从 AudioEngine.hpp 取一个 AUDIO_SAMPLE_RATE 常量，而那个头
#     **不 include miniaudio**（只前置声明 ma_device/ma_decoder），所以它不需要设备依赖 ——
#     这条是"audio 默认 ON、audio_device 默认 OFF"能成立的前提。
#   * stb_vorbis_impl.cpp 只是给 miniaudio 提供 OGG 解码后端（`stb_vorbis_*` 在本模块里
#     一个调用点都没有），所以它跟 AudioEngine.cpp 一起归设备那侧，合成/导出不必背 stb。
add_library(adofai_audio STATIC
    HitsoundManager.cpp)

target_include_directories(adofai_audio
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# 单向依赖 core（HitsoundManager.hpp include core/timeline/HitsoundTimestampGroup.hpp）。
target_link_libraries(adofai_audio PUBLIC adofai_core)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_audio PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_audio PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去叫 ADOFAI::audio。
set_target_properties(adofai_audio PROPERTIES EXPORT_NAME audio)

add_library(ADOFAI::audio ALIAS adofai_audio)

# ── 可选的设备播放 ────────────────────────────────────────────────────────────
# AudioEngine（miniaudio 设备 + 流式播放）+ stb_vorbis（OGG 解码后端）。
# 默认 OFF：这条线会引入 miniaudio（平台后端一堆），而"合成/导出打拍音"完全用不到它。
if(ADOFAI_LIB_AUDIO_DEVICE)
    add_library(adofai_audio_device STATIC
        AudioEngine.cpp
        stb_vorbis_impl.cpp)

    target_include_directories(adofai_audio_device
        PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>"
        PRIVATE "$<BUILD_INTERFACE:${miniaudio_SOURCE_DIR}>"
                "$<BUILD_INTERFACE:${stb_SOURCE_DIR}>")

    # 设备播放是 audio 的上层（app 用它把 HitsoundManager 的缓冲喂给设备）。
    target_link_libraries(adofai_audio_device PUBLIC adofai_audio)

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(adofai_audio_device PRIVATE -O3 -march=native -fomit-frame-pointer)
    endif()
    set_target_properties(adofai_audio_device PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    set_target_properties(adofai_audio_device PROPERTIES EXPORT_NAME audio_device)

    add_library(ADOFAI::audio_device ALIAS adofai_audio_device)
endif()
