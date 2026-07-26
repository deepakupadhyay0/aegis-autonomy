cmake_minimum_required(VERSION 3.8)
project(base_node)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(autonomy_config REQUIRED)

# This is now a fully compiled SHARED library
add_library(${PROJECT_NAME} SHARED 
  src/mutex.cpp
  src/ipc_socket.cpp
  src/shm_ring_buffer.cpp
  src/config_dir.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  rclcpp::rclcpp
  autonomy_config::autonomy_config
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_ipc test/test_ipc.cpp)
  target_link_libraries(test_ipc ${PROJECT_NAME})
  ament_add_gtest(test_buffers test/test_buffers.cpp)
  target_link_libraries(test_buffers ${PROJECT_NAME})
endif()

ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(rclcpp autonomy_config)

ament_package()
