#!/bin/bash
# Environment variables for epl_test project

export DOCKER_CONTAINER="drive-sdk"
export THOR="thor"
export THOR_DEPLOY_DIR="/home/autoware/epl_test"

# DriveOS SDK paths inside container
export SDK_INC="/drive/drive-linux/include"
export SDK_LIB="/drive/drive-linux/lib-target"

echo "Environment loaded: THOR=${THOR}, DEPLOY=${THOR_DEPLOY_DIR}"
