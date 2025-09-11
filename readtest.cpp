#include <chrono>
#include <thread>
#include "butch.hpp"

int main(int argc, const char** argv)
{
  if (argc < 3)
  {
    fprintf(stderr,
            "Syntax: readtest <butch_file> <file_to_parse> "
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
  auto start = std::chrono::system_clock::now();
  auto res = Parser::read_file(argv[1]);
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
    }
    return 1;
  }
  auto& p = res.ok;
  p.debug(stdout);
  p.flags = flags;
  Parser::FileParse fp = p.parse_file(argv[2]);
  if (fp.parsed_code == nullptr)
  {
    fprintf(stderr, "Failed to open file_to_parse: '%s'\n", argv[2]);
    return 1;
  }
  p.debug_output(stdout, fp.units.to_view());
  free(fp.parsed_code);
  p.destroy();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> secs = end - start;
  printf("Done in %lf seconds.\n", secs.count());
  return 0;
}

