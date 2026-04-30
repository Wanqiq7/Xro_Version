# CMakeLists.txt for RLS (递推最小二乘)

# 将模块目录加入 include 路径
target_include_directories(xr PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# 自动收集源文件
file(GLOB MODULE_RLS_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

target_sources(xr PRIVATE ${MODULE_RLS_SRC})

# INTERFACE 依赖占位
get_filename_component(_MODULE_DIRNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
set(_DEPS_TARGET "${_MODULE_DIRNAME}_deps")
add_library(${_DEPS_TARGET} INTERFACE)

# 注册到全局模块依赖列表
set_property(GLOBAL APPEND PROPERTY XR_MODULE_DEPS ${_DEPS_TARGET})
