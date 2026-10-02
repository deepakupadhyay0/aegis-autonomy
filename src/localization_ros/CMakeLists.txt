cmake_minimum_required(VERSION 3.18)
project(localization_ros)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wextra>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(accelerator REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(base_core REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(common REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(localization REQUIRED)
find_package(logging REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(ndt_localization_node
  src/localization_config.cpp
  src/localization_node.cpp
  src/localization_node_exe.cpp
)
target_include_directories(ndt_localization_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ndt_localization_node
  accelerator::accelerator
  autonomy_config::autonomy_config
  base_core::base_core
  common::common
  localization::localization
  logging::logging
)
ament_target_dependencies(ndt_localization_node
  builtin_interfaces
  geometry_msgs
  rclcpp
  sensor_msgs
)

install(
  TARGETS ndt_localization_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(FILES README.md DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)

  ament_cppcheck()
  ament_uncrustify()
endif()

ament_package()
