#!/bin/bash
# Run content_server (Update Package Server) on Thor via SSH.
# The binary must be built first with build.sh (outputs to bin/).
# Usage: ./run_server.sh <dupkg_path_on_thor>
#   dupkg_path_on_thor: path to the .dupkg file on the Thor target (required)
#
# Options passed after <dupkg_path>:
#   -r / --resume : resume a previous serve session
#   -c / --clear  : clear context and exit
#   -i / --idle   : start in idle mode (no package)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../env.sh"

REPO_ROOT="${SCRIPT_DIR}/../.."
BINARY_SRC="${REPO_ROOT}/bin/content_server"
DEPLOY_DIR="${THOR_DEPLOY_DIR}"
BINARY_DST="${DEPLOY_DIR}/content_server"
LOG_DIR="${SCRIPT_DIR}/../logs"
LOG_FILE="${LOG_DIR}/server_$(date +%Y%m%d_%H%M%S).log"

mkdir -p "${LOG_DIR}"

if [[ ! -f "${BINARY_SRC}" ]]; then
    echo "Error: binary not found: ${BINARY_SRC}"
    echo "Run ./build.sh first."
    exit 1
fi

if [[ $# -lt 1 && "$1" != "-i" && "$1" != "--idle" ]]; then
    echo "Usage: $0 <dupkg_path_on_thor> [options]"
    echo "       $0 --idle"
    echo ""
    echo "Options:"
    echo "  -r, --resume   Resume previous serve session"
    echo "  -c, --clear    Clear context and exit"
    echo "  -i, --idle     Start in idle mode (no package)"
    exit 1
fi

echo "=== content_server: deploy and run ==="
echo "Thor: ${THOR}"
echo "Deploy path: ${BINARY_DST}"

ssh "${THOR}" "mkdir -p ${DEPLOY_DIR}"
scp "${BINARY_SRC}" "${THOR}:${BINARY_DST}"

echo "Binary deployed to Thor."

CMD="${BINARY_DST} ${*}"
echo "Running: ${CMD}"
echo "Log: ${LOG_FILE}"

ssh "${THOR}" "${CMD}" 2>&1 | tee "${LOG_FILE}"
