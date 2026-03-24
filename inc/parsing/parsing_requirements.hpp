#ifndef ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP
#define ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP

namespace alchemy::parser {

// configuration for what data to extract from AST during parsing
struct ParsingRequirements {
  bool needsStructParsing = false;
  bool needsFunctionParsing = false;  // future extension

  // combine requirements from multiple sources
  ParsingRequirements&
  operator|=(const ParsingRequirements& other)
  {
    needsStructParsing |= other.needsStructParsing;
    needsFunctionParsing |= other.needsFunctionParsing;
    return *this;
  }
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_PARSING_REQUIREMENTS_HPP
