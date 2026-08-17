BUILD_TYPE ?= Debug
CONAN_STAMP := build/$(BUILD_TYPE)/generators/.conan_deps.stamp

.PHONY: all clean test init conan_deps

# Default target runs colcon build with Ninja generator for maximum speed
all: $(CONAN_STAMP)
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon build --event-handlers console_direct+ --cmake-args -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/build/$(BUILD_TYPE)/generators/conan_toolchain.cmake

$(CONAN_STAMP): conanfile.txt
	$(MAKE) conan_deps BUILD_TYPE=$(BUILD_TYPE)

# One-time workspace setup (creates venv and installs Python packages, then runs conan_deps)
init:
	python3 -m venv --system-site-packages .agv_venv
	./.agv_venv/bin/pip install --upgrade pip
	./.agv_venv/bin/pip install -r requirements.txt
	$(MAKE) conan_deps
	@echo "=========================================================="
	@echo "Workspace initialized!"
	@echo "=========================================================="

conan_deps:
	./.agv_venv/bin/conan profile detect --force
	./.agv_venv/bin/conan install . --build=missing -s build_type=$(BUILD_TYPE) -u
	@touch $(CONAN_STAMP)

clean:
	rm -rf .colcon_cache build install log

test:
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon test --event-handlers console_direct+
	colcon test-result --all --test-result-base .colcon_cache/test_results
