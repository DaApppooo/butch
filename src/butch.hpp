#ifndef H_BUTCH
#define H_BUTCH
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <regex>
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
  static constexpr ctx_id ERROR_CONTEXT_MISMATCH = -2;
  static constexpr ctx_id NO_REPARSE = -1;
  uint32_t s_start;
  // number of children units following this unit in the output unit vector
  // representing a tree
  rule_id id; // points to Parser.rules
              // if id == LITERAL then this is unparsed code
  uint32_t s_len;
  ctx_id reparse_ctx;
  uint16_t children_len;
  
  constexpr Unit()
    : s_start(0), id(0), s_len(0), reparse_ctx(0), children_len(0) {}
  static constexpr Unit init(
    class_id id,
    uint32_t begin,
    uint32_t len,
    uint32_t children_count
  ) {
    Unit self;
    self.s_start = begin;
    self.id = id;
    self.s_len = len;
    self.children_len = children_count;
    self.reparse_ctx = NO_REPARSE;
    return self;
  }

  constexpr uint32_t s_end() const { return s_start + s_len; }
  _butch_utils::View<char> str(const Parser&);
  bool is(const Parser&, const _butch_utils::View<const char> s) const;
  bool is(const Parser& p, const char* s) const
  { return is(p, _butch_utils::View<const char>::range(s, _butch_utils::str_len(s))); }
  // use this after collecting rule ids with parser.get_rule(const char*)
  inline bool is(const Parser& p, rule_id rid) const;
};

