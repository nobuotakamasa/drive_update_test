# DriveOS DRIVE Update V3 Test

## Development Policy
Write all code comments in English.
Maintain README.md in English.
Claude.md は日本語で良い。

## Project Overview
DRIVE Update V3 の基本動作（パッケージ配布・更新適用）を検証するテストプロジェクト。

**検証対象:**
1. sample_driveupdate  
   - Update Client の基本動作確認
   - コンテンツ取得 → 適用フローの検証

2. content_server  
   - Update パッケージ配信サーバ
   - HTTP 経由でのコンテンツ配布確認

3. DU CLI Tool (driveupdate_tool)  
   - パッケージ操作・状態確認
   - update 実行の補助

## Environment
環境変数は `env.sh` で管理している。作業前に必ず source する。  
SSH接続先は `~/.ssh/config` の `thor` を使用する。

## Project Structure
.
├── build.sh
├── env.sh
├── docker.sh
├── driveupdate_test/
│   ├── README.md
│   ├── scripts/
│   │   ├── run_client.sh
│   │   ├── run_server.sh
│   │   └── run_tool.sh
│   └── logs/
└── docs/
    └── driveupdate.md

## Docker Container
コンテナ名: `drive-sdk`

source env.sh && docker ps | grep ${DOCKER_CONTAINER} || \
docker run -d --rm -it --privileged --net=host \
    -v /dev:/dev \
    -v $HOME/driveos_ws:/home/nvidia \
    --name ${DOCKER_CONTAINER} \
    nvcr.io/drive/driveos-sdk/drive-agx-linux-nsr-aarch64-sdk-build-x86:7.0.3.0-0010

## Toolchain (コンテナ内)
- クロスコンパイル不要（サンプル利用）

## Pre-check (重要)
docker exec -it ${DOCKER_CONTAINER} bash

find / -name "sample_driveupdate" 2>/dev/null
find / -name "content_server" 2>/dev/null
find / -name "driveupdate_tool" 2>/dev/null

## Run

### Content Server 起動
./driveupdate_test/scripts/run_server.sh

### Update Client 実行
./driveupdate_test/scripts/run_client.sh

### CLI Tool 実行
./driveupdate_test/scripts/run_tool.sh

## Workflow
1. env.sh 確認
2. Docker起動確認
3. サンプルパス探索
4. スクリプト作成
5. 実行・ログ取得

## Reference
DRIVE Update V3 Developer Guide
https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-linux-sdk/embedded-software-components/DRIVE_AGX_SoC/DRIVE_Update_V3/DRIVE_Update_V3_Developer_Guide.html
