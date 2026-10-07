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
