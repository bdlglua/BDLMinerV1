# PlatformIO pre script: language standards and optimization of the firmware's
# own sources and libraries (src/, lib/, .pio/libdeps/); the framework keeps
# Espressif's flags.
#
# Standards: C++23 and C23. The framework's flags end with -std=gnu++2a after
# -std=gnu++2b (the last one wins: C++20) and -std=gnu17 for C; both are
# replaced here. C23 applies to our own C (src/, and lib/: secp256k1,
# Monocypher); the C files of downloaded libraries (.pio/libdeps) keep C17,
# since C23 made bool, true and false keywords that M5GFX's qrcode defines
# itself. src/lang_check.cpp and src/lang_check.c stop the build if another
# standard comes back.
#
# Optimization: built for size (-Os, the framework default), except the mining
# hot paths, built for speed. -O2 everywhere made the firmware ~ 1.95 MB, too
# close to the 1.9 MB application slot of 4 MB boards (T-Display), for no
# hashrate gain outside these files (the hardware loops and the software
# prefilter are assembly).
import os

Import("env")

CXX_STD = "-std=gnu++23"
C_STD = "-std=gnu23"

FAST = {
    "sha256.cpp",   # software SHA-256d kernel and candidate checks
    "hw_sha.cpp",   # hardware SHA driver (S2/S3/C3 loop in C)
    "miner.cpp",    # mining tasks and hash accounting
    "work.cpp",     # job building and share targets
    "fill.cpp",     # idle-cycle hashing buffers
    "bm1370.cpp",   # Gamma ASIC protocol
    "gamma601.cpp", # Gamma ASIC worker
}

PROJECT = os.path.realpath(env.subst("$PROJECT_DIR")) + os.sep
LIBDEPS = os.path.realpath(env.subst("$PROJECT_LIBDEPS_DIR")) + os.sep


def without_std(flags):
    return [f for f in flags if not str(f).startswith("-std=")]


# One middleware: PlatformIO hands a file to the next one only while the
# previous ones returned it unchanged.
def build_flags(env, node):
    path = os.path.realpath(node.srcnode().get_abspath())
    if not path.startswith(PROJECT):
        return node  # framework sources: Espressif's flags
    ext = os.path.splitext(node.name)[1].lower()
    if ext in (".cpp", ".cc", ".cxx", ".ino"):
        flags = dict(CXXFLAGS=without_std(env.get("CXXFLAGS", [])) + [CXX_STD])
    elif ext == ".c" and not path.startswith(LIBDEPS):
        flags = dict(CFLAGS=without_std(env.get("CFLAGS", [])) + [C_STD])
    else:
        return node  # assembly (.S), downloaded C libraries: unchanged
    flags["CCFLAGS"] = without_std(env["CCFLAGS"]) + (["-O2"] if node.name in FAST else [])
    return env.Object(node, **flags)


env.AddBuildMiddleware(build_flags)
