# ADOFAI::render —— 砖块渲染（默认 OFF，见 PLAN.md §5）。
#
# render=ON 时它会引入 glad（手写 GL 加载器）→ GLFW → 平台 GL。公共头里确实暴露 GL 类型
# （Planet.hpp / PlanetTrail.hpp / Shader.hpp / TileMesh.hpp 里有 GLuint 等），所以这条线
# 一开就绕不开 GL；OFF 时下面这些东西一个都不该出现 —— 由根 CMakeLists 的选项守卫保证
# （连这个目录都不会被 add_subdirectory）。
add_library(adofai_render STATIC
    Camera.cpp
    Shader.cpp
    TileShape.cpp
    TileMesh.cpp
    Planet.cpp
    PlanetTrail.cpp)

target_include_directories(adofai_render
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# glad 写 PUBLIC：render 的公共头里 include <glad/gl_core.hpp>，消费者要拿到它的 include 根。
target_link_libraries(adofai_render
    PUBLIC  adofai_core adofai_glad)

# GLFW 与平台 GL：render 的公共头带 GLuint 等类型，而且 glad 靠 glfwGetProcAddress 解析入口，
# 所以消费者必须能链到 GLFW 与 GL。
# 但**装出去的接口里不能出现它们**：`$<BUILD_INTERFACE:>` 在构建树里仍会求值，install(EXPORT)
# 会把 "glfw" / "-framework OpenGL" 这种构建期名字原样写进 Targets.cmake（实测过）。
# 所以这三条依赖只挂构建树，装出去之后由 cmake/ADOFAI.LibConfig.cmake.in 的
# find_dependency(glfw3/OpenGL) 接回来 —— 与 glm 的处理方式一致。
target_link_libraries(adofai_render PRIVATE glfw)
if(WIN32)
    target_link_libraries(adofai_render PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(adofai_render PRIVATE "-framework OpenGL")
else()
    target_link_libraries(adofai_render PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adofai_render PRIVATE -O3 -march=native -fomit-frame-pointer)
endif()
set_target_properties(adofai_render PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# 装出去叫 ADOFAI::render。
set_target_properties(adofai_render PROPERTIES EXPORT_NAME render)

add_library(ADOFAI::render ALIAS adofai_render)

# TileGeometryReference.cpp —— 改造前的 CPU 几何，**只进测试**的 A/B 基准
# （tests/tile_expansion_test.cpp 用它逐位钉住新实现）。渲染路径不依赖它，
# 所以它编成一个**不安装**的 target。
add_library(adofai_tile_reference STATIC
    TileGeometryReference.cpp)
target_include_directories(adofai_tile_reference PUBLIC
    "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>")
set_target_properties(adofai_tile_reference PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
