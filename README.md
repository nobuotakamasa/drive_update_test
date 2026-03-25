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

## Component Details

### content_server

**Role:** Content provider plugin that serves `.dupkg` package files over DU Link IPC transport. It registers with DU Master so that other plugins (e.g. `driveupdate`) can discover and pull files from it.

**How it works:**

1. Reads `list_of_files.json` inside the `.dupkg` directory to determine which files/directories to expose.
2. Exports each entry as a DU Link node under `/content/files/`.
3. Responds to read requests from DU Master by streaming the local file contents over DU Link.

**CLI (interactive, runs after init):**

| Command | Description |
|---------|-------------|
| `serve [<dupkg>]` | Start serving the given DUPKG (or resume from context store) |
| `stop` | Stop serving; unlink all exported nodes |
| `status` | Show serving status, context store path, cached DUPKG path |
| `set <dupkg>` | Set DUPKG path locally (cache only, does not serve) |
| `save <dupkg>` | Persist DUPKG path to context store |
| `clear` | Remove DUPKG path from context store |
| `quit` | Exit |

**Startup options:**

```
content_server <dupkg_path>   # Serve the given DUPKG immediately
content_server                # Resume previously served DUPKG from context store
content_server -r             # Same as above (explicit resume)
content_server -i             # Start in idle mode (no package)
content_server -c             # Clear context store and exit
```

---

### driveupdate

**Role:** Update client that connects to DU Master via DU Link and DUCC (Command & Control API), triggers package deployment, and monitors update progress.

**How it works:**

1. Connects to DU Master over DU Link and DUCC.
2. Sends a `deploy metadata=<path>/du_master.json content_root=<path>` command to DU Master.
3. Monitors `DUCC_Get_Current_Status()` in a background thread; automatically raises the run level when DU Master requests it.
4. Exits when state reaches `UP_TO_DATE` or `UPDATE_FAILED`.

**Sub-commands (interactive or single-shot via CLI flag):**

| Command | Description |
|---------|-------------|
| `package <dulink_path> [options]` | Trigger deployment of the DUPKG at the given DU Link path |
| `abort` | Force-abort an in-progress deployment |
| `query_bootchain` | Print which boot chain (A/B) each Tegra is on |
| `part_ver` | Print all partition versions from PVIT |
| `get_runlevel` | Print current run level |
| `set_runlevel <0-6>` | Request a specific run level |
| `print_status on\|off` | Enable/disable periodic status printing |
| `du_read <node> [dst]` | Read a DU Link node (optionally save to file) |
| `du_write <data> <node>` | Write data to a DU Link node |
| `du_list <dir>` | List children of a DU Link directory |
| `shell <cmd>` | Execute a shell command |
| `exit` | Exit the interactive shell |

**`package` options:**

| Option | Description |
|--------|-------------|
| `-t, --auth` | Force authentication check (`auth_conf.json` must be present) |
| `-u, --push` | Push mode: client actively writes images to storage partitions |
| `-d, --delta` | Delta-push mode using bsdiff |
| `-c, --target-controlled` | Prompt user before raising run level |

**Example:**

```
# Deploy a package already exposed at /content/files
driveupdate package /content/files
```

---

### du_cli

**Role:** Low-level CLI tool that communicates directly with the DU Server (DUS) daemon to flash partition payloads. Unlike `driveupdate`, it bypasses DU Master and writes payloads to the inactive boot chain via the `duscExecute` API.

**How it works:**

1. Scans `<dupkg_dir>` for numbered payload files (`1.l1pt`, `1.pt`, `1.patch`, `2.pt`, …).
2. Flashes L1 partition tables (`.l1pt`) to the persistent chain, and regular payloads (`.patch`) to the inactive boot chain.
3. Reloads the partition table after each PT flash.
4. Reports progress (percentage, speed, elapsed time) to stdout.
5. Optionally retrieves and saves the DU Server log after completion.

**Options:**

| Option | Description |
|--------|-------------|
| `-d <dupkg_path>` | Directory containing the OTA payload files |
| `-l <log_path>` | Optional path to save the DU Server log |
| `-h` | Print help |

**Example:**

```
du_cli -d /path/to/dupkg
du_cli -d /path/to/dupkg -l /tmp/dus.log
```

---

### duinstaller

**Role:** Installer plugin that receives `deploy` and `commit` commands from DU Master over DU Link and writes update files to the local filesystem atomically.

**How it works:**

- `deploy path=<DU_LINK_PATH> savename=<name>`: reads the file from DU Link at `path` and writes it to `<installDir>/<name>.staged`.
- `commit`: atomically renames `<name>.staged` → `<name>`.
- `clear_error`: deletes the `.staged` file (if any) and returns to IDLE.

