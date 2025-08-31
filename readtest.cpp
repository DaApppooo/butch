#include "butch.hpp"

int main(int argc, const char** argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "Syntax: readtest <butch_file> <file_to_parse>\n");
    return 1;
  }
  auto res = Parser::read_file(argv[1]);
  if (res.type == ERR)
  {
    switch (res.err.type)
    {
      case Parser::Error::CANT_OPEN_FILE:
        fprintf(stderr, "Couldn't open file.\n");
        break;
      case Parser::Error::DOUBLE_QUOTE_MISMATCH:
        fprintf(stderr, "Double quote mismatch at char %i\n", res.err.offset);
        break;
      case Parser::Error::SINGLE_QUOTE_MISMATCH:
        fprintf(stderr, "Single quote mismatch at char %i\n", res.err.offset);
        break;
    }
    return 1;
  }
  auto& p = res.ok;
  p.debug(stdout);
  Parser::FileParse fp = p.parse_file(argv[2]);
  if (fp.parsed_code == nullptr)
  {
    fprintf(stderr, "Failed to open file_to_parse: '%s'\n", argv[2]);
    return 1;
  }
  p.debug_output(stdout, fp.units);
  free(fp.parsed_code);
  p.destroy();
  return 0;
}

