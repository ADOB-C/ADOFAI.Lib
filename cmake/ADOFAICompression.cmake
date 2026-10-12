# 压缩库（liblzma + zstd）的接线。被 archive 与 export **两个模块**共用。
#
# 为什么抽出来：静态库不传递依赖，所以两个 target 都得自己链一份；而"内嵌 / 系统库"两条分支
# 抄两遍必然漂移（BUNDLE_DEPS=OFF 时 zstd 的包名有 static/shared/无后缀三种写法，漏一个就是
# 只在别人的机器上炸）。放在这里只有一份事实。
#
# 用法：include(${CMAKE_SOURCE_DIR}/cmake/ADOFAICompression.cmake)（顶层已 include），
# 然后在模块里 adofai_link_compression(<target>)。
#
# 全部走 PRIVATE + $<BUILD_INTERFACE:>：
#   * PRIVATE —— 这些库是实现细节，不写进导出接口（消费者由 ADOFAI.LibConfig.cmake 接回来）；
#   * BUILD_INTERFACE —— liblzma / libzstd_static 是构建树里的 target 名，不属于导出集，
#     带进 install(EXPORT) 会让消费者拿到解析不了的名字。

function(adofai_link_compression target)
    if(ADOFAI_LIB_BUNDLE_DEPS)
        target_link_libraries(${target} PRIVATE
            "$<BUILD_INTERFACE:liblzma>" "$<BUILD_INTERFACE:libzstd_static>")
        # 让实现方知道"压缩库是内嵌的"（上游 ADOCAO 的同名宏语义）。
        target_compile_definitions(${target} PUBLIC ADOFAI_LIB_BUNDLE_DEPS=1)
    else()
        find_package(LibLZMA REQUIRED)
        find_package(zstd REQUIRED)
        target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:LibLZMA::LibLZMA>")
        if(TARGET zstd::libzstd_static)
            target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:zstd::libzstd_static>")
        elseif(TARGET zstd::libzstd_shared)
            target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:zstd::libzstd_shared>")
        else()
            target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:zstd::libzstd>")
        endif()
    endif()
endfunction()
