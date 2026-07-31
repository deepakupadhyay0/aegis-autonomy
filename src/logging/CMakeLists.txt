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
find_package(autonomy_config REQUIRED)
find_package(common REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(Git QUIET)
find_package(rclcpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

ament_package_xml()

set(LOGGING_GIT_PROJECT "unknown")
set(LOGGING_LAST_SHA "unknown")
set(LOGGING_BRANCH_NAME "detached")
set(LOGGING_CICD_SHA "not_available")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
      remote get-url origin
    OUTPUT_VARIABLE LOGGING_GIT_REMOTE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT LOGGING_GIT_REMOTE STREQUAL "")
    string(REGEX REPLACE "^.*[/:]" ""
      LOGGING_GIT_PROJECT "${LOGGING_GIT_REMOTE}")
    string(REGEX REPLACE "\\.git$" ""
      LOGGING_GIT_PROJECT "${LOGGING_GIT_PROJECT}")
  else()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
        rev-parse --show-toplevel
      OUTPUT_VARIABLE LOGGING_GIT_ROOT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(NOT LOGGING_GIT_ROOT STREQUAL "")
      get_filename_component(LOGGING_GIT_PROJECT "${LOGGING_GIT_ROOT}" NAME)
    endif()
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
      rev-parse HEAD
    OUTPUT_VARIABLE LOGGING_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT LOGGING_GIT_SHA STREQUAL "")
    set(LOGGING_LAST_SHA "${LOGGING_GIT_SHA}")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
      branch --show-current
    OUTPUT_VARIABLE LOGGING_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT LOGGING_GIT_BRANCH STREQUAL "")
    set(LOGGING_BRANCH_NAME "${LOGGING_GIT_BRANCH}")
  endif()
endif()

if(NOT "$ENV{CICD_SHA}" STREQUAL "")
  set(LOGGING_CICD_SHA "$ENV{CICD_SHA}")
elseif(NOT "$ENV{CI_COMMIT_SHA}" STREQUAL "")
  set(LOGGING_CICD_SHA "$ENV{CI_COMMIT_SHA}")
elseif(NOT "$ENV{GITHUB_SHA}" STREQUAL "")
  set(LOGGING_CICD_SHA "$ENV{GITHUB_SHA}")
endif()
if(NOT "$ENV{CI_COMMIT_REF_NAME}" STREQUAL "")
  set(LOGGING_BRANCH_NAME "$ENV{CI_COMMIT_REF_NAME}")
elseif(NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
  set(LOGGING_BRANCH_NAME "$ENV{GITHUB_REF_NAME}")
endif()
string(TIMESTAMP LOGGING_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

add_library(${PROJECT_NAME} SHARED
  src/diagnostic_dds_sink.cpp
  src/log_backend.cpp
  src/log_queue.cpp
  src/logging_config.cpp
  src/logger.cpp
  src/node_log_sink.cpp
)
add_library(logging::logging ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  LOGGING_BUILDING_DLL
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_CMAKE_PROJECT=\"${PROJECT_NAME}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_PACKAGE_VERSION=\"${${PROJECT_NAME}_VERSION}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_GIT_PROJECT=\"${LOGGING_GIT_PROJECT}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_LAST_SHA=\"${LOGGING_LAST_SHA}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_CICD_SHA=\"${LOGGING_CICD_SHA}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_BRANCH_NAME=\"${LOGGING_BRANCH_NAME}\">"
  "$<$<COMPILE_LANGUAGE:CXX>:CORE_LOGGER_BUILD_TIMESTAMP=\"${LOGGING_BUILD_TIMESTAMP}\">"
)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}
  autonomy_config::autonomy_config
  common::common
  spdlog::spdlog
  Threads::Threads
)
ament_target_dependencies(${PROJECT_NAME} diagnostic_msgs rclcpp)

install(DIRECTORY include/ DESTINATION include)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_logging
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_log_backend test/test_log_backend.cpp)
  target_link_libraries(test_log_backend ${PROJECT_NAME})
  ament_add_gtest(test_log_queue test/test_log_queue.cpp)
  target_link_libraries(test_log_queue ${PROJECT_NAME})
endif()

ament_export_targets(
  export_logging
  HAS_LIBRARY_TARGET
  NAMESPACE logging::
)
ament_export_dependencies(
  common
  autonomy_config
  diagnostic_msgs
  rclcpp
  spdlog
  Threads
)

ament_package()