struct Match
{
  uint32_t s_start;
  uint32_t s_len : 31;
  bool matched : 1;
  // these two fields come with (pre|post)peeping
  uint16_t u_start; // unit view begin (included)
  uint16_t u_end; // unit view end (excluded)
  constexpr uint32_t s_end() { return s_start+s_len; }
  _butch_utils::View<char> s_view(const Parser&);
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
  bool in(Match other)
  {
    return s_start >= other.s_start && s_end() <= other.s_end();
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
  static constexpr rule_id UNKNOWN_REPARSE = -13;
  static constexpr rule_id REPARSE = -14;

  static constexpr class_id ALIAS = -2;
  static constexpr class_id BEGIN = -3;
  static constexpr class_id END = -4;
  

  static constexpr Match PARTIAL_FAILURE = {0, 0, true, 0, 0};
  static constexpr Match TOTAL_FAILURE = {0, 0, false, 0, 0};
  uint32_t ruleset_start;
  uint32_t input_len;
  class_id out;
  uint32_t _flags;
  static Rule init()
  {
    Rule self;
    self._flags = 0;
    return self;
  }
  static Rule init_context(class_id cid, ctx_id ctid)
  {
    assert(ctid >= 0);
    Rule self;
    self.ruleset_start = ctid;
    self.input_len = 0;
    self.is_context(true);
    self.out = cid;
    return self;
  }
  static Rule init_unknown_context(class_id cid)
  {
    Rule self;
    self.ruleset_start = 0;
    self.input_len = 1;
    self.is_context(true); 
    self.out = cid;
    return self;
  }
  static Rule init_unknown_copy(class_id out)
  {
    Rule self;
    self._flags = _UNKNOWN_COPY;
    self.out = out;
    return self;
  }

  static constexpr uint32_t _TOKEN        = 0b0001;
  static constexpr uint32_t _PREPEEP      = 0b0010;
  static constexpr uint32_t _POSTPEEP     = 0b0100;
  static constexpr uint32_t _TWOPEEP      = 0b0110;
  static constexpr uint32_t _RL_PARSE     = 0b1000;

  static constexpr uint32_t _CONTEXT      = 0b0101;
  static constexpr uint32_t _ALIAS        = 0b0011;
  static constexpr uint32_t _UNKNOWN_COPY = 0b1011;
  constexpr bool is_token() const
  { return _flags == _TOKEN; }
  constexpr bool is_token(bool x)
  { if (x) _flags = _TOKEN; return is_token(); }
  constexpr bool has_prepeep() const
  { return _flags & _PREPEEP && !(_flags & _TOKEN); }
  constexpr bool has_prepeep(bool x)
  { if (x) _flags |= PREPEEP; return has_prepeep(); }
  constexpr bool has_postpeep() const
  { return _flags & _POSTPEEP && !(_flags & _TOKEN); }
  constexpr bool has_postpeep(bool x)
  { if (x) _flags |= POSTPEEP; return has_postpeep(); }
  constexpr bool has_rl_parse() const
  { return _flags & _RL_PARSE && !(_flags & _TOKEN); }
  constexpr bool has_rl_parse(bool x)
  { if (x) _flags |= _RL_PARSE; return has_rl_parse(); }
  constexpr bool is_alias() const
  { return _flags == _ALIAS; }
  constexpr bool is_alias(bool x)
  { if (x) _flags = _ALIAS; return is_alias(); }
  constexpr bool is_unknown_copy() const
  { return _flags == _UNKNOWN_COPY; }
  constexpr bool is_context() const
  { return _flags == _CONTEXT; }
  constexpr bool is_context(bool x)
  { if (x) _flags = _CONTEXT; return is_context(); }
  constexpr bool is_unknown_context() const
  { return is_context() && input_len == 1; }
  constexpr void set_context(ctx_id ctid)
  { assert(is_context()); input_len = 0; ruleset_start = ctid; }
  constexpr ctx_id get_context() const
  { assert(is_context() && input_len == 0); return ruleset_start; }
  void _apply_context(
    Parser&,
    _butch_utils::Pack<Unit>&,
    _butch_utils::list<Rule::Pt>&,
    Match m
  );

  Match _sequence_match(
    Parser&,
    _butch_utils::Pack<Unit>&,
    _butch_utils::list<Pt>&
  );
  Match _match_str(
    const Parser&,
    _butch_utils::View<Unit>,
    _butch_utils::list<Pt>&,
    const class_id
  );
  Match _match_rgx(
    Parser&,
    _butch_utils::View<Unit>,
    _butch_utils::list<Pt>&,
    const class_id
  );
  Match _match_context(
    Parser& p,
    _butch_utils::Pack<Unit>& units,
    _butch_utils::list<Rule::Pt>& u
  );
};

constexpr ctx_id GLOBAL_CONTEXT = -1;
struct Context
{
  static constexpr uint32_t MAX_BE = 4;
  static constexpr uint32_t MAX_DS = 2;
  uint32_t rulesets_start; // redunduncy in favor of data proximity
                        // (parser.rules[rules_start].rulset_start
                        // == .rulesets_start)
  uint32_t rules_start;
  class_id name;
  rule_id begin[MAX_BE];
  rule_id end[MAX_BE];
  rule_id depth_stoppers[MAX_DS];
  uint16_t rule_count;
  uint8_t be_count;
  uint8_t ds_count;
};

Own<char*> _butch_read_file(Ref<const char*> filename);
Own<char*> _butch_allocpy_view(const _butch_utils::View<char> v);
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
    : Returns a tree with a linear layout in memoy in a _butch_utils::list (eqv.
      std::vector). Each node is a 'Unit', and has Unit.child_count elements.
  Parser.parse_file(Ref<const char*> filename)
    : Returns a Parser::FileParse containing a string with the content of the
      opened file (can be used to retrieve the content of tokens for example),
      and the _butch_utils::list tree as explained with Parser.parse
*/
struct Parser
{
  using RegexMatch = std::match_results<char*, std::string::allocator_type>;
  using RegexPatt = std::basic_regex<char>;
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
      MISPLACED_COPY, // .offset
      CANT_OPEN_FILE, // none
      UNDECLARED_RULE, //    .name
      UNDECLARED_CONTEXT, // same for all errors from this point
      MISPLACED_PLUS,
      MISPLACED_PREPEEP,
      DOUBLE_PREPEEP,
      DOUBLE_POSTPEEP,
      MISPLACED_NOT,
      ALIAS_SYNTAX_ERROR,
      RULE_SYNTAX_ERROR,
      REGEX_SYNTAX_ERROR,
      REPARSE_SYNTAX_ERROR,
      CONTEXT_MISSING_BEGIN,
      CONTEXT_MISSING_END,
      CONTEXT_SPECS_ERROR,
      DESERIALIZATION_ERROR
    } type;
    union { uint32_t offset; Own<char*> name; };
    constexpr bool owns_name() const
    { return type >= UNDECLARED_RULE; }
  };
  struct Output
  {
    Own<Unit*> units;
    uint32_t ucount;
  };
  struct FileParse
  {
    Own<char*> parsed_code;
    Own<Output> output;
  };
  struct RulePack
  { _butch_utils::View<char> class_name; Rule rule; };
  Own<char*> butch_code;
  _butch_utils::list<Context> ctxs;
  _butch_utils::list<Rule> rules;
  _butch_utils::list<_butch_utils::View<char>> classes;
  // these two are read linearly when testing every rule, so it's faster
  // to sequence then in memory
  _butch_utils::list<rule_id> rulesets_rules; // TODO: use same cap and length data for both
  _butch_utils::list<class_id> rulesets_classes;
  _butch_utils::list<RegexPatt> regex_patterns;
  _butch_utils::string buff;
  _butch_utils::View<char> parsed_code;
  uint32_t flags;
  int flags_data;

  Parser()
  {
    butch_code = nullptr;
    parsed_code = _butch_utils::View<char>::empty();
    flags = FLAG_TRIM_SPACE_LITERALS;
    flags_data = 0;
    rules.init(); classes.init();
    rulesets_rules.init(); rulesets_classes.init();
    regex_patterns.init();
    ctxs.init();
    buff.init();
  }
  Parser(const Parser&) = default;
  Parser(Parser&&) = default;
  Parser& operator = (const Parser&) = default;
  Parser& operator = (Parser&&) = default;

  static Result<Own<Parser>, Error> read(Own<char*> s);
  static Result<Own<Parser>, Error> read_file(
    Ref<const char*> filename
  )
  { return Parser::read(_butch_read_file(filename)); }

  size_t serialize_file(FILE*);
  Own<char*> serialize(size_t*);
  size_t serialize(size_t(*)(const void* src, size_t, void* data), void*);

  static Result<Own<Parser>, Error> deserialize_file(FILE*);
  static Result<Own<Parser>, Error> deserialize(const char*, size_t);
  static Result<Own<Parser>, Error> deserialize(
    size_t(*)(void* dst, size_t, void* data),
    void*
  );

  void debug(FILE* f);
  void debug_output(FILE* f, const _butch_utils::View<Unit> ls);
  Own<Output> parse(Ref<char*> s);
  FileParse parse_file(Ref<const char*> filename)
  {
    char* code = _butch_read_file(filename);
    if (code == nullptr)
    {
      return {nullptr, {nullptr, 0}};
    }
    auto ret = parse(code);
    return {code, ret};
  }
  rule_id get_rule(const char* s) const;

  Error::Type _make_rule(Rule& r, Ref<char*>& s);
  class_id _find_or_make_class(_butch_utils::View<char>);
  rule_id _find_rule(class_id);
  ctx_id _find_ctx(class_id);
  bool _check_and_do_rule(
    Rule& r,
    _butch_utils::Pack<Unit>& out,
    _butch_utils::list<Rule::Pt>& pt_buffer
  );
  // just loads [s.start, s.start+s.len[ U {0} into buff
  //   surround is used to add "\\b" around a regex string 
  const char* _load_str(
    _butch_utils::View<char> s,
    _butch_utils::View<char> surround = _butch_utils::View<char>::empty(),
    _butch_utils::View<char> start = _butch_utils::View<char>::empty()
  );
  // same as _load but transforms "\\n" into "\n"
  const char* _comp_str(_butch_utils::View<char> s);
  /*
    On sucess:
      returns x >= 0, x is the index in .regex_patterns of the compiled regex
    On failure:
      return -1 and push in the classes a special view such that:
        it's a _butch_utils::View<Own<char>> (you need to free the pointer in it)
        its length is 0 (you can retrieve the acutal length via the cstr way)
    The 'rule_output' parameter is used to distinguish Begin/End from other
    rules.
  */
  int32_t _comp_rgx(const _butch_utils::View<char> patt, rule_id rule_output);
  _butch_utils::View<char> _rgx_match(_butch_utils::View<char> big, const RegexPatt& pattern);
  bool _should_be_trimmed(uint32_t begin, uint32_t len);
  RulePack operator [] (int index)
  { return {classes[index], rules[index]}; }
  constexpr inline bool is_rule(
    const Rule& r,
    int index,
    std::initializer_list<rule_id> tests
  ) {
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
    buff.destroy();
    ctxs.destroy();
    for (uint32_t i = 0; i < regex_patterns.len(); i++)
    {
      regex_patterns[i].~basic_regex();
    }
    regex_patterns.destroy();
  }
};

inline _butch_utils::View<char> Match::s_view(const Parser& p)
{
  return _butch_utils::View<char>::range(p.parsed_code.start+s_start, s_len);
}

inline bool Unit::is(const Parser& p, rule_id rid) const
{
  return id == rid || p.rules[rid].out == p.rules[id].out;
}

#undef FLAG_T
#endif

