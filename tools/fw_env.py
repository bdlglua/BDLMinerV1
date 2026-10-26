# PlatformIO pre-build script: tells the firmware which environment it was
# built for (FW_ENV), the name of its files in a release (<env>-firmware.bin).
Import("env")  # noqa: F821

import sys
from pathlib import Path
sys.path.insert(0, str(Path(env['PROJECT_DIR']) / 'tools'))
from make_web import generate
generate(Path(env['PROJECT_DIR']))

env.Append(CPPDEFINES=[("FW_ENV", env.StringifyMacro(env["PIOENV"]))])  # noqa: F821
