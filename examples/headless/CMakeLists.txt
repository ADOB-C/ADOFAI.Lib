# 消费者验收：core 打底，archive 有就一起链（带出 .xz/.zst 支持），完全不碰 GL。
#
# "有没有 archive"必须由构建系统显式告知，不能用 __has_include 让 main.cpp 自己猜：
# ADOFAI_LIB_ARCHIVE=OFF 时 archive/Install.hpp 仍在源码树里（只是没编进任何 target），
# 于是 __has_include 会为真、链接期报未定义符号 adofai::archive::install()。
#
# 链 archive 时不再重复链 core：archive 是 PUBLIC 依赖 core 的。
add_executable(adofai_headless main.cpp)
if(TARGET ADOFAI::archive)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::archive)
    target_compile_definitions(adofai_headless PRIVATE ADOFAI_HEADLESS_HAVE_ARCHIVE=1)
else()
    target_link_libraries(adofai_headless PRIVATE ADOFAI::core)
endif()

install(TARGETS adofai_headless RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
