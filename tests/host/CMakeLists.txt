cmake_minimum_required(VERSION 3.20)

if((DEFINED CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER MATCHES "arm-none-eabi") OR
   ("$ENV{CXX}" MATCHES "arm-none-eabi"))
  message(FATAL_ERROR
    "Host tests require a native C++ compiler, but the configured compiler is "
    "ARM GCC. Clear CC/CXX/CMAKE_CXX_COMPILER/toolchain settings or configure "
    "with a native compiler such as MSVC, MinGW, or clang.")
endif()

project(XroHostTests LANGUAGES CXX)

enable_testing()

add_executable(remote_input_mapping_test remote_input_mapping_test.cpp)
target_compile_features(remote_input_mapping_test PRIVATE cxx_std_17)
target_include_directories(remote_input_mapping_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
add_test(NAME remote_input_mapping_test COMMAND remote_input_mapping_test)

add_executable(ins_quaternion_estimator_test ins_quaternion_estimator_test.cpp)
target_compile_features(ins_quaternion_estimator_test PRIVATE cxx_std_20)
target_include_directories(ins_quaternion_estimator_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
add_test(NAME ins_quaternion_estimator_test COMMAND ins_quaternion_estimator_test)

add_executable(gimbal_protocol_math_test gimbal_protocol_math_test.cpp)
target_compile_features(gimbal_protocol_math_test PRIVATE cxx_std_17)
target_include_directories(gimbal_protocol_math_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
add_test(NAME gimbal_protocol_math_test COMMAND gimbal_protocol_math_test)

add_executable(chassis_follow_gimbal_math_test chassis_follow_gimbal_math_test.cpp)
target_compile_features(chassis_follow_gimbal_math_test PRIVATE cxx_std_20)
target_include_directories(chassis_follow_gimbal_math_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
add_test(NAME chassis_follow_gimbal_math_test COMMAND chassis_follow_gimbal_math_test)

add_executable(bmi088_gyro_calibration_static_test ../bmi088_gyro_calibration_static_test.cpp)
target_compile_features(bmi088_gyro_calibration_static_test PRIVATE cxx_std_20)
target_include_directories(bmi088_gyro_calibration_static_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
add_test(NAME bmi088_gyro_calibration_static_test COMMAND bmi088_gyro_calibration_static_test)
