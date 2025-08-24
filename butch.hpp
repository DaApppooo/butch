#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "list.hpp"

enum ResultType { OK, ERR };
template <class Ok = int, class Err = int>
struct Result
{
  ResultType type;
  union { Ok ok; Err err; };
  Result() {}
  template <class T>
  Result(ResultType r, const T& t)
  {
    switch (r)
    { case OK: memcpy(&ok, &t, sizeof(Ok)); break;
      case ERR: memcpy(&err, &t, sizeof(Err)); break; }
    type = r;
  }
  Result(const Result<Ok, Err>& r)
  {
    switch (r.type)
    { case OK: memcpy(&ok, &r.ok , sizeof(Ok)); break;
      case ERR: memcpy(&err, &r.err , sizeof(Err)); break; }
  }
  Result(const Result<Ok, Err>&& r)
  {
    switch (r.type)
    { case OK: memcpy(&ok, &r.ok , sizeof(Ok)); break;
      case ERR: memcpy(&err, &r.err , sizeof(Err)); break; }
  }
  template <class T>
  Result(const Result<Ok, T>& r)
  {
    assert(r.type == OK);
    type = r.type;
    memcpy(&ok, &r.ok, sizeof(Ok));
  }
  template <class T>
  Result(const Result<T, Err>& r)
  {
    assert(r.type == ERR);
    type = r.type;
    memcpy(&err, &r.err, sizeof(Err));
  }
  ~Result()
  {
    switch (type)
    { case OK: ok.~Ok();
      case ERR: err.~Err(); }
  }
};
template <class Err, class Ok>
Result<Ok,Err> ok(Ok& ok_) { return Result<Ok, Err>(OK, ok_); }
template <class Ok, class Err>
Result<Ok,Err> err(Err& err_) { return Result<Ok, Err>(ERR, err_); }
template <class Err, class Ok>
Result<Ok,Err> ok(Ok&& ok_) { return Result<Ok, Err>(OK, ok_); }
template <class Ok, class Err>
Result<Ok,Err> err(Err&& err_) { return Result<Ok, Err>(ERR, err_); }
#define expect(COND, OK_TYPE, ERROR_CODE) \
  if (!(COND)) return err<OK_TYPE>(ERROR_CODE)


typedef int16_t rule_id;
typedef int16_t class_name_id;
constexpr rule_id GROUP_BEGIN = -1; // basically doing token-scale regex
constexpr rule_id GROUP_END = -2;
constexpr rule_id PLUS = -3;
constexpr rule_id DOT = -4;
constexpr rule_id PIPE = -5;
constexpr class_name_id NO_PARENT = -1;
struct Parser;
template <class T> using Own = T;
template <class T> using Ref = T;
struct Unit
{
  uint32_t str_begin;
  // number of children units following this unit in the output unit vector
  // representing a tree
  uint32_t children_len;
  class_name_id id; // points to Parser.rules
  uint16_t str_len;
  inline uint32_t str_end() const { return str_begin + str_len; }
  
  Unit() {}
  Unit(uint16_t id, uint32_t begin, uint16_t len, int children_count)
    : str_begin(begin), id(id), str_len(len), children_len(children_count) {}
  Unit(const Unit& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}
  Unit(const Unit&& u) : str_begin(u.str_begin), id(u.id), str_len(u.str_len) {}

  View<char> str(Parser&);
  Unit& child(Parser&);
};

struct Rule
{
  struct RulePt
  { int pos; int depth; };
  struct MatchDescriptor
  {
    uint32_t str_begin;
    uint16_t str_len;
    bool matched;
    bool include(MatchDescriptor m)
    {
      if (!m.matched)
        return true;
      if (m.str_len == 0)
        return false;
      if (m.str_begin < str_begin)
      {
        str_len += str_begin - m.str_begin;
        str_begin = m.str_begin;
      }
      if (m.str_len > str_len)
        str_len = m.str_len;
      return false;
    }
    bool operator == (MatchDescriptor o)
    { return o.matched == matched
             && o.str_begin == str_begin
             && o.str_len == str_len; }
  };
  static constexpr MatchDescriptor PARTIAL_FAILURE = {0, 0, true};
  static constexpr MatchDescriptor TOTAL_FAILURE = {0, 0, false};
  enum AtomType
  {
    ATOM_STRING_MATCH, // "string exact match"
    ATOM_REGEX_MATCH, // 'regex expression'
    ATOM_SYMBOL, // regex operators ( | + * . )
    ATOM_CLASS // class name
  };
  uint32_t in_len;
  class_name_id parent; // parent class with 'Parent/Child' syntax
                        // useful for precision parsing.
                        // (use a global class for all of cases and for one
                        // specific case use a child class)
  class_name_id out;
  rule_id* in;
  static Rule init(uint32_t input_count)
  {
    Rule self;
    self.in_len = input_count;
    self.in = new rule_id[input_count];
    self.parent = NO_PARENT;
    return self;
  }
  MatchDescriptor _sequence_match(Parser&, View<Unit>);
  MatchDescriptor _match_str(Parser&, View<Unit>, list<RulePt>&, list<int>&);
  void destroy()
  {
    delete[] in;
  }
};

Own<char*> _butch_read_file(Ref<const char*> filename);
Result<int,int> _butch_count_units(char* s);

struct Parser
{
  struct Error
  {
    enum Type {
      DOUBLE_QUOTE_MISMATCH,
      SINGLE_QUOTE_MISMATCH,
      CANT_OPEN_FILE
    } type;
    uint32_t offset;
  };
  struct RulePack
  { View<char> class_name; Rule rule; };
  char* butch_code;
  // i hate to mix lifetime handling technics, but i can't be bothered
  // writing linear dynamic array
  list<View<char>> classes;
  list<Rule> rules;

  Parser() :
    butch_code(nullptr),
    classes(),
    rules()
  { classes.init(); rules.init(); }

  static Result<Own<Parser>, Error> read(Own<char*> s);
  static Result<Own<Parser>, Error> read_file(Ref<const char*> filename)
  { return Parser::read(_butch_read_file(filename)); }
  void _make_rule(Rule& r, Ref<char*>& s);
  rule_id _find_or_make_class(View<char>);
  bool _check_and_do_rule(Rule& r, list<Unit>& out);
  RulePack operator [] (int index)
  { return {classes[index], rules[index]}; }
  void debug(FILE* f);
  Own<list<Unit>> parse(Ref<char*> s);
  Own<list<Unit>> parse_file(Ref<const char*> filename)
  {
    char* code = _butch_read_file(filename);
    auto ret = parse(code);
    free(code);
    return ret;
  }
  Rule::AtomType atom_type(rule_id in)
  {
    if (in < 0) return Rule::ATOM_SYMBOL;
    const char* const s = classes[in].start;
    if (*s == '"') return Rule::ATOM_STRING_MATCH;
    if (*s == '\'') return Rule::ATOM_REGEX_MATCH;
    return Rule::ATOM_CLASS;
  }
  void destroy()
  {
    free(butch_code);
    butch_code = nullptr;
    classes.destroy();
    rules.destroy();
  }
};


