"""
Installer helper functions mapped from src/duinstaller/duinstaller_helpers.c.

C -> Python mappings:
  parseCmd()       -> parse_cmd()
  execDeployCmd()  -> exec_deploy_cmd()
  execCommitCmd()  -> exec_commit_cmd()
"""

import logging
import os
import shlex
from pathlib import Path

from .dulink_stub import DuLinkStub

logger = logging.getLogger(__name__)

STAGED_SUFFIX  = ".staged"
MAX_CHUNK_LEN  = 16 * 1024  # 16 KB per DU Link read chunk

# Command IDs (mirrors SAMPLE_CMD_ID_* in duinstaller.h)
CMD_DEPLOY      = 0
CMD_COMMIT      = 1
CMD_CLEAR_ERROR = 2

CMDS_TABLE = ["deploy", "commit", "clear_error"]


def parse_cmd(
    cmd_input: str,
) -> tuple[int | None, str, str]:
    """
    Parse a raw command string into (cmd_id, file_path, savename).
    Mirrors parseCmd() in duinstaller_helpers.c.

    Returns (None, "", "") on parse failure.
    Extracts 'path=' and optional 'savename=' tags for deploy commands.
    """
    tokens = shlex.split(cmd_input)
    if not tokens:
        return None, "", ""

    cmd_name = tokens[0].lower()
    cmd_id: int | None = None
    for idx, name in enumerate(CMDS_TABLE):
        if cmd_name == name:
            cmd_id = idx
            break

    if cmd_id is None:
        logger.error("Invalid cmd: %s", cmd_name)
        return None, "", ""

    file_path = ""
    savename  = ""

    if cmd_id == CMD_DEPLOY:
        for tok in tokens[1:]:
            if tok.startswith("path="):
                file_path = tok[len("path="):]
            elif tok.startswith("savename="):
                savename = tok[len("savename="):]

        if not file_path:
            logger.error("deploy: missing path= argument")
            return None, "", ""

    return cmd_id, file_path, savename


def exec_deploy_cmd(
    file_path: str,
    savename: str,
    install_dir: str,
    dulink: DuLinkStub,
    staged_file_path_out: list[str],
) -> bool:
    """
    Execute a deploy command: read file from DU Link (stub) and write to
    <install_dir>/<savename>.staged.
    Mirrors execDeployCmd() in duinstaller_helpers.c.

    staged_file_path_out is a 1-element list used as an output parameter
    (mirrors pMyInstaller->stagedFilePathBuf).

    Returns True on success.
    """
    total_bytes = dulink.get_size(file_path)
    if total_bytes <= 0:
        logger.error("Could not get size of file at %s", file_path)
        return False

    # Determine save name (mirrors basename logic in C source)
    save = savename if savename else Path(file_path).name
    staged_path = str(Path(install_dir) / save) + STAGED_SUFFIX
    staged_file_path_out[0] = staged_path

    try:
        with open(staged_path, "wb") as fh:
            offset = 0
            remaining = total_bytes
            while remaining > 0:
                chunk_len = min(remaining, MAX_CHUNK_LEN)
                data = dulink.read_retry(file_path, offset, chunk_len)
                if not data:
                    logger.error("Failed to read from DU Link path %s", file_path)
                    return False
                fh.write(data)
                offset    += len(data)
                remaining -= len(data)
    except OSError as exc:
        logger.error("Could not write staged file %s: %s", staged_path, exc)
        return False

    logger.info("Deployed %s -> %s", file_path, staged_path)
    return True


def exec_commit_cmd(staged_file_path: str) -> bool:
    """
    Commit a staged file by atomically renaming <file>.staged -> <file>.
    Mirrors execCommitCmd() in duinstaller_helpers.c.

    Returns True on success.
    """
    if not Path(staged_file_path).exists():
        logger.error("Expected staged file %s does not exist!", staged_file_path)
        return False

    suffix_pos = staged_file_path.rfind(STAGED_SUFFIX)
    if suffix_pos == -1:
        logger.error("Unexpected staged filename: %s", staged_file_path)
        return False

    commit_path = staged_file_path[:suffix_pos]
    try:
        os.rename(staged_file_path, commit_path)
    except OSError as exc:
        logger.error("Committing file failed: %s", exc)
        return False

    logger.info("Committed %s -> %s", staged_file_path, commit_path)
    return True
