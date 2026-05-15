#pragma once
#include <string>
#include <vector>
#include <chrono>

// Utility function to convert std::string (UTF-8) to std::wstring (UTF-16)
inline std::wstring stringToWS(const std::string& s) 
{
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> buf(len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), len);
    return std::wstring(buf.begin(), buf.end());
}

// Utility function to get the elapsed time in seconds since the first call to this function, using std::chrono for high-resolution timing.
inline double GetTimeStd()
{
    static auto start = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(now - start).count();
}