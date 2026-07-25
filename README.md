# AGV ROS 2 Jazzy Workspace

This repository contains the deterministic, C++17 real-time perception and navigation stack for the AGV.

## 1. Environment Setup (Distrobox)

This project is designed to be built inside a clean, isolated **Distrobox** container running Ubuntu 24.04 and ROS 2 Jazzy, with native NVIDIA GPU pass-through for accelerated deep learning (ONNX Runtime / TensorRT).

To recreate the exact development container from scratch on any host machine, run:

```bash
distrobox create --name agv_jazzy --image docker.io/osrf/ros:jazzy-desktop --nvidia
distrobox enter agv_jazzy
```

## 2. Dependency Initialization

Once inside the container, you must install the core system dependencies (like Ninja, Python Virtual Environments, OpenCV data, and CUDA libraries).

Run the automated setup script from the root of the workspace:

```bash
bash setup_distrobox.sh
```

This will automatically configure system dependencies, fetch the NVIDIA Container Toolkit GPG key, and prevent `apt` signature errors.

## 3. Downloading AI Models & Building the Workspace

This workspace utilizes the incredibly fast **OpenCV YuNet** for sub-pixel facial landmark extraction, and HuggingFace's **Depth Anything V2** (VITS) for absolute metric depth scaling.

The repository is equipped with a highly optimized wrapper `Makefile` that automatically triggers `colcon build` using the lightning-fast Ninja generator. 

As a bonus, the perception module will automatically fetch its own AI weights from HuggingFace and GitHub seamlessly during the build process!

Simply run:

```bash
make
```

To clean the build artifacts, run:
```bash
make clean
```

## 5. Executing the Perception Stack

The project includes a robust `tmuxp` configuration that will automatically manage the ROS 2 environment, spawn the camera publisher, boot the AI perception node, and launch the `rqt_image_view` GUI in a 3-pane split screen.

Launch the full stack with:

```bash
tmuxp load config/presence_perception.yaml
```

## 6. Pro-Tip: Automated Terminal Sourcing

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
