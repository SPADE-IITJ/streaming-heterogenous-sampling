CXX := g++
NVCC := nvcc
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -I./include
NVCCFLAGS := -std=c++17 -O3 -Xcompiler "-fPIC" -I./include
CUDA_LIBS := -lcuda -lcudart
PTHREAD_LIB := -lpthread

SRC_DIR := src
GPU_DIR := $(SRC_DIR)/GPU
CPU_DIR := $(SRC_DIR)/CPU
INCLUDE_DIR := include
BUILD_DIR := build
EXEC_DIR := exec
OUTPUT_DIR := output

$(shell mkdir -p $(BUILD_DIR) $(EXEC_DIR) $(OUTPUT_DIR))

UTILS_SRCS := $(INCLUDE_DIR)/utils.hpp $(INCLUDE_DIR)/lct.hpp $(INCLUDE_DIR)/filters.hpp $(INCLUDE_DIR)/reservoir.hpp
CPU_KERNELS := $(CPU_DIR)/kernels_cpu.cpp

TARGETS := pds_cpu res_cpu fresh_cpu fresh_gpu

all: $(TARGETS)

$(EXEC_DIR)/pds_cpu: $(CPU_DIR)/pds_main.cpp $(CPU_KERNELS) $(UTILS_SRCS)
	@echo "Building PDS (CPU-only)..."
	$(CXX) $(CXXFLAGS) -DPDS_MAIN -o $@ $(CPU_DIR)/pds_main.cpp $(CPU_KERNELS) -lm $(PTHREAD_LIB)
	@echo "✓ PDS executable created: $@"

$(EXEC_DIR)/res_cpu: $(CPU_DIR)/res_main.cpp $(CPU_KERNELS) $(UTILS_SRCS)
	@echo "Building RES (CPU-only)..."
	$(CXX) $(CXXFLAGS) -DRES_MAIN -o $@ $(CPU_DIR)/res_main.cpp $(CPU_KERNELS) -lm $(PTHREAD_LIB)
	@echo "✓ RES executable created: $@"

$(EXEC_DIR)/fresh_cpu: $(CPU_DIR)/fresh_main.cpp $(CPU_KERNELS) $(UTILS_SRCS)
	@echo "Building FRESH (CPU-only)..."
	$(CXX) $(CXXFLAGS) -DFRESH_MAIN -o $@ $(CPU_DIR)/fresh_main.cpp $(CPU_KERNELS) -lm $(PTHREAD_LIB)
	@echo "✓ FRESH (CPU) executable created: $@"

$(BUILD_DIR)/fresh_main.o: $(CPU_DIR)/fresh_main.cpp $(INCLUDE_DEPS)
	@echo "Compiling FRESH (CPU side)..."
	$(CXX) $(CXXFLAGS) -I./include -DFRESH_MAIN -c -o $@ $<

$(BUILD_DIR)/kernels.o: $(GPU_DIR)/kernels.cu $(INCLUDE_DEPS)
	@echo "Compiling GPU kernels..."
	$(NVCC) $(NVCCFLAGS) -I./include -c -o $@ $<

$(EXEC_DIR)/fresh_gpu: $(BUILD_DIR)/fresh_main.o $(BUILD_DIR)/kernels.o
	@echo "Linking FRESH (GPU-enabled)..."
	$(NVCC) $(NVCCFLAGS) -o $@ $^ $(CUDA_LIBS) $(PTHREAD_LIB)
	@echo "✓ FRESH (GPU) executable created: $@"

pds_cpu: $(EXEC_DIR)/pds_cpu
res_cpu: $(EXEC_DIR)/res_cpu
fresh_cpu: $(EXEC_DIR)/fresh_cpu
fresh_gpu: $(EXEC_DIR)/fresh_gpu

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/* $(EXEC_DIR)/* $(OUTPUT_DIR)/*
	@echo "[.] Cleanup complete"

distclean: clean
	@echo "Deep cleaning..."
	rm -rf $(BUILD_DIR) $(EXEC_DIR)
	@echo "[.] Distribution clean complete"

test: all
	@echo "Running algorithm tests..."
	@if [ -f "queries/test_graph.txt" ]; then \
		$(EXEC_DIR)/pds_cpu; \
		$(EXEC_DIR)/res_cpu; \
		$(EXEC_DIR)/fresh_cpu; \
	else \
		echo "[x] Test graph not found. Run 'make setup-test' first."; \
	fi

setup-dirs:
	@echo "Setting up directory structure..."
	mkdir -p $(OUTPUT_DIR)/timing $(OUTPUT_DIR)/graphs $(OUTPUT_DIR)/logs
	@echo "✓ Directories created"

info:
	@echo "=== Smash ==="
	@echo "CXX: $(CXX)"
	@echo "NVCC: $(NVCC)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "NVCCFLAGS: $(NVCCFLAGS)"
	@echo ""
	@echo "Targets:"
	@echo "  pds_cpu       - CPU-only PDS variant"
	@echo "  res_cpu       - CPU-only RES variant"
	@echo "  fresh_cpu     - CPU-only FRESH variant"
	@echo "  fresh_gpu     - GPU-accelerated FRESH variant"
	@echo ""
	@echo "Utilities:"
	@echo "  clean         - Remove build artifacts"
	@echo "  distclean     - Remove all generated files"
	@echo "  test          - Run basic tests"
	@echo "  info          - Display this information"

help: info

.PHONY: all pds_cpu res_cpu fresh_cpu fresh_gpu clean distclean test setup-dirs info help
