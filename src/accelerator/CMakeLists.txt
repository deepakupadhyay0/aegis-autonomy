cmake_minimum_required(VERSION 3.18)
project(accelerator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(DEFINED ENV{ROBOT_AUTONOMY_CUDA_ARCHITECTURES} AND
  NOT "$ENV{ROBOT_AUTONOMY_CUDA_ARCHITECTURES}" STREQUAL "")
  set(CMAKE_CUDA_ARCHITECTURES "$ENV{ROBOT_AUTONOMY_CUDA_ARCHITECTURES}")
endif()

# A previous configure can leave this cache entry defined but empty. CMake rejects an empty
# architecture list before it can ask NVCC for its default, so remove only that invalid value.
# Explicit non-empty values supplied by a toolchain or the user are preserved.
if(DEFINED CMAKE_CUDA_ARCHITECTURES AND
  "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL "")
  unset(CMAKE_CUDA_ARCHITECTURES)
  unset(CMAKE_CUDA_ARCHITECTURES CACHE)
endif()

# CMake 3.24 and newer can ask the CUDA runtime for the architecture of the visible GPU.
# Older CMake versions retain NVCC's default unless a toolchain supplies an explicit value.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND
  CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
  set(CMAKE_CUDA_ARCHITECTURES native)
endif()

enable_language(CUDA)

option(ACCELERATOR_BUILD_EXAMPLES "Build accelerator usage examples" ON)

add_compile_options(
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wextra>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(common REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/accelerator.cpp
  src/operation.cpp
  src/cuda_operations/elementwise_multiply.cu
  src/cuda_operations/matrix_multiply.cu
)
add_library(accelerator::accelerator ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  CUDA_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_definitions(${PROJECT_NAME} PRIVATE ACCELERATOR_BUILDING_DLL)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}
  PUBLIC common::common
  PRIVATE CUDA::cublas CUDA::cudart
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY examples/ DESTINATION share/${PROJECT_NAME}/examples)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

if(ACCELERATOR_BUILD_EXAMPLES)
  add_executable(accelerator_vector_pipeline_example
    examples/vector_pipeline/vector_pipeline.cpp
    examples/vector_pipeline/vector_pipeline.cu
  )
  target_compile_options(accelerator_vector_pipeline_example PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
  )
  target_link_libraries(accelerator_vector_pipeline_example
    PRIVATE ${PROJECT_NAME} CUDA::cudart
  )

  add_executable(accelerator_point_cloud_centroid_example
    examples/point_cloud_centroid/point_cloud_centroid.cpp
    examples/point_cloud_centroid/point_cloud_centroid.cu
  )
  target_compile_options(accelerator_point_cloud_centroid_example PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
  )
  target_link_libraries(accelerator_point_cloud_centroid_example
    PRIVATE ${PROJECT_NAME} CUDA::cudart
  )

  add_executable(accelerator_image_box_blur_example
    examples/image_box_blur/image_box_blur.cpp
    examples/image_box_blur/image_box_blur.cu
  )
  target_compile_options(accelerator_image_box_blur_example PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
  )
  target_link_libraries(accelerator_image_box_blur_example
    PRIVATE ${PROJECT_NAME} CUDA::cudart
  )

  install(
    TARGETS
      accelerator_vector_pipeline_example
      accelerator_point_cloud_centroid_example
      accelerator_image_box_blur_example
    RUNTIME DESTINATION lib/${PROJECT_NAME}
  )
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)
  ament_cppcheck()
  ament_uncrustify()

  ament_add_gtest(test_accelerator test/test_accelerator.cpp)
  target_sources(test_accelerator PRIVATE test/test_cuda_patterns.cu)
  target_compile_options(test_accelerator PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
  )
  target_link_libraries(test_accelerator ${PROJECT_NAME} CUDA::cudart)
endif()

ament_export_targets(
  export_${PROJECT_NAME}
  HAS_LIBRARY_TARGET
  NAMESPACE accelerator::
)
ament_export_dependencies(common)

ament_package()
