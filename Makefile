BUILD_TYPE ?= Debug
VENV_DIR := $(CURDIR)/.agv_venv
VENV_PIP := $(VENV_DIR)/bin/pip
VENV_CONAN := $(VENV_DIR)/bin/conan
CONAN_STAMP := build/$(BUILD_TYPE)/generators/.conan_deps.stamp

.PHONY: all clean test init conan_deps

# Default target runs colcon build with Ninja generator for maximum speed
all: $(CONAN_STAMP)
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon build --event-handlers console_direct+ --cmake-args -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/build/$(BUILD_TYPE)/generators/conan_toolchain.cmake

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
