"""
RunLevel definitions mapped from ducommon.h DU_RUN_LEVEL enum.
"""

from enum import IntEnum


class RunLevel(IntEnum):
    """Drive Update run level values."""
    DORMANT   = 0
    BG_APPLY  = 3
    INVALID   = 255
