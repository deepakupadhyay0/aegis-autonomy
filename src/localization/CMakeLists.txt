cmake_minimum_required(VERSION 3.18)
project(localization)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(DEFINED CMAKE_CUDA_ARCHITECTURES AND
  "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL "")
  unset(CMAKE_CUDA_ARCHITECTURES)
  unset(CMAKE_CUDA_ARCHITECTURES CACHE)
endif()
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND
  CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
  set(CMAKE_CUDA_ARCHITECTURES native)
endif()
enable_language(CUDA)

add_compile_options(
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wextra>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(accelerator REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(common REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/cuda_operations/ndt_evaluation.cu
  src/cuda_ndt_localizer.cpp
  src/ndt_localizer.cpp
  src/ndt_map.cpp
)
add_library(localization::localization ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  CUDA_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(${PROJECT_NAME} PRIVATE LOCALIZATION_BUILDING_DLL)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    accelerator::accelerator
    autonomy_config::autonomy_config
    common::common
  PRIVATE CUDA::cudart Eigen3::Eigen
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
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)

  ament_cppcheck()
  ament_uncrustify()

  ament_add_gtest(test_ndt_localization
    test/test_ndt_localization.cpp
  )
  target_link_libraries(test_ndt_localization ${PROJECT_NAME})
endif()

ament_export_targets(
  export_${PROJECT_NAME}
  HAS_LIBRARY_TARGET
  NAMESPACE localization::
)
ament_export_dependencies(accelerator autonomy_config common)

ament_package()
