"""
Entry point: python -m python_src.content_server [args]

Usage:
  python -m python_src.content_server <dupkg_path>
  python -m python_src.content_server --idle
  python -m python_src.content_server -h
"""

import argparse
import logging
import sys

from .content_server import run_server


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="content_server",
        description=(
            "Drive Update Content Server (Python stub). "
            "Serves files from a DUPKG directory over DU Link (stubbed)."
        ),
    )
    parser.add_argument(
        "dupkg_path",
        nargs="?",
        default=None,
        help="Path to the DUPKG directory to serve",
    )
    parser.add_argument(
        "-i", "--idle",
        action="store_true",
        help="Start content server in idle mode (do not auto-serve)",
    )
    parser.add_argument(
        "-r", "--resume",
        action="store_true",
        help="Resume serving from previously cached dupkg path (stub: no-op)",
    )
    parser.add_argument(
        "-c", "--clear", "-a",
        action="store_true",
        dest="clear",
        help="Clear saved dupkg path from context store and exit (stub: no-op)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = _parse_args(argv)

    if args.clear:
        logging.info("[STUB] Clearing dupkg path from context store")
        return 0

    return run_server(dupkg_path=args.dupkg_path, idle=args.idle or args.resume)


if __name__ == "__main__":
    sys.exit(main())
