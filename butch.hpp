#ifndef H_BUTCH
#define H_BUTCH
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <type_traits>
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
    static_assert(std::is_same_v<T, Ok> || std::is_same_v<T, Err>, "Diff types ???");
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
// #define expect(COND, OK_TYPE, ERROR_CODE) \
//   if (!(COND)) return err<OK_TYPE>(ERROR_CODE)

template <class T> using Own = T;
template <class T> using Ref = T;

using rule_id = int32_t;
using class_id = int32_t;
using ctx_id = int32_t;
struct Parser;
#define FLAG_T static constexpr uint32_t
struct Unit
{
  static constexpr rule_id LITERAL = -1; // for Unit.id
  static constexpr rule_id ERROR_CONTEXT_MISMATCH = -2;
  uint32_t s_start;
  // number of children units following this unit in the output unit vector
  // representing a tree
  rule_id id; // points to Parser.rules
              // if id == LITERAL then this is unparsed code
  uint32_t s_len;
  uint32_t children_len;
  inline uint32_t str_end() const { return s_start + s_len; }
  
  Unit() {}
  Unit(class_id id, uint32_t begin, uint32_t len, uint32_t children_count)
    : s_start(begin), id(id), s_len(len), children_len(children_count) {}
  Unit(const Unit& u)
    : s_start(u.s_start), id(u.id), s_len(u.s_len),
      children_len(u.children_len) {}
  Unit(const Unit&& u)
    : s_start(u.s_start), id(u.id), s_len(u.s_len),
      children_len(u.children_len) {}
  constexpr Unit& operator = (const Unit& u)
  { s_start=u.s_start; id=u.id; s_len=u.s_len;
    children_len=u.children_len; return *this; }
  constexpr Unit& operator = (const Unit&& u)
  { s_start=u.s_start; id=u.id; s_len=u.s_len;
    children_len=u.children_len; return *this; }

  View<char> str(const Parser&);
  bool is(const Parser&, const View<char> s);
};

struct Match
{
  uint32_t s_start;
  uint32_t s_len : 31;
  bool matched : 1;
  // these two fields come with (pre|post)peeping
  uint16_t u_start; // unit view begin (included)
  uint16_t u_end; // unit view end (excluded)
  constexpr inline uint32_t s_end() { return s_start+s_len; }
  inline View<char> s_view(const Parser&);
  bool include(Match m)
  {
    if (!m.matched)
      return true;
    if (m.s_len == 0)
      return false;
    if (s_len == 0)
    {
      s_start = m.s_start;
      s_len = m.s_len;
      matched = m.matched;
      return false;
    }
    const uint32_t os = m.s_start;
    const uint32_t oe = os+m.s_len;
    uint32_t ss = s_start;
    uint32_t se = ss+s_len;
    if (os < ss)
      ss = os;
    if (oe > se)
      se = oe;
    s_start = ss;
    s_len = se - ss;
    return false;
  }
  bool include(Unit u)
  {
    return include(Match{
      u.s_start,
      u.s_len,
      true
    });
  }
  bool operator == (Match o)
  { return o.matched == matched
           && o.s_start == s_start
           && o.s_len == s_len; }
};

struct Rule
{
  struct Pt
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
  static constexpr rule_id CONTEXT = -13; // class_id is a ctx_id

  static constexpr class_id ALIAS = -2;
  static constexpr class_id BEGIN = -3;
  static constexpr class_id END = -4;

  static constexpr Match PARTIAL_FAILURE = {0, 0, true};
  static constexpr Match TOTAL_FAILURE = {0, 0, false};
  uint32_t ruleset_start;
  uint32_t input_len;
  class_id out;
  bool is_token : 1;
  bool is_alias : 1;
  bool has_prepeep : 1;
  bool has_postpeep : 1;
  bool are_inputs_owned : 1;
  static Rule init()
  {
    Rule self;
    self.ruleset_start = 
    self.is_token
      = self.has_prepeep
      = self.has_postpeep
      = self.is_alias
      = false;
    self.are_inputs_owned = true;
    return self;
  }

  Match _sequence_match(
    Parser&,
    Pack<Unit>&,
    list<Pt>&
  );
  Match _match_str(
    const Parser&,
    View<Unit>,
    list<Pt>&,
    const class_id
  );
  Match _match_rgx(
    Parser&,
    View<Unit>,
    list<Pt>&,
    const class_id
  );
  Match _match_context(
    Parser& p,
    Pack<Unit>& units,
    list<Rule::Pt>& u,
    ctx_id cid
  );
};

