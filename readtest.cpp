#include "butch.hpp"

int main(int argc, const char** argv)
{
  if (argc == 1)
  {
    perror("Syntax: readtest <file>");
    return 1;
  }
  auto res = Parser::read_file(argv[1]);
  if (res.type == ERR)
  {
    switch (res.err.type)
    {
      case Parser::Error::CANT_OPEN_FILE:
        perror("Couldn't open file.");
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

