#!/usr/bin/env bash
#
# coverage-summary.sh - whole-codebase coverage summary.
#
# Every first-party firmware .c is compiled into the coverage build (the ones no
# harness exercises are compiled to a .gcno only, so they show up at their true
# 0% with full per-line detail). This means the lcov tracefile already covers
# the WHOLE first-party codebase - there is no separate hand-maintained blind
# spot list and no source-line approximation. All numbers below are derived
# entirely from the lcov .info.
#
# Reads a filtered lcov .info tracefile and the full first-party source list
# (git ls-files, minus third-party submodules and the test harnesses) and emits:
#   - overall first-party line% and branch% (straight from lcov),
#   - the list of 0%-coverage files (real: lines-hit == 0),
#   - files below 100% branch coverage, worst-first (searchable weak spots).
# Outputs summary.txt and summary.json into <out-dir>, and a summary.html that
# is surfaced at the top of the genhtml report (linked from index.html).
#
# Usage: ./coverage-summary.sh <coverage.info> <out-dir>
set -euo pipefail

INFO=${1:?usage: coverage-summary.sh <coverage.info> <out-dir>}
OUT=${2:?usage: coverage-summary.sh <coverage.info> <out-dir>}
REPO=$(git -C .. rev-parse --show-toplevel)

# --- full first-party file list (denominator) -------------------------------
mapfile -t FIRST_PARTY < <(
	git -C "$REPO" ls-files '*.c' \
		':!:libs/qrcodegen/**' ':!:libs/littlefs/**' ':!:test/**' | sort
)

