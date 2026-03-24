#ifndef ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP
#define ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP

namespace alchemy::parser {

// configuration for what data to extract from AST during parsing
struct ParsingRequirements {
  bool needsStructParsing = false;

  // combine requirements from multiple sources
  ParsingRequirements&
  operator|=(const ParsingRequirements& other)
  {
    needsStructParsing |= other.needsStructParsing;
    return *this;
  }
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP
