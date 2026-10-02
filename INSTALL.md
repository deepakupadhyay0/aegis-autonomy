# Installation

[Project overview](README.md)

This workspace is developed in an Ubuntu 24.04 Distrobox with ROS 2 Jazzy and
native NVIDIA GPU access. Keeping ROS, CUDA development packages, LibTorch, and
Conan dependencies in the container avoids coupling the workspace to the host
distribution.

The commands below download and install software. Review
[`setup_distrobox.sh`](setup_distrobox.sh) before running it if you manage CUDA
or disk space manually.

## 1. Host prerequisites

Install and configure these on the host:

- a working NVIDIA driver;
- Distrobox;
- Podman or Docker configured as the Distrobox container manager; and
- enough space for the ROS image, CUDA 12.8 development libraries, LibTorch,
  Conan packages, and workspace build artifacts.

Confirm the host driver first:

```bash
nvidia-smi
```

The container uses the host kernel driver. Do not install a second NVIDIA
kernel driver inside the container.

## 2. Create the development container

Create an Ubuntu 24.04 ROS 2 Jazzy container with GPU pass-through:

```bash
distrobox create \
  --name agv_jazzy_gpu \
  --image docker.io/osrf/ros:jazzy-desktop \
  --nvidia
```

Enter it:

```bash
distrobox enter agv_jazzy_gpu
```

Distrobox shares the host home directory, so the repository remains available
at the same `/home/...` path inside the container.

Verify ROS and the GPU:

```bash
source /opt/ros/jazzy/setup.bash
echo "$ROS_DISTRO"
nvidia-smi
```

`ROS_DISTRO` must report `jazzy`. The setup script intentionally refuses to
run when ROS has not been sourced or when it is executed in the wrong
environment.

## 3. Install system, CUDA, and LibTorch dependencies

From the repository root inside `agv_jazzy_gpu`:

```bash
cd "$HOME/ros2_ws/robot_autonomy"
bash setup_distrobox.sh
```

The script installs:

- compiler, CMake, Ninja, OpenCV, SQLite, spdlog, tmux, tmuxp, and ROS lint
  tooling;
- CUDA 12.8 compiler and libraries, NVRTC development files, NVTX, and cuDNN
  9 development files; and
- CUDA-enabled C++ LibTorch under `/opt/libtorch`.

It also registers `/opt/libtorch/lib` with the dynamic linker. The script
skips LibTorch when a valid CUDA installation already exists at that path and
stops rather than overwriting an unexpected `/opt/libtorch` directory.

Verify the toolchain:

```bash
/usr/local/cuda/bin/nvcc --version
test -f /opt/libtorch/share/cmake/Torch/TorchConfig.cmake
test -f /opt/libtorch/lib/libtorch_cuda.so
ldconfig -p | grep -E 'libtorch_cuda|libc10_cuda'
```

## 4. Initialize workspace dependencies

The repository separates dependency ownership:

- the OSRF image provides ROS 2 Jazzy;
- APT provides system and NVIDIA development libraries;
- `/opt/libtorch` provides the CUDA-enabled C++ PyTorch distribution; and
- Conan provides portable libraries such as TOML++, Eigen, libcurl, and
  nlohmann JSON.

Initialize the local Python environment and Conan dependencies once:

```bash
make init
```

The environment is stored in `.agv_venv`. Conan outputs and normal CMake
artifacts are stored under `build/<BuildType>`. The normal workspace build is
stored under `.colcon_cache`.

Although Conan Center publishes a `libtorch` recipe, this workspace uses the
official CUDA binary explicitly because the recipe configuration evaluated for
this project did not provide the required CUDA build.

## 5. Build the workspace

Build in Debug mode:

```bash
make
```

Build optimized binaries:

```bash
make BUILD_TYPE=Release
```

The Makefile supplies these defaults:

```text
CUDA compiler:          /usr/local/cuda/bin/nvcc
CUDA architecture:      native
LibTorch CMake prefix:  /opt/libtorch/share/cmake
Torch CUDA arch list:   12.0
```

They can be overridden for another environment:

```bash
make \
  CUDA_COMPILER=/path/to/nvcc \
  CUDA_ARCHITECTURES=120 \
  TORCH_CUDA_ARCH_LIST=12.0 \
  TORCH_CMAKE_PREFIX=/path/to/libtorch/share/cmake
```

`make` refreshes Conan dependencies automatically when `conanfile.txt`
changes. Use `make conan_deps` to refresh them explicitly.

The `presence_detection` CMake configuration downloads its pinned ONNX Runtime
GPU package and model assets when they are absent, so its first build also
requires network access.

## 6. Build the offline place-model experiment

The place-model trainer is intentionally outside the default colcon package
set. Build it explicitly:

```bash
make place_model
```

Its Debug executable is:

```text
build/Debug/place_recognition/place_model
```

Run the encoder smoke test, including a required CUDA forward pass:

```bash
make place_model_test
```

## 7. Run tests

Run the colcon test suite:

```bash
make test
```

The command writes test results under `.colcon_cache/test_results` and prints
the aggregated result through `colcon test-result`.

## 8. Source the workspace

Source ROS and the colcon install space in each new container shell:

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/ros2_ws/robot_autonomy/.colcon_cache/install/setup.bash"
```

An optional shell helper for Distrobox is:

```bash
if [ -f /run/.containerenv ]; then
    if [ -n "${ROS_DISTRO:-}" ] && \
       [ -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then
        source "/opt/ros/${ROS_DISTRO}/setup.bash"
    fi

    workspace_setup="$HOME/ros2_ws/robot_autonomy/.colcon_cache/install/setup.bash"
    if [ -f "$workspace_setup" ]; then
        source "$workspace_setup"
    fi
fi

export PATH="$HOME/.local/bin:$PATH"
```

Place it in the shell configuration used inside the container. If the host and
container share the same shell configuration, keep the `/run/.containerenv`
guard so ROS paths are not sourced on the host.

## 9. Runtime configuration

Generated defaults are installed with `autonomy_config`. Create a deployment
directory and copy only the configurations you need:

```bash
mkdir -p "$PWD/runtime_config"
cp "$(ros2 pkg prefix autonomy_config)/share/autonomy_config/config/localization.toml.default" \
  "$PWD/runtime_config/localization.toml"
cp "$(ros2 pkg prefix autonomy_config)/share/autonomy_config/config/place_recognition.toml.default" \
  "$PWD/runtime_config/place_recognition.toml"

export AEGIS_AUTONOMY_CONFIG_DIR="$PWD/runtime_config"
```

Edit the copied files. Do not edit generated headers. Relative paths in
configuration should be interpreted deliberately; use absolute checkpoint and
dataset paths for repeatable runs.

## 10. Clean generated artifacts

To remove workspace and standalone CMake outputs:

```bash
make clean
```

This removes `.colcon_cache`, `build`, `install`, and `log` under the
repository. It does not remove `/opt/libtorch`, CUDA packages, Conan's global
cache, datasets, or Distrobox containers.
