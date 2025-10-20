#!/bin/bash
# Benchmark script to compare AddressBitset overhead
# Compares local build (new sharded implementation) vs global ddprof

set -e

# Configuration - Use RELEASE builds
BUILD_DIR="build_gcc_unknown-linux-2.35_Rel"
SIMPLE_MALLOC="./${BUILD_DIR}/test/simple_malloc"
DDPROF_LOCAL="./${BUILD_DIR}/ddprof"
DDPROF_ABSL="../ddprof_2/${BUILD_DIR}/ddprof"  # Absl hash map version
DDPROF_GLOBAL="ddprof"  # Assumes global install

# Test parameters
MALLOC_SIZE=1000
LOOP_COUNT=10000000
THREADS=8
TIMEOUT_MS=30000

# Profiling parameters
# p=1048576 means sample every 1MB allocated (lower = more samples, higher overhead)
ALLOC_PERIOD=1048576  # 1MB
PROFILING_ARGS="-e sALLOC,mode=l,p=${ALLOC_PERIOD}"

# Output directory
RESULTS_DIR="benchmark_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

echo "=========================================="
echo "AddressBitset Overhead Benchmark"
echo "=========================================="
echo "Malloc size: $MALLOC_SIZE bytes"
echo "Loop count: $LOOP_COUNT"
echo "Threads: $THREADS"
echo "Timeout: ${TIMEOUT_MS}ms"
echo "Sampling: Every ${ALLOC_PERIOD} bytes (sALLOC,mode=l,p=${ALLOC_PERIOD})"
echo "Build type: RELEASE"
echo "Results: $RESULTS_DIR"
echo ""


# Verify release build exists
if [ ! -f "$SIMPLE_MALLOC" ]; then
    echo "ERROR: simple_malloc not found at $SIMPLE_MALLOC"
    echo "Please build with: cmake --build $BUILD_DIR --target simple_malloc -j\$(nproc)"
    exit 1
fi

if [ ! -f "$DDPROF_LOCAL" ]; then
    echo "ERROR: ddprof not found at $DDPROF_LOCAL"
    echo "Please build with: cmake --build $BUILD_DIR --target ddprof -j\$(nproc)"
    exit 1
fi

echo "Using release builds from: $BUILD_DIR"
echo "  ddprof:       $DDPROF_LOCAL"
echo "  simple_malloc: $SIMPLE_MALLOC"
echo ""

# Function to run test
run_test() {
    local test_name=$1
    local ddprof_cmd=$2
    local output_file="$RESULTS_DIR/${test_name}.txt"
    
    echo "----------------------------------------"
    echo "Running: $test_name"
    echo "----------------------------------------"
    
    # Run with perf stat (--inherit tracks child processes)
    # but in this case we mostly care about overhead of parent
    perf stat  \
        -e cycles,instructions,cache-misses,context-switches,cpu-clock \
        -o "$RESULTS_DIR/${test_name}_perf.txt" \
        $ddprof_cmd \
        -l informational \
        $PROFILING_ARGS \
        --debug_pprof_prefix "$RESULTS_DIR/${test_name}_profile.pprof" \
        $SIMPLE_MALLOC \
            --malloc $MALLOC_SIZE \
            --loop $LOOP_COUNT \
            --threads $THREADS \
            --timeout $TIMEOUT_MS \
            --spin 10 \
        2>&1 | tee "$output_file"
    # We add a spin to avoid overloading number of events
    # To reproduce:
    # ./ddprof \
    #   -l informational \
    #   -e sALLOC,mode=l,p=1048576 -e sCPU \
    #   ./test/simple_malloc \
    #     --malloc 1000 \
    #     --loop 10000000 \
    #     --threads 8 --spin 10 \
    #     --timeout 30000
    echo "✓ Completed: $test_name"
    echo ""
}

# Test 1: Baseline (no profiling)
echo "=========================================="
echo "Test 1: BASELINE (no profiling)"
echo "=========================================="
perf stat --inherit \
    -e cycles,instructions,cache-misses,context-switches,cpu-clock \
    -o "$RESULTS_DIR/baseline_perf.txt" \
        $SIMPLE_MALLOC \
            --malloc $MALLOC_SIZE \
            --loop $LOOP_COUNT \
            --threads $THREADS \
            --timeout $TIMEOUT_MS \
            --spin 10 \
    2>&1 | tee "$RESULTS_DIR/baseline.txt"

echo "✓ Baseline completed"
echo ""

# Test 2: Local ddprof (new sharded implementation)
echo "=========================================="
echo "Test 2: LOCAL DDPROF (new sharded)"
echo "=========================================="
run_test "local_sharded" "$DDPROF_LOCAL"