**State machine:**

```
IDLE ──[deploy]──> PENDING_RL ──[RL ≥ RL_BG_APPLY]──> INSTALLING
                                                             |
                                                       [deploy done]
                                                             v
IDLE <──[commit done]────────────────────── PENDING_COMMIT

Any state ──[error]──> ERROR ──[clear_error]──> IDLE
```

**State descriptions:**

| State | Description |
|-------|-------------|
| `IDLE` | Waiting for a `deploy` command |
| `PENDING_RL` | Waiting for run level to reach `RL_BG_APPLY` |
| `INSTALLING` | Executing deploy: copying file from DU Link to `<name>.staged` |
| `PENDING_COMMIT` | Deploy done; waiting for `commit` command |
| `ERROR` | A command failed; only `clear_error` accepted |

**DU Link nodes exported:**

| Node | Description |
|------|-------------|
| `plugin-type` | `"installer"` |
| `cmd` | Write commands: `deploy path=… savename=…`, `commit`, `clear_error` |
| `state` | Current state string |
| `progress` | Progress string during install |
| `current_rl` / `pending_rl` | Run level tracking |
| `ver/installer` | Installer version string |
| `last_deployed/metadata` | Path of last deployed file |
| `last_deployed/result` | `"success"` or `"failed"` |

---

## Creating a DUPKG

`.dupkg` is the directory-based update package format used by DRIVE Update V3.
Use the **`dupkg` tool** (Python tool bundled with the SDK) to generate a package from a template.

### Install dupkg Tool (once, inside Docker)

```bash
source env.sh
docker exec -it ${DOCKER_CONTAINER} bash

# Install dependencies
pip3 install jinja2 tabulate --break-system-packages

# Install dupkg tool
cd /drive/drive-foundation/tools/driveupdate/dupkg
pip3 install -e . --break-system-packages

# Verify
dupkg --help
```

### List Available Templates

```bash
# List all available templates
dupkg lstemplate

# Show required variables for a specific template
dupkg lsin --template dupkg_sample_template
```

### Creating a Package for duinstaller (`dupkg_sample_template`)

This project's `duinstaller` is a generic file installer that receives a file and saves it to an arbitrary path.
Use `dupkg_sample_template` to generate a compatible package.

#### Required Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `SAMPLE_SRC` | Directory containing the file to install | `/tmp/myfiles` |
| `SAMPLE_NAME` | Filename to install | `update.bin` |
| `DESTINATION_SRC` | Install path on Thor (saved filename) | `update.bin` |

#### Steps

```bash
# 1. Prepare the file to install
mkdir -p /tmp/myfiles
echo "hello update" > /tmp/myfiles/update.bin

# 2. Generate the package (run inside Docker container)
dupkg gen \
  --template dupkg_sample_template \
  --in SAMPLE_SRC=/tmp/myfiles \
       SAMPLE_NAME=update.bin \
       DESTINATION_SRC=update.bin \
  --out /tmp/my_dupkg

# 3. Check the output
ls /tmp/my_dupkg/
```

Generated directory structure:

```
my_dupkg/
├── du_master.json        # Orchestration definition for DU Master
├── list_of_files.json    # File list published by content_server
└── update.bin            # File to install
```

#### list_of_files.json Structure

`content_server` uses this file to determine which nodes to export over DU Link:

```json
[
  {"name": "./",              "type": "dir"},
  {"name": "./du_master.json","type": "file"},
  {"name": "./list_of_files.json", "type": "file"},
  {"name": "./update.bin",    "type": "file"}
]
```

#### du_master.json Structure (generated by sample template)

```json
{
  "metaVersion": 3,
  "restoreSequence": ["*/ddu", "*/du-client", "*/content", "/auth", "/tii"],
  "actions": [
    {
      "actionId": 0,
      "pluginType": "installer",
      "pluginPath": "/plugin",
      "startOnState": ["IDLE"],
      "exitOnStateTransitionTo": ["PENDING_COMMIT"],
      "failOnStateTransitionTo": ["ERROR"],
      "cmd": "deploy path=$CONTENT_ROOT$/update.bin savename=update.bin"
    },
    {
      "actionId": 1,
      "gatedBy": [0],
      "pluginType": "installer",
      "pluginPath": "/plugin",
      "startOnState": ["PENDING_COMMIT"],
      "exitOnStateTransitionTo": ["IDLE"],
      "failOnStateTransitionTo": ["ERROR"],
      "cmd": "commit"
    }
  ]
}
```

