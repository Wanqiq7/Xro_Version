# CMakeLists.txt for SMC (Sliding Mode Control)

# Add module to include path
target_include_directories(xr PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Auto-include source files (header-only, GLOB will be empty)
file(GLOB MODULE_SMC_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

target_sources(xr PRIVATE ${MODULE_SMC_SRC})