constexpr ctx_id GLOBAL_CONTEXT = -1;
struct Context
{
  static constexpr uint32_t MAX_BE = 4;
  uint32_t rulesets_start; // redunduncy in favor of data proximity
                        // (parser.rules[rules_start].rulset_start
                        // == .rulesets_start)
  uint32_t rules_start;
  uint32_t rule_count;
  class_id name;
  rule_id begin[MAX_BE];
  rule_id end[MAX_BE];
  uint8_t be_count; // begin & end count, must always be greater than 0
};

Own<char*> _butch_read_file(Ref<const char*> filename);
Own<char*> _butch_allocpy_view(const View<char> v);
bool _butch_alias_match(
  const Parser& p,
  const Rule& alias,
  const Unit& u
);

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
  FLAG_T _FLAG_START_FIXED_MATCH      = 0b1000U;
  // TODO add more flags
  struct Error
  {
    enum Type {
      NONE, // for internal use only
      DOUBLE_QUOTE_MISMATCH, // .offset
      SINGLE_QUOTE_MISMATCH, // .offset
      CANT_OPEN_FILE,
      UNDECLARED_RULE, // .name
      MISPLACED_PLUS, // .name (name of the rule where the error occured)
      MISPLACED_PREPEEP, // (same as MISPLACED_PLUS)
      DOUBLE_PREPEEP, // (same as MISPLACED_PLUS)
      DOUBLE_POSTPEEP, // (same as MISPLACED_PLUS)
      MISPLACED_NOT, // (same as MISPLACED_PLUS)
      ALIAS_SYNTAX_ERROR, // .name (same as MISPLACED_PLUS)
      CONTEXT_MISSING_BEGIN,
      CONTEXT_MISSING_END,
      CONTEXT_TOO_MANY_INHERITS
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
  list<Context> ctxs;
  list<Rule> rules;
  list<View<char>> classes;
  // these two are read linearly when testing every rule, so it's faster
  // to sequence then in memory
  list<rule_id> rulesets_rules; // TODO: use same cap and length data for both
  list<class_id> rulesets_classes;
  string buff;
  View<char> parsed_code;
  uint32_t flags;
  int flags_data;

  Parser() :
    butch_code(nullptr),
    classes(),
    rules(),
    parsed_code(nullptr, nullptr),
    flags(FLAG_TRIM_SPACE_LITERALS),
    flags_data(0)
  {
    rules.init(); classes.init();
    rulesets_rules.init(); rulesets_classes.init();
    ctxs.init();
    buff.init();
  }

  static Result<Own<Parser>, Error> read(Own<char*> s);
  static inline Result<Own<Parser>, Error> read_file(
    Ref<const char*> filename
  )
  { return Parser::read(_butch_read_file(filename)); }
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
  Error::Type _make_rule(Rule& r, Ref<char*>& s);
  class_id _find_or_make_class(View<char>);
  rule_id _find_rule(class_id);
  ctx_id _find_ctx(class_id);
  bool _check_and_do_rule(
    Rule& r,
    Pack<Unit>& out,
    list<Rule::Pt>& pt_buffer
  );
  // just loads [s.start, s.start+s.len[ U {0} into buff
  //   surround is used to add "\\b" around a regex string 
  const char* _load_str(
    View<char> s,
    View<char> surround = View<char>(nullptr, nullptr),
    View<char> start = View<char>(nullptr, nullptr)
  );
  // same as _load but transforms "\\n" into "\n"
  const char* _comp_str(View<char> s);
  View<char> _rgx_match(View<char> big, View<char> pattern);
  bool _is_to_be_trimmed(uint32_t begin, uint32_t len);
  RulePack operator [] (int index)
  { return {classes[index], rules[index]}; }
  constexpr inline
  bool is_rule(const Rule& r, int index, std::initializer_list<rule_id> tests)
  {
    for (rule_id t : tests)
      if (in_rule(r, index) == t)
        return true;
    return false;
  }
  inline void _push_rc(rule_id rid, class_id cid)
  { rulesets_rules.push(rid); rulesets_classes.push(cid); }
  inline rule_id& in_rule(const Rule& r, int index) const
  { return rulesets_rules[r.ruleset_start + index]; }
  inline class_id& in_class(const Rule& r, int index) const
  { return rulesets_classes[r.ruleset_start + index]; }
  void destroy()
  {
    free(butch_code);
    butch_code = nullptr;
    classes.destroy();
    rules.destroy();
    rulesets_classes.destroy();
    rulesets_rules.destroy();
    ctxs.destroy();
  }
};

inline View<char> Match::s_view(const Parser& p)
{
  return View<char>(p.parsed_code.start+s_start, s_len);
}

#undef FLAG_T
#endif