# Test 3: Absl hash map implementation
if [ -f "$DDPROF_ABSL" ]; then
    echo "=========================================="
    echo "Test 3: ABSL HASH MAP (fixed size, not thread-safe)"
    echo "=========================================="
    run_test "absl_hashmap" "$DDPROF_ABSL"
else
    echo "⚠ Absl ddprof not found at $DDPROF_ABSL, skipping"
fi

# Test 4: Global ddprof (original implementation)
if command -v $DDPROF_GLOBAL &> /dev/null; then
    echo "=========================================="
    echo "Test 4: GLOBAL DDPROF (original)"
    echo "=========================================="
    run_test "global_original" "$DDPROF_GLOBAL"
else
    echo "⚠ Global ddprof not found, skipping comparison"
fi

# Parse and summarize results
echo "=========================================="
echo "SUMMARY"
echo "=========================================="

parse_stats() {
    local name=$1
    local stats_file="$RESULTS_DIR/${name}.txt"
    local perf_file="$RESULTS_DIR/${name}_perf.txt"
    
    if [ -f "$stats_file" ]; then
        echo "--- $name ---"
        
        # Extract allocation stats from simple_malloc output
        grep "TestStats" "$stats_file" | head -1 | awk -F',' '{
            printf "  Allocations: %s\n", $3
            printf "  Bytes:       %s\n", $4
            printf "  Wall time:   %.2f s\n", $5 / 1000000000
            printf "  CPU time:    %.2f s\n", $6 / 1000000000
            printf "  Max RSS:     %s KB\n", $7
        }'
        
        # Extract perf stats (from child processes)
        if [ -f "$perf_file" ]; then
            echo "  Perf stats:"
            grep -E "(cycles|instructions|cache-misses|context-switches|cpu-clock|seconds time elapsed)" "$perf_file" | \
                sed 's/^/    /'
        fi
        
        echo ""
    fi
}

parse_stats "baseline"
parse_stats "local_sharded"
parse_stats "absl_hashmap"
parse_stats "global_original"

# Calculate overhead
calculate_overhead() {
    local baseline_time=$(grep "TestStats" "$RESULTS_DIR/baseline.txt" | head -1 | awk -F',' '{print $5}')
    local profiled_time=$(grep "TestStats" "$RESULTS_DIR/$1.txt" 2>/dev/null | head -1 | awk -F',' '{print $5}')
    
    if [ -n "$baseline_time" ] && [ -n "$profiled_time" ]; then
        local overhead=$(awk "BEGIN {printf \"%.2f\", (($profiled_time - $baseline_time) / $baseline_time) * 100}")
        echo "$overhead"
    else
        echo "N/A"
    fi
}

echo "=========================================="
echo "OVERHEAD COMPARISON"
echo "=========================================="

local_overhead=$(calculate_overhead local_sharded)
absl_overhead=$(calculate_overhead absl_hashmap)
global_overhead=$(calculate_overhead global_original)

printf "%-25s %10s\n" "Configuration" "Overhead"
printf "%-25s %10s\n" "-------------------------" "----------"
printf "%-25s %10s\n" "Baseline (no profiling)" "0.00%"
printf "%-25s %10s\n" "Local (sharded)" "${local_overhead}%"
if [ "$absl_overhead" != "N/A" ]; then
    printf "%-25s %10s\n" "Absl (hashmap)" "${absl_overhead}%"
fi
if [ "$global_overhead" != "N/A" ]; then
    printf "%-25s %10s\n" "Global (original)" "${global_overhead}%"
fi

echo ""
echo "Key metrics (from perf):"
echo "  Baseline cycles:      $(grep "cycles" "$RESULTS_DIR/baseline_perf.txt" 2>/dev/null | awk '{print $1}' | head -1)"
echo "  Local cycles:         $(grep "cycles" "$RESULTS_DIR/local_sharded_perf.txt" 2>/dev/null | awk '{print $1}' | head -1)"
if [ -f "$RESULTS_DIR/absl_hashmap_perf.txt" ]; then
    echo "  Absl cycles:          $(grep "cycles" "$RESULTS_DIR/absl_hashmap_perf.txt" 2>/dev/null | awk '{print $1}' | head -1)"
fi
if [ -f "$RESULTS_DIR/global_original_perf.txt" ]; then
    echo "  Global cycles:        $(grep "cycles" "$RESULTS_DIR/global_original_perf.txt" 2>/dev/null | awk '{print $1}' | head -1)"
fi

echo ""
echo "Results saved to: $RESULTS_DIR"
echo "  View detailed perf stats: cat $RESULTS_DIR/*_perf.txt"
echo "  View allocation stats:    cat $RESULTS_DIR/*.txt"
echo "=========================================="

