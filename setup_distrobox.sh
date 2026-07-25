#!/bin/bash
set -e

echo "=========================================================="
echo "Initializing AGV Distrobox Environment"
echo "=========================================================="

echo "Fixing NVIDIA Container Toolkit GPG Key..."
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor --yes -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

echo "Installing base system dependencies (CUDA, Python, Ninja)..."
sudo apt update
sudo apt install -y \
    build-essential \
    ninja-build \
    python3-pip \
    python3-venv \
    wget \
    tar \
    cmake \
    tmux \
    tmuxp \
    opencv-data

echo "Installing NVIDIA cuDNN 9 (APT Method)..."
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb -O /tmp/cuda-keyring.deb
sudo dpkg -i /tmp/cuda-keyring.deb
sudo apt update
sudo apt install -y cuda-libraries-12-8 libcudnn9-dev-cuda-12
rm /tmp/cuda-keyring.deb

echo "=========================================================="
echo "Distrobox Setup Complete!"
echo "=========================================================="
