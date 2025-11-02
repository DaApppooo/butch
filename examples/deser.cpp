#include <chrono>
#include "../src/butch.hpp"

int main(int argc, const char** argv)
{
  if (argc < 2)
  {
    fprintf(stderr,
            "Syntax: deser <serialized_binary>\n");
    return 1;
  }
  FILE* f = fopen(argv[1], "rb");
  if (f == nullptr)
  {
    fprintf(stderr, "Failed to open file.\n");
    return 1;
  }
  const auto deser_start = std::chrono::system_clock::now();
  auto res = Parser::deserialize_file(f);
  const auto deser_end = std::chrono::system_clock::now();
  fclose(f);
  if (res.type == ERR)
  {
    assert(res.err.type == Parser::Error::DESERIALIZATION_ERROR);
    fprintf(stderr, "Deserialization error with file '%s'\n", argv[1]);
    return 1;
  }
  auto& p = res.ok;
  p.debug(stdout);
  p.destroy();
  using dt_t = std::chrono::duration<double>;
  dt_t deserin = deser_end - deser_start;
  printf("Deserialization took %lf seconds.\n", deserin.count());
  return 0;
}

