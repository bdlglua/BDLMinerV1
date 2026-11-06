// The firmware's C++ is C++23 (tools/optimize.py replaces the framework's
// -std flags). Stops the build if another standard comes back.
static_assert(__cplusplus >= 202302L, "BDLMiner is built as C++23 (see tools/optimize.py)");
