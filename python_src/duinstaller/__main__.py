"""
Entry point: python -m python_src.duinstaller [args]

DU Installer Plugin (Python stub edition).
Implements the state machine without requiring a live DU Link / DU Master.
Commands can be injected via DuInstaller.write_cmd() for testing.
"""

import argparse
import logging
import sys

from .installer import SampleInstaller, installer_init, installer_run

HELP_MSG = """\
DU Installer Plugin (Python stub) v1

This program implements the DRIVE Update installer plugin state machine
in Python. All DU Link SDK calls are replaced with log-only stubs.

Commands accepted (via write_cmd API):
  deploy path=<dulink_path> [savename=<name>]
      Read file from DU Link path, write as <savename>.staged in install_dir.
  commit
      Rename <file>.staged -> <file> (atomic).
  clear_error
      Transition from ERROR back to IDLE.

Usage:
  python -m python_src.duinstaller [-h]
"""


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    parser = argparse.ArgumentParser(
        prog="duinstaller",
        description="DU Installer Plugin (Python stub)",
        add_help=True,
    )
    # -h is handled automatically by argparse
    args = parser.parse_args(argv)  # noqa: F841

    inst = SampleInstaller()
    if not installer_init(inst):
        logging.error("Installer initialization failed")
        return 1

    print(HELP_MSG)
    logging.info("Installer running (stub mode). Use write_cmd() API to inject commands.")

    try:
        installer_run(inst)
    except KeyboardInterrupt:
        print("\n[Interrupted]")

    return 0


if __name__ == "__main__":
    sys.exit(main())
