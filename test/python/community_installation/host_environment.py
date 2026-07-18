"""Own caller-root admission and the minimal stock-host environment."""

from __future__ import annotations

import pathlib
import stat


ISOLATED_PATH = "/usr/bin:/bin"


class HostEnvironmentError(ValueError):
    """A caller-owned host root is unsafe or ambiguous."""


def private_root(path: pathlib.Path, label: str) -> pathlib.Path:
    lexical = pathlib.Path(path).expanduser().absolute()
    try:
        metadata = lexical.lstat()
    except OSError as error:
        raise HostEnvironmentError(f"caller-owned {label} is unavailable") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise HostEnvironmentError(f"caller-owned {label} must be a real directory")
    resolved = lexical.resolve(strict=True)
    if any(resolved.iterdir()):
        raise HostEnvironmentError(f"caller-owned {label} must be new and empty")
    return resolved


def isolated_environment(
    root: pathlib.Path, *, venv_launcher: pathlib.Path | None
) -> dict[str, str]:
    """Create the complete env-i-style authority under a new caller root."""

    for name in ("home", "tmp", "cache", "config"):
        (root / name).mkdir(mode=0o700)
    environment = {
        "HOME": str(root / "home"),
        "LANG": "C",
        "LC_ALL": "C",
        "PATH": ISOLATED_PATH,
        "TMPDIR": str(root / "tmp"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "XDG_CONFIG_HOME": str(root / "config"),
    }
    if venv_launcher is not None:
        environment["__PYVENV_LAUNCHER__"] = str(venv_launcher)
    return environment
