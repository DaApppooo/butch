#include "butch.hpp"

int main(int argc, const char** argv)
{
  if (argc == 1)
  {
    fprintf(stderr, "Syntax: readtest <file>\n");
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
  p.destroy();
  return 0;
}

