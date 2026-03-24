#ifndef ALCHEMY_APP_COLOR_HPP
#define ALCHEMY_APP_COLOR_HPP

// ANSI escape codes for inline colorization
namespace alchemy::color::ansi {
constexpr const char* Reset = "\033[0m";
constexpr const char* Bold = "\033[1m";

// standard colors
constexpr const char* Red = "\033[31m";
constexpr const char* Green = "\033[32m";
constexpr const char* Yellow = "\033[33m";
constexpr const char* Blue = "\033[34m";
constexpr const char* Magenta = "\033[35m";
constexpr const char* Cyan = "\033[36m";

// bright variants (more vibrant)
constexpr const char* BrightRed = "\033[91m";
constexpr const char* BrightGreen = "\033[92m";
constexpr const char* BrightYellow = "\033[93m";
constexpr const char* BrightMagenta = "\033[95m";
constexpr const char* BrightCyan = "\033[96m";
}  // namespace alchemy::color::ansi
#endif  // ALCHEMY_APP_COLOR_HPP
