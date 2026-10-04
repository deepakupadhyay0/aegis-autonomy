cmake_minimum_required(VERSION 3.8)
project(ai_diagnostics)

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
find_package(autonomy_msgs REQUIRED)
find_package(base_core REQUIRED)
find_package(common REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(logging REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/ai_diagnostics_config.cpp
  src/ai_diagnostics_node.cpp
  src/incident_window.cpp
  src/llm_client.cpp
  src/llm_schema.cpp
  src/log_reader.cpp
  src/resource_monitor.cpp
)
add_library(ai_diagnostics::ai_diagnostics ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  AI_DIAGNOSTICS_BUILDING_DLL
)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}
  nlohmann_json::nlohmann_json
)
ament_target_dependencies(${PROJECT_NAME}
  autonomy_config
  autonomy_msgs
  base_core
  common
  diagnostic_msgs
  logging
  rclcpp
)

add_executable(ai_diagnostics_node_exe src/ai_diagnostics_node_exe.cpp)
target_link_libraries(ai_diagnostics_node_exe ${PROJECT_NAME})
ament_target_dependencies(ai_diagnostics_node_exe
  base_core
  logging
)

install(DIRECTORY include/ DESTINATION include)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(
  TARGETS ${PROJECT_NAME} ai_diagnostics_node_exe
  EXPORT export_${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
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

  ament_add_gtest(test_llm_schema test/test_llm_schema.cpp)
  target_link_libraries(test_llm_schema ${PROJECT_NAME})
  ament_add_gtest(test_log_reader test/test_log_reader.cpp)
  target_link_libraries(test_log_reader ${PROJECT_NAME})
  ament_add_gtest(test_incident_window test/test_incident_window.cpp)
  target_link_libraries(test_incident_window ${PROJECT_NAME})
  ament_add_gtest(test_ai_diagnostics_node test/test_ai_diagnostics_node.cpp)
  target_link_libraries(test_ai_diagnostics_node ${PROJECT_NAME})
  ament_target_dependencies(test_ai_diagnostics_node
    autonomy_msgs
    base_core
    diagnostic_msgs
    logging
    rclcpp
  )
  ament_add_gtest(test_local_model test/test_local_model.cpp TIMEOUT 360)
  target_link_libraries(test_local_model ${PROJECT_NAME})
  target_compile_definitions(test_local_model PRIVATE
    AI_DIAGNOSTICS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data"
  )
endif()

ament_export_targets(
  export_${PROJECT_NAME}
  HAS_LIBRARY_TARGET
  NAMESPACE ai_diagnostics::
)
ament_export_dependencies(
  autonomy_config
  autonomy_msgs
  base_core
  common
  diagnostic_msgs
  logging
  rclcpp
  nlohmann_json
)

ament_package()
