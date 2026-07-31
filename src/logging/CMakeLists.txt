cmake_minimum_required(VERSION 3.8)
project(logging)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(common REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

add_library(logging_client SHARED
  src/log_client.cpp
  src/log_queue.cpp
  src/logger.cpp
  src/unix_log_transport.cpp
)
add_library(logging::logging_client ALIAS logging_client)

set_target_properties(logging_client PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(logging_client PRIVATE LOGGING_BUILDING_DLL)
target_include_directories(logging_client PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(logging_client
  PUBLIC common::common
  PRIVATE Threads::Threads
)

add_executable(logging_service
  src/diagnostic_dds_sink.cpp
  src/logging_service.cpp
  src/main.cpp
  src/node_log_sink.cpp
)
target_include_directories(logging_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(logging_service
  common::common
  spdlog::spdlog
  Threads::Threads
)
ament_target_dependencies(logging_service diagnostic_msgs rclcpp)

install(DIRECTORY include/ DESTINATION include)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(
  TARGETS logging_client
  EXPORT export_logging
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)
install(
  TARGETS logging_service
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_log_client test/test_log_client.cpp)
  target_link_libraries(test_log_client logging_client)
  ament_add_gtest(test_log_queue test/test_log_queue.cpp)
  target_link_libraries(test_log_queue logging_client)
endif()

ament_export_targets(
  export_logging
  HAS_LIBRARY_TARGET
  NAMESPACE logging::
)
ament_export_dependencies(common)

ament_package()
