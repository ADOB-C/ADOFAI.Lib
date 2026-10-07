# 镜像来的源码都在 src/adofai/ 下；各模块的 include 目录就是它。
# 模块边界靠"文件列表"表达（PLAN.md §2），不靠目录结构。
#
# archive 现在是**真正可选**的独立模块（上游 2026-10 的依赖倒置）：core 不认识 archive，
# 只认 core/level/ByteSource.hpp 那套接口；archive 单向依赖 core 并注册解压后端。
# 所以 ADOFAI_LIB_ARCHIVE=OFF 只是不编这个目录 —— 不需要任何桩。
if(ADOFAI_LIB_ENABLE_ARCHIVE)
    add_subdirectory(adofai/archive)
endif()
add_subdirectory(adofai/core)

# audio 依赖 core（HitsoundManager.hpp 引 core/timeline/HitsoundTimestampGroup.hpp）；
# 可选的设备播放（audio/CMakeLists.txt 里由 ADOFAI_LIB_AUDIO_DEVICE 控制）也在这个目录里。
if(ADOFAI_LIB_ENABLE_AUDIO)
    add_subdirectory(adofai/audio)
endif()

# render（默认 OFF）：glad 必须在 render 之前 —— 后者 PUBLIC 依赖它。
# 根 CMakeLists 已经把 GLFW / OpenGL 准备好了（只在 render=ON 时）。
if(ADOFAI_LIB_ENABLE_RENDER)
    add_subdirectory(adofai/glad)
    add_subdirectory(adofai/render)
endif()
