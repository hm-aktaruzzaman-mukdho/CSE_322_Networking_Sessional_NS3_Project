#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_FILE="sim_results.csv"
PARALLEL_JOBS="${PARALLEL_JOBS:-$(nproc)}"
ITERATIONS="${ITERATIONS:-1}"
TMP_DIR="$(mktemp -d "${ROOT_DIR}/tmp_eval_XXXXXX")"
JOB_FILE="${TMP_DIR}/jobs.txt"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

# Start with a fresh CSV for a full batch run.
rm -f "$RESULTS_FILE"

declare -a NET_TYPES=("wired" "wireless")
declare -a SCENARIOS=("baseline" "attack" "defense")

# Defaults used when a parameter is not being varied.
DEFAULT_NODES=60
DEFAULT_FLOWS=20
DEFAULT_PPS=100
DEFAULT_SPEED=10
DEFAULT_TX_RANGE=1

NODES_VALUES="${NODES_VALUES:-5 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90 95 100}"
FLOWS_VALUES="${FLOWS_VALUES:-10 15 20 25 30 35 40 45 50}"
PPS_VALUES="${PPS_VALUES:-100 150 200 250 300 350 400 450 500}"
SPEED_VALUES="${SPEED_VALUES:-5 10 15 20 25}"
COVERAGE_VALUES="${COVERAGE_VALUES:-1 2 3 4 5}"

enqueue_case() {
  local iteration="$1"
  local netType="$2"
  local scenario="$3"
  local varyingName="$4"
  local varyingValue="$5"
  local nNodes="$6"
  local nFlows="$7"
  local pps="$8"
  local speed="$9"
  local txRange="${10}"

  printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n' \
    "$iteration" "$netType" "$scenario" "$varyingName" "$varyingValue" \
    "$nNodes" "$nFlows" "$pps" "$speed" "$txRange" >> "$JOB_FILE"
}

enqueue_matrix_for_value() {
  local varyingName="$1"
  local varyingValue="$2"
  local nNodes="$3"
  local nFlows="$4"
  local pps="$5"
  local speed="$6"
  local txRange="$7"

  for ((i = 1; i <= ITERATIONS; i++)); do
    for net in "${NET_TYPES[@]}"; do
      for sc in "${SCENARIOS[@]}"; do
        enqueue_case "$i" "$net" "$sc" "$varyingName" "$varyingValue" "$nNodes" "$nFlows" "$pps" "$speed" "$txRange"
      done
    done
  done
}

# Compile and sanity-run once before the full campaign.
echo "[INFO] Compile sanity run..."
./ns3 run "scratch/aodv-wormhole-eval --netType=wired --scenario=baseline --nNodes=20 --nFlows=10 --pps=100 --speed=10 --txRange=1 --varyingParamName=Warmup --paramValue=0 --resultsFile=${TMP_DIR}/warmup.csv"

SIM_BINARY="$(find "$ROOT_DIR/build/scratch" -maxdepth 1 -type f -name 'ns3.39-aodv-wormhole-eval-*' | head -n 1)"
if [[ -z "$SIM_BINARY" ]]; then
  echo "[ERROR] Could not locate compiled aodv-wormhole-eval binary under build/scratch" >&2
  exit 1
fi
if [[ ! -x "$SIM_BINARY" ]]; then
  echo "[ERROR] Binary is not executable: $SIM_BINARY" >&2
  exit 1
fi

echo "[INFO] Preparing jobs (iterations=${ITERATIONS}, parallel_jobs=${PARALLEL_JOBS})..."
> "$JOB_FILE"

# 1) Nodes
for v in $NODES_VALUES; do
  enqueue_matrix_for_value "Nodes" "$v" "$v" "$DEFAULT_FLOWS" "$DEFAULT_PPS" "$DEFAULT_SPEED" "$DEFAULT_TX_RANGE"
done

# 2) Flows
for v in $FLOWS_VALUES; do
  enqueue_matrix_for_value "Flows" "$v" "$DEFAULT_NODES" "$v" "$DEFAULT_PPS" "$DEFAULT_SPEED" "$DEFAULT_TX_RANGE"
done

# 3) PPS
for v in $PPS_VALUES; do
  enqueue_matrix_for_value "PPS" "$v" "$DEFAULT_NODES" "$DEFAULT_FLOWS" "$v" "$DEFAULT_SPEED" "$DEFAULT_TX_RANGE"
done

# 4) Speed
for v in $SPEED_VALUES; do
  enqueue_matrix_for_value "Speed" "$v" "$DEFAULT_NODES" "$DEFAULT_FLOWS" "$DEFAULT_PPS" "$v" "$DEFAULT_TX_RANGE"
done

# 5) TxRange Multiplier / Coverage
for v in $COVERAGE_VALUES; do
  enqueue_matrix_for_value "Coverage" "$v" "$DEFAULT_NODES" "$DEFAULT_FLOWS" "$DEFAULT_PPS" "$DEFAULT_SPEED" "$v"
done

TOTAL_JOBS="$(wc -l < "$JOB_FILE" | tr -d ' ')"
echo "[INFO] Launching ${TOTAL_JOBS} jobs..."

active_jobs=0
while IFS="|" read -r iter net sc varyingName varyingValue nNodes nFlows pps speed txRange; do
  outFile="${TMP_DIR}/res_${iter}_${net}_${sc}_${varyingName}_${varyingValue}_${nNodes}_${nFlows}_${pps}_${speed}_${txRange}.csv"
  echo "[RUN] iter=${iter} netType=${net} scenario=${sc} ${varyingName}=${varyingValue} nNodes=${nNodes} nFlows=${nFlows} pps=${pps} speed=${speed} txRange=${txRange}"

  "$SIM_BINARY" \
    --netType="$net" \
    --scenario="$sc" \
    --nNodes="$nNodes" \
    --nFlows="$nFlows" \
    --pps="$pps" \
    --speed="$speed" \
    --txRange="$txRange" \
    --varyingParamName="$varyingName" \
    --paramValue="$varyingValue" \
    --resultsFile="$outFile" &

  active_jobs=$((active_jobs + 1))
  if (( active_jobs >= PARALLEL_JOBS )); then
    wait -n
    active_jobs=$((active_jobs - 1))
  fi
done < "$JOB_FILE"

wait

echo "NetType,Scenario,VaryingParamName,ParamValue,Throughput,Delay,PDR,DropRatio,Energy" > "$RESULTS_FILE"
for f in "$TMP_DIR"/res_*.csv; do
  if [[ -f "$f" ]]; then
    tail -n +2 "$f" >> "$RESULTS_FILE"
  fi
done

echo "[DONE] Evaluation complete. Results written to ${RESULTS_FILE}"