> **`$CONTENT_ROOT$`** is a dynamic variable expanded to the DU Link `/content/files` path at runtime.

### Transfer the Package to Thor and Serve with content_server

```bash
# Copy from Docker to host
docker cp ${DOCKER_CONTAINER}:/tmp/my_dupkg ./my_dupkg

# Transfer to Thor
scp -r ./my_dupkg ${THOR}:/home/autoware/my_dupkg

# Serve with content_server
./driveupdate_test/scripts/run_server.sh /home/autoware/my_dupkg
```

### Template Reference

| Template | Use case |
|----------|----------|
| `dupkg_sample_template` | For `duinstaller` (sample plugin). Best for file deploy verification |
| `dupkg_single_tegra_template` | OTA for a single Tegra (requires BSP image files) |
| `dupkg_dual_tegra_template` | OTA for dual Tegra (TA + TB) configuration |
| `dupkg_push_template` | Push mode update (used with driveupdate `--push` option) |
| `dupkg_clone_template` | Clone active chain to inactive chain |
| `dupkg_recovery_template` | Chain C recovery update |

> Reference: [DRIVE Update V3 Developer Guide](https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-linux-sdk/embedded-software-components/DRIVE_AGX_SoC/DRIVE_Update_V3/DRIVE_Update_V3_Developer_Guide.html)

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

Each script automatically:

1. Transfers the binary in `bin/` to Thor's `${THOR_DEPLOY_DIR}` via `scp`
2. Runs the binary on Thor via `ssh`
3. Saves stdout/stderr to `driveupdate_test/logs/` with a timestamp

> **Prerequisite:** Binaries must be present in `bin/` (run `./build.sh` first).

---

### Standard Workflow (DU Link-based update)

Standard DRIVE Update V3 flow using `content_server` and `driveupdate` together.
Uses **2 terminals**.

#### Step 1: Start Content Server (Terminal A)

```bash
source env.sh
# Serve the .dupkg on Thor (e.g. located at /home/autoware/my.dupkg)
./driveupdate_test/scripts/run_server.sh /home/autoware/my.dupkg
```

After startup, the `CONTENT-SERVER>` prompt appears and waits for input.

```
=== content_server: deploy and run ===
Thor: thor
Deploy path: /home/autoware/epl_test/content_server
Binary deployed to Thor.
Running: /home/autoware/epl_test/content_server /home/autoware/my.dupkg
CONTENT-SERVER[/home/autoware/my.dupkg]>
```

#### Step 2: Run Update Client (Terminal B)

```bash
source env.sh
# Launch in interactive mode
./driveupdate_test/scripts/run_client.sh

# Or specify a package deploy directly at startup
./driveupdate_test/scripts/run_client.sh package /content/files
```

After startup, it connects to DU Master and the `(client)` prompt appears.

```
=== driveupdate: deploy and run ===
Thor: thor
Deploy path: /home/autoware/epl_test/driveupdate
Binary deployed to Thor.
Running: /home/autoware/epl_test/driveupdate
(client)
```

To trigger a deploy manually from the prompt:

```
(client) package /content/files
```

When the update completes, `[Result]Deploy OK!` or `[Result]Deploy FAILURE!` is displayed.

#### Step 3: Check Logs

```bash
# Latest server log
ls -lt driveupdate_test/logs/server_*.log | head -1

# Latest client log
ls -lt driveupdate_test/logs/client_*.log | head -1
```

---

### Content Server Options

```bash
# Serve the given DUPKG immediately
./driveupdate_test/scripts/run_server.sh /home/autoware/my.dupkg

# Start in idle mode (no package; use `serve` from the CLI prompt later)
./driveupdate_test/scripts/run_server.sh --idle

# Resume the previously served DUPKG from the context store
./driveupdate_test/scripts/run_server.sh --resume

# Clear the context store and exit
./driveupdate_test/scripts/run_server.sh --clear
```

Interactive CLI (available at the prompt after startup):

```
CONTENT-SERVER[/path]> serve /home/autoware/other.dupkg  # switch to a different DUPKG
CONTENT-SERVER[/path]> stop                               # stop serving
CONTENT-SERVER[/path]> status                             # show status
CONTENT-SERVER[/path]> quit                               # exit
```

---

### du_cli (Low-level OTA)

Use this when writing directly to DU Server without going through DU Master.

```bash
source env.sh

# Deploy an OTA package
./driveupdate_test/scripts/run_tool.sh -d /home/autoware/ota_pkg

# Deploy with DU Server log saved to file
./driveupdate_test/scripts/run_tool.sh -d /home/autoware/ota_pkg -l /tmp/du.log
```

