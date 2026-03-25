"""
Common content provider data structures mapped from
plugin_common/ducontent_provider_common.c/h.

C -> Python mappings:
  CONTENT_STATE enum              -> ContentState(Enum)
  CONTENT_CMD enum                -> ContentCmd(Enum)
  CONTENT_PROVIDER_COMMON struct  -> ContentProvider dataclass
    pthread_mutex_t               -> threading.Lock
    pthread_cond_t                -> threading.Condition
  contentProviderSetState()       -> ContentProvider.set_state()
  initContentProvider()           -> ContentProvider.__post_init__()
"""

import logging
import threading
from dataclasses import dataclass, field
from enum import Enum

from .run_level import RunLevel

logger = logging.getLogger(__name__)

CONTENT_STATES_TABLE = ["INIT", "IDLE", "CONFIGURED"]
CONTENT_CMDS_TABLE   = ["serve", "stop_serving"]


class ContentState(Enum):
    INIT       = 0
    IDLE       = 1
    CONFIGURED = 2


class ContentCmd(Enum):
    SERVE      = 0
    STOP_SERVE = 1


@dataclass
class ContentProvider:
    """
    Python equivalent of CONTENT_PROVIDER_COMMON struct.
    All fields protected by a threading.Condition/Lock pair.
    """
    state: ContentState = ContentState.INIT
    state_str: str = "INIT"

    content_cmd: str = ""

    current_rl: RunLevel = RunLevel.DORMANT
    pending_rl: RunLevel = RunLevel.INVALID
    requested_rl: RunLevel = RunLevel.INVALID

    b_resume_pending: bool = False
    persist_ctx_path: str = ""

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

    def set_state(self, state: ContentState) -> None:
        """
        Set provider state and emit stub notify on change.
        Mirrors contentProviderSetState().
        """
        with self._lock:
            if self.state == state:
                return
            self.state = state
            self.state_str = CONTENT_STATES_TABLE[state.value]
        logger.info("[STUB] dulinkTriggerNotify(state, %s)", self.state_str)
