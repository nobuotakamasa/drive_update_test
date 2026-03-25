"""
Common installer data structures mapped from plugin_common/duinstaller_common.c/h.

C -> Python mappings:
  INSTALLER_STATE enum        -> InstallerState(Enum)
  INSTALLER_CMD enum          -> InstallerCmd(Enum)
  INSTALLER_RESULT enum       -> InstallerResult(Enum)
  DU_INSTALLER struct         -> DuInstaller dataclass
    pthread_mutex_t           -> threading.Lock
    pthread_cond_t            -> threading.Condition
  setInstallerState()         -> DuInstaller.set_state()
  setInstallerProgress()      -> DuInstaller.set_progress()
  setInstallerCurrentRl()     -> DuInstaller.set_current_rl()
  setInstallerPendingRl()     -> DuInstaller.set_pending_rl()
  installerCmdCB() write      -> DuInstaller.write_cmd()
  dulinkTriggerNotify         -> callback stub (log only)
"""

import logging
import threading
from dataclasses import dataclass, field
from enum import Enum

from .run_level import RunLevel

logger = logging.getLogger(__name__)

MAX_CMD_LEN = 512
MAX_VER_LENGTH = 256

INSTALLER_STATES_TABLE = [
    "IDLE",
    "INSTALLING",
    "PENDING_RL",
    "PENDING_RESTART",
    "PENDING_COMMIT",
    "REVERTING",
    "ERROR",
    "RESTORE_CTX",
]

INSTALLER_CMDS_TABLE = [
    "deploy",
    "commit",
    "revert",
    "abort",
    "clear_error",
]

INSTALLER_RESULTS_TABLE = [
    "SUCCESS",
    "FAILED",
    "ABORTED",
]


class InstallerState(Enum):
    IDLE            = 0
    INSTALLING      = 1
    PENDING_RL      = 2
    PENDING_RESTART = 3
    PENDING_COMMIT  = 4
    REVERTING       = 5
    ERROR           = 6
    RESTORE_CTX     = 7


class InstallerCmd(Enum):
    DEPLOY      = 0
    COMMIT      = 1
    REVERT      = 2
    ABORT       = 3
    CLEAR_ERROR = 4


class InstallerResult(Enum):
    SUCCESS = 0
    FAILED  = 1
    ABORTED = 2


@dataclass
class DuInstaller:
    """
    Python equivalent of the C DU_INSTALLER struct.
    All fields protected by a single threading.Lock; callers wait on
    the associated threading.Condition.
    """
    # Current state
    state: InstallerState = InstallerState.IDLE
    state_str: str = "IDLE"

    # Progress 0-100
    progress: int = 0
    progress_str: str = "0"

    # Last deployed info
    last_deployed_metadata: str = ""
    last_deployed_result: str = ""
    last_deployed_version: str = ""

    # Run levels
    current_rl: RunLevel = RunLevel.DORMANT
    pending_rl: RunLevel = RunLevel.INVALID
    requested_rl: RunLevel = RunLevel.INVALID

    # Pending flags
    b_resume_pending: bool = False
    b_cmd_pending: bool = False

    # Persistent context path
    persist_ctx_path: str = ""

    # Current command string
    installer_cmd: str = ""

    # Synchronization primitives (created in __post_init__)
    _lock: threading.Lock = field(default=None, init=False, repr=False, compare=False)
    _cond: threading.Condition = field(default=None, init=False, repr=False, compare=False)

    def __post_init__(self) -> None:
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)

    @property
    def lock(self) -> threading.Lock:
        return self._lock

    @property
    def cond(self) -> threading.Condition:
        return self._cond

    # ------------------------------------------------------------------
    # Setter helpers (mirror C setInstaller* functions)
    # dulinkTriggerNotify is stubbed as a log line.
    # ------------------------------------------------------------------

    def set_state(self, state: InstallerState) -> None:
        """Set installer state and emit stub notify."""
        with self._lock:
            old_state = self.state
            self.state = state
            self.state_str = INSTALLER_STATES_TABLE[state.value]
        if state != old_state:
            logger.info("[STUB] dulinkTriggerNotify(state, %s)", self.state_str)

    def set_progress(self, progress: int) -> None:
        """Set progress 0-100 and emit stub notify on change."""
        if not (0 <= progress <= 100):
            raise ValueError(f"Progress must be 0-100, got {progress}")
        with self._lock:
            old = self.progress
            self.progress = progress
            self.progress_str = str(progress)
        if progress != old:
            logger.info("[STUB] dulinkTriggerNotify(progress, %s)", self.progress_str)

    def set_current_rl(self, rl: RunLevel) -> None:
        """Set current run level and emit stub notify on change."""
        with self._lock:
            old = self.current_rl
            self.current_rl = rl
        if rl != old:
            logger.info("[STUB] dulinkTriggerNotify(current_rl, %s)", rl.name)

    def set_pending_rl(self, rl: RunLevel) -> None:
        """
        Set pending run level using the same merge logic as the C version,
        and emit stub notify on change.
        """
        with self._lock:
            old = self.pending_rl
            if rl >= RunLevel.INVALID:
                # Clear pending RL
                self.pending_rl = RunLevel.INVALID
            elif old >= RunLevel.INVALID:
                # No RL was pending; accept new one
                self.pending_rl = rl
            elif rl <= old:
                # New requirement is lower (or equal); tighten it
                self.pending_rl = rl
            # else: already waiting on lower RL; keep it
            new = self.pending_rl
        if new != old:
            logger.info("[STUB] dulinkTriggerNotify(pending_rl, %s)", rl.name)

    def write_cmd(self, cmd: str) -> bool:
        """
        Write a new command string (mirror of installerCmdCB WRITE path).
        Returns False if a command is already pending.
        """
        if len(cmd) >= MAX_CMD_LEN:
            logger.error("Command exceeds max length")
            return False
        with self._cond:
            if self.b_cmd_pending:
                logger.error("Installer is pending cmd, please retry later")
                return False
            self.installer_cmd = cmd
            self.b_cmd_pending = True
            self._cond.notify_all()
        return True

    def set_last_deployed_info(
        self,
        result: InstallerResult,
        metadata_path: str,
        version: str = "0",
    ) -> None:
        """Update last-deployed fields (mirrors setInstallerLastDeployedInfo)."""
        with self._lock:
            self.last_deployed_result = INSTALLER_RESULTS_TABLE[result.value]
            self.last_deployed_metadata = metadata_path
            self.last_deployed_version = version if version else "0"
