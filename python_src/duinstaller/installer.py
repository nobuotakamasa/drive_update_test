"""
DU Installer state machine, mapped from src/duinstaller/duinstaller.c.

C -> Python mappings:
  SAMPLE_INSTALLER struct     -> SampleInstaller dataclass
  installerInit()             -> installer_init()
  installerRun()              -> installer_run()
  installerStateIdle()        -> _state_idle()
  installerStateInstalling()  -> _state_installing()
  installerStatePendingRL()   -> _state_pending_rl()
  installerStatePendingCommit -> _state_pending_commit()
  installerStateError()       -> _state_error()
  setState()                  -> _set_state()

Threading: threading.Condition replaces pthread_cond_t + pthread_mutex_t.
DU Link SDK calls are replaced with DuLinkStub.
"""

import logging
import os
import threading
from dataclasses import dataclass, field
from pathlib import Path

from ..common.installer_common import (
    DuInstaller,
    InstallerResult,
    InstallerState,
)
from ..common.run_level import RunLevel
from .dulink_stub import DuLinkStub
from .installer_helpers import (
    CMD_CLEAR_ERROR,
    CMD_COMMIT,
    CMD_DEPLOY,
    exec_commit_cmd,
    exec_deploy_cmd,
    parse_cmd,
)

logger = logging.getLogger(__name__)

SAMPLE_VERSION       = "1"
INSTALL_PATH_MAX_LEN = 512


@dataclass
class SampleInstaller:
    """
    Python equivalent of the SAMPLE_INSTALLER struct.
    DuInstaller owns the single Condition used by all callbacks.
    """
    du_installer: DuInstaller = field(default_factory=DuInstaller)

    b_update_failed: bool  = False
    cmd_id: int            = 0

    file_path_buf:        str = ""
    savename_buf:         str = ""
    staged_file_path_buf: str = ""
    install_dir:          str = ""
    installer_cmp_buf:    str = ""

    # DU Link stub (injected or defaulted)
    dulink: DuLinkStub = field(default_factory=DuLinkStub)

    def __post_init__(self) -> None:
        # Ensure DuInstaller threading primitives are ready
        if self.du_installer._lock is None:  # noqa: SLF001
            self.du_installer.__post_init__()


# ------------------------------------------------------------------
# State machine helpers
# ------------------------------------------------------------------

def _set_state(inst: SampleInstaller, state: InstallerState) -> bool:
    """
    Perform state transition side-effects then update state.
    Mirrors setState() in duinstaller.c.

    Returns True on success.
    """
    di = inst.du_installer

    if state == InstallerState.IDLE:
        result = InstallerResult.FAILED if inst.b_update_failed else InstallerResult.SUCCESS
        di.set_last_deployed_info(result, inst.file_path_buf, None)

        with di.cond:
            di.installer_cmd = ""
            di.b_cmd_pending = False

        inst.file_path_buf        = ""
        inst.savename_buf         = ""
        inst.staged_file_path_buf = ""
        inst.install_dir          = ""
        inst.b_update_failed      = False

    elif state == InstallerState.PENDING_COMMIT:
        # Reset so installerCmdCB can accept the next command (commit)
        with di.cond:
            di.installer_cmd = ""
            di.b_cmd_pending = False

    elif state == InstallerState.ERROR:
        inst.b_update_failed = True
        # Allow clear_error to be accepted
        with di.cond:
            di.installer_cmd = ""
            di.b_cmd_pending = False

    elif state in (InstallerState.INSTALLING, InstallerState.PENDING_RL):
        pass  # No side-effects

    else:
        logger.error("Unexpected state %s", state)
        return False

    di.set_state(state)
    return True


def _state_idle(inst: SampleInstaller) -> bool:
    """
    Wait for a command, then transition to INSTALLING or PENDING_RL.
    Mirrors installerStateIdle().
    """
    logger.info("Installer IDLE state")
    di = inst.du_installer

    with di.cond:
        di.cond.wait_for(lambda: di.b_cmd_pending)
        cmd_id, file_path, savename = parse_cmd(di.installer_cmd)
        needs_rl = di.current_rl < RunLevel.BG_APPLY

    if cmd_id is None:
        logger.error("Invalid cmd: parse failed")
        return False

    inst.cmd_id       = cmd_id
    inst.file_path_buf = file_path
    inst.savename_buf  = savename

    if not needs_rl:
        return _set_state(inst, InstallerState.INSTALLING)
    else:
        di.set_pending_rl(RunLevel.BG_APPLY)
        return _set_state(inst, InstallerState.PENDING_RL)


