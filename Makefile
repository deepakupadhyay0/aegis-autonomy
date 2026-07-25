.PHONY: all clean test init

# Default target runs colcon build with Ninja generator for maximum speed
all:
	COLCON_DEFAULTS_FILE=colcon_defaults.yaml colcon build --event-handlers console_direct+ --cmake-args -G Ninja

# Initialize the workspace (create venv, install Python dependencies)
# NOTE: Not used currently as Conan is disabled.
init:
#	python3 -m venv --system-site-packages .agv_venv
#	./.agv_venv/bin/pip install -r requirements.txt
#	@echo "=========================================================="
#	@echo "Workspace initialized! Now run: source .agv_venv/bin/activate"
#	@echo "=========================================================="

# Clean up colcon artifacts
clean:
	rm -rf .colcon_cache build install log

# Run colcon tests
test:
	colcon test
