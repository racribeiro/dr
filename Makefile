BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python3
JAVA_BUILD_DIR ?= $(BUILD_DIR)/java
CMAKE_FLAGS ?=

.PHONY: all configure build test clean activation-check java java-test capture-review

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DDJI_NEO_BUILD_TESTS=ON $(CMAKE_FLAGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)

activation-check:
	$(PYTHON) reference/tools/generate_activation.py --check src/activation_frames.inc

java:
	$(CMAKE) -S . -B $(JAVA_BUILD_DIR) -DDJI_NEO_BUILD_JAVA=ON -DDJI_NEO_BUILD_TESTS=ON $(CMAKE_FLAGS)
	$(CMAKE) --build $(JAVA_BUILD_DIR) --parallel

java-test: java
	ctest --test-dir $(JAVA_BUILD_DIR) --output-on-failure

# Offline only: no socket creation, credentials/GPS printed or drone traffic.
capture-review: build
	@test -n "$(CAPTURE)" || { echo 'Usage: make capture-review CAPTURE=/path/to/file.pcap'; exit 2; }
	"$(BUILD_DIR)/review_capture" "$(CAPTURE)"
