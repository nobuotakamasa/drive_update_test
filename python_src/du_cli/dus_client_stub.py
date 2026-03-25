"""
DUS (Drive Update Server) client stub.
Mirrors dus_client.h API: duscInit / duscExecute / duscDeinit.

All calls log the operation and return success so that du_cli logic
can be exercised without a live DUS server.
"""

import logging
from enum import IntEnum

logger = logging.getLogger(__name__)


class Chain(IntEnum):
    """Boot chain identifiers (mirrors dus_client.h CHAIN_* constants)."""
    A       = 0
    B       = 1
    C       = 2
    PERSIST = 3
    INVALID = 255


class DusCommand(IntEnum):
    """DUS command codes (mirrors CMD_* constants in dus_client.h)."""
    APPLY_PAYLOAD            = 0
    APPLY_PAYLOAD_PERSISTENT = 1
    GET_PT_LOG               = 2
    GET_SERVER_LOG           = 3
    RELOAD_PT                = 4


# Error codes (mirrors DUSCLIENT_ERROR enum)
DUSCLIENT_NO_ERROR                        = 0
DUSCLIENT_ERROR_RECEIVE_DUSERVER_RETURN_ERROR = 1


class DusClientStub:
    """Stub implementation of the DUS client API."""

    def __init__(self) -> None:
        self._initialized = False

    def init(self) -> int:
        """Stub for duscInit()."""
        logger.info("[STUB] duscInit()")
        self._initialized = True
        return DUSCLIENT_NO_ERROR

    def execute(
        self,
        cmd: DusCommand,
        chain: Chain,
        payload: bytes | None = None,
        payload_len: int = 0,
        out_buf_size: int = 0,
    ) -> tuple[int, bytes]:
        """
        Stub for duscExecute().

        Returns (error_code, output_bytes).
        """
        logger.info(
            "[STUB] duscExecute(cmd=%s, chain=%s, payload_len=%d, out_buf_size=%d)",
            cmd.name if isinstance(cmd, DusCommand) else cmd,
            chain.name if isinstance(chain, Chain) else chain,
            len(payload) if payload else payload_len,
            out_buf_size,
        )
        return DUSCLIENT_NO_ERROR, b""

    def deinit(self) -> int:
        """Stub for duscDeinit()."""
        logger.info("[STUB] duscDeinit()")
        self._initialized = False
        return DUSCLIENT_NO_ERROR
