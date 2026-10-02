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
    unzip \
    opencv-data

echo "Installing ROS ${ROS_DISTRO} dependencies..."
sudo apt install -y \
    "ros-${ROS_DISTRO}-ament-cmake-clang-tidy"

echo "Installing NVIDIA cuDNN 9 (APT Method)..."
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb -O /tmp/cuda-keyring.deb
sudo dpkg -i /tmp/cuda-keyring.deb
sudo apt update
sudo apt install -y \
    cuda-compiler-12-8 \
    cuda-libraries-12-8 \
    cuda-nvrtc-dev-12-8 \
    cuda-nvtx-12-8 \
    libcudnn9-dev-cuda-12
rm /tmp/cuda-keyring.deb

LIBTORCH_VERSION="2.7.1"
LIBTORCH_CUDA="cu128"
LIBTORCH_INSTALL_DIR="/opt/libtorch"
LIBTORCH_ARCHIVE="/tmp/libtorch-${LIBTORCH_CUDA}.zip"
LIBTORCH_URL="https://download.pytorch.org/libtorch/${LIBTORCH_CUDA}/libtorch-cxx11-abi-shared-with-deps-${LIBTORCH_VERSION}%2B${LIBTORCH_CUDA}.zip"

echo "Installing CUDA-enabled LibTorch ${LIBTORCH_VERSION}..."
if [[ -f "${LIBTORCH_INSTALL_DIR}/share/cmake/Torch/TorchConfig.cmake" &&
      -f "${LIBTORCH_INSTALL_DIR}/lib/libtorch_cuda.so" ]]; then
    echo "LibTorch is already installed at ${LIBTORCH_INSTALL_DIR}; skipping."
elif [[ -e "${LIBTORCH_INSTALL_DIR}" ]]; then
    echo "Error: ${LIBTORCH_INSTALL_DIR} exists but is not a valid CUDA LibTorch installation." >&2
    echo "Move or remove it, then run this script again." >&2
    exit 1
else
    wget "${LIBTORCH_URL}" -O "${LIBTORCH_ARCHIVE}"
    sudo unzip -q "${LIBTORCH_ARCHIVE}" -d /opt
    rm "${LIBTORCH_ARCHIVE}"
fi

echo "${LIBTORCH_INSTALL_DIR}/lib" | \
    sudo tee /etc/ld.so.conf.d/libtorch.conf >/dev/null
sudo ldconfig

echo "=========================================================="
echo "Distrobox Setup Complete!"
echo "=========================================================="
