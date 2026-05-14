#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXEC="${PROJECT_DIR}/exec/pds_cpu"
OUTPUT_DIR="${PROJECT_DIR}/output"
QUERY_DIR="${PROJECT_DIR}/queries"
RESULTS_FILE="${OUTPUT_DIR}/pds_results.txt"

GRAPH_SIZES=(100 500 1000 5000)
SEED=42

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -s, --sizes SIZE1,SIZE2,... Graph sizes to benchmark (default: 100,500,1000,5000)"
    echo "  --seed SEED                 Random seed (default: 42)"
    echo "  -h, --help                  Show this help message"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_title() {
    echo ""
    echo -e "${YELLOW}=== $1 ===${NC}"
    echo ""
}

## main benchmarking

prepare_environment() {
    log_info "PREPARING ENV..."
    
    if [ ! -f "$EXEC" ]; then
        log_error "[x] EXE NOT FOUND: $EXEC"
        log_info "RUN 'make pds_cpu' TO BUILD EXECUTABLES"
        exit 1
    fi
    
    mkdir -p "$OUTPUT_DIR"/{timing,graphs,logs}
    
    log_success "[.] ENV READY"
}

build_if_needed() {
    log_info "CHECKING IF BUILD IS NEEDED..."
    
    if [ ! -f "$EXEC" ]; then
        log_info "BUILDING PDS EXE..."
        cd "$PROJECT_DIR"
        make pds_cpu
        cd - > /dev/null
        log_success "[.] BUILD COMPLETE"
    else
        log_success "[.] EXE ALREADY BUILT"
    fi
}

generate_test_graph() {
    local graph_size=$1
    local graph_file="${QUERY_DIR}/synthetic/pl_graph_${graph_size}.txt"
    
    log_info "GENERATING POWER-LAW GRAPH WITH $graph_size VERTICES..."
    
    if [ ! -f "$graph_file" ]; then
        cd "$QUERY_DIR"
        python3 gen_pl_graph.py \
            --vertices "$graph_size" \
            --edges-per-vertex 5 \
            --output "synthetic/pl_graph_${graph_size}.txt" \
            --seed "$SEED" \
            --stats
        cd - > /dev/null
    else
        log_success "GRAPH ALREADY EXISTS: $graph_file"
    fi
    
    echo "$graph_file"
}

generate_update_stream() {
    local graph_size=$1
    local n_updates=$((graph_size * 5))  # 5x the graph size
    local updates_file="${QUERY_DIR}/updates_${graph_size}.txt"
    
    log_info "GENERATING $n_updates UPDATE OPERATIONS..."
    
    if [ ! -f "$updates_file" ]; then
        cd "$QUERY_DIR"
        python3 gen_updates.py \
            --vertices "$graph_size" \
            --updates "$n_updates" \
            --output "updates_${graph_size}.txt" \
            --seed "$SEED"
        cd - > /dev/null
    else
        log_success "UPDATES ALREADY EXIST: $updates_file"
    fi
    
    echo "$updates_file"
}

run_benchmark() {
    local graph_size=$1
    
    log_title "BENCHMARKING PDS W/ $graph_size VERTICES"
    
    local graph_file=$(generate_test_graph "$graph_size")
    local updates_file=$(generate_update_stream "$graph_size")
    
    log_info "SETTING UP INPUT FILES..."
    cp "$graph_file" "$PROJECT_DIR/graph.txt"
    cp "$updates_file" "$PROJECT_DIR/updates.txt"
    
    log_info "RUNNING PDS BENCHMARK..."
    
    local start_time=$(date +%s%N)
    
    cd "$PROJECT_DIR"
    if "$EXEC" > "${OUTPUT_DIR}/logs/pds_${graph_size}.log" 2>&1; then
        local end_time=$(date +%s%N)
        local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
        
        log_success "[.] BENCHMARK COMPLETED IN ${elapsed_ms}MS"
        
        echo "Size: $graph_size | Time: ${elapsed_ms}ms" >> "$RESULTS_FILE"
        
    else
        log_error "[x] BENCHMARK FAILED. CHECK LOG: ${OUTPUT_DIR}/logs/pds_${graph_size}.log"
        return 1
    fi
    cd - > /dev/null
}

## cli arguments 

while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--sizes)
            IFS=',' read -ra GRAPH_SIZES <<< "$2"
            shift 2
            ;;
        --seed)
            SEED="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done


log_title "SMASH-PDS"

prepare_environment
build_if_needed

> "$RESULTS_FILE"

log_info "TESTING WITH GRAPH SIZES: ${GRAPH_SIZES[@]}"

for size in "${GRAPH_SIZES[@]}"; do
    if run_benchmark "$size"; then
        log_success "[.] COMPLETED $size VERTEX BENCHMARK"
    else
        log_error "[x] FAILED TO COMPLETE $size VERTEX BENCHMARK"
    fi
done

log_title "DONE."
log_success "RESULTS SAVED TO: $RESULTS_FILE"

echo ""
cat "$RESULTS_FILE"
echo ""
