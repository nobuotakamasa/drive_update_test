"""
Entry point: python -m python_src.du_cli [args]
"""

import sys
from .du_cli import main

if __name__ == "__main__":
    sys.exit(main())
