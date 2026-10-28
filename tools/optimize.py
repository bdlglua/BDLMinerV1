# PlatformIO pre script: the firmware, the Arduino core and the libraries are
# built for size (-Os, the framework default), except the mining hot paths,
# built for speed. -O2 everywhere made the firmware ~ 1.95 MB, too close to the
# 1.9 MB application slot of 4 MB boards (T-Display), for no hashrate gain
# outside these files (the hardware loops and the software prefilter are
# assembly).
Import("env")

FAST = {
    "sha256.cpp",   # software SHA-256d kernel and candidate checks
    "hw_sha.cpp",   # hardware SHA driver (S2/S3/C3 loop in C)
    "miner.cpp",    # mining tasks and hash accounting
    "work.cpp",     # job building and share targets
    "fill.cpp",     # idle-cycle hashing buffers
    "bm1370.cpp",   # Gamma ASIC protocol
    "gamma601.cpp", # Gamma ASIC worker
}


def fast(env, node):
    if node.name in FAST:
        return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-O2"])
    return node


env.AddBuildMiddleware(fast, "*.cpp")
