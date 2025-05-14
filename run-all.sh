#!/bin/bash

# Set the base directory for the CS534 repository here
BASE_DIR="/home/ubuntu/CS534-Final-Project"  # Modify this path

# Ensure the directory exists
if [ ! -d "$BASE_DIR" ]; then
    echo "Error: Base directory does not exist: $BASE_DIR"
    exit 1
fi

MODE=""
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      shift
      MODE="$1"
      shift
      ;;
    --) 
      shift
      EXTRA_ARGS=("$@")
      break
      ;;
    *)
      echo "Unknown option: $1"
      usage
      ;;
  esac
done

if [[ -z "$MODE" ]]; then
  echo "Error: --mode is required"
fi

BIN_DIR="$BASE_DIR/bin"
DIJKSTRA_DIR="$BIN_DIR/dijkstra"
QSORT_DIR="$BIN_DIR/qsort"

configs="configs/deprecated/example/se.py \
    --cpu-type=ArmMinorCPU \
    --cpu-clock=4GHz \
    --cacheline_size=64 \
    --caches \
    --l1i_size=8kB \
    --l1d_size=8kB \
    --l2cache \
    --l2_size=4MB \
    --l2_assoc=16 \
    --num-cpus=4"

# List of binaries to test
binaries=(
    "$BIN_DIR/basicmath_small"
    #"$BIN_DIR/basicmath_large"
    #"$BIN_DIR/fft"
    #"$DIJKSTRA_DIR/dijkstra_small"
    #"$DIJKSTRA_DIR/dijkstra_large"
    #"$QSORT_DIR/qsort_small"
    #"$QSORT_DIR/qsort_large"
)

# Corresponding input arguments (same index as binaries)
binary_args=(
    ""                                      # basicmath_small
    ""                                      # basicmath_large
    "8 1024"                                # fft
    "$DIJKSTRA_DIR/input.dat"              # dijkstra_small
    "$DIJKSTRA_DIR/input.dat"              # dijkstra_large
    "$QSORT_DIR/input_small.dat"           # qsort_small
    "$QSORT_DIR/input_large.dat"           # qsort_large
)

# Set the base directory of gem5 repository (modify this path)
case "$MODE" in
  algo)
    GEM5_DIR="/home/ubuntu/gem5_algo"
    ;;
  baseline)
    GEM5_DIR="/home/ubuntu/gem5_base"
    ;;
  *)
    echo "Error: invalid mode '$MODE', must be 'algo' or 'baseline'"
    usage
    ;;
esac
base_cmd="${GEM5_DIR}/build/ARM/gem5.debug"

# Check if the gem5 directory exists
if [ ! -d "$GEM5_DIR" ]; then
    echo "Error: gem5 directory does not exist: $GEM5_DIR"
    exit 1
fi

# Change to the gem5 directory to find gem5.opt
cd "$GEM5_DIR"

# Run gem5 for each binary
for i in "${!binaries[@]}"; do
    bin="${binaries[$i]}"
    args="${binary_args[$i]}"

    # Use binary name as a unique output directory
    outdir="m5out/$(basename "$bin")/"
    mkdir -p "$outdir"

    echo "Running $(basename "$bin")..."

    # Run gem5 with the correct order for --outdir
    $base_cmd --debug-flags=MMU --debug-file="debug_output"  --outdir="$outdir" $configs --cmd="$bin" --options="$args"

    echo "Finished $(basename "$bin"). Output in $outdir"
done