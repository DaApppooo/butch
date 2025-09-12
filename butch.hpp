#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
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


typedef int32_t rule_id;
typedef int32_t class_name_id;
struct Parser;
template <class T> using Own = T;
template <class T> using Ref = T;
struct Unit
{
  static constexpr rule_id LITERAL = -1; // for Unit.id
  uint32_t str_begin;
  // number of children units following this unit in the output unit vector
  // representing a tree
  rule_id id; // points to Parser.rules
              // if id == LITERAL then this is unparsed code
  uint32_t str_len;
  uint32_t children_len;
  inline uint32_t str_end() const { return str_begin + str_len; }
  
  Unit() {}
  Unit(class_name_id id, uint32_t begin, uint32_t len, uint32_t children_count)
    : str_begin(begin), id(id), str_len(len), children_len(children_count) {}
  Unit(const Unit& u)
    : str_begin(u.str_begin), id(u.id), str_len(u.str_len),
      children_len(u.children_len) {}
  Unit(const Unit&& u)
    : str_begin(u.str_begin), id(u.id), str_len(u.str_len),
      children_len(u.children_len) {}
  constexpr Unit& operator = (const Unit& u)
  { str_begin=u.str_begin; id=u.id; str_len=u.str_len;
    children_len=u.children_len; return *this; }
  constexpr Unit& operator = (const Unit&& u)
  { str_begin=u.str_begin; id=u.id; str_len=u.str_len;
    children_len=u.children_len; return *this; }

  View<char> str(const Parser&);
  bool is(const Parser&, const View<char> s);
};

struct Rule
{
  struct RulePt
  { uint32_t r; uint32_t u; };
  // basically doing token-scale regex
  static constexpr rule_id GROUP_BEGIN = -1;
  static constexpr rule_id GROUP_END = -2;
  static constexpr rule_id PLUS = -3;
  static constexpr rule_id DOT = -4;
  static constexpr rule_id PIPE = -5;
  static constexpr rule_id NOT = -6;
  static constexpr rule_id PREPEEP = -7;
  static constexpr rule_id POSTPEEP = -8;
  
  static constexpr rule_id STRING = -10;
  static constexpr rule_id REGEX = -11;
  static constexpr rule_id UNKNOWN_RULE = -12;

  static constexpr rule_id NO_RECURSE = -1;
  static constexpr rule_id RECURSE_ALL = -2;
  static constexpr class_name_id ALIAS = -2;
  struct MatchDescriptor
  {
    uint32_t str_begin;
    uint32_t str_len : 31;
    bool matched : 1;
    // these two fields come with (pre|post)peeping
    uint16_t u_begin; // unit view begin (included)
    uint16_t u_end; // unit view end (excluded)
    bool include(MatchDescriptor m)
    {
      if (!m.matched)
        return true;
      if (m.str_len == 0)
        return false;
      if (str_len == 0)
      {
        str_begin = m.str_begin;
        str_len = m.str_len;
        matched = m.matched;
        return false;
      }
      const uint32_t os = m.str_begin;
      const uint32_t oe = os+m.str_len;
      uint32_t ss = str_begin;
      uint32_t se = ss+str_len;
      if (os < ss)
        ss = os;
      if (oe > se)
        se = oe;
      str_begin = ss;
      str_len = se - ss;
      return false;
    }
    bool include(Unit u)
    {
      return include(MatchDescriptor{
        u.str_begin,
        u.str_len,
        true
      });
    }
    bool operator == (MatchDescriptor o)
    { return o.matched == matched
             && o.str_begin == str_begin
             && o.str_len == str_len; }
  };
  static constexpr MatchDescriptor PARTIAL_FAILURE = {0, 0, true};
  static constexpr MatchDescriptor TOTAL_FAILURE = {0, 0, false};
  rule_id* in_rules;
  class_name_id* in_class_names;
  uint32_t in_len;
  /*
  if recurse == NO_RECURSE: basic sequence match, depending on .is_token
  if recurse == RECURSE_ALL: (top down)
    _check_and_do_rule is re-ran on children with every rule (once)
  if recurse == any(r): (bottom up)
    Tries to find a unit that matched r and then runs _check_and_do_rule
    on it once with the current rule.
  */
  rule_id recurse;
  class_name_id out;
  bool is_token : 1;
  bool is_alias : 1;
  bool has_prepeep : 1;
  bool has_postpeep : 1;
  bool are_inputs_owned : 1;
  static Rule init(uint32_t input_count)
  {
    Rule self;
    self.in_len = input_count;
    if (input_count)
    {
      self.in_rules = new rule_id[input_count];
      self.in_class_names = new class_name_id[input_count];
    }
    else
      self.in_rules = self.in_class_names = nullptr;
    self.recurse = NO_RECURSE;
    self.is_token
      = self.has_prepeep
      = self.has_postpeep
      = self.is_alias
      = false;
    self.are_inputs_owned = true;
    return self;
  }
  inline rule_id& in_rule(int index) const
  { return in_rules[index]; }
  inline class_name_id& in_class(int index) const
  { return in_class_names[index]; }

  constexpr inline
  bool is_rule(int index, std::initializer_list<rule_id> tests)
  {
    for (rule_id r : tests)
      if (in_rules[index] == r)
        return true;
    return false;
  }

