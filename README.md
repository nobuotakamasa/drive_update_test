# DRIVE Update V3 Test

Verification project for DRIVE Update V3 basic operations (package distribution and update application).

## Architecture

```
[x86 host]                          [Thor target (aarch64)]

  build.sh                            DU Master (system daemon)
  (Docker cross-compile)                  |
       |                                  | DU Link (IPC)
       v                            ------+------+----------+
     bin/                           |            |          |
  ┌──────────────┐             content_server  driveupdate  duinstaller
  │content_server│  ──scp──>   (serves .dupkg   driveupdate  (installer
  │driveupdate   │             over DU Link)    (update       plugin)
  │  update      │                              client)
  │du_cli        │  ──scp──>   du_cli
  │duinstaller   │             (CLI operations)
  └──────────────┘
```

### Components

| Binary | Source | Role |
|--------|--------|------|
| `content_server` | `src/content_server/` | Serves update packages (.dupkg) over DU Link transport |
| `driveupdate` | `src/driveupdate/` | Update client - fetches content and drives the update flow |
| `du_cli` | `src/du-cli/` | CLI tool for package operations and status queries |
| `duinstaller` | `src/duinstaller/` | Installer plugin - receives deploy/commit commands from DU Master and writes files |

### Update Flow

```
1. content_server starts and exposes package contents over DU Link
2. driveupdate connects to DU Master, discovers content_server
3. DU Master sends "deploy path=<DU_LINK_PATH>" to duinstaller
4. duinstaller reads the file from DU Link and saves it as <name>.staged
5. DU Master sends "commit" to duinstaller
6. duinstaller renames <name>.staged -> <name>  (atomic file replacement)
```

### duinstaller State Machine

```
IDLE ──[deploy cmd]──> PENDING_RL ──[RL reached]──> INSTALLING
                                                         |
                                                    [deploy done]
                                                         v
IDLE <──[commit done]── PENDING_COMMIT <────────────────

Any state ──[error]──> ERROR ──[clear_error]──> IDLE
```

`duinstaller` writes exactly one file per deploy/commit cycle:
- **deploy**: reads from `path=<DU_LINK_PATH>`, writes `<installDir>/<savename>.staged`
- **commit**: renames `.staged` -> `<savename>` (atomic)
- **clear_error**: removes `.staged` if it exists

---

## Prerequisites

### Host

- Docker container `drive-sdk` running (DRIVE OS SDK image)
- SSH access to Thor configured in `~/.ssh/config` as `thor`

Start the container if not running:

```bash
source env.sh
docker ps | grep ${DOCKER_CONTAINER} || \
docker run -d --rm -it --privileged --net=host \
    -v /dev:/dev \
    -v $HOME/driveos_ws:/home/nvidia \
    --name ${DOCKER_CONTAINER} \
    nvcr.io/drive/driveos-sdk/drive-agx-linux-nsr-aarch64-sdk-build-x86:7.0.3.0-0010
```

### Target (Thor)

- DRIVE OS running
- DU Master daemon active
- Deploy directory accessible: `${THOR_DEPLOY_DIR}` (default: `/home/autoware/epl_test`)

---

## Environment

All environment variables are managed in `env.sh`. Source it before any operation:

```bash
source env.sh
```

| Variable | Default | Description |
|----------|---------|-------------|
| `DOCKER_CONTAINER` | `drive-sdk` | Docker container name for cross-compilation |
| `THOR` | `thor` | SSH host name for the target |
| `THOR_DEPLOY_DIR` | `/home/autoware/epl_test` | Deploy directory on Thor |

---

## Build

Cross-compiles all binaries for aarch64 inside the Docker container and copies them to `bin/`:

```bash
source env.sh
./build.sh
```

Output:

```
bin/
├── content_server
├── driveupdate
├── du_cli
├── duinstaller
├── libnvdubhc_api.so
├── ffu_update    # optional, requires libnvmnand_private
└── remote_content_provider  # optional, requires libcurl
```

All mandatory binaries are AArch64 ELF. Optional targets print a warning if their dependencies are absent.

---

## Run

Each script deploys the binary to Thor via SCP, then runs it via SSH. Logs are saved to `driveupdate_test/logs/`.

### 1. Content Server

Serves an update package over DU Link transport.

```bash
# Serve a package
./driveupdate_test/scripts/run_server.sh <dupkg_path_on_thor>

# Start in idle mode (no package)
./driveupdate_test/scripts/run_server.sh --idle

# Resume a previous session
./driveupdate_test/scripts/run_server.sh <dupkg_path_on_thor> --resume
```

### 2. Update Client

Connects to DU Master and drives the content fetch and apply flow.

```bash
# Basic run
./driveupdate_test/scripts/run_client.sh

# Pass extra arguments
./driveupdate_test/scripts/run_client.sh package /content/files
```

### 3. CLI Tool

Sends commands to DU Master for package operations and status queries.

```bash
# Deploy an OTA package
./driveupdate_test/scripts/run_tool.sh --deploy <dupkg_dir_on_thor>

# With log output
./driveupdate_test/scripts/run_tool.sh --deploy <dupkg_dir_on_thor> --logging /tmp/du.log
```

---

## Project Structure

```
.
├── env.sh                     # Environment variables
├── build.sh                   # Cross-compilation script (host → Docker → bin/)
├── bin/                       # Built aarch64 binaries (gitignored)
├── src/
│   ├── common.mk              # Shared toolchain/flag definitions
│   ├── content_server/        # Update package server
│   ├── driveupdate/     # Update client
│   ├── du-cli/                # CLI tool
│   ├── duinstaller/           # Installer plugin
│   │   ├── duinstaller.c/h            # State machine + DU Link node table
│   │   ├── duinstaller_helpers.c/h    # deploy / commit command execution
│   │   └── duinstaller_main.c         # Entry point
│   ├── plugin_common/         # Shared installer callbacks (installerCmdCB etc.)
│   ├── bhc_plugin/            # BHC API shared library
│   ├── remote_content_provider/  # Remote content provider (optional, needs libcurl)
│   └── ffu_update/     # FFU update tool (optional, needs libnvmnand_private)
└── driveupdate_test/
    ├── scripts/
    │   ├── run_server.sh      # Deploy and run content_server on Thor
    │   ├── run_client.sh      # Deploy and run driveupdate on Thor
    │   └── run_tool.sh        # Deploy and run du_cli on Thor
    └── logs/                  # Run logs with timestamps (gitignored)
```
