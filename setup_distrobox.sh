#!/bin/bash
set -e

echo "=========================================================="
echo "Initializing AGV Distrobox Environment"
echo "=========================================================="

if [[ -z "${ROS_DISTRO:-}" ]]; then
    echo "Error: ROS_DISTRO is unset. Source /opt/ros/<distro>/setup.bash first." >&2
    exit 1
fi

echo "Fixing NVIDIA Container Toolkit GPG Key..."
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor --yes -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

echo "Installing base system dependencies..."
sudo apt update
sudo apt install -y \
    build-essential \
    libopencv-dev \
    libspdlog-dev \
    libsqlite3-dev \
    ninja-build \
    python3-pip \
    python3-venv \
    wget \
    tar \
    cmake \
    tmux \
    tmuxp \
    opencv-data

echo "Installing ROS ${ROS_DISTRO} dependencies..."
sudo apt install -y \
    "ros-${ROS_DISTRO}-ament-cmake-clang-tidy"

echo "Installing NVIDIA cuDNN 9 (APT Method)..."
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb -O /tmp/cuda-keyring.deb
sudo dpkg -i /tmp/cuda-keyring.deb
sudo apt update
sudo apt install -y cuda-libraries-12-8 libcudnn9-dev-cuda-12
rm /tmp/cuda-keyring.deb

echo "=========================================================="
echo "Distrobox Setup Complete!"
echo "=========================================================="