  MatchDescriptor _sequence_match(Parser&, View<Unit>, list<RulePt>&);
  MatchDescriptor _match_str(Parser&, View<Unit>, list<RulePt>&);
  MatchDescriptor _match_rgx(Parser&, View<Unit>, list<RulePt>&);
  void destroy()
  {
    if (are_inputs_owned)
    {
      if (in_rules)
        delete[] in_rules;
      if (in_class_names)
        delete[] in_class_names;
    }
    in_rules = nullptr;
    in_class_names = nullptr;
  }
};

Own<char*> _butch_read_file(Ref<const char*> filename);
Result<int,int> _butch_count_units(char* s);

/*
== The Butch Parser ==
Create with (both return a valid Parser structure):
  Parser::read(Own<char*> butch_code) : give code (with ownership) directly.
  Parser::read_file(Ref<const char*> butch_file)
    : open, read and calls Parser::read.
Use the parser to parse your custom language with:
  Parser.parse(Ref<char*> code)
    : Returns a tree with a linear layout in memoy in a list (eqv.
      std::vector). Each node is a 'Unit', and has Unit.child_count elements.
  Parser.parse_file(Ref<const char*> filename)
    : Returns a Parser::FileParse containing a string with the content of the
      opened file (can be used to retrieve the content of tokens for example),
      and the list tree as explained with Parser.parse
*/
struct Parser
{
#define FLAG_T static constexpr uint32_t
  FLAG_T FLAG_TRIM_SPACE_LITERALS     = 0b0001U;
  // Equivalent to surrounding the string with regex's \b
  // (you can't do that in butch)
  //   ex with flag:
  //     - "a" won't match anything in "aaa"
  //     - "b" will match in "a b c"
  //     - "abc" won't match in "abc123"
  //     - "ab1" won't match "ab123"
  //     - "abc" will match in "abc++"
  //     - "+" will match in "++"
  //       (you could use rule ordering to match "++" first)
  FLAG_T FLAG_ISOLATED_MODE_STRINGS   = 0b0010U;
  FLAG_T FLAG_ISOLATED_MODE_REGEX     = 0b0100U; // same but regex
  // TODO add more flags
#undef FLAG_T
  struct Error
  {
    enum Type {
      NONE, // for internal use only
      DOUBLE_QUOTE_MISMATCH, // .offset
      SINGLE_QUOTE_MISMATCH, // .offset
      RECURSION_SYNTAX_ERROR, // .offset
      CANT_OPEN_FILE,
      UNDECLARED_RULE, // .name
      MISPLACED_PLUS, // .name (name of the rule where the error occured)
      MISPLACED_PREPEEP, // (same as MISPLACED_PLUS)
      DOUBLE_PREPEEP, // (same as MISPLACED_PLUS)
      DOUBLE_POSTPEEP, // (same as MISPLACED_PLUS)
      MISPLACED_NOT, // (same as MISPLACED_PLUS)
      ALIAS_SYNTAX_ERROR // .name (same as MISPLACED_PLUS)
    } type;
    union { uint32_t offset; Own<char*> name; };
  };
  struct FileParse
  {
    Own<char*> parsed_code;
    Own<list<Unit>> units;
  };
  struct RulePack
  { View<char> class_name; Rule rule; };
  char* butch_code;
  // i hate to mix lifetime handling technics, but i can't be bothered
  // writing linear dynamic array
  list<View<char>> classes;
  list<Rule> rules;
  string buff;
  Ref<char*> parsed_code;
  uint32_t flags;
  int flags_data;

  Parser() :
    butch_code(nullptr),
    classes(),
    rules(),
    parsed_code(nullptr),
    flags(FLAG_TRIM_SPACE_LITERALS),
    flags_data(0)
  { classes.init(); rules.init(); buff.init(); }

  static Result<Own<Parser>, Error> read(Own<char*> s);
  static Result<Own<Parser>, Error> read_file(Ref<const char*> filename)
  { return Parser::read(_butch_read_file(filename)); }
  Error::Type _make_rule(Rule& r, Ref<char*>& s);
  class_name_id _find_or_make_class(View<char>);
  rule_id _find_rule(class_name_id s);
  rule_id _find_next(rule_id rid);
  bool _check_and_do_rule(
    Rule& r,
    list<Unit>& out,
    uint32_t start,
    uint32_t end
  );
  // just loads [s.start, s.start+s.len[ U {0} into buff
  //   surround is used to add "\\b" around a regex string 
  const char* _load_str(
    View<char> s,
    View<char> surround = View<char>(nullptr, nullptr)
  );
  // same as _load but transforms "\\n" into "\n"
  const char* _comp_str(View<char> s);
  View<char> _rgx_match(View<char> big, View<char> pattern);
  bool _is_to_be_trimmed(uint32_t begin, uint32_t len);
  RulePack operator [] (int index)
  { return {classes[index], rules[index]}; }
  void debug(FILE* f);
  void debug_output(FILE* f, const View<Unit> ls);
  Own<list<Unit>> parse(Ref<char*> s);
  FileParse parse_file(Ref<const char*> filename)
  {
    char* code = _butch_read_file(filename);
    if (code == nullptr)
    {
      list<Unit> err;
      err.init();
      return {nullptr, err};
    }
    auto ret = parse(code);
    return {code, ret};
  }
  void destroy()
  {
    free(butch_code);
    butch_code = nullptr;
    classes.destroy();
    rules.destroy();
  }
};


