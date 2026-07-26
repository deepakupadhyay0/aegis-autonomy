cmake_minimum_required(VERSION 3.8)
project(autonomy_msgs)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

file(GLOB INTERFACE_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "msg/*.msg"
  "srv/*.srv"
  "action/*.action"
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${INTERFACE_FILES}
  DEPENDENCIES std_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
