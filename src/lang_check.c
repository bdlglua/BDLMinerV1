// The firmware's C is C23 (tools/optimize.py replaces the framework's -std
// flags). Stops the build if another standard comes back. GCC 14 reports C23
// as 202000L, GCC 15 and later as 202311L; C17 is 201710L.
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000L
#error "BDLMiner is built as C23 (see tools/optimize.py)"
#endif
