#ifndef ALCHEMY_PARSING_PARSER_HPP
#define ALCHEMY_PARSING_PARSER_HPP

// std
#include <string_view>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/parsing_requirements.hpp"

namespace alchemy::parser {

// generic parser adapter interface (language-agnostic)
// concrete implementations: ClangParser, (future) RustParser, etc.
class ParsingRuleAdapter {
public:
  virtual ~ParsingRuleAdapter() = default;
  ParsingRuleAdapter() = default;
  ParsingRuleAdapter(const ParsingRuleAdapter&) = delete;
  ParsingRuleAdapter&
  operator=(const ParsingRuleAdapter&) = delete;
  ParsingRuleAdapter(ParsingRuleAdapter&&) = default;
  ParsingRuleAdapter&
  operator=(ParsingRuleAdapter&&) = default;
  virtual alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
  parse(const alchemy::parser::ParsingRequirements& requirements) = 0;
  // metadata
  virtual std::string_view
  getName() const = 0;
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_PARSER_HPP
