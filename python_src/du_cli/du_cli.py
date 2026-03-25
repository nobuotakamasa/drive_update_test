"""
DU CLI main logic, mapped from src/du-cli/du_cli.c.

C -> Python mappings:
  PAYLOAD_FILE struct     -> PayloadFile dataclass
  parsePayload()          -> parse_payload()
  getTotalPayload()       -> get_total_payload()
  convertSize()           -> convert_size()
  displayProgress()       -> display_progress()
  getActiveChain()        -> get_active_chain()
  deploy()                -> deploy()
  main()                  -> main()  (see __main__.py)

SDK API calls are routed through DusClientStub so logic can run
without a live DUS server.
"""

import logging
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .dus_client_stub import (
    Chain,
    DusClientStub,
    DusCommand,
    DUSCLIENT_NO_ERROR,
)

logger = logging.getLogger(__name__)

# File suffix constants (mirrors C source)
L1PT_SUFFIX   = ".l1pt"
PT_SUFFIX     = ".pt"
PATCH_SUFFIX  = ".patch"
FILE_SUFFIXES = [L1PT_SUFFIX, PT_SUFFIX, PATCH_SUFFIX]

# Size unit thresholds
_KB = 1 << 10
_MB = 1 << 20
_GB = 1 << 30
_UNITS = [(1, "B"), (_KB, "KB"), (_MB, "MB"), (_GB, "GB")]

# Magic value for DUS payload header (b"NVDUPLD\0")
MAGIC_NVDUPLD = 0x4E5644_55504C44_00 if False else int.from_bytes(b"NVDUPLD\x00", "little")

# DUS_PAYLOAD_HEADER layout (little-endian):
#   uint64_t magic      (8 bytes)
#   uint32_t version    (4 bytes)
#   uint32_t flags      (4 bytes)
# Total: 16 bytes
_HEADER_FMT  = "<QII"
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)


@dataclass
class PayloadFile:
    """Python equivalent of the PAYLOAD_FILE struct in du_cli.c."""
    payload_path: str = ""
    payload_data: bytes = b""
    payload_file_len: int = 0
    partition_name_len: int = 0
    partition_name: str = ""
    offset_len_bytes: int = 0
    offset: int = 0
    data_len: int = 0


def convert_size(size: float) -> tuple[float, str]:
    """
    Convert byte count to a human-readable value + unit string.
    Mirrors convertSize() in du_cli.c.
    """
    for threshold, unit in reversed(_UNITS):
        if size >= threshold:
            return size / threshold, unit
    return size, "B"


def _elapsed(start: float) -> float:
    return time.monotonic() - start


def display_progress(
    done_num: int,
    total_num: int,
    flashed_bytes: int,
    partition_name: str,
    start: float,
) -> None:
    """
    Print progress line using \\r overwrite.
    Mirrors displayProgress() in du_cli.c.
    """
    elapsed = _elapsed(start)
    val_b, unit_b = convert_size(float(flashed_bytes))
    speed = flashed_bytes / elapsed if elapsed > 0 else 0.0
    val_s, unit_s = convert_size(speed)
    pct = done_num * 100.0 / total_num if total_num else 0.0
    print(
        f"\rProgress {pct:6.2f}% | {done_num:4d}/{total_num:<4d} | "
        f"{val_b:.2f}{unit_b} | {val_s:.2f} {unit_s}/s @{partition_name}\033[K",
        end="",
        flush=True,
    )


