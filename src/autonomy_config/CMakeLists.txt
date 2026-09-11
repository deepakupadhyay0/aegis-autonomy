cmake_minimum_required(VERSION 3.8)
project(autonomy_config)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(common REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

set(TOML_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/toml2cpp.py")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(TOML_SCHEMAS
  "${CMAKE_CURRENT_SOURCE_DIR}/config/ai_diagnostics.toml.default"
  "${CMAKE_CURRENT_SOURCE_DIR}/config/database.toml.default"
  "${CMAKE_CURRENT_SOURCE_DIR}/config/logging.toml.default"
  "${CMAKE_CURRENT_SOURCE_DIR}/config/presence_detection.toml.default"
  "${CMAKE_CURRENT_SOURCE_DIR}/config/topic_timing.toml.default"
)
set(GENERATED_SOURCES "")
foreach(SCHEMA_FILE ${TOML_SCHEMAS})
  string(REGEX REPLACE "\\.toml\\.default$" "" BARE_PATH "${SCHEMA_FILE}")
  get_filename_component(BARE_NAME "${BARE_PATH}" NAME)

  set(GEN_HPP "${GEN_DIR}/include/autonomy_config/${BARE_NAME}.hpp")
  set(GEN_CPP "${GEN_DIR}/src/${BARE_NAME}.cpp")

  add_custom_command(
    OUTPUT ${GEN_HPP} ${GEN_CPP}
    COMMAND python3 ${TOML_SCRIPT} --toml=${SCHEMA_FILE} --hpp=${GEN_HPP} --cpp=${GEN_CPP} --ns=autonomy_config
    DEPENDS ${TOML_SCRIPT} ${SCHEMA_FILE}
    COMMENT "Generating C++ TOML configuration from ${BARE_NAME}.toml.default"
  )

  list(APPEND GENERATED_SOURCES ${GEN_CPP})
endforeach()

if(NOT GENERATED_SOURCES)
  message(FATAL_ERROR "No *.toml.default schema files found in ${CMAKE_CURRENT_SOURCE_DIR}/config/")
endif()

add_library(${PROJECT_NAME} SHARED
  ${GENERATED_SOURCES}
  src/config_directory.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${GEN_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  common::common
  tomlplusplus::tomlplusplus
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  DIRECTORY ${GEN_DIR}/include/
  DESTINATION include
)

install(
  DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
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
  find_package(ament_cmake_clang_tidy REQUIRED)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)

  ament_clang_tidy(${CMAKE_BINARY_DIR})
  ament_cppcheck()
  ament_uncrustify()

  ament_add_gtest(test_config test/test_config.cpp)
  target_link_libraries(test_config ${PROJECT_NAME})
endif()

ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(common tomlplusplus)

ament_package()
