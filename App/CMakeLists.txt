set(APP_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${APP_ROOT_DIR}/Chassis/*.cpp"
    "${APP_ROOT_DIR}/Cmd/*.cpp"
    "${APP_ROOT_DIR}/Gimbal/*.cpp"
    "${APP_ROOT_DIR}/Health/*.cpp"
    "${APP_ROOT_DIR}/Robot/*.cpp"
    "${APP_ROOT_DIR}/Shoot/*.cpp"
    "${APP_ROOT_DIR}/Topics/*.cpp")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${APP_ROOT_DIR}
    ${APP_ROOT_DIR}/Chassis
    ${APP_ROOT_DIR}/Cmd
    ${APP_ROOT_DIR}/Config
    ${APP_ROOT_DIR}/Gimbal
    ${APP_ROOT_DIR}/Health
    ${APP_ROOT_DIR}/Robot
    ${APP_ROOT_DIR}/Shoot
    ${APP_ROOT_DIR}/Topics)

if(APP_SOURCES)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES})
endif()
