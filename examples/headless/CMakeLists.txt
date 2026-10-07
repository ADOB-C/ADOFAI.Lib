# 消费者验收：core 打底；archive / audio 有就一起链，完全不碰 GL。
#
# "有没有某个模块"必须由构建系统显式告知，不能用 __has_include 让 main.cpp 自己猜：
# 模块 OFF 时它的头仍在源码树里（只是没编进任何 target），于是 __has_include 会为真、
# 链接期报未定义符号。
#
# 不直接链 ADOFAI::core：archive 与 audio 都是 PUBLIC 依赖 core 的，只要链了其中一个
# core 就会传递进来。直接再链一次会让 ld 报 "ignoring duplicate libraries"。
# 只有两个模块都关掉时才需要显式链 core。
add_executable(adofai_headless main.cpp)
set(_adofai_headless_linked FALSE)
if(TARGET ADOFAI::archive)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::archive)
    target_compile_definitions(adofai_headless PRIVATE ADOFAI_HEADLESS_HAVE_ARCHIVE=1)
    set(_adofai_headless_linked TRUE)
endif()
if(TARGET ADOFAI::audio)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::audio)
    target_compile_definitions(adofai_headless PRIVATE ADOFAI_HEADLESS_HAVE_AUDIO=1)
    set(_adofai_headless_linked TRUE)
endif()
if(NOT _adofai_headless_linked)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::core)
endif()

install(TARGETS adofai_headless RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
