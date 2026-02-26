// tests/stdfs.h
#pragma once
#include <filesystem>
#include <string>
#include <type_traits>

namespace fs = std::filesystem;

// --- UTF-8 helpers -----------------------------------------------------------

// Always return UTF-8 bytes as std::string, regardless of standard/library.
inline std::string path_u8string(const fs::path& p)
{
#if defined(__cpp_char8_t) // C++20+: u8string() = std::u8string
    auto u8 = p.u8string(); // std::u8string
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else // C++17: u8string() = std::string
    return p.u8string();
#endif
}

// C++17/20 compatible replacement for fs::u8path(...) to silence deprecation
inline fs::path u8path_compat(const char* s)
{
#if defined(__cpp_char8_t)
    return fs::path(reinterpret_cast<const char8_t*>(s));
#else
    return fs::u8path(s);
#endif
}

inline fs::path u8path_compat(const std::string& s)
{
#if defined(__cpp_char8_t)
    return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
#else
    return fs::u8path(s);
#endif
}

#if defined(__cpp_char8_t)
// Convert a u8"" literal to std::string (byte-preserving).
inline std::string u8_to_std_string(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}

// U8("…") -> std::string with the same bytes, on C++20+ (token-paste to avoid spaces)
#define U8(x) u8_to_std_string(u8##x)
#else
// On C++17, narrow literals are already UTF-8 in our builds; just copy.
inline std::string u8_to_std_string(const char* s)
{
    return std::string(s);
}

#define U8(x) x
#endif
