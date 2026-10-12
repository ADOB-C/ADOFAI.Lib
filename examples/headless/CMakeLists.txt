# 消费者验收：core 打底；archive / audio / export 有就一起链，完全不碰 GL。
#
# "有没有某个模块"必须由构建系统显式告知，不能用 __has_include 让 main.cpp 自己猜：
# 模块 OFF 时它的头仍在源码树里（只是没编进任何 target），于是 __has_include 会为真、
# 链接期报未定义符号。
#
# 不直接链 ADOFAI::core：archive 与 audio 都是 PUBLIC 依赖 core 的，只要链了其中一个
# core 就会传递进来。直接再链一次会让 ld 报 "ignoring duplicate libraries"。
# 只有所有模块都关掉时才需要显式链 core。
#
# 同理 export **PUBLIC 依赖 archive**（压缩库与 `.adocao` 打包器都在那一格里），
# 所以 archive 只在 export 缺席时才单独链 —— 两个都链会让 `libadofai_archive.a`
# 在链接行出现两次（实测 ld 会就此告警）。
add_executable(adofai_headless main.cpp)
set(_adofai_headless_linked FALSE)

if(TARGET ADOFAI::export)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::export)
    target_compile_definitions(adofai_headless PRIVATE ADOFAI_HEADLESS_HAVE_EXPORT=1)
    set(_adofai_headless_linked TRUE)
elseif(TARGET ADOFAI::archive)
    target_link_libraries(adofai_headless PRIVATE ADOFAI::archive)
    set(_adofai_headless_linked TRUE)
endif()

# 宏与链接是两件事：export=OFF 但 archive=ON 时，示例照样要**读**得动 .xz/.zst/.adocao。
if(TARGET ADOFAI::archive)
    target_compile_definitions(adofai_headless PRIVATE ADOFAI_HEADLESS_HAVE_ARCHIVE=1)
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
