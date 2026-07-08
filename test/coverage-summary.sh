#!/usr/bin/env bash
#
# coverage-summary.sh - whole-codebase coverage summary + blind-spot list.
#
# Reads a filtered lcov .info tracefile and the full first-party source list
# (git ls-files, minus third-party submodules and the test harnesses) and emits:
#   - overall first-party line% and branch% (blind spots folded into the
#     line denominator as 0%, denominator stated explicitly),
#   - the list of 0%-coverage files,
#   - files below 100% branch coverage, worst-first (searchable blind spots),
#   - the explicit BLIND SPOTS list (non-host-compilable files + reason).
# Outputs summary.txt and summary.json into <out-dir>, and a summary.html that
# is surfaced at the top of the genhtml report (linked from index.html).
#
# Usage: ./coverage-summary.sh <coverage.info> <out-dir>
set -euo pipefail

INFO=${1:?usage: coverage-summary.sh <coverage.info> <out-dir>}
OUT=${2:?usage: coverage-summary.sh <coverage.info> <out-dir>}
REPO=$(git -C .. rev-parse --show-toplevel)

# --- explicit BLIND SPOTS: files that cannot be host-compiled (with reason) --
# Keep in sync with test/Makefile COV_ZERO_SRCS commentary.
BLIND_FILES=(network/ntp.c network/wifi.c storage/storage.c)
blind_reason() {
	case "$1" in
	network/ntp.c) echo "needs pico/cyw43_arch.h + lwip/udp.h + lwip/dns.h (no host stack)" ;;
	network/wifi.c) echo "needs pico/cyw43_arch.h (cyw43 driver, no host stack)" ;;
	storage/storage.c) echo "needs pico/flash.h + hardware/flash.h/sync.h + littlefs block device" ;;
	*) echo "non-host-compilable" ;;
	esac
}

is_blind() {
	local f
	for f in "${BLIND_FILES[@]}"; do
		[ "$f" = "$1" ] && return 0
	done
	return 1
}

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

# --- approximate instrumentable line count for uncompiled (blind-spot) files -
# Counts non-blank lines that are not pure comments or lone braces. Conservative
# stand-in for gcov line count so blind spots weigh honestly in the denominator.
approx_lines() {
	local f=$1 n=0 t
	while IFS= read -r t; do
		t=${t#"${t%%[![:space:]]*}"} # strip leading whitespace
		[ -z "$t" ] && continue
		case "$t" in
		'//'* | '/*'* | '*'* | '{' | '}' | '};') continue ;;
		esac
		n=$((n + 1))
	done <"$REPO/$f"
	echo "$n"
}

pct() { # hits found -> percentage string with one decimal
	local h=$1 f=$2
	if [ "$f" -eq 0 ]; then
		echo "n/a"
	else
		awk -v h="$h" -v f="$f" 'BEGIN{printf "%.1f", (h*100.0)/f}'
	fi
}

# --- aggregate --------------------------------------------------------------
tot_lf=0 tot_lh=0 tot_brf=0 tot_brh=0
n_files=0 n_zero=0 n_blind=0 n_measured=0 n_exercised=0
zero_list=()   # "path" with 0% lines
below_list=()  # "brpct|path|BRH|BRF" below 100% branch
for f in "${FIRST_PARTY[@]}"; do
	n_files=$((n_files + 1))
	if [ -n "${LF[$f]+x}" ]; then
		# measured by lcov
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
	else
		# not in lcov: blind spot (or otherwise uncompiled) -> 0% lines folded in
		al=$(approx_lines "$f")
		tot_lf=$((tot_lf + al))
		n_zero=$((n_zero + 1))
		if is_blind "$f"; then
			n_blind=$((n_blind + 1))
		fi
		zero_list+=("$f (0%, uncompiled ~${al} lines)")
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
	echo "  host-compiled into coverage build: $n_measured"
	echo "  actually exercised (>0% lines):    $n_exercised"
	echo "  0% coverage (untested + blind):    $n_zero"
	echo "  non-host-compilable blind spots:   $n_blind"
	echo
	echo "Overall LINE coverage:   ${line_pct}%  (${tot_lh}/${tot_lf} lines)"
	echo "  denominator folds blind-spot / uncompiled files in at 0%"
	echo "  (their instrumentable lines are approximated from source)."
	echo "Overall BRANCH coverage: ${br_pct}%  (${tot_brh}/${tot_brf} branches)"
	echo "  branch denominator counts only host-compiled files; blind-spot"
	echo "  branches are unmeasured (see the blind-spot list below)."
	echo
	echo "0%-COVERAGE FILES ($n_zero):"
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
	echo
	echo "BLIND SPOTS (non-host-compilable, counted 0% in denominator):"
	for f in "${BLIND_FILES[@]}"; do
		printf '  - %s  -- %s\n' "$f" "$(blind_reason "$f")"
	done
} >"$TXT"

# --- summary.json -----------------------------------------------------------
JSON="$OUT/summary.json"
{
	printf '{\n'
	printf '  "first_party_files": %s,\n' "$n_files"
	printf '  "compiled_files": %s,\n' "$n_measured"
	printf '  "exercised_files": %s,\n' "$n_exercised"
	printf '  "zero_coverage_files": %s,\n' "$n_zero"
	printf '  "blind_spot_files": %s,\n' "$n_blind"
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
	printf '  "blind_spots": ['
	sep=""
	for f in "${BLIND_FILES[@]}"; do
		printf '%s{"file": "%s", "reason": "%s"}' "$sep" "$f" "$(blind_reason "$f")"
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
		banner='<div id="hslock-summary-banner" style="padding:10px;margin:0;background:#fffae6;border-bottom:2px solid #e0c000;font-family:monospace"><b>Whole-codebase coverage:</b> '"${line_pct}"'% lines, '"${br_pct}"'% branches over '"${n_files}"' first-party files ('"${n_zero}"' at 0%, '"${n_blind}"' non-host-compilable blind spots). &nbsp;<a href="summary.html">full summary &amp; blind-spot list &rarr;</a></div>'
		awk -v b="$banner" '/<body/ && !done {print; print b; done=1; next} {print}' "$IDX" >"$IDX.tmp" &&
			mv "$IDX.tmp" "$IDX"
	fi
fi

echo "summary written: $TXT, $JSON"
echo "  line ${line_pct}% (${tot_lh}/${tot_lf}), branch ${br_pct}% (${tot_brh}/${tot_brf}), ${n_zero} zero-cov, ${n_blind} blind spots"