# --- parse lcov .info: per repo-relative file -> LF LH BRF BRH ---------------
declare -A LF LH BRF BRH
cur=""
while IFS= read -r line; do
	case "$line" in
	SF:*)
		path=${line#SF:}
		cur=${path#"$REPO"/}
		LF[$cur]=0
		LH[$cur]=0
		BRF[$cur]=0
		BRH[$cur]=0
		;;
	LF:*) LF[$cur]=${line#LF:} ;;
	LH:*) LH[$cur]=${line#LH:} ;;
	BRF:*) BRF[$cur]=${line#BRF:} ;;
	BRH:*) BRH[$cur]=${line#BRH:} ;;
	esac
done <"$INFO"

pct() { # hits found -> percentage string with one decimal
	local h=$1 f=$2
	if [ "$f" -eq 0 ]; then
		echo "n/a"
	else
		awk -v h="$h" -v f="$f" 'BEGIN{printf "%.1f", (h*100.0)/f}'
	fi
}

# --- aggregate (entirely from lcov data) ------------------------------------
tot_lf=0 tot_lh=0 tot_brf=0 tot_brh=0
n_files=0 n_zero=0 n_measured=0 n_exercised=0 n_missing=0
zero_list=()    # paths with 0% lines (real: lines-hit == 0)
missing_list=() # first-party files absent from the lcov data (should be none)
below_list=()   # "brpct|path|BRH|BRF" below 100% branch
for f in "${FIRST_PARTY[@]}"; do
	n_files=$((n_files + 1))
	if [ -z "${LF[$f]+x}" ]; then
		# Not in lcov at all - this file failed to compile into the coverage
		# build. Flagged (not approximated) so the gap is loud, never hidden.
		n_missing=$((n_missing + 1))
		missing_list+=("$f")
		continue
	fi
	n_measured=$((n_measured + 1))
	lf=${LF[$f]} lh=${LH[$f]} brf=${BRF[$f]} brh=${BRH[$f]}
	tot_lf=$((tot_lf + lf))
	tot_lh=$((tot_lh + lh))
	tot_brf=$((tot_brf + brf))
	tot_brh=$((tot_brh + brh))
	if [ "$lh" -eq 0 ]; then
		n_zero=$((n_zero + 1))
		zero_list+=("$f")
	else
		n_exercised=$((n_exercised + 1))
	fi
	if [ "$brf" -gt 0 ] && [ "$brh" -lt "$brf" ]; then
		p=$(awk -v h="$brh" -v f="$brf" 'BEGIN{printf "%07.3f",(h*100.0)/f}')
		below_list+=("$p|$f|$brh|$brf")
	fi
done

line_pct=$(pct "$tot_lh" "$tot_lf")
br_pct=$(pct "$tot_brh" "$tot_brf")

# worst-first branch list
mapfile -t below_sorted < <(printf '%s\n' "${below_list[@]}" | sort -t'|' -k1,1n)

# --- summary.txt ------------------------------------------------------------
mkdir -p "$OUT"
TXT="$OUT/summary.txt"
{
	echo "hslock host coverage summary"
	echo "============================"
	echo
	echo "First-party files (denominator): $n_files"
	echo "  in coverage report (lcov):         $n_measured"
	echo "  actually exercised (>0% lines):    $n_exercised"
	echo "  compiled but not exercised (0%):   $n_zero"
	if [ "$n_missing" -gt 0 ]; then
		echo "  MISSING from coverage build:       $n_missing"
	fi
	echo
	echo "Overall LINE coverage:   ${line_pct}%  (${tot_lh}/${tot_lf} lines)"
	echo "Overall BRANCH coverage: ${br_pct}%  (${tot_brh}/${tot_brf} branches)"
	echo "  every first-party file is in the lcov data; these totals are the"
	echo "  real instrumented-line / -branch counts, no approximation."
	echo
	echo "0%-COVERAGE FILES ($n_zero) - compiled for visibility, no harness yet:"
	if [ ${#zero_list[@]} -eq 0 ]; then
		echo "  (none)"
	else
		printf '  - %s\n' "${zero_list[@]}"
	fi
	echo
	echo "FILES BELOW 100% BRANCH COVERAGE (worst-first):"
	if [ ${#below_sorted[@]} -eq 0 ]; then
		echo "  (none among measured files)"
	else
		for row in "${below_sorted[@]}"; do
			IFS='|' read -r p f h t <<<"$row"
			printf '  - %6.2f%%  %s  (%s/%s branches)\n' "$p" "$f" "$h" "$t"
		done
	fi
	if [ "$n_missing" -gt 0 ]; then
		echo
		echo "FILES MISSING FROM COVERAGE BUILD ($n_missing) - failed to compile:"
		printf '  - %s\n' "${missing_list[@]}"
	fi
} >"$TXT"

# --- summary.json -----------------------------------------------------------
JSON="$OUT/summary.json"
{
	printf '{\n'
	printf '  "first_party_files": %s,\n' "$n_files"
	printf '  "compiled_files": %s,\n' "$n_measured"
	printf '  "exercised_files": %s,\n' "$n_exercised"
	printf '  "zero_coverage_files": %s,\n' "$n_zero"
	printf '  "missing_files": %s,\n' "$n_missing"
	printf '  "line_pct": "%s",\n' "$line_pct"
	printf '  "lines_hit": %s,\n' "$tot_lh"
	printf '  "lines_found": %s,\n' "$tot_lf"
	printf '  "branch_pct": "%s",\n' "$br_pct"
	printf '  "branches_hit": %s,\n' "$tot_brh"
	printf '  "branches_found": %s,\n' "$tot_brf"
	printf '  "zero_files": ['
	sep=""
	for f in "${zero_list[@]}"; do
		printf '%s"%s"' "$sep" "$f"
		sep=", "
	done
	printf '],\n'
	printf '  "missing_files_list": ['
	sep=""
	for f in "${missing_list[@]}"; do
		printf '%s"%s"' "$sep" "$f"
		sep=", "
	done
	printf ']\n}\n'
} >"$JSON"

# --- summary.html (surfaced at top of the Pages report) ---------------------
if [ -d "$OUT/html" ]; then
	HTML="$OUT/html/summary.html"
	{
		echo '<!doctype html><meta charset="utf-8"><title>hslock coverage summary</title>'
		echo '<style>body{font:14px/1.5 monospace;margin:2rem;max-width:60rem}'
		echo 'h1{font-size:1.4rem}pre{background:#f4f4f4;padding:1rem;overflow:auto}'
		echo 'a{color:#06c}</style>'
		echo '<h1>hslock host coverage summary</h1>'
		echo '<p><a href="index.html">&rarr; full per-file / per-line report</a></p>'
		echo '<pre>'
		sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' "$TXT"
		echo '</pre>'
	} >"$HTML"
	# Prepend a prominent banner + summary link at the top of genhtml's index.
	IDX="$OUT/html/index.html"
	if [ -f "$IDX" ] && ! grep -q 'hslock-summary-banner' "$IDX"; then
		banner='<div id="hslock-summary-banner" style="padding:10px;margin:0;background:#fffae6;border-bottom:2px solid #e0c000;font-family:monospace"><b>Whole-codebase coverage:</b> '"${line_pct}"'% lines, '"${br_pct}"'% branches over '"${n_files}"' first-party files ('"${n_zero}"' at 0%, all in the report). &nbsp;<a href="summary.html">full summary &rarr;</a></div>'
		awk -v b="$banner" '/<body/ && !done {print; print b; done=1; next} {print}' "$IDX" >"$IDX.tmp" &&
			mv "$IDX.tmp" "$IDX"
	fi
fi

echo "summary written: $TXT, $JSON"
echo "  line ${line_pct}% (${tot_lh}/${tot_lf}), branch ${br_pct}% (${tot_brh}/${tot_brf}), ${n_zero} zero-cov, ${n_missing} missing"
