#!/usr/bin/env bash
# SAATHI: every automated check in one command. Logs go to reports/test_logs/, summary (with the time each check took
# and the environment) to reports/test_logs/SUMMARY.md.
# Needs: Python packages from requirements.txt, g++ (for the firmware logic tests). About 1 minute on 2 CPUs.
# Usage: bash run_tests.sh            all 9 checks (after bash run_all.sh, which creates the data)
#        bash run_tests.sh --quick    skips the two raw-waveform checks (the slowest); for a quick re-check only
# Each check has its own time limit (SAATHI_TEST_TIMEOUT seconds, default 900), so a hang is reported as a failure
# with its log instead of stalling the whole run. Measured times are in SUMMARY.md.
set -u
L=reports/test_logs
mkdir -p "$L"
QUICK=0; [ "${1:-}" = "--quick" ] && QUICK=1
TO="${SAATHI_TEST_TIMEOUT:-900}"
if ! command -v python >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then   # laptops with only python3
  SHIM=$(mktemp -d); ln -s "$(command -v python3)" "$SHIM/python"; export PATH="$SHIM:$PATH"; fi
if command -v timeout >/dev/null 2>&1; then TCMD="timeout $TO"
elif command -v gtimeout >/dev/null 2>&1; then TCMD="gtimeout $TO"        # macOS with coreutils
else TCMD=""; fi                                                          # macOS without: no per-check limit
pass=0; fail=0; skip=0; rows=""; T0=$(date +%s)
t() {   # name, command
  if [ "$QUICK" = 1 ] && [ "${3:-}" = slow ]; then skip=$((skip+1)); rows="$rows| $1 | skipped (--quick) | |\n"; echo "SKIP  $1"; return; fi
  local s=$(date +%s) rc
  $TCMD bash -c "$2" > "$L/$1.log" 2>&1; rc=$?
  local d=$(( $(date +%s) - s ))
  if [ $rc -eq 0 ]; then pass=$((pass+1)); rows="$rows| $1 | PASS | ${d} s |\n"; echo "PASS  $1  (${d} s)"
  elif [ $rc -eq 124 ]; then fail=$((fail+1)); rows="$rows| $1 | **TIMEOUT** after ${TO} s (see $L/$1.log) | ${d} s |\n"; echo "FAIL  $1  timed out after ${TO} s"
  else fail=$((fail+1)); rows="$rows| $1 | **FAIL** (see $L/$1.log) | ${d} s |\n"; echo "FAIL  $1  (${d} s, see $L/$1.log)"; fi
}
t data_rules           "python validate_data.py && ! grep -q '| FAIL |' reports/validation_report.md"
t firmware_regressions "python firmware/host_test/run_host_tests.py"
t firmware_parity      "python firmware/host_test/run_parity.py && python -c \"import json,sys; r=json.load(open('firmware/host_test/parity_result.json')); sys.exit(r['disagreements'] != 0)\""
t firmware_edge_cases  "python firmware/host_test/edge_cases.py"
t firmware_dsp_parity  "python firmware/host_test/dsp_parity.py" slow
t no_auto_format       "! grep -rnE '^[^/]*LittleFS\.begin\(true\)' firmware/saathi_chair firmware/wokwi"
t raw_fault_injection  "python raw_fault_injection.py && python -c \"import json,sys; r=json.load(open('reports/raw_fault_injection.json')); bad=[x for x in r if x['WRONG']>0 and 'partly failed' not in x['fault']]; print(bad); sys.exit(len(bad)>0)\"" slow
t demo_scenario        "python demo_scenario.py | grep -q 'First alert: day'"
t locked_test_intact   "python locked_eval.py --verify"
TT=$(( $(date +%s) - T0 ))
ENV="$(python --version 2>&1); $(g++ --version 2>/dev/null | head -1); $(uname -sm); $(nproc 2>/dev/null) CPU"
printf "# Test summary\n\n%d passed, %d failed, %d skipped; total %d s (%s)\n\nEnvironment: %s\n\n| Test | Result | Time |\n| --- | --- | --- |\n%b" \
  "$pass" "$fail" "$skip" "$TT" "$(date -u +%Y-%m-%dT%H:%MZ)" "$ENV" "$rows" > "$L/SUMMARY.md"
echo "$pass passed, $fail failed, $skip skipped in ${TT} s. Logs: $L/"
[ "$fail" -eq 0 ]
