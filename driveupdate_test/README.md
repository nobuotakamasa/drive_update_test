# DRIVE Update V3 Test

Test project for verifying basic DRIVE Update V3 operations.

## Targets

| Binary | Source | Role |
|--------|--------|------|
| `sample_driveupdate` | `sample_client_app/` | Update Client - content fetch and apply flow |
| `content_server` | `content_server/` | Content Server - serves update packages over DU transport |
| `du_cli` | `du-cli/` | CLI Tool - package operations and status queries |

All binaries are cross-compiled for **aarch64** (DRIVE AGX / Thor target).

## Prerequisites

1. Docker container `drive-sdk` must be running:
   ```bash
   source env.sh
   docker ps | grep ${DOCKER_CONTAINER}
   ```
2. Thor must be reachable via SSH (`~/.ssh/config` entry: `thor`).

## Build

```bash
source env.sh
./build.sh
```

Binaries are generated inside the container at:
- `/drive/drive-linux/samples/driveupdate/sample_client_app/sample_driveupdate`
- `/drive/drive-linux/samples/driveupdate/content_server/content_server`
- `/drive/drive-linux/samples/driveupdate/du-cli/du_cli`

## Run

Each script deploys the binary to Thor via SCP, then runs it via SSH.
Logs are saved to `logs/`.

### Content Server

```bash
# Serve a package
./scripts/run_server.sh /path/to/package.dupkg

# Idle mode (no package)
./scripts/run_server.sh --idle

# Resume previous session
./scripts/run_server.sh /path/to/package.dupkg --resume
```

### Update Client

```bash
# Interactive mode (connect to DU master)
./scripts/run_client.sh

# Deploy a package
./scripts/run_client.sh package /content/files
```

### CLI Tool

```bash
# Deploy OTA package
./scripts/run_tool.sh --deploy /path/to/dupkg_dir

# With log output
./scripts/run_tool.sh --deploy /path/to/dupkg_dir --logging /tmp/du.log
```

## Logs

All run logs are saved under `logs/` with timestamps:
- `logs/client_YYYYMMDD_HHMMSS.log`
- `logs/server_YYYYMMDD_HHMMSS.log`
- `logs/tool_YYYYMMDD_HHMMSS.log`