def parse_payload(data: bytes, pf: PayloadFile) -> bool:
    """
    Parse a DUS payload binary blob.
    Mirrors parsePayload() in du_cli.c.

    Returns True on success, False on error.
    """
    if len(data) < _HEADER_SIZE:
        logger.error("Payload too small to contain header (%d bytes)", len(data))
        return False

    magic, _ver, _flags = struct.unpack_from(_HEADER_FMT, data, 0)

    # Validate magic (b"NVDUPLD\0" treated as little-endian uint64)
    expected = int.from_bytes(b"NVDUPLD\x00", "little")
    if magic != expected:
        logger.error("Invalid payload magic: 0x%x (expected 0x%x)", magic, expected)
        return False

    ptr = _HEADER_SIZE

    # partitionNameLen (uint32_t)
    if ptr + 4 > len(data):
        logger.error("Out of boundary reading partitionNameLen")
        return False
    (partition_name_len,) = struct.unpack_from("<I", data, ptr)
    ptr += 4

    # partitionName (raw bytes)
    if ptr + partition_name_len > len(data):
        logger.error("Out of boundary reading partitionName")
        return False
    partition_name = data[ptr : ptr + partition_name_len].decode("utf-8", errors="replace")
    ptr += partition_name_len

    # offsetLenBytes (uint32_t)
    if ptr + 4 > len(data):
        logger.error("Out of boundary reading offsetLenBytes")
        return False
    (offset_len_bytes,) = struct.unpack_from("<I", data, ptr)
    ptr += 4

    # offset (variable length, big-endian bytes)
    if ptr + offset_len_bytes > len(data):
        logger.error("Out of boundary reading offset")
        return False
    offset = int.from_bytes(data[ptr : ptr + offset_len_bytes], "big")
    ptr += offset_len_bytes

    # dataLen (uint32_t)
    if ptr + 4 > len(data):
        logger.error("Out of boundary reading dataLen")
        return False
    (data_len,) = struct.unpack_from("<I", data, ptr)

    pf.payload_file_len = len(data)
    pf.partition_name_len = partition_name_len
    pf.partition_name = partition_name
    pf.offset_len_bytes = offset_len_bytes
    pf.offset = offset
    pf.data_len = data_len
    return True


def get_total_payload(dupkg_dir: str) -> int:
    """
    Count payload files in dupkg_dir (1.l1pt, 1.pt, 1.patch, 2.l1pt, …).
    Mirrors getTotalPayload() in du_cli.c.

    Returns total count (0 means no payloads found).
    """
    base = Path(dupkg_dir)
    total = 0
    pt_num = 0
    suffix_index = 0
    i = 1

    while suffix_index < len(FILE_SUFFIXES):
        path = base / f"{i}{FILE_SUFFIXES[suffix_index]}"
        if path.exists():
            total += 1
            if suffix_index < 2:
                pt_num += 1
            i += 1
        else:
            suffix_index += 1

    print(
        f"Partition table: {pt_num}, Other payloads: {total - pt_num}, "
        f"Total payloads: {total}"
    )
    return total


def get_active_chain() -> Chain:
    """
    Read active boot chain from device-tree.
    Mirrors getActiveChain() Linux path in du_cli.c.
    """
    dt_path = Path("/proc/device-tree/chosen/update-info/active-boot-chain")
    try:
        raw = dt_path.read_bytes()
        if len(raw) < 4:
            logger.error("Device-tree file too short")
            return Chain.INVALID
        import socket
        # ntohl equivalent: big-endian 4-byte int
        bootchain = int.from_bytes(raw[:4], "big")
        if bootchain < int(Chain.INVALID):
            return Chain(bootchain)
        logger.error("Bootchain read %d is invalid", bootchain)
    except OSError as exc:
        logger.error("Failed to open device tree file: %s", exc)
    return Chain.INVALID


def _read_file(path: Path, pf: PayloadFile) -> bool:
    """Read payload file into PayloadFile buffer. Returns False on error."""
    try:
        data = path.read_bytes()
    except OSError as exc:
        logger.error("Failed to read %s: %s", path, exc)
        return False

    if len(data) <= _HEADER_SIZE:
        logger.error("File %s too small (%d bytes)", path, len(data))
        return False

    pf.payload_path = str(path)
    pf.payload_data = data
    pf.payload_file_len = len(data)
    return True


