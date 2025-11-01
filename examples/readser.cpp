#include <chrono>
#include "butch.hpp"

int main(int argc, const char** argv)
{
  if (argc < 3)
  {
    fprintf(stderr,
            "Syntax: readser <butch_file> <serialized_binary> "
            "[-is] [-ir]\n");
    fprintf(stderr, "\t-is\tenables FLAG_ISOLATED_STRINGS\n");
    fprintf(stderr, "\t-ir\tenables FLAG_ISOLATED_REGEX\n");
    return 1;
  }
  uint32_t flags = Parser::FLAG_TRIM_SPACE_LITERALS;
  for (int i = 3; i < argc; i++)
  {
#define CHECK(S) if (strcmp(argv[i], S) == 0)
    CHECK("-is")
      flags |= Parser::FLAG_ISOLATED_MODE_STRINGS;
    CHECK("-ir")
      flags |= Parser::FLAG_ISOLATED_MODE_REGEX;
#undef CHECK
  }
  const auto start = std::chrono::system_clock::now();
  auto res = Parser::read_file(argv[1]);
  const auto read_end = std::chrono::system_clock::now();
  if (res.type == ERR)
  {
    switch (res.err.type)
    {
      case Parser::Error::NONE:
        fprintf(stderr, "Parser::Error::NONE shouldn't be returned.\n");
        assert(false);
        break;
      case Parser::Error::CANT_OPEN_FILE:
        fprintf(stderr, "Couldn't open file.\n");
        break;
      case Parser::Error::DOUBLE_QUOTE_MISMATCH:
        fprintf(stderr, "Double quote mismatch at char %i\n", res.err.offset);
        break;
      case Parser::Error::SINGLE_QUOTE_MISMATCH:
        fprintf(stderr, "Single quote mismatch at char %i\n", res.err.offset);
        break;
      case Parser::Error::UNDECLARED_RULE:
        fprintf(stderr,
                "Unknown class name: '%s'\n",
                res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::MISPLACED_PLUS:
        fprintf(stderr,
        "Misplaced '+' (must not be placed first) in rule named: '%s'\n",
                res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::ALIAS_SYNTAX_ERROR:
        fprintf(stderr,
          "Aliases can only contain an enumeration of classes or aliases,"
          " see: '%s'\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::DOUBLE_POSTPEEP:
        fprintf(stderr,
          "Rules can only contain one postpeep operator (']'),"
          " see: '%s'\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::DOUBLE_PREPEEP:
        fprintf(stderr,
          "Rules can only contain one prepeep operator ('['),"
          " see: '%s'\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::MISPLACED_PREPEEP:
        fprintf(stderr,
          "The prepeep operator cannot be placed after a postpeep,"
          " see: '%s'\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::MISPLACED_NOT:
        fprintf(stderr,
          "The not operator ('!') cannot be placed before another operator or"
          " at the end of a rule, see: '%s'\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::CONTEXT_SPECS_ERROR:
        fprintf(stderr,
          "The context '%s' inherits begin and end from too many other"
          " contexts.\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::CONTEXT_MISSING_BEGIN:
        fprintf(stderr,
          "The context '%s' inherits a begin from someone who doesn't have one"
          ".\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::CONTEXT_MISSING_END:
        fprintf(stderr,
          "The context '%s' inherits an end from someone who doesn't have one."
          "\n",
          res.err.name
        );
        free(res.err.name);
        break;
      case Parser::Error::MISPLACED_COPY:
        fprintf(stderr,
          "Misplaced copy operator (+ at %u): you can only copy rules "
          "outside of the global context.\n",
          res.err.offset
        );
        break;
      case Parser::Error::RULE_SYNTAX_ERROR:
        fprintf(stderr,
          "Rule syntax error for rule with output class '%s'. See the"
          "README.md for the full syntax.\n",
          res.err.name
        );
        break;
      case Parser::Error::REGEX_SYNTAX_ERROR:
        fprintf(stderr,
          "Regex syntax error: %s",
          res.err.name
        );
        break;
      default:
        fprintf(stderr, "Unknown error (%i).\n", res.err.type);
        break;
    }
    return 1;
  }
  auto& p = res.ok;
  p.debug(stdout);
  p.flags = flags;
  FILE* f = fopen(argv[2], "wb");
  const auto ser_start = std::chrono::system_clock::now();
  const size_t result = p.serialize_file(f);
  const auto ser_end = std::chrono::system_clock::now();
  printf("Wrote %lu bytes (0 means failure).\n", result);
  p.destroy();
  const auto end = std::chrono::system_clock::now();
  using dt_t = std::chrono::duration<double>;
  dt_t secs = end - start;
  dt_t readin = read_end - start;
  dt_t serin = ser_end - ser_start;
  printf("Done in %lf seconds.\n", secs.count());
  printf("Reading only took %lf seconds.\n", readin.count());
  printf("Serialization only took %lf seconds.\n", serin.count());
  return 0;
}

