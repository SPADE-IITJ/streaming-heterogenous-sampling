#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXEC="${PROJECT_DIR}/exec/res_cpu"
OUTPUT_DIR="${PROJECT_DIR}/output"
QUERY_DIR="${PROJECT_DIR}/queries"
RESULTS_FILE="${OUTPUT_DIR}/res_results.txt"

GRAPH_SIZES=(100 500 1000 5000)

BATCH_SIZES=(50 100 200)
TIME_THRESHOLDS=(25 50 100)

SEED=42

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -s, --sizes SIZE1,SIZE2,... Graph sizes to test (default: 100,500,1000,5000)"
    echo "  -b, --batch BATCH1,BATCH2,... Batch sizes to test (default: 50,100,200)"
    echo "  -t, --time TIME1,TIME2,... Time thresholds in ms (default: 25,50,100)"
    echo "  --seed SEED                   Random seed (default: 42)"
    echo "  -h, --help                    Show help message"
}

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[✓]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_title() { echo ""; echo -e "${YELLOW}=== $1 ===${NC}"; echo ""; }

prepare_environment() {
    log_info "PREPARING ENV..."
    if [ ! -f "$EXEC" ]; then
        log_error "[x] EXE NOT FOUND: $EXEC"
        log_info "RUN 'make res_cpu' TO BUILD"
        exit 1
    fi
    mkdir -p "$OUTPUT_DIR"/{timing,graphs,logs}
    log_success "[.] ENV READY"
}

build_if_needed() {
    log_info "CHECKING IF BUILD IS NEEDED..."
    if [ ! -f "$EXEC" ]; then
        log_info "BUILDING RES EXE..."
        cd "$PROJECT_DIR"
        make res_cpu
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
        log_success "[.] GRAPH ALREADY EXISTS: $graph_file"
    fi
    
    echo "$graph_file"
}

generate_update_stream() {
    local graph_size=$1
    local n_updates=$((graph_size * 5))
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
        log_success "[.] UPDATES ALREADY EXIST: $updates_file"
    fi
    
    echo "$updates_file"
}

run_benchmark() {
    local graph_size=$1
    local batch_size=$2
    local time_threshold=$3
    
    log_title "RES: Size=$graph_size, B_max=$batch_size, τ=${time_threshold}ms"
    
    local graph_file=$(generate_test_graph "$graph_size")
    local updates_file=$(generate_update_stream "$graph_size")
    
    cp "$graph_file" "$PROJECT_DIR/graph.txt"
    cp "$updates_file" "$PROJECT_DIR/updates.txt"
    
    log_info "RUNNING BENCHMARK..."
    
    local start_time=$(date +%s%N)
    
    cd "$PROJECT_DIR"
    if "$EXEC" > "${OUTPUT_DIR}/logs/res_${graph_size}_${batch_size}_${time_threshold}.log" 2>&1; then
        local end_time=$(date +%s%N)
        local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
        
        log_success "[.] COMPLETED IN ${elapsed_ms}MS"
        
        echo "Graph: $graph_size | B_max: $batch_size | τ: ${time_threshold}ms | Time: ${elapsed_ms}ms" >> "$RESULTS_FILE"
        
    else
        log_error "[x] BENCHMARK FAILED. CHECK LOG: ${OUTPUT_DIR}/logs/res_${graph_size}_${batch_size}_${time_threshold}.log"
        return 1
    fi
    cd - > /dev/null
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--sizes)
            IFS=',' read -ra GRAPH_SIZES <<< "$2"
            shift 2
            ;;
        -b|--batch)
            IFS=',' read -ra BATCH_SIZES <<< "$2"
            shift 2
            ;;
        -t|--time)
            IFS=',' read -ra TIME_THRESHOLDS <<< "$2"
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

log_title "SMASH-RES"

prepare_environment
build_if_needed

> "$RESULTS_FILE"

log_info "Graph sizes: ${GRAPH_SIZES[@]}"
log_info "Batch sizes: ${BATCH_SIZES[@]}"
log_info "Time thresholds: ${TIME_THRESHOLDS[@]}"

for size in "${GRAPH_SIZES[@]}"; do
    for batch in "${BATCH_SIZES[@]}"; do
        for time_t in "${TIME_THRESHOLDS[@]}"; do
            run_benchmark "$size" "$batch" "$time_t"
        done
    done
done

log_title "DONE."
log_success "RESULTS SAVED TO: $RESULTS_FILE"

echo ""
cat "$RESULTS_FILE"
echo ""
