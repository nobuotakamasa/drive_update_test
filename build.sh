#!/bin/bash
# Build DRIVE Update V3 binaries inside the Docker container.
# Binaries are cross-compiled for aarch64 (DRIVE AGX / Thor target).
#
# Flow:
#   1. Copy src/ into the container at /tmp/du_build/
#   2. Build each target with make
#   3. Copy resulting binaries to bin/
#   4. Clean up /tmp/du_build/ inside the container

set -e

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
source "${REPO_ROOT}/env.sh"

SRC_DIR="${REPO_ROOT}/src"
BIN_DIR="${REPO_ROOT}/bin"
CONTAINER_BUILD_DIR="/tmp/du_build"

# Build targets: (subdir, binary_name)
# Mandatory targets - build failure aborts the script
TARGETS=(
    "driveupdate:driveupdate"
    "content_server:content_server"
    "du-cli:du_cli"
    "duinstaller:duinstaller"
    "bhc_plugin:libnvdubhc_api.so"
)
# Optional targets - missing dependencies print a warning but do not abort
OPTIONAL_TARGETS=(
    "remote_content_provider:remote_content_provider"   # requires libcurl
    "ffu_update:ffu_update"               # requires libnvmnand_private
)

mkdir -p "${BIN_DIR}"

echo "=== DRIVE Update V3 Build ==="
echo "Container : ${DOCKER_CONTAINER}"
echo "Source    : ${SRC_DIR}"
echo "Output    : ${BIN_DIR}"
echo ""

# Step 1: Copy src/ into the container
echo "--- Copying src/ to container:${CONTAINER_BUILD_DIR} ---"
docker exec "${DOCKER_CONTAINER}" rm -rf "${CONTAINER_BUILD_DIR}"
docker cp "${SRC_DIR}/." "${DOCKER_CONTAINER}:${CONTAINER_BUILD_DIR}"

build_target() {
    local subdir="$1"
    local binary="$2"
    local optional="$3"

    echo ""
    echo "--- Building: ${subdir} (→ ${binary}) ---"
    if ! docker exec "${DOCKER_CONTAINER}" bash -c \
        "cd ${CONTAINER_BUILD_DIR}/${subdir} && make clean && make"; then
        if [[ "${optional}" == "true" ]]; then
            echo "WARNING: ${subdir} build failed (optional - skipping)"
            return 0
        else
            echo "ERROR: ${subdir} build failed"
            return 1
        fi
    fi

    echo "--- Copying binary: ${binary} ---"
    docker cp "${DOCKER_CONTAINER}:${CONTAINER_BUILD_DIR}/${subdir}/${binary}" \
              "${BIN_DIR}/${binary}"
}

# Step 2 & 3: Build each target and copy binary to bin/
for entry in "${TARGETS[@]}"; do
    build_target "${entry%%:*}" "${entry##*:}" "false"
done

for entry in "${OPTIONAL_TARGETS[@]}"; do
    build_target "${entry%%:*}" "${entry##*:}" "true"
done

# Step 4: Clean up inside container
echo ""
echo "--- Cleaning up container build directory ---"
docker exec "${DOCKER_CONTAINER}" rm -rf "${CONTAINER_BUILD_DIR}"

echo ""
echo "=== Build complete ==="
echo "Binaries:"
ls -lh "${BIN_DIR}/"
