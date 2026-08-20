cmake_minimum_required(VERSION 3.8)
project(common)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(CURL CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/cancellable_curl.cpp
  src/exit_detection.cpp
  src/ipc_codec.cpp
  src/time.cpp
  src/time_conversion.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
  CURL::libcurl
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
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

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(CURL)

if(BUILD_TESTING)
  find_package(ament_cmake_clang_tidy REQUIRED)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)

  ament_clang_tidy(${CMAKE_BINARY_DIR})
  ament_cppcheck()
  ament_uncrustify()

  ament_add_gtest(test_ipc_codec test/test_ipc_codec.cpp)
  target_link_libraries(test_ipc_codec ${PROJECT_NAME})
  ament_add_gtest(test_fixed_string test/test_fixed_string.cpp)
  target_link_libraries(test_fixed_string ${PROJECT_NAME})
  ament_add_gtest(test_strict_vector test/test_strict_vector.cpp)
  target_link_libraries(test_strict_vector ${PROJECT_NAME})
  ament_add_gtest(test_numeric_types test/test_numeric_types.cpp)
  target_link_libraries(test_numeric_types ${PROJECT_NAME})
  ament_add_gtest(test_time test/test_time.cpp)
  target_link_libraries(test_time ${PROJECT_NAME})
  ament_add_gtest(test_wraparound test/test_wraparound.cpp)
  target_link_libraries(test_wraparound ${PROJECT_NAME})
  ament_add_gtest(
    test_cancellable_curl
    test/test_cancellable_curl.cpp
  )
  target_link_libraries(test_cancellable_curl ${PROJECT_NAME})
  ament_add_gtest(
    test_bounded_mpsc_queue
    test/test_bounded_mpsc_queue.cpp
  )
  target_link_libraries(test_bounded_mpsc_queue ${PROJECT_NAME})
endif()

ament_package()
