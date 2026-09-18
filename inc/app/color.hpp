#ifndef ALCHEMY_APP_COLOR_HPP
#define ALCHEMY_APP_COLOR_HPP

// ANSI escape codes for inline colorization
namespace alchemy::color::ansi {
constexpr const char* Reset = "\033[0m";
constexpr const char* Bold = "\033[1m";

// standard colors
constexpr const char* Yellow = "\033[33m";
constexpr const char* Magenta = "\033[35m";
constexpr const char* Cyan = "\033[36m";

// bright variants
constexpr const char* BrightGreen = "\033[92m";

// bold+bright variants (labels, emphasis)
constexpr const char* BoldBrightGreen = "\033[1;92m";
constexpr const char* BoldMagenta = "\033[1;35m";
}  // namespace alchemy::color::ansi
#endif  // ALCHEMY_APP_COLOR_HPP
