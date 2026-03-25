"""
DU Link stub, replacing dulinkGetSize / dulinkReadRetry / dulinkTriggerNotify.

All calls log the operation and return dummy values so that the installer
state machine can be exercised without a live DU Link router.
"""

import logging

logger = logging.getLogger(__name__)

# Dummy file size returned by get_size stub (16 KB)
DUMMY_FILE_SIZE = 16 * 1024


class DuLinkStub:
    """Stub implementation of the DU Link API used by the installer."""

    def get_size(self, path: str) -> int:
        """
        Stub for dulinkGetSize().
        Returns a fixed dummy size in bytes.
        """
        logger.info("[STUB] dulinkGetSize(%s) -> %d", path, DUMMY_FILE_SIZE)
        return DUMMY_FILE_SIZE

    def read_retry(self, path: str, offset: int, length: int) -> bytes:
        """
        Stub for dulinkReadRetry().
        Returns zero-filled bytes of the requested length.
        """
        logger.info(
            "[STUB] dulinkReadRetry(%s, offset=%d, length=%d)",
            path, offset, length,
        )
        return b"\x00" * length

    def trigger_notify(self, node: str, value: str) -> None:
        """Stub for dulinkTriggerNotify()."""
        logger.info("[STUB] dulinkTriggerNotify(%s, %s)", node, value)
