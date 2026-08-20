# Aegis Autonomy (AGV ROS 2 Workspace)

This repository contains the deterministic, C++20 real-time presence detection and AI perception stack for autonomous robotics, featuring zero-copy shared memory IPC and lock-free thread-safe buffers.

## 1. Environment Setup (Distrobox)

This project is designed to be built inside a clean, isolated **Distrobox** container running Ubuntu 24.04 and ROS 2 Jazzy, with native NVIDIA GPU pass-through for accelerated deep learning (ONNX Runtime / TensorRT).

To recreate the exact development container from scratch on any host machine, run:

```bash
distrobox create --name agv_jazzy --image docker.io/osrf/ros:jazzy-desktop --nvidia
distrobox enter agv_jazzy
```

## 2. Dependency Initialization

The OSRF image provides ROS 2 Jazzy and exports `ROS_DISTRO=jazzy`. You do not
need to set `ROS_DISTRO` manually. The setup script validates this variable to
prevent it from being run on the host or in the wrong container.

Run the setup script from the repository root to install system dependencies,
ROS tooling not included in the base image, and CUDA libraries:

```bash
bash setup_distrobox.sh
```

Dependency ownership is intentionally split as follows:

- The OSRF image provides ROS 2 and its standard packages.
- APT provides system libraries, CUDA, and missing ROS development tools.
- Conan provides portable C++ libraries: TOML++, Eigen, libcurl, and
  nlohmann JSON.

Next, initialize the local Python virtual environment and install the Conan
C++ dependencies:

```bash
make init
```

> [!NOTE]
> The default `make` target refreshes Conan dependencies automatically whenever
> `conanfile.txt` changes. Use `make conan_deps` only when you want to refresh
> them explicitly without building the workspace.

## 3. Downloading AI Models & Building the Workspace

This workspace utilizes the incredibly fast **OpenCV YuNet** for sub-pixel facial landmark extraction, and HuggingFace's **Depth Anything V2** (VITS) for absolute metric depth scaling.

The repository is equipped with a highly optimized wrapper `Makefile` that automatically triggers `colcon build` using the lightning-fast Ninja generator. 

As a bonus, the perception module will automatically fetch its own AI weights from HuggingFace and GitHub seamlessly during the build process!

Simply run (defaults to `Debug` build type):

```bash
make
```

You can also specify a release build type:

```bash
make BUILD_TYPE=Release
```

To run workspace unit tests, run:

```bash
make test
```

To clean the build artifacts, run:

```bash
make clean
```

## 4. Executing the Perception Stack

The project includes a robust `tmuxp` configuration that will automatically manage the ROS 2 environment, spawn the camera publisher, boot the AI perception node, and launch the `rqt_image_view` GUI in a 3-pane split screen.

Launch the full stack with:

```bash
tmuxp load config/presence_perception.yaml
```

## 5. Pro-Tip: Automated Terminal Sourcing

Since Distrobox shares your host system's home directory, you can add this "smart" script to your host's `~/.bashrc` (or `~/.bash_aliases`). 

It will automatically detect when you enter the ROS 2 Distrobox container, source the correct ROS 2 Jazzy and Workspace environments, and update your prompt so you never have to type `source` manually!

```bash
if [ -f "/run/.containerenv" ]; then
    # We are inside a distrobox container
    export PS1="📦[\[\033[01;36m\]${CONTAINER_ID}\[\033[00m\]] $PS1"
    if [ -n "${ROS_DISTRO:-}" ] && [ -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then
        source "/opt/ros/${ROS_DISTRO}/setup.bash"
        if [ -f "$HOME/ros2_ws/robot_autonomy/.colcon_cache/install/setup.bash" ]; then
            source "$HOME/ros2_ws/robot_autonomy/.colcon_cache/install/setup.bash"
        fi
    fi
fi

export PATH="$HOME/.local/bin:$PATH"
```
