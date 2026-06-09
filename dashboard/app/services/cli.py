import json
import subprocess
from pathlib import Path

from ..config import settings


class CliError(Exception):
    """Raised when the C++ CLI returns a non-zero exit code or invalid JSON."""

    def __init__(self, message: str, exit_code: int = 1, stderr: str = ""):
        self.exit_code = exit_code
        self.stderr = stderr
        super().__init__(message)


_ALLOWED_COMMANDS = {"download", "import", "import-history", "analyze", "refresh", "report", "trades", "export"}


def run_cli(command: str, *args: str) -> dict:
    if command not in _ALLOWED_COMMANDS:
        raise CliError(f"Command not allowed: {command}")
    cmd = [
        str(settings.cli_path),
        "--format",
        "json",
        "--quiet",
        command,
        *args,
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError as e:
        raise CliError(
            message=e.stderr.strip() or f"CLI command failed with exit code {e.returncode}",
            exit_code=e.returncode,
            stderr=e.stderr,
        ) from e

    stdout = result.stdout.strip()
    if not stdout:
        return {}

    try:
        data = json.loads(stdout)
    except json.JSONDecodeError as e:
        raise CliError(
            message=f"CLI returned invalid JSON: {e}",
            exit_code=0,
            stderr=stdout[:500],
        ) from e

    if data.get("status") == "error":
        raise CliError(
            message=data.get("error", "Unknown CLI error"),
            exit_code=1,
            stderr=data.get("detail", ""),
        )

    return data


def download_flex(token: str, query_id: str, account: str, force: bool = False) -> dict:
    args = ["--token", token, "--query-id", query_id, "--account", account]
    if force:
        args.append("--force")
    return run_cli("download", *args)


def import_csv(file_path: Path | None = None) -> dict:
    """Import trades from CSV file.

    SECURITY: file_path must NOT be user-supplied. It is constructed
    internally from the download directory. Do not expose this parameter
    to HTTP endpoints or user input.
    """
    args = []
    if file_path:
        args.extend(["--file", str(file_path)])
    return run_cli("import", *args)


def refresh_market_data() -> dict:
    """Refresh prices and earnings for all open position underlyings."""
    return run_cli("refresh")