def deploy(dupkg_dir: str, client: DusClientStub) -> int:
    """
    Iterate over payload files in dupkg_dir, parse and flash each one.
    Mirrors deploy() in du_cli.c.

    Returns 0 on success, 1 on failure.
    """
    total = get_total_payload(dupkg_dir)
    if total == 0:
        logger.info("No payloads to be deployed")
        return 0

    pf = PayloadFile()
    done_num = 0
    flashed_bytes = 0
    start = time.monotonic()
    suffix_index = 0
    base = Path(dupkg_dir)

    for i in range(1, total + 1):
        # Find file with any of the suffixes
        found = False
        while suffix_index < len(FILE_SUFFIXES):
            path = base / f"{i}{FILE_SUFFIXES[suffix_index]}"
            if _read_file(path, pf):
                found = True
                break
            suffix_index += 1

        if not found:
            logger.error("Could not find payload %d", i)
            return 1

        # Parse payload header
        if not parse_payload(pf.payload_data, pf):
            logger.error("Failed to parse payload %s", pf.payload_path)
            return 1

        # Flash via DUS client stub
        if suffix_index == 0:
            # l1pt: flash persistent
            ret, _ = client.execute(
                DusCommand.APPLY_PAYLOAD_PERSISTENT,
                Chain.PERSIST,
                pf.payload_data,
                pf.payload_file_len,
            )
        else:
            # Determine target chain
            active = get_active_chain()
            if pf.partition_name_len >= 2 and pf.partition_name[:2] == "C_":
                target = Chain.C
            elif active == Chain.A:
                target = Chain.B
            elif active in (Chain.B, Chain.C):
                target = Chain.A
            else:
                logger.error("Failed to get target bootchain")
                return 1
            ret, _ = client.execute(
                DusCommand.APPLY_PAYLOAD,
                target,
                pf.payload_data,
                pf.payload_file_len,
            )

        if ret != DUSCLIENT_NO_ERROR:
            logger.error("Failed to flash %s", pf.partition_name)
            return 1

        done_num += 1
        flashed_bytes += pf.data_len
        display_progress(done_num, total, flashed_bytes, pf.partition_name, start)

        # Reload partition table after l1pt or pt
        if suffix_index in (0, 1):
            ret2, _ = client.execute(DusCommand.RELOAD_PT, Chain.INVALID)
            print("\nReloading partition table... ", end="", flush=True)
            if ret2 != DUSCLIENT_NO_ERROR:
                logger.error("Failed to reload partition table")
                return 1
            print("PASS")

    elapsed = time.monotonic() - start
    print(f"\nTotal time = {elapsed:.2f} seconds")
    return 0


def main(argv: list[str] | None = None) -> int:
    """
    Entry point for du_cli.
    Mirrors main() in du_cli.c.
    Parses -d / -l / -h flags via argparse and runs the deploy flow.
    """
    import argparse
    import signal

    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    parser = argparse.ArgumentParser(
        prog="du_cli",
        description="Drive Update CLI tool (Python stub edition)",
    )
    parser.add_argument("-d", "--deploy", dest="dupkg_dir",
                        metavar="DUPKG_PATH",
                        help="Directory of the OTA package")
    parser.add_argument("-l", "--logging", dest="dus_log_file",
                        metavar="LOG_FILE",
                        help="File to save DU server log (optional)")

    args = parser.parse_args(argv)

    client = DusClientStub()

    def _cleanup(signum, _frame):  # noqa: ANN001
        logger.info("Received signal %d. Cleaning up...", signum)
        client.deinit()
        sys.exit(1)

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, _cleanup)

    ret = client.init()
    if ret != DUSCLIENT_NO_ERROR:
        logger.error("ERROR in duscInit, error = 0x%x", ret)
        return 1

    # Show partition table info (stub)
    _ret_pt, pt_log = client.execute(
        DusCommand.GET_PT_LOG, Chain.INVALID, out_buf_size=1024 * 1024
    )
    if _ret_pt == DUSCLIENT_NO_ERROR and pt_log:
        print(pt_log.decode("utf-8", errors="replace"), end="")

    result = 0
    if args.dupkg_dir:
        result = deploy(args.dupkg_dir, client)
        if result == 0:
            print("Deploy SUCCESS!! You can switch to inactive chain now.")
        else:
            print("Deploy FAIL!!")
    else:
        logger.info("No -d option provided; skipping deploy.")

    # Optionally dump DUS log
    if args.dus_log_file or result != 0:
        _ret_log, log_data = client.execute(
            DusCommand.GET_SERVER_LOG, Chain.INVALID, out_buf_size=1024 * 1024
        )
        if _ret_log == DUSCLIENT_NO_ERROR and log_data:
            if args.dus_log_file:
                Path(args.dus_log_file).write_bytes(log_data)
            else:
                print(log_data.decode("utf-8", errors="replace"), end="")

    client.deinit()
    return result
