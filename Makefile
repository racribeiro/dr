BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python3

.PHONY: all configure build test clean activation-check

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DDJI_NEO_BUILD_TESTS=ON

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)

activation-check:
	$(PYTHON) reference/tools/generate_activation.py --check src/activation_frames.inc
