cmake_minimum_required(VERSION 3.8)
project(base_core)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(common REQUIRED)

# This is now a fully compiled SHARED library
add_library(${PROJECT_NAME} SHARED 
  src/mutex.cpp
  src/ipc_socket.cpp
  src/named_resource_lock.cpp
  src/shared_memory.cpp
  src/shm_slot.cpp
  src/shm_ring_buffer.cpp
  src/thread_scheduling.cpp
  src/config_dir.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle
  autonomy_config::autonomy_config
  common::common
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
  find_package(std_msgs REQUIRED)
  ament_add_gtest(test_ipc test/test_ipc.cpp)
  target_link_libraries(test_ipc ${PROJECT_NAME})
  ament_add_gtest(test_buffers test/test_buffers.cpp)
  target_link_libraries(test_buffers ${PROJECT_NAME})
  ament_add_gtest(test_mutex test/test_mutex.cpp)
  target_link_libraries(test_mutex ${PROJECT_NAME})
  ament_add_gtest(test_named_resource_lock test/test_named_resource_lock.cpp)
  target_link_libraries(test_named_resource_lock ${PROJECT_NAME})
  ament_add_gtest(test_waitset test/test_waitset.cpp)
  target_link_libraries(test_waitset ${PROJECT_NAME})
  ament_target_dependencies(test_waitset rclcpp std_msgs)
  ament_add_gtest(test_base_node_runner test/test_base_node_runner.cpp)
  target_link_libraries(test_base_node_runner ${PROJECT_NAME})
  ament_target_dependencies(test_base_node_runner rclcpp rclcpp_lifecycle)
  ament_add_gtest(test_waiting_subscriber test/test_waiting_subscriber.cpp)
  target_link_libraries(test_waiting_subscriber ${PROJECT_NAME})
  ament_target_dependencies(
    test_waiting_subscriber
    rclcpp
    rclcpp_lifecycle
    std_msgs
  )
endif()

ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(rclcpp rclcpp_lifecycle autonomy_config common)

ament_package()
