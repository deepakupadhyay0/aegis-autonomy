BUILD_TYPE ?= Debug
CUDA_ARCHITECTURES ?= native
CUDA_COMPILER ?= /usr/local/cuda/bin/nvcc
TORCH_CUDA_ARCH_LIST ?= 12.0
TORCH_CMAKE_PREFIX ?= /opt/libtorch/share/cmake
VENV_DIR := $(CURDIR)/.agv_venv
VENV_PIP := $(VENV_DIR)/bin/pip
VENV_CONAN := $(VENV_DIR)/bin/conan
CONAN_STAMP := build/$(BUILD_TYPE)/generators/.conan_deps.stamp
PLACE_MODEL_BUILD_DIR := build/$(BUILD_TYPE)/place_recognition

.PHONY: all check_cuda check_torch clean test init conan_deps place_model place_model_configure place_model_test

# Default target runs colcon build with Ninja generator for maximum speed
all: check_cuda check_torch $(CONAN_STAMP)
	CUDACXX="$(CUDA_COMPILER)" \
	ROBOT_AUTONOMY_CUDA_ARCHITECTURES="$(CUDA_ARCHITECTURES)" \
	TORCH_CUDA_ARCH_LIST="$(TORCH_CUDA_ARCH_LIST)" \
	TORCH_CMAKE_PREFIX="$(TORCH_CMAKE_PREFIX)" \
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon build --event-handlers console_direct+ --cmake-args \
		-G Ninja \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_PREFIX_PATH="$(TORCH_CMAKE_PREFIX)" \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/build/$(BUILD_TYPE)/generators/conan_toolchain.cmake

check_cuda:
	@test -x "$(CUDA_COMPILER)" || \
		(echo "CUDA compiler is not executable: $(CUDA_COMPILER)" && false)

check_torch:
	@test -n "$(TORCH_CMAKE_PREFIX)" || \
		(echo "Set TORCH_CMAKE_PREFIX to the standalone LibTorch share/cmake directory" && false)
	@test -f "$(TORCH_CMAKE_PREFIX)/Torch/TorchConfig.cmake" || \
		(echo "TORCH_CMAKE_PREFIX does not contain Torch/TorchConfig.cmake" && false)

$(CONAN_STAMP): conanfile.txt
	$(MAKE) conan_deps BUILD_TYPE=$(BUILD_TYPE)

# One-time workspace setup (creates venv and installs Python packages, then runs conan_deps)
init:
	python3 -m venv --system-site-packages $(VENV_DIR)
	$(VENV_PIP) install --upgrade pip
	$(VENV_PIP) install -r requirements.txt
	$(MAKE) conan_deps
	@echo "=========================================================="
	@echo "Workspace initialized!"
	@echo "=========================================================="

conan_deps:
	$(VENV_CONAN) profile detect --force
	$(VENV_CONAN) install . --build=missing -s build_type=$(BUILD_TYPE) -u
	@touch $(CONAN_STAMP)

clean:
	rm -rf .colcon_cache build install log

test:
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon test --event-handlers console_direct+
	colcon test-result --all --test-result-base .colcon_cache/test_results


place_model_configure: check_cuda check_torch
	CUDACXX="$(CUDA_COMPILER)" \
	TORCH_CUDA_ARCH_LIST="$(TORCH_CUDA_ARCH_LIST)" \
	TORCH_CMAKE_PREFIX="$(TORCH_CMAKE_PREFIX)" \
	cmake -S ml/place_recognition -B $(PLACE_MODEL_BUILD_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_PREFIX_PATH="$(TORCH_CMAKE_PREFIX)"

place_model: place_model_configure
	cmake --build $(PLACE_MODEL_BUILD_DIR)

place_model_test: place_model
	PLACE_RECOGNITION_REQUIRE_CUDA=1 \
	ctest --test-dir $(PLACE_MODEL_BUILD_DIR) --output-on-failure
