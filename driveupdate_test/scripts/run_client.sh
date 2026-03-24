#!/bin/bash
# Run driveupdate (Update Client) on Thor via SSH.
# The binary must be built first with build.sh (outputs to bin/).
# Thor must be reachable via SSH (see ~/.ssh/config entry "thor").

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../env.sh"

REPO_ROOT="${SCRIPT_DIR}/../.."
BINARY_SRC="${REPO_ROOT}/bin/driveupdate"
DEPLOY_DIR="${THOR_DEPLOY_DIR}"
BINARY_DST="${DEPLOY_DIR}/driveupdate"
LOG_DIR="${SCRIPT_DIR}/../logs"
LOG_FILE="${LOG_DIR}/client_$(date +%Y%m%d_%H%M%S).log"

mkdir -p "${LOG_DIR}"

if [[ ! -f "${BINARY_SRC}" ]]; then
    echo "Error: binary not found: ${BINARY_SRC}"
    echo "Run ./build.sh first."
    exit 1
fi

echo "=== driveupdate: deploy and run ==="
echo "Thor: ${THOR}"
echo "Deploy path: ${BINARY_DST}"

ssh "${THOR}" "mkdir -p ${DEPLOY_DIR}"
scp "${BINARY_SRC}" "${THOR}:${BINARY_DST}"

echo "Binary deployed to Thor."

# Run on Thor - pass any extra arguments through (e.g. "package /content/files")
CMD="${BINARY_DST} ${*}"
echo "Running: ${CMD}"
echo "Log: ${LOG_FILE}"

ssh "${THOR}" "${CMD}" 2>&1 | tee "${LOG_FILE}"
