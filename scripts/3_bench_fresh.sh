#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXEC="${PROJECT_DIR}/exec/fresh_cpu"
OUTPUT_DIR="${PROJECT_DIR}/output"
QUERY_DIR="${PROJECT_DIR}/queries"
RESULTS_FILE="${OUTPUT_DIR}/fresh_results.txt"

GRAPH_TYPES=("pl" "ws") 
GRAPH_SIZES=(100 500 1000 5000)

SEED=42

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --types TYPE1,TYPE2,...  Graph types: pl,ws (default: pl,ws)"
    echo "  -s, --sizes SIZE1,SIZE2,...   Graph sizes (default: 100,500,1000,5000)"
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
        log_info "RUN 'make fresh_cpu' TO BUILD"
        exit 1
    fi
    mkdir -p "$OUTPUT_DIR"/{timing,graphs,logs}
    log_success "[.] ENV READY"
}

build_if_needed() {
    log_info "CHECKING IF BUILD IS NEEDED..."
    if [ ! -f "$EXEC" ]; then
        log_info "BUILDING FRESH EXE..."
        cd "$PROJECT_DIR"
        make fresh_cpu
        cd - > /dev/null
        log_success "[.] BUILD COMPLETE"
    else
        log_success "[.] EXE ALREADY BUILT"
    fi
}

generate_graph() {
    local graph_type=$1
    local graph_size=$2
    
    case $graph_type in
        pl)
            local graph_file="${QUERY_DIR}/synthetic/pl_graph_${graph_size}.txt"
            log_info "GENERATING POWER-LAW GRAPH ($graph_size VERTICES)..."
            
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
            ;;
            
        ws)
            local graph_file="${QUERY_DIR}/synthetic/ws_graph_${graph_size}.txt"
            log_info "GENERATING WATTS-STROGATZ GRAPH ($graph_size VERTICES)..."
            
            if [ ! -f "$graph_file" ]; then
                cd "$QUERY_DIR"
                python3 gen_ws_graph.py \
                    --vertices "$graph_size" \
                    --neighbors 6 \
                    --rewire-prob 0.3 \
                    --output "synthetic/ws_graph_${graph_size}.txt" \
                    --seed "$SEED" \
                    --stats
                cd - > /dev/null
            else
                log_success "[.] GRAPH ALREADY EXISTS: $graph_file"
            fi
            ;;
    esac
    
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
    local graph_type=$1
    local graph_size=$2
    
    log_title "FRESH: Type=$graph_type, Size=$graph_size"
    
    local graph_file=$(generate_graph "$graph_type" "$graph_size")
    local updates_file=$(generate_update_stream "$graph_size")
    
    cp "$graph_file" "$PROJECT_DIR/graph.txt"
    cp "$updates_file" "$PROJECT_DIR/updates.txt"
    
    log_info "RUNNING BENCHMARK..."
    
    local start_time=$(date +%s%N)
    
    cd "$PROJECT_DIR"
    if "$EXEC" > "${OUTPUT_DIR}/logs/fresh_${graph_type}_${graph_size}.log" 2>&1; then
        local end_time=$(date +%s%N)
        local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
        
        log_success "[.] COMPLETED IN ${elapsed_ms}ms"
        
        local graph_name=$(echo "$graph_type" | tr '[:lower:]' '[:upper:]')
        echo "Graph: $graph_name ($graph_size) | Time: ${elapsed_ms}ms" >> "$RESULTS_FILE"
        
    else
        log_error "[x] BENCHMARK FAILED."
        return 1
    fi
    cd - > /dev/null
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--types)
            IFS=',' read -ra GRAPH_TYPES <<< "$2"
            shift 2
            ;;
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

log_title "SMASH-FRESH"

prepare_environment
build_if_needed

> "$RESULTS_FILE"

log_info "Graph types: ${GRAPH_TYPES[@]}"
log_info "Graph sizes: ${GRAPH_SIZES[@]}"

for type in "${GRAPH_TYPES[@]}"; do
    for size in "${GRAPH_SIZES[@]}"; do
        run_benchmark "$type" "$size"
    done
done

log_title "DONE."
log_success "RESULTS SAVED TO: $RESULTS_FILE"

echo ""
cat "$RESULTS_FILE"
echo ""
