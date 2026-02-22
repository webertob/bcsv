"""Build, execute, and pin benchmark binaries."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

from .aggregation import read_json, write_json
from .constants import MACRO_FILE_STEMS, TYPE_ROWS
from .platform_info import platform_info


# ------------------------------------------------------------------
# Argument parsing helpers
# ------------------------------------------------------------------

def parse_types(value: str) -> list[str]:
    """Parse and deduplicate a comma-separated type list."""
    items = [t.strip().upper() for t in value.split(",") if t.strip()]
    if not items:
        raise ValueError("--type must include at least one type")
    allowed = {"MICRO", "MACRO-SMALL", "MACRO-LARGE"}
    invalid = [t for t in items if t not in allowed]
    if invalid:
        raise ValueError(f"Unsupported type(s): {', '.join(invalid)}")
    seen: set[str] = set()
    return [t for t in items if not (t in seen or seen.add(t))]    # type: ignore[func-returns-value]


def parse_pin(value: str) -> tuple[bool, int]:
    """Parse ``--pin NONE|CPU<id>``."""
    token = value.strip().upper()
    if token == "NONE":
        return False, 0
    if token.startswith("CPU") and token[3:].isdigit():
        return True, int(token[3:])
    raise ValueError("--pin must be NONE or CPU<id> (e.g. CPU2)")


def parse_languages(value: str) -> list[str]:
    """Parse optional language-lane list (python, csharp)."""
    if not value:
        return []
    items = [t.strip().lower() for t in value.split(",") if t.strip()]
    allowed = {"python", "csharp"}
    invalid = [t for t in items if t not in allowed]
    if invalid:
        raise ValueError(f"Unsupported language lane(s): {', '.join(invalid)}")
    seen: set[str] = set()
    return [t for t in items if not (t in seen or seen.add(t))]     # type: ignore[func-returns-value]


# ------------------------------------------------------------------
# CPU pinning
# ------------------------------------------------------------------

def pin_cmd(cmd: list[str], pin_enabled: bool, pin_cpu: int) -> list[str]:
    """Prepend ``taskset -c <cpu>`` when pinning is enabled."""
    if not pin_enabled:
        return cmd
    if shutil.which("taskset"):
        return ["taskset", "-c", str(pin_cpu)] + cmd
    return cmd


# ------------------------------------------------------------------
# Build
# ------------------------------------------------------------------

def run_build(root: Path, build_dir: Path, build_type: str = "Release") -> None:
    """Configure + build bench targets via CMake presets (fallback to raw)."""
    preset = "ninja-release" if build_type.lower() == "release" else "ninja-debug"
    build_preset = f"{preset}-build"

    try:
        subprocess.run(
            ["cmake", "--preset", preset],
            cwd=str(root), check=True, capture_output=True, text=True,
        )
        subprocess.run(
            ["cmake", "--build", "--preset", build_preset,
             "-j", str(os.cpu_count() or 1),
             "--target", "bench_macro_datasets", "bench_micro_types"],
            cwd=str(root), check=True, capture_output=True, text=True,
        )
    except subprocess.CalledProcessError:
        cxx_flags = "-O3 -march=native" if build_type == "Release" else ""
        subprocess.run(
            ["cmake", "-S", str(root), "-B", str(build_dir),
             "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={build_type}",
             f"-DCMAKE_CXX_FLAGS={cxx_flags}"],
            check=True, capture_output=True, text=True,
        )
        subprocess.run(
            ["cmake", "--build", str(build_dir),
             "-j", str(os.cpu_count() or 1),
             "--target", "bench_macro_datasets", "bench_micro_types"],
            check=True, capture_output=True, text=True,
        )


# ------------------------------------------------------------------
# Execution
# ------------------------------------------------------------------

def run_macro(
    executable: Path,
    out_dir: Path,
    run_type: str,
    rows: int,
    build_type: str,
    pin_enabled: bool,
    pin_cpu: int,
    profile: str = "",
    scenario: str = "",
    tracking: str = "both",
    storage: str = "both",
    codec: str = "both",
    timeout: int = 3600,
) -> dict:
    """Run the macro benchmark binary and return the JSON payload."""
    stem = MACRO_FILE_STEMS[run_type]
    output_file = out_dir / f"{stem}_results.json"
    stdout_log = out_dir / f"{stem}_stdout.log"
    stderr_log = out_dir / f"{stem}_stderr.log"

    cmd = [
        str(executable),
        f"--output={output_file}",
        f"--build-type={build_type}",
        f"--rows={rows}",
        f"--tracking={tracking}",
        f"--storage={storage}",
        f"--codec={codec}",
    ]
    if profile:
        cmd.append(f"--profile={profile}")
    if scenario:
        cmd.append(f"--scenario={scenario}")
    cmd = pin_cmd(cmd, pin_enabled, pin_cpu)

    result = subprocess.run(cmd, timeout=timeout, capture_output=True, text=True)
    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")

    if result.returncode != 0:
        if not output_file.exists():
            raise RuntimeError(
                f"Macro benchmark failed with exit code {result.returncode}"
            )
        # benchmark produced output but returned non-zero → continue with warning
        import sys
        print(
            f"WARNING: {run_type} exited with code {result.returncode} "
            f"but produced {output_file.name}; continuing.",
            file=sys.stderr,
        )

    if not output_file.exists():
        raise RuntimeError(f"{run_type} completed without {output_file.name}")
    payload = read_json(output_file)
    # Strip skipped rows from stored JSON (leaner output, ~49% reduction)
    if isinstance(payload, dict) and "results" in payload:
        ok_rows = [r for r in payload["results"] if r.get("status") != "skipped"]
        skipped_count = len(payload["results"]) - len(ok_rows)
        if skipped_count > 0:
            payload["results"] = ok_rows
            payload["skipped_count"] = skipped_count
            write_json(output_file, payload)
    return payload


def run_micro(
    executable: Path,
    out_dir: Path,
    pin_enabled: bool,
    pin_cpu: int,
    timeout: int = 900,
) -> dict:
    """Run the micro benchmark binary and return the JSON payload."""
    output_file = out_dir / "micro_results.json"
    stdout_log = out_dir / "micro_stdout.log"
    stderr_log = out_dir / "micro_stderr.log"

    cmd = [
        str(executable),
        "--benchmark_format=json",
        f"--benchmark_out={output_file}",
    ]
    cmd = pin_cmd(cmd, pin_enabled, pin_cpu)

    result = subprocess.run(cmd, timeout=timeout, capture_output=True, text=True)
    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")

    if result.returncode != 0:
        raise RuntimeError(
            f"Micro benchmark failed with exit code {result.returncode}"
        )
    if not output_file.exists():
        raise RuntimeError("Micro benchmark completed without micro_results.json")
    return read_json(output_file)


# ------------------------------------------------------------------
# Platform / manifest writers
# ------------------------------------------------------------------

def write_platform_json(
    out_dir: Path,
    build_type: str,
    git_label: str,
    run_types: list[str],
    repetitions: int,
    pin_value: str,
    pin_effective: bool,
    pin_cpu: int | None,
) -> None:
    """Write ``platform.json`` with host/build metadata."""
    info = platform_info()
    info.update({
        "build_type": build_type,
        "git_label": git_label,
        "run_types": run_types,
        "repetitions": repetitions,
        "pin": pin_value,
        "pin_effective": pin_effective,
        "pin_cpu": pin_cpu,
    })
    write_json(out_dir / "platform.json", info)


def write_manifest(out_dir: Path, *, args_dict: dict) -> None:
    """Write ``manifest.json`` capturing CLI arguments."""
    from datetime import datetime
    payload = {"timestamp": datetime.now().isoformat()}
    payload.update(args_dict)
    write_json(out_dir / "manifest.json", payload)
