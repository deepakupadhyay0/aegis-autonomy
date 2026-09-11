cmake_minimum_required(VERSION 3.8)
project(utils)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(base_core REQUIRED)
find_package(common REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/topic_timing_config.cpp
  src/topic_timing_monitor.cpp
)
target_compile_definitions(${PROJECT_NAME} PRIVATE UTILS_BUILDING_DLL)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME} PUBLIC
  autonomy_config::autonomy_config
  base_core::base_core
  common::common
)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_clang_tidy REQUIRED)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)
  ament_clang_tidy(${CMAKE_BINARY_DIR})
  ament_cppcheck()
  ament_uncrustify()
  ament_add_gtest(test_topic_timing_monitor test/test_topic_timing_monitor.cpp)
  target_link_libraries(test_topic_timing_monitor ${PROJECT_NAME})
endif()

ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(autonomy_config base_core common)
ament_package()
