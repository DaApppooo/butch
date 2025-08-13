#include <cstdint>
#include <vector>
#include <string_view>
#include <string>
// #include <regex>

typedef int32_t unit_id;
constexpr unit_id GROUP_BEGIN = -1; // basically doing token-scale regex
constexpr unit_id GROUP_END = -2;
constexpr unit_id STAR = -3;
constexpr unit_id PLUS = -4;
constexpr unit_id DOT = -5;
struct Parser;
struct Unit
{
  uint32_t str_begin;
  // number of children units following this unit in the output unit vector
  // representing a tree
  uint32_t children_len;
  uint16_t id; // points to Parser.
  uint16_t str_len;

  Unit() {}
  Unit(uint16_t id, uint32_t begin, uint16_t len)
    : str_begin(begin), id(id), str_len(len) {}
  Unit(const Unit& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}
  Unit(const Unit&& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}

  std::string_view name(Parser&);
  Unit& child(Parser&);
};

struct Rule
{
  unit_id out;
  uint32_t in_len;
  // unit_id in[in_len];
  static Rule* init(uint32_t input_count)
  {
    auto self = (Rule*)calloc(1, sizeof(Rule)+sizeof(unit_id)*input_count);
    self->in_len = input_count;
    return self;
  }
  unit_id& in(int index) { return *(((unit_id*)(this+1)) + index); }
};

struct Parser
{
  std::string butch_code;
  // std::string_view space_chars;
  std::vector<Unit> units;
  std::vector<Rule*> rules;

  Parser() :
    butch_code(nullptr),
    units(),
    rules()
  {}

  
  
};

int main()
{
  
}




