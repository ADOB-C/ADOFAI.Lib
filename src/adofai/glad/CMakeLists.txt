# ADOFAI::glad —— 上游手写的极简 GL 加载器（只列项目用到的入口，不是完整的 glad 生成物）。
#
# 它通过 glfwGetProcAddress 取函数指针，所以**依赖 GLFW** —— 也就是说 render=ON 一定会把
# GLFW 拉进来。这正是 PLAN.md §5 那条承诺的反面：render=OFF 时这些东西一个都不该出现
# （不 find_package(OpenGL/glfw)、不拉 GLFW、公共头里没有 GL 类型、没有 ADOFAI::render）。
add_library(adofai_glad STATIC
    gl_core.cpp)

# 装出去也叫 ADOFAI::glad：render 的公共头里会 include <glad/gl_core.hpp>，
# 消费者必须拿得到它的 include 根，所以它得进导出集（glfw 是它的 PRIVATE 依赖）。
target_include_directories(adofai_glad
    PUBLIC  "$<BUILD_INTERFACE:${ADOFAI_LIB_SRC_ROOT}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/adofai>")

# glfw 是**父目录**（render 那层）在 render=ON 时提供的 target。
# PRIVATE 足够：gl_core.cpp 需要它，符号（glfwGetProcAddress）由它解析；
# 消费者的链接需求由 ADOFAI::render 带出（见 render/CMakeLists.txt）。
target_link_libraries(adofai_glad PRIVATE glfw)

set_target_properties(adofai_glad PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
set_target_properties(adofai_glad PROPERTIES EXPORT_NAME glad)

add_library(ADOFAI::glad ALIAS adofai_glad)
