"""
Content server CLI and state management, mapped from src/content_server/content_server.c.

C -> Python mappings:
  CONTENT_SERVER struct       -> ContentServer dataclass
  run_CLI()                   -> ContentServerCLI(cmd.Cmd)
  contentServerConstruct()    -> ContentServer.__init__()
  executeCmd()                -> ContentServer.execute_cmd()
  parseCmd()                  -> parse_cmd()
  serveFolder()               -> ContentServer.serve_folder()
  stopServe()                 -> ContentServer.stop_serve()
  cli_Help / cli_Status ...   -> do_help / do_status ... methods

DU Link export/register calls are replaced with stub log lines.
"""

import cmd
import logging
import os
import shlex
import signal
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path

from ..common.content_provider_common import (
    ContentCmd,
    ContentProvider,
    ContentState,
)
from .file_list import FileList

logger = logging.getLogger(__name__)

CONTENT_SERVER_NAME = "content"
CONTENT_FILE_PATH   = "files"


@dataclass
class ContentServer:
    """Python equivalent of the CONTENT_SERVER struct."""
    content_provider: ContentProvider = field(default_factory=ContentProvider)
    b_use_context_store: bool = False
    local_path: str = ""
    dupkg_path_cached: str = ""
    last_cmd_id: int = 0
    last_cmd_result: int = 0  # 0 = OK

    def __post_init__(self) -> None:
        # Ensure threading primitives are initialised
        self.content_provider.__post_init__()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _serve_folder(self, root_path: str) -> bool:
        """
        Export each entry in list_of_files.json to DU Link (stub).
        Mirrors serveFolder() in content_server.c.
        """
        try:
            file_list = FileList.load(root_path)
        except (FileNotFoundError, ValueError) as exc:
            logger.error("Failed to load file list: %s", exc)
            return False

        for entry in file_list.entries:
            local = Path(root_path) / entry.name
            if not local.exists():
                logger.error("File not accessible: %s", local)
                return False
            if entry.type == "dir":
                logger.info("[STUB] dulinkExportDirectory(%s/%s/%s)",
                            CONTENT_SERVER_NAME, CONTENT_FILE_PATH, entry.name)
            elif entry.type == "file":
                logger.info("[STUB] dulinkExportFile(%s/%s/%s)",
                            CONTENT_SERVER_NAME, CONTENT_FILE_PATH, entry.name)
            else:
                logger.error("Invalid file type '%s' for %s", entry.type, entry.name)
                return False
        return True

    def _stop_serve(self, root_path: str) -> bool:
        """
        Unlink each entry from DU Link (stub).
        Mirrors stopServe() in content_server.c.
        Processes entries in reverse order.
        """
        try:
            file_list = FileList.load(root_path)
        except (FileNotFoundError, ValueError) as exc:
            logger.error("Failed to load file list: %s", exc)
            return False

        for entry in reversed(file_list.entries):
            if entry.type == "dir":
                logger.info("[STUB] dulinkUnlinkDirectory(%s/%s/%s)",
                            CONTENT_SERVER_NAME, CONTENT_FILE_PATH, entry.name)
            else:
                logger.info("[STUB] dulinkUnlinkFile(%s/%s/%s)",
                            CONTENT_SERVER_NAME, CONTENT_FILE_PATH, entry.name)
        return True

    def execute_cmd(self, cmd_id: ContentCmd, local_path: str) -> bool:
        """
        Execute a parsed command.
        Mirrors executeCmd() in content_server.c.
        Must be called with content_provider.lock held.
        """
        self.last_cmd_id = cmd_id.value
        self.last_cmd_result = 1  # default: error

        if cmd_id == ContentCmd.SERVE:
            if self.content_provider.state == ContentState.CONFIGURED:
                logger.warning("Files are already being served (%s)", self.local_path)
                return False
            if not self._serve_folder(local_path):
                logger.error("Failed to serve '%s'", local_path)
                return False
            # Release lock while changing state (mirrors C unlock/lock pattern)
            self.content_provider.set_state(ContentState.CONFIGURED)
            self.local_path = local_path
            logger.info("Files successfully exported to DU Link (stub)")

        elif cmd_id == ContentCmd.STOP_SERVE:
            if self.content_provider.state != ContentState.CONFIGURED:
                logger.error("Not currently serving files!")
                return False
            if not self._stop_serve(self.local_path):
                logger.error("Failed to stop serve '%s' cleanly!", self.local_path)
                return False
            self.content_provider.set_state(ContentState.IDLE)
            self.local_path = ""
        else:
            return False

        self.last_cmd_result = 0
        return True


def parse_cmd(raw: str) -> tuple[ContentCmd | None, str]:
    """
    Parse a raw command string into (ContentCmd, local_path).
    Mirrors parseCmd() in content_server.c.

    Returns (None, "") on parse error.
    """
    tokens = shlex.split(raw)
    if not tokens:
        return None, ""

    cmd_name = tokens[0].lower()
    try:
        cmd_id = {
            "serve":        ContentCmd.SERVE,
            "stop_serving": ContentCmd.STOP_SERVE,
        }[cmd_name]
    except KeyError:
        logger.error("Invalid cmd: %s", cmd_name)
        return None, ""

    local_path = ""
    if cmd_id == ContentCmd.SERVE:
        # Extract local_path= tag
        for tok in tokens[1:]:
            if tok.startswith("local_path="):
                local_path = tok[len("local_path="):]
                break

    return cmd_id, local_path