---

### Script Internals

Each script performs the following:

```
[host]                            [Thor]
  |                                  |
  |-- scp bin/<binary> -----------> mkdir -p ${THOR_DEPLOY_DIR}
  |                                  |  receive: ${THOR_DEPLOY_DIR}/<binary>
  |-- ssh <binary> [args] ---------> execute
  |<-- stdout/stderr -------------- output
  |
  tee driveupdate_test/logs/<name>_YYYYMMDD_HHMMSS.log
```

| Script | Binary transferred | Log file |
|--------|--------------------|----------|
| `run_server.sh` | `bin/content_server` | `logs/server_*.log` |
| `run_client.sh` | `bin/driveupdate` | `logs/client_*.log` |
| `run_tool.sh` | `bin/du_cli` | `logs/tool_*.log` |

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
│   ├── driveupdate/           # Update client
│   ├── du-cli/                # CLI tool
│   ├── duinstaller/           # Installer plugin
│   │   ├── duinstaller.c/h            # State machine + DU Link node table
│   │   ├── duinstaller_helpers.c/h    # deploy / commit command execution
│   │   └── duinstaller_main.c         # Entry point
│   ├── plugin_common/         # Shared installer callbacks (installerCmdCB etc.)
│   ├── bhc_plugin/            # BHC API shared library
│   ├── remote_content_provider/  # Remote content provider (optional, needs libcurl)
│   └── ffu_update/            # FFU update tool (optional, needs libnvmnand_private)
├── python_src/                # Python re-implementation of C components (Phase 1-4)
│   ├── common/
│   │   ├── run_level.py               # RunLevel enum (DORMANT / BG_APPLY / INVALID)
│   │   ├── installer_common.py        # InstallerState/Cmd/Result + DuInstaller dataclass
│   │   └── content_provider_common.py # ContentState/Cmd + ContentProvider dataclass
│   ├── du_cli/
│   │   ├── du_cli.py                  # Payload parsing, deploy loop, progress display
│   │   └── dus_client_stub.py         # duscInit/Execute/Deinit stubs (log only)
│   ├── content_server/
│   │   ├── content_server.py          # CLI loop (cmd.Cmd), state management
│   │   └── file_list.py               # list_of_files.json parser
│   └── duinstaller/
│       ├── installer.py               # State machine + main loop
│       ├── installer_helpers.py       # deploy / commit logic
│       └── dulink_stub.py             # dulinkGetSize/ReadRetry/TriggerNotify stubs
└── driveupdate_test/
    ├── scripts/
    │   ├── run_server.sh      # Deploy and run content_server on Thor
    │   ├── run_client.sh      # Deploy and run driveupdate on Thor
    │   └── run_tool.sh        # Deploy and run du_cli on Thor
    └── logs/                  # Run logs with timestamps (gitignored)
```

---

## Python Implementation (`python_src/`)

Python re-implementation of the C components (Phases 1–4). All DU Link / DU Master
SDK API calls are replaced with log-only stubs so that the logic can run on any host
without a DRIVE OS target.

### Scope

| Phase | Module | C source | SDK stub |
|-------|--------|----------|----------|
| 1 | `python_src.du_cli` | `src/du-cli/du_cli.c` | `duscInit/Execute/Deinit` |
| 2 | `python_src.common` | `src/plugin_common/` | `dulinkTriggerNotify` |
| 3 | `python_src.content_server` | `src/content_server/` | `dulinkExport*/Unlink*`, `registerToMaster` |
| 4 | `python_src.duinstaller` | `src/duinstaller/` | `dulinkGetSize`, `dulinkReadRetry` |

### Usage

```bash
# du_cli: show help (-d not provided → skip deploy, stubs only)
python3 -m python_src.du_cli -h
python3 -m python_src.du_cli -d /path/to/dupkg

# content_server: interactive CLI
python3 -m python_src.content_server /path/to/my.dupkg
python3 -m python_src.content_server --idle

# duinstaller: runs state machine (stubs; blocks waiting for write_cmd())
python3 -m python_src.duinstaller
```

### Design Notes

- **Stubs**: SDK calls are isolated in `*_stub.py` classes that emit `[STUB]` log lines.
- **Threading**: `threading.Condition` replaces `pthread_mutex_t + pthread_cond_t`.
- **Binary parsing**: `struct.unpack` decodes the `DUS_PAYLOAD_HEADER` C struct.
- **Atomic rename**: `os.rename()` on POSIX guarantees atomicity within the same filesystem.
- **CLI**: `cmd.Cmd` implements the `content_server` interactive shell.
