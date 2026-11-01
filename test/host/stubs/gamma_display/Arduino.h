#pragma once
// Only Arduino String/time are mocked; OLED drawing uses real LovyanGFX.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
using std::min;
using std::max;
class String {
    std::string value;
public:
    String() = default;
    String(const char *s) : value(s ? s : "") {}
    String(const std::string &s) : value(s) {}
    template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    String(T n) : value(std::to_string(n)) {}
    String(double n, unsigned digits = 2) {
        char text[64]; snprintf(text, sizeof(text), "%.*f", int(digits), n); value = text;
    }
    String substring(unsigned from, unsigned to = unsigned(-1)) const {
        return from < value.size() ? value.substr(from, to == unsigned(-1) ? std::string::npos : to - from) : "";
    }
    const char *c_str() const { return value.c_str(); }
    operator const char *() const { return c_str(); }
    size_t length() const { return value.length(); }
    void reserve(size_t n) { value.reserve(n); }
    bool isEmpty() const { return value.empty(); }
    char operator[](size_t i) const { return value[i]; }
    String &operator+=(char c) { value += c; return *this; }
    String &operator+=(const String &s) { value += s.value; return *this; }
    bool operator==(const char *s) const { return value == s; }
    bool operator==(const String &s) const { return value == s.value; }
    bool operator!=(const String &s) const { return value != s.value; }
    friend String operator+(const String &a, const String &b) {
        return a.value + b.value;
    }
    template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    friend String operator+(const String &s, T n) { return s + String(n); }
};
inline uint32_t testMillis = 1000;
inline uint32_t millis() { return testMillis; }
