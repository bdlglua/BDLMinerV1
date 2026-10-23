# PlatformIO pre-build script: tells the firmware which environment it was
# built for (FW_ENV), the name of its files in a release (<env>-firmware.bin).
Import("env")  # noqa: F821

env.Append(CPPDEFINES=[("FW_ENV", env.StringifyMacro(env["PIOENV"]))])  # noqa: F821
