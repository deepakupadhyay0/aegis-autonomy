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

Once inside the container, you must install the core system dependencies (like Ninja, Python Virtual Environments, OpenCV data, and CUDA libraries).

Run the automated setup script from the root of the workspace to configure system dependencies, fetch the NVIDIA Container Toolkit GPG key, and install CUDA libraries:

```bash
bash setup_distrobox.sh
```

Next, initialize the local Python virtual environment and download all Conan C++ dependencies (TOML++, Eigen):

```bash
make init
```

> [!NOTE]
> Whenever you modify [conanfile.txt](file:///home/cadmus/ros2_ws/robot_autonomy/conanfile.txt) (e.g., adding a new C++ library or changing versions), you do not need to rerun `make init`. Simply run:
> ```bash
> make conan_deps
> ```

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
    if [ -f "/opt/ros/jazzy/setup.bash" ]; then
        source /opt/ros/jazzy/setup.bash
        if [ -f "$HOME/ros2_ws/install/setup.bash" ]; then
            source "$HOME/ros2_ws/install/setup.bash"
        fi
    fi
fi

export PATH="$HOME/.local/bin:$PATH"
```
