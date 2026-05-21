#!/usr/bin/env bash
# =============================================================================
# build.sh â€” Compile C & Go sources from SOURCE_DIR â†’ dist/
# =============================================================================
# HOW IT WORKS:
#   1. Recursively scans SOURCE_DIR for *.c and *.go files.
#   2. Compiles each with the system compiler (gcc / go build).
#   3. Mirrors the folder structure of SOURCE_DIR inside dist/.
#   4. Copies the compiled binary to the mirrored path (no source files).
#   5. Special rule: the "orchestration" sub-folder (anywhere inside
#      SOURCE_DIR) is copied verbatim â€” all files preserved, no compilation.
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# CONFIGURATION â€” change SOURCE_DIR to match your project layout
# ---------------------------------------------------------------------------
SOURCE_DIR="agent"        # <â”€â”€ folder inside project root to process
DIST_DIR="dist"              # output directory (created in project root)
ORCHESTRATION_DIR="orchestration"   # sub-folder that is copied as-is

# ---------------------------------------------------------------------------
# Resolve the project root as the directory containing this script
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
SRC_PATH="$PROJECT_ROOT/$SOURCE_DIR"
DIST_PATH="$PROJECT_ROOT/$DIST_DIR"

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; }

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
if [[ ! -d "$SRC_PATH" ]]; then
    error "Source directory not found: $SRC_PATH"
    exit 1
fi

command -v gcc  &>/dev/null || warn "gcc not found â€” C files will be skipped"
command -v go   &>/dev/null || warn "go not found  â€” Go files will be skipped"

# ---------------------------------------------------------------------------
# (Re)create dist directory
# ---------------------------------------------------------------------------
info "Cleaning and recreating: $DIST_PATH"
rm -rf "$DIST_PATH"
mkdir -p "$DIST_PATH"

# ---------------------------------------------------------------------------
# Counters
# ---------------------------------------------------------------------------
compiled_ok=0
compiled_fail=0
copied_dirs=0

# ---------------------------------------------------------------------------
# STEP 1 â€” Handle the special "orchestration" folder
#           Copy it verbatim into dist, preserving structure relative to
#           SOURCE_DIR. Every occurrence (there may be more than one) is
#           handled.
# ---------------------------------------------------------------------------
info "Scanning for '$ORCHESTRATION_DIR' directories to copy verbatim â€¦"

while IFS= read -r -d '' orch_dir; do
    # Relative path inside SOURCE_DIR  e.g. "foo/orchestration"
    rel="${orch_dir#"$SRC_PATH/"}"
    dest="$DIST_PATH/$rel"

    info "  Copying orchestration folder: $rel"
    mkdir -p "$(dirname "$dest")"
    cp -r "$orch_dir" "$dest"
    (( copied_dirs++ )) || true
    success "  â†’ $DIST_DIR/$rel"
done < <(find "$SRC_PATH" -type d -name "$ORCHESTRATION_DIR" -print0)

# ---------------------------------------------------------------------------
# STEP 2 â€” Compile C source files  (*.c)
# ---------------------------------------------------------------------------
if command -v gcc &>/dev/null; then
    info "Compiling C source files â€¦"

    while IFS= read -r -d '' src_file; do
        # Skip anything inside an orchestration directory
        if [[ "$src_file" == *"/$ORCHESTRATION_DIR/"* ]]; then
            continue
        fi

        rel_file="${src_file#"$SRC_PATH/"}"          # e.g. core/parser/lexer.c
        rel_dir="$(dirname "$rel_file")"              # e.g. core/parser
        base_name="$(basename "$src_file" .c)"        # e.g. lexer

        dest_dir="$DIST_PATH/$rel_dir"
        dest_bin="$dest_dir/$base_name"

        mkdir -p "$dest_dir"

        echo -ne "  ${BOLD}gcc${RESET} $rel_file â€¦ "
        if gcc -o "$dest_bin" "$src_file" 2>/tmp/_build_err; then
            echo -e "${GREEN}OK${RESET}"
            (( compiled_ok++ )) || true
        else
            echo -e "${RED}FAILED${RESET}"
            error "    $(cat /tmp/_build_err)"
            (( compiled_fail++ )) || true
        fi
    done < <(find "$SRC_PATH" -type f -name "*.c" -print0)
else
    warn "Skipping C compilation (gcc not available)"
fi

# ---------------------------------------------------------------------------
# STEP 3 â€” Compile Go source files  (*.go)
#
# Go requires a package-level build rather than per-file compilation.
# Strategy: group *.go files by their containing directory (= one package)
# and run "go build -o <dist/rel_dir/dirname>" in each package directory.
# ---------------------------------------------------------------------------
if command -v go &>/dev/null; then
    info "Compiling Go packages â€¦"

    # Collect unique Go package directories (excluding orchestration)
    declare -A go_pkg_dirs

    while IFS= read -r -d '' go_file; do
        if [[ "$go_file" == *"/$ORCHESTRATION_DIR/"* ]]; then
            continue
        fi
        pkg_dir="$(dirname "$go_file")"
        go_pkg_dirs["$pkg_dir"]=1
    done < <(find "$SRC_PATH" -type f -name "*.go" -print0)

    for pkg_dir in "${!go_pkg_dirs[@]}"; do
        rel_dir="${pkg_dir#"$SRC_PATH/"}"             # e.g. agent/runner
        binary_name="$(basename "$pkg_dir")"           # e.g. runner
        dest_dir="$DIST_PATH/$rel_dir"
        dest_bin="$dest_dir/$binary_name"

        mkdir -p "$dest_dir"

        echo -ne "  ${BOLD}go build${RESET} $rel_dir â€¦ "
        if (cd "$pkg_dir" && go build -o "$dest_bin" . 2>/tmp/_build_err); then
            echo -e "${GREEN}OK${RESET}"
            (( compiled_ok++ )) || true
        else
            echo -e "${RED}FAILED${RESET}"
            error "    $(cat /tmp/_build_err)"
            (( compiled_fail++ )) || true
        fi
    done
else
    warn "Skipping Go compilation (go not available)"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}========== Build Summary ==========${RESET}"
echo -e "  Source directory : ${CYAN}$SOURCE_DIR${RESET}"
echo -e "  Output directory : ${CYAN}$DIST_DIR${RESET}"
echo -e "  Compiled OK      : ${GREEN}$compiled_ok${RESET}"
echo -e "  Compile failures : $([ "$compiled_fail" -eq 0 ] && echo "${GREEN}0${RESET}" || echo "${RED}$compiled_fail${RESET}")"
echo -e "  Orchestration dirs copied : ${YELLOW}$copied_dirs${RESET}"
echo -e "${BOLD}====================================${RESET}"
echo ""

if [[ "$compiled_fail" -gt 0 ]]; then
    error "Build completed with $compiled_fail failure(s)."
    exit 1
fi

success "Build complete. Binaries are in: $DIST_DIR/"
