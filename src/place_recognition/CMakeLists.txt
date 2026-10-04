cmake_minimum_required(VERSION 3.24)
project(place_recognition)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)

# This package does not compile CUDA sources. A stale workspace-wide value makes
# Torch warn even though Torch selects its own CUDA targets.
unset(CMAKE_CUDA_ARCHITECTURES)
unset(CMAKE_CUDA_ARCHITECTURES CACHE)
if(DEFINED ENV{TORCH_CUDA_ARCH_LIST} AND
  NOT "$ENV{TORCH_CUDA_ARCH_LIST}" STREQUAL "")
  set(TORCH_CUDA_ARCH_LIST "$ENV{TORCH_CUDA_ARCH_LIST}")
endif()
if(DEFINED ENV{TORCH_CMAKE_PREFIX} AND
  NOT "$ENV{TORCH_CMAKE_PREFIX}" STREQUAL "")
  set(
    Torch_DIR
    "$ENV{TORCH_CMAKE_PREFIX}/Torch"
    CACHE PATH "Path containing TorchConfig.cmake" FORCE)
endif()
# LibTorch's Caffe2 config probes nvtx3 with a different package name.
set(FPHSA_NAME_MISMATCHED TRUE)
find_package(Torch REQUIRED)
unset(FPHSA_NAME_MISMATCHED)
separate_arguments(TORCH_COMPILE_OPTIONS NATIVE_COMMAND "${TORCH_CXX_FLAGS}")

add_library(${PROJECT_NAME} SHARED
  src/camera_encoder.cpp
  src/place_encoder.cpp
  src/wifi_encoder.cpp
)
add_library(place_recognition::place_recognition ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME} PUBLIC ${TORCH_LIBRARIES})
target_compile_options(${PROJECT_NAME}
  PUBLIC ${TORCH_COMPILE_OPTIONS}
  PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

install(DIRECTORY include/ DESTINATION include)
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

ament_export_dependencies(Torch)
ament_export_targets(
  export_${PROJECT_NAME}
  HAS_LIBRARY_TARGET
  NAMESPACE place_recognition::
)
ament_package()