# ------------------------------------------------------------------
# Interactive CLI
# ------------------------------------------------------------------

class ContentServerCLI(cmd.Cmd):
    """
    Interactive CLI for the content server.
    Mirrors run_CLI() in content_server.c.
    """

    prompt = "content-server> "
    intro  = "Drive Update Content Server (Python stub). Type 'help' for commands."

    def __init__(self, server: ContentServer) -> None:
        super().__init__()
        self._server = server
        self._update_prompt()
        self._stop_event = threading.Event()

    def _update_prompt(self) -> None:
        with self._server.content_provider.lock:
            if self._server.content_provider.state == ContentState.CONFIGURED:
                self.prompt = f"CONTENT-SERVER[{self._server.local_path}]> "
            else:
                self.prompt = "content-server> "

    # ---- Command implementations ----

    def do_help(self, _arg: str) -> None:  # noqa: D401
        """Print help screen."""
        print("help           Prints this message")
        print("status         Displays various information")
        print("serve [dupkg]  Starts serving DUPKG.")
        print("               If <dupkg> missing, the previously used dupkg will be used")
        print("stop           Stop serving dupkg")
        print("clear          Clear dupkg path in context store")
        print("set <dupkg>    Set dupkg path locally")
        print("save <dupkg>   Save dupkg path in context store")
        print("quit           Quit")

    def do_status(self, _arg: str) -> None:
        """Display server status."""
        srv = self._server
        with srv.content_provider.lock:
            serving = srv.content_provider.state == ContentState.CONFIGURED
            print(f"serving               : {'Yes' if serving else 'No'}")
            if srv.b_use_context_store:
                print("context store         : Used")
            else:
                print("context store         : Not used")
            print(f"DUPKG path (cached)   : '{srv.dupkg_path_cached}'")
            print(f"DUPKG path (in use)   : '{srv.local_path}'")
            print(f"Last Command (Result) : {srv.last_cmd_id} ({srv.last_cmd_result:#x})")

    def do_serve(self, arg: str) -> None:
        """Serve DUPKG: serve [<path>]"""
        path = arg.strip()
        srv = self._server
        with srv.content_provider.lock:
            if path:
                logger.info("Serve DUPKG '%s' as specified", path)
                srv.execute_cmd(ContentCmd.SERVE, path)
            elif srv.dupkg_path_cached:
                logger.info("Serve DUPKG '%s' locally cached", srv.dupkg_path_cached)
                srv.execute_cmd(ContentCmd.SERVE, srv.dupkg_path_cached)
            else:
                logger.info("No DUPKG path available")
                print("No DUPKG path set. Use 'set <dupkg>' first.")
        self._update_prompt()

    def do_stop(self, _arg: str) -> None:
        """Stop serving current DUPKG."""
        srv = self._server
        with srv.content_provider.lock:
            logger.info("Stopping %s", srv.local_path)
            srv.execute_cmd(ContentCmd.STOP_SERVE, "")
        self._update_prompt()

    def do_clear(self, _arg: str) -> None:
        """Clear dupkg path in context store."""
        srv = self._server
        with srv.content_provider.lock:
            srv.dupkg_path_cached = ""
            if srv.b_use_context_store:
                logger.info("[STUB] dulinkWrite(ctx_store, clear)")
        print("Cleared.")

    def do_set(self, arg: str) -> None:
        """Set dupkg path locally: set <path>"""
        path = arg.strip()
        with self._server.content_provider.lock:
            self._server.dupkg_path_cached = path
        print(f"Set to '{path}'")

    def do_save(self, arg: str) -> None:
        """Save dupkg path in context store: save <path>"""
        path = arg.strip()
        with self._server.content_provider.lock:
            if self._server.b_use_context_store:
                logger.info("[STUB] dulinkWrite(ctx_store, %s)", path)
            else:
                logger.warning("Context store not available")
        print(f"Saved '{path}' (stub)")

    def do_quit(self, _arg: str) -> bool:  # noqa: D401
        """Quit the content server CLI."""
        return True

    # Alias
    do_EOF = do_quit

    def postcmd(self, stop: bool, _line: str) -> bool:
        self._update_prompt()
        return stop


def run_server(dupkg_path: str | None = None, idle: bool = False) -> int:
    """
    Initialise and run the content server CLI.
    Mirrors main() + run() + contentServerInit() in content_server.c.

    DU Link init/register calls are replaced with stub log lines.
    """
    logger.info("[STUB] dulinkInit(%s, ...)", CONTENT_SERVER_NAME)
    logger.info("[STUB] dulinkOpen(...)")
    logger.info("[STUB] registerToMaster(...)")
    logger.info("[STUB] exportDULinkNodes(...)")

    server = ContentServer()
    server.content_provider.set_state(ContentState.IDLE)

    if dupkg_path and not idle:
        server.dupkg_path_cached = dupkg_path
        with server.content_provider.lock:
            server.execute_cmd(ContentCmd.SERVE, dupkg_path)

    # Signal handler: graceful shutdown
    def _on_signal(signum, _frame):  # noqa: ANN001
        logger.info("Signal %d received; exiting", signum)
        print("\n[Interrupted]")
        sys.exit(0)

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, _on_signal)

    cli = ContentServerCLI(server)
    try:
        cli.cmdloop()
    except KeyboardInterrupt:
        print()

    logger.info("[STUB] dulinkClose(...)")
    return 0
