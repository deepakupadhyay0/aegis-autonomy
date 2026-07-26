cmake_minimum_required(VERSION 3.8)
project(presence_detection)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wextra>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wpedantic>
)

# --- AI Model Initialization ---
message(STATUS "Checking and downloading AI models...")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "scripts/download_models.py" "scripts/models.json")

execute_process(
  COMMAND python3 scripts/download_models.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE DOWNLOAD_RESULT
)
if(NOT DOWNLOAD_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to download AI models! Check network connection.")
endif()

# --- Dependencies ---
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(base_node REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(autonomy_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)
find_package(ament_index_cpp REQUIRED)

set(COMMON_DEPS
  rclcpp
  base_node
  sensor_msgs
  std_msgs
  cv_bridge
  OpenCV
)

# --- External GPU Dependencies (ONNX Runtime) ---
include(FetchContent)
FetchContent_Declare(
  onnxruntime
  URL https://github.com/microsoft/onnxruntime/releases/download/v1.19.0/onnxruntime-linux-x64-gpu-1.19.0.tgz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

# Encapsulate raw .so and headers into a Modern CMake Imported Target
add_library(onnxruntime::onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
  IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so"
  INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
)

# --- Targets: camera_node ---
add_library(camera_node src/camera_node.cpp)
target_include_directories(camera_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(camera_node ${COMMON_DEPS} autonomy_config)

add_executable(camera_node_exe src/camera_node_exe.cpp)
target_link_libraries(camera_node_exe camera_node)
ament_target_dependencies(camera_node_exe ${COMMON_DEPS})

# --- Targets: perception_node ---
add_library(perception_node src/perception_node.cpp)
target_include_directories(perception_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(perception_node onnxruntime::onnxruntime)
ament_target_dependencies(perception_node ${COMMON_DEPS} autonomy_config autonomy_msgs ament_index_cpp)

add_executable(perception_node_exe src/perception_node_exe.cpp)
target_link_libraries(perception_node_exe perception_node)
ament_target_dependencies(perception_node_exe ${COMMON_DEPS} ament_index_cpp)

# --- Runtime Library Search Paths (RPATH) ---
set(GPU_RPATH_DIRS
  "${onnxruntime_SOURCE_DIR}/lib"
  "/usr/local/cuda/lib64"
  "/usr/local/cuda-12.8/lib64"
)
list(JOIN GPU_RPATH_DIRS ":" GPU_RPATH_STR)

set_target_properties(perception_node perception_node_exe PROPERTIES
  BUILD_RPATH "${GPU_RPATH_STR}"
  INSTALL_RPATH "${GPU_RPATH_STR}"
)

# --- Installation & Export ---
install(TARGETS
  camera_node_exe
  perception_node_exe
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY models/
  DESTINATION share/${PROJECT_NAME}/models
)

ament_export_include_directories(include)
ament_package()
