#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <type_traits>
#include <vector>
#include <string_view>
#include <string>
// #include <regex>

typedef int16_t unit_id;
constexpr unit_id GROUP_BEGIN = -1; // basically doing token-scale regex
constexpr unit_id GROUP_END = -2;
constexpr unit_id STAR = -3;
constexpr unit_id PLUS = -4;
constexpr unit_id DOT = -5;
struct Parser;
template <class T> using Own = T;
template <class T> using Ref = T;
struct Unit
{
  uint32_t str_begin;
  // number of children units following this unit in the output unit vector
  // representing a tree
  uint32_t children_len;
  unit_id id; // points to Parser.types
  uint16_t str_len;

  Unit() {}
  Unit(uint16_t id, uint32_t begin, uint16_t len)
    : str_begin(begin), id(id), str_len(len) {}
  Unit(const Unit& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}
  Unit(const Unit&& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}

  std::string_view name(Parser&);
  Unit& child(Parser&);
};
template <class T>
struct View
{
  using mut_T = std::remove_const<T>;
  T* begin; uint32_t len;
  View() {}
  View(T* begin, uint32_t len) : begin(begin), len(len) {}
  View(T* begin, T* end) : begin(begin), len(end-begin) {}
  View(const View<T>& v) : begin(v.begin), len(v.len) {}
  View(const View<T>&& v) : begin(v.begin), len(v.len) {}
  View<mut_T> mut() const
  { return {const_cast<mut_T>(begin), len}; }
};
template <>
struct View<char>
{
  char* begin; uint32_t len;
  View() {}
  View(char* begin, uint32_t len) : begin(begin), len(len) {}
  View(char* begin, char* end) : begin(begin), len(end-begin) {}
  View(const View<char>& v) : begin(v.begin), len(v.len) {}
  View(const View<char>&& v) : begin(v.begin), len(v.len) {}
  char fix() const
  { char tmp = begin[len-1]; begin[len-1] = 0; return tmp; }
  void mov(char c) const
  { begin[len-1] = c; }
};

struct Rule
{
  uint32_t in_len;
  unit_id out;
  unit_id* in;
  static Rule init(uint32_t input_count)
  {
    Rule self;
    self.in_len = input_count;
    self.in = new unit_id[input_count];
    return self;
  }
  void destroy()
  {
    delete[] in;
  }
};

struct Parser
{
  struct RulePack
  { View<char> class_name; Rule rule; };
  char* butch_code;
  // i hate to mix lifetime handling technics, but i can't be bothered
  // writing linear dynamic array
  std::vector<View<char>> classes;
  std::vector<Rule> rules;

  Parser() :
    butch_code(nullptr),
    classes(),
    rules()
  {}

  static Parser read(Own<char*> s);
  static Parser read(const char* filename);
  void make_rule(Rule& r, Ref<char*> s);
  RulePack operator [] (int index)
  { return {classes[index], rules[index]}; }
  std::vector<Unit> parse(Ref<char*> s);
  std::vector<Unit> parse_file(const char* filename)
  {
    FILE* f = fopen(filename, "r");
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    auto code = new char[len+1];
    fread(code, 1, len, f);
    fclose(f);
    code[len] = 0;
    auto ret = parse(code);
    free(code);
    return ret;
  }
  void destroy()
  { free(butch_code);
    for (Rule& r : rules) r.destroy();
    butch_code = nullptr; }
};

#define SKIP_SPACES_ONLY \
    { \
      while (*s == ' ' || *s == '\t') s++; \
      continue; \
    }
#define SKIP_STRING(str_char) \
  while (*(++s) != str_char) \
    if (*s == '\\') \
      s++;
int _butch_count_units(char* s)
{
  int input_count = 0;
  while (*s != '\n' && *s != '#')
  {
    if (*s == '"')
    {
      SKIP_STRING('"')
      input_count++; s++;
      SKIP_SPACES_ONLY
    }
    else if (*s == '\'')
    {
      SKIP_STRING('\'')
      input_count++; s++;
      SKIP_SPACES_ONLY
    }
    else
    {
      switch (*s)
      {
        case '(':
        case ')':
        case '.':
        case '*':
        case '+':
          input_count++; s++;
          break;
        default:
          input_count++;
          while (!isspace(*s))
            s++;
          SKIP_SPACES_ONLY;
          break;
      }
    }
  }
  return input_count;
}


Parser Parser::read(Own<char*> s)
{
  Parser self;
  self.butch_code = s;
  
  char* start = s;
#define CHECK_COMMENTS \
    if (*s == '#') \
    { \
      while (*s != '\n' && *s != 0) s++; \
      continue; \
    }


  while (*s != 0)
  {
    CHECK_COMMENTS;

    { // class name
      while (*s != '=' && !isspace(*s))
        s++;
      self.classes.push_back(View<char>(start, s));
    }
    
    { // handle midway space and '='
      while (*s == '=' || isspace(*s))
        s++;
      start = s;
    }
    
    { // class rule
      self.rules.push_back(Rule::init(_butch_count_units(s)));
      Rule& r = self.rules.back();
      r.out = self.classes.size()-1;
      
    }
    s++;
  }
  
  return self;
}


