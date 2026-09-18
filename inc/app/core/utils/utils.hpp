#ifndef ALCHEMY_APP_CORE_UTILS_UTILS_HPP
#define ALCHEMY_APP_CORE_UTILS_UTILS_HPP

// std
#include <type_traits>
#include <variant>
#include <vector>

// 3rd party

// local

namespace alchemy::core::utils {

// extract variants generically
template <typename ExtractVariantT, typename VariantT>
std::vector<ExtractVariantT>
extractVariantFrom(const std::vector<VariantT>& variants)
{
  std::vector<ExtractVariantT> result;
  result.reserve(variants.size());

  for (const auto& variant : variants)
  {
    std::visit(
        [&](const auto& value) {
          using value_t = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<value_t, ExtractVariantT>)
          {
            result.push_back(value);
          }
        },
        variant);
  }
  return result;
}

}  // namespace alchemy::core::utils
#endif  // ALCHEMY_APP_CORE_UTILS_UTILS_HPP
