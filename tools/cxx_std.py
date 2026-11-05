# PlatformIO post-build script (C++23 study): compile C++ sources as C++23.
# Arduino-ESP32 3.x (GCC 14) passes -std=gnu++2b, the earlier name of C++23;
# every -std option is replaced by an explicit -std=gnu++23 for C++ only.
Import("env", "projenv")  # noqa: F821

STANDARD = "-std=gnu++23"


def _is_std(flag):
    return isinstance(flag, str) and (flag.startswith("-std=gnu++") or flag.startswith("-std=c++"))


for scope in (env, projenv, DefaultEnvironment()):  # noqa: F821
    for key in ("CXXFLAGS", "CCFLAGS"):
        scope.Replace(**{key: [f for f in scope.get(key, []) if not _is_std(f)]})
    scope.Append(CXXFLAGS=[STANDARD])
print("C++ standard: " + STANDARD)
