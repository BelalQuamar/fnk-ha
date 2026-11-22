"""Set the firmware version shown by the display during a PlatformIO build."""

import os
import re
import subprocess

Import("env")  # Provided by PlatformIO.

version = os.environ.get("RELEASE_VERSION", "")
if not re.fullmatch(r"v[0-9][A-Za-z0-9._-]{0,62}", version):
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--match", "v[0-9]*", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"), capture_output=True, text=True, check=True,
        )
        version = result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        version = "dev"
if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", version):
    version = "dev"

env.Append(CPPDEFINES=[("FNK_HA_VERSION", env.StringifyMacro(version))])