def _state_pending_rl(inst: SampleInstaller) -> bool:
    """
    Wait until current_rl >= pending_rl, then move to INSTALLING.
    Mirrors installerStatePendingRL().
    """
    logger.info("Installer PENDING_RL state")
    di = inst.du_installer

    with di.cond:
        while (
            di.pending_rl < RunLevel.INVALID
            and di.current_rl < di.pending_rl
        ):
            logger.info("Waiting on runlevel %s", di.pending_rl.name)
            di.cond.wait()

    di.set_pending_rl(RunLevel.INVALID)
    return _set_state(inst, InstallerState.INSTALLING)


def _state_installing(inst: SampleInstaller) -> bool:
    """
    Execute the deploy command.
    Mirrors installerStateInstalling().
    """
    logger.info("Installer INSTALLING state")

    if inst.cmd_id != CMD_DEPLOY:
        logger.error("Unexpected cmd %d in INSTALLING state", inst.cmd_id)
        return False

    logger.info("Executing deploy cmd")
    staged_out = [""]
    ok = exec_deploy_cmd(
        inst.file_path_buf,
        inst.savename_buf,
        inst.install_dir,
        inst.dulink,
        staged_out,
    )
    inst.staged_file_path_buf = staged_out[0]
    if not ok:
        logger.error("Deploy cmd failed")
        return False

    return _set_state(inst, InstallerState.PENDING_COMMIT)


def _state_pending_commit(inst: SampleInstaller) -> bool:
    """
    Wait for a commit command, then commit the staged file.
    Mirrors installerStatePendingCommit().
    """
    logger.info("Installer PENDING_COMMIT state")
    di = inst.du_installer

    with di.cond:
        di.cond.wait_for(lambda: di.b_cmd_pending)
        cmd_id, file_path, savename = parse_cmd(di.installer_cmd)

    if cmd_id != CMD_COMMIT:
        logger.error("Expected commit cmd, got %s", cmd_id)
        return False

    logger.info("Committing previous deployment")
    if not exec_commit_cmd(inst.staged_file_path_buf):
        logger.error("Commit command failed")
        return False

    return _set_state(inst, InstallerState.IDLE)


def _state_error(inst: SampleInstaller) -> bool:
    """
    Wait for a clear_error command, then return to IDLE.
    Mirrors installerStateError().
    """
    logger.info("Installer ERROR state")
    di = inst.du_installer

    with di.cond:
        while not di.b_cmd_pending:
            logger.info("Error occurred: waiting for clear_error cmd")
            di.cond.wait()
        cmd_id, _, _ = parse_cmd(di.installer_cmd)

    if cmd_id != CMD_CLEAR_ERROR:
        logger.error("Error must be cleared before new command can be executed")
        return False

    logger.info("Clearing ERROR state")
    # Remove staged file if it exists
    if inst.staged_file_path_buf:
        try:
            os.unlink(inst.staged_file_path_buf)
        except OSError:
            pass  # File may not exist; ignore

    return _set_state(inst, InstallerState.IDLE)


# ------------------------------------------------------------------
# Public API
# ------------------------------------------------------------------

def installer_init(inst: SampleInstaller) -> bool:
    """
    Initialise installer data structures.
    Mirrors installerInit() in duinstaller.c.

    DU Link SDK calls are replaced with stub log lines.
    """
    from ..common.installer_common import DuInstaller
    inst.du_installer = DuInstaller()

    # Set installation directory to current working directory
    inst.install_dir = os.getcwd()
    logger.info("Install directory: %s", inst.install_dir)

    # DU Link SDK stubs
    logger.info("[STUB] getPluginConnInfo(plugin)")
    logger.info("[STUB] dulinkInit(plugin, ...)")
    logger.info("[STUB] exportDULinkNodes(...)")
    logger.info("[STUB] dulinkOpen(...)")
    logger.info("[STUB] registerToMaster(...)")

    _set_state(inst, InstallerState.IDLE)
    return True


def installer_run(inst: SampleInstaller) -> None:
    """
    Main state machine loop.
    Mirrors installerRun() in duinstaller.c.
    Runs indefinitely until a fatal error or KeyboardInterrupt.
    """
    _DISPATCH = {
        InstallerState.IDLE:            _state_idle,
        InstallerState.INSTALLING:      _state_installing,
        InstallerState.PENDING_RL:      _state_pending_rl,
        InstallerState.PENDING_COMMIT:  _state_pending_commit,
        InstallerState.ERROR:           _state_error,
    }

    while True:
        state = inst.du_installer.state
        handler = _DISPATCH.get(state)
        if handler is None:
            logger.error("Unexpected state %s", state)
            _set_state(inst, InstallerState.ERROR)
            continue

        ok = handler(inst)
        if not ok:
            _set_state(inst, InstallerState.ERROR)
