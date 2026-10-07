BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: all configure build test clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DDJI_NEO_BUILD_TESTS=ON

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
