"""
list_of_files.json parser for content_server.
Mirrors the JSON-parsing logic inside content_server.c (loadFileListAndTokenize /
entryOperation).

C -> Python mappings:
  JSON parsing via dujsonparser  -> json.load()
  struct-like access             -> FileEntry dataclass list
"""

import json
import logging
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

FILE_LIST_NAME = "list_of_files.json"


@dataclass
class FileEntry:
    """Represents one entry in list_of_files.json."""
    name: str   # relative path within the dupkg (may start with ./)
    type: str   # "file" or "dir"


class FileList:
    """Loads and holds entries from list_of_files.json."""

    def __init__(self, entries: list[FileEntry]) -> None:
        self.entries = entries

    @classmethod
    def load(cls, root_path: str) -> "FileList":
        """
        Load list_of_files.json from root_path.
        Raises FileNotFoundError or ValueError on failure.
        """
        json_path = Path(root_path) / FILE_LIST_NAME
        try:
            with open(json_path, "r", encoding="utf-8") as fh:
                raw = json.load(fh)
        except OSError as exc:
            raise FileNotFoundError(
                f"Failed to open {json_path}: {exc}"
            ) from exc
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON in {json_path}: {exc}") from exc

        if not isinstance(raw, list):
            raise ValueError(f"Expected JSON array in {json_path}, got {type(raw)}")

        entries: list[FileEntry] = []
        for item in raw:
            name = item.get("name", "")
            ftype = item.get("type", "")
            if not name or not ftype:
                logger.warning("Skipping incomplete entry: %s", item)
                continue
            # Strip leading ./ if present (mirrors C source)
            if name.startswith("./"):
                name = name[2:]
            entries.append(FileEntry(name=name, type=ftype))

        logger.info("Loaded %d entries from %s", len(entries), json_path)
        return cls(entries)
