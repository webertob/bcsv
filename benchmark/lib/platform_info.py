"""Cross-platform host / CPU detection shared by all benchmark runners."""

from __future__ import annotations

import os
import platform
import socket
from datetime import datetime


def cpu_model() -> str:
    """Return a human-readable CPU model string."""
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as fh:
            for line in fh:
                if "model name" in line:
                    return line.split(":", 1)[1].strip()
    except (FileNotFoundError, PermissionError):
        pass
    return platform.processor() or "unknown"


def core_count() -> int:
    """Return the number of logical CPU cores."""
    return os.cpu_count() or 1


def platform_info(
    build_type: str = "Release",
    git_label: str = "wip",
    run_types: list[str] | None = None,
    repetitions: int = 1,
    pin: str = "NONE",
    pin_effective: bool = False,
    pin_cpu: int | None = None,
) -> dict:
    """Generate a platform.json-compatible dict."""
    return {
        "hostname": socket.gethostname(),
        "os": f"{platform.system()} {platform.release()}",
        "architecture": platform.machine(),
        "cpu_model": cpu_model(),
        "cpu_count": core_count(),
        "python_version": platform.python_version(),
        "build_type": build_type,
        "timestamp": datetime.now().isoformat(),
        "git_label": git_label,
        "run_types": run_types or [],
        "repetitions": repetitions,
        "pin": pin,
        "pin_effective": pin_effective,
        "pin_cpu": pin_cpu,
    }
