cmake_minimum_required(VERSION 3.8)
project(database)

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
find_package(common REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/database_backend.cpp
  src/database_config.cpp
  src/database_handler.cpp
  src/sqlite_database.cpp
)
add_library(database::database ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(${PROJECT_NAME} PRIVATE DATABASE_BUILDING_DLL)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME} PUBLIC
  autonomy_config::autonomy_config
  common::common
  SQLite::SQLite3
  Threads::Threads
)

install(DIRECTORY include/ DESTINATION include)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
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

  ament_add_gtest(test_database test/test_database.cpp)
  target_link_libraries(test_database ${PROJECT_NAME})
endif()

ament_export_targets(
  export_${PROJECT_NAME}
  HAS_LIBRARY_TARGET
  NAMESPACE database::
)
ament_export_dependencies(autonomy_config common SQLite3 Threads)
ament_package()
