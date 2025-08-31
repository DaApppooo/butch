#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>
// #include <regex>
#include "butch.hpp"


Own<char*> _butch_read_file(Ref<const char*> filename)
{
    FILE* f = fopen(filename, "r");
    if (f == nullptr) return nullptr;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    auto s = new char[len+1];
    fread(s, 1, len, f);
    fclose(f);
    s[len] = 0;
    return s;
}

#define SKIP_SPACES_ONLY \
    { \
      while ((*s == ' ' || *s == '\t') && *s != 0) s++; \
    }
#define SKIP_STRING(str_char) \
  while (*(++s) != str_char) \
    if (*s == '\\') \
      s++;

View<char> Unit::str(Parser& p)
{
  return View<char>(p.parsed_code+str_begin, str_len);
}

Result<int,int> _butch_count_units(char* s)
{
  int input_count = 0;
  while (*s != '\n' && *s != '#' && *s != 0)
  {
    if (*s == '"')
    {
      SKIP_STRING('"')
      expect(*s != 0, int, 0);
      input_count++; s++;
    }
    else if (*s == '\'')
    {
      SKIP_STRING('\'')
      expect(*s != 0, int, 1);
      input_count++; s++;
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
          while (!isspace(*s) && *s != '\n')
            s++;
          break;
      }
    }
    SKIP_SPACES_ONLY
  }
  return ok<int>(input_count);
}

void Parser::_make_rule(Rule& r, char*& s)
{
  char* start = s;
  int i = 0;
  while (*s != '\n' && *s != '#')
  {
    if (*s == '"')
    {
      start = s;
      SKIP_STRING('"')
      r.in_rule(Rule::STRING);
      r.in_class(i++) = _find_or_make_class(View(start, ++s));
    }
    else if (*s == '\'')
    {
      start = s;
      SKIP_STRING('\'')
      r.in_rule(Rule::REGEX);
      r.in_class(i++) = _find_or_make_class(View(start, ++s));
    }
    else
    {
      switch (*s)
      {
        case '(':
          r.in_rule(i++) = Rule::GROUP_BEGIN; s++; break;
        case ')':
          r.in_rule(i++) = Rule::GROUP_END; s++; break;
        case '.':
          r.in_rule(i++) = Rule::DOT; s++; break;
        case '+':
          r.in_rule(i++) = Rule::PLUS; s++; break;
        default:
          start = s;
          while (!isspace(*s) && *s != '\n')
            s++;
          r.in_class(i) = _find_or_make_class(View(start, s));
          r.in_rule(i) = _find_rule(r.in_class(i));
          break;
      }
    }
    SKIP_SPACES_ONLY;
  }
  if (*s == '#')
    while (*s != 0 && *s != '\n') s++;
}

class_name_id Parser::_find_or_make_class(View<char> s)
{
  for (int i = 0; i < classes.len(); i++)
  {
    View<char> v = classes[i];
    if (v.len != s.len)
      continue;
    if (memcmp(v.start, s.start, v.len) == 0)
      return i;
  }
  classes.push(s);
  return classes.len()-1;
}

rule_id Parser::_find_rule(class_name_id class_name)
{
  for (int i = 0; i < rules.len(); i++)
  {
    if (rules[i].out == class_name)
      return i;
  }
  return Rule::UNKNOWN_RULE;
}

Result<Parser, Parser::Error> Parser::read(Own<char*> s)
{
  if (s == nullptr)
    return err<Parser>(Error{Error::CANT_OPEN_FILE, 0});
  Parser self;
  self.butch_code = s;
  
  View<char> class_name;
  View<char> parent_class_name;
  char* start = s;
#define CHECK_COMMENTS \
    if (*s == '#') \
    { \
      while (*s != '\n' && *s != 0) s++; \
      s++; \
      continue; \
    }

  while (*s != 0)
  {
    if (*s == '\n')
    { s++; continue; }
    SKIP_SPACES_ONLY;
    CHECK_COMMENTS;

    { // class name (and parent class)
      start = s;
      parent_class_name.start = nullptr;
      while (*s != '=' && !isspace(*s) && *s != '/' && *s != 0)
        s++;
      char* const temp = s;
      SKIP_SPACES_ONLY;
      if (*s == '/')
      {
        parent_class_name = View<char>(start, temp);
        s++;
        SKIP_SPACES_ONLY;
        start = s;
        while (*s != '=' && !isspace(*s) && *s != 0)
          s++;
      }
      else
        s = temp;
      class_name = View<char>(start, s);
    }
    
    { // handle midway space and '='
      while (*s == '=' || isspace(*s))
        s++;
      start = s;
    }
    
    { // class rule
      auto res = _butch_count_units(s);
      if (res.type == ERR)
      {
        self.destroy();
        if (res.err == 0)
          return err<Parser>(Error{
            Error::DOUBLE_QUOTE_MISMATCH,
            uint32_t(s - self.butch_code)
          });
        else
          return err<Parser>(Error{
            Error::SINGLE_QUOTE_MISMATCH,
            uint32_t(s - self.butch_code)
          });
      }
      self.rules.push(Rule::init(res.ok));
      Rule& r = self.rules[-1];
      r.out = self._find_or_make_class(class_name);
      if (parent_class_name.start)
        r.parent = self._find_or_make_class(parent_class_name);
      self._make_rule(r, s);
    }
    assert(*s == '\n' || *s == 0);
    if (*s == '\n')
      s++;
  }

  // resolve rules with Rule.in_rule(x) == UNKNOWN_RULE
  for (uint32_t i = 0; i < self.rules.len(); i++)
  {
    Rule& r = self.rules[i];
    for (int j = 0; j < r.in_len; j++)
    {
      if (r.in_rule(j) == Rule::UNKNOWN_RULE)
      {
        const rule_id find_test = self._find_rule(r.in_class(j));
        if (find_test == Rule::UNKNOWN_RULE)
          return err<Parser>(Error{Error::UNDECLARED_RULE, i});
        r.in_rule(j) = find_test;
      }
    }
  }
  
  return ok<Error>(self);
}

void Parser::debug(FILE* f)
{
  char c;
  fprintf(f, "=== Classes ===\n");
  for (View<char>& t : classes)
  {
    c = t.fix();
    fprintf(f, "%s\n", t.start);
    t.mov(c);
  }
  fprintf(f, "=== Rules ===\n");
  for (Rule& r : rules)
  {
    if (r.parent != Rule::NO_PARENT)
    {
      c = classes[r.parent].fix();
      fprintf(f, "%s/", classes[r.parent].start);
      classes[r.parent].mov(c);
    }
    c = classes[r.out].fix();
      fprintf(f, "%s =", classes[r.out].start);
    classes[r.out].mov(c);
    for (int i = 0; i < r.in_len; i++)
    {
      if (r.in_rule(i) < 0 && r.in_rule(i) > Rule::STRING)
      {
        const char* p = nullptr;
        switch (r.in_rule(i))
        {
          case Rule::GROUP_BEGIN: p = " ("; break;
          case Rule::GROUP_END: p = " )"; break;
          case Rule::PLUS: p = " +"; break;
          case Rule::DOT: p = " ."; break;
          case Rule::PIPE: p = " |"; break;
          default:
            fprintf(f, "<??? %i \?\?\?>", r.in_rule(i));
            assert(!"Unknown symbol token.");
            break;
        }
        fprintf(f, "%s", p);
      } else {
        c = classes[r.in_class(i)].fix();
        fprintf(f, " %s", classes[r.in_class(i)].start);
        classes[r.in_class(i)].mov(c);
      }
    }
    fprintf(f, "\n");
  }
}

void Parser::debug_output(FILE* f, const list<Unit> ls)
{
  int8_t depth_pt = 0;
  int32_t depth_stack[127];
  for (int i = 0; i < ls.len(); i++)
  {
    const View<char> s =
      ls[i].id == Unit::LITERAL ? ls[i].str(*this)
                                : classes[rules[ls[i].id].out];
    for (int j = 0; j < s.len; j++)
      putc(s[j], f);
    putc(' ', f);
    if (ls[i].children_len > 0)
    {
      fprintf(f, "( ");
      depth_stack[depth_pt++] = ls[i].children_len+1;
    }
    for (int j = depth_pt-1; j >= 0; j--)
    {
      depth_stack[j]--;
      if (depth_stack[j] <= 0)
      {
        depth_pt--;
        fprintf(f, ") ");
      }
    }
  }
}

const char* Parser::_load_str(View<char> s)
{
  if (buff._cap < s.len+1)
    buff.prealloc(s.len+1);
  memcpy(buff.data(), s.start, s.len);
  buff[s.len] = 0;
  return buff.data();
}

View<char> _butch_str_match(View<char> big, View<char> pattern)
{
  for (int i = 0; i < big.len - pattern.len; i++)
  {
    if (memcmp(big.start+i, pattern.start, pattern.len) == 0)
      return {big.start+i, pattern.len};
  }
  return View<char>(nullptr, nullptr);
}
View<char> Parser::_rgx_match(View<char> big, View<char> pattern)
{
  // WARN: C++ vomit ahead
  using Match = std::match_results<char*, std::string::allocator_type>;
  std::regex patt(pattern.start+1, pattern.start+pattern.len-2);
  Match match;
  if ((bool(*)(
    char* __first, char* __last,
    Match& m,
    const std::basic_regex<char, std::regex_traits<char>>& e,
    std::regex_constants::match_flag_type flags
  ))(std::regex_search)(
    big.start, big.start+big.len,
    match,
    patt
  )) {
    return View<char>(
      (char*)&*match[0].str().begin(),
      (char*)&*match[0].str().end()
    );
  }
  return View<char>(nullptr, nullptr);
}

inline void _next(int& start, View<Unit> units)
{ start += units[start].children_len+1; }


Rule::MatchDescriptor _butch_failure(
  list<Rule::RulePt>& rpt,
  list<int>& upt,
  rule_id* in,
  int rule_len
) {
  if (rpt.len() > 1)
  {
    rpt.pop();
    return Rule::PARTIAL_FAILURE;
  }
  const int depth = rpt[-1].depth;
  for (;;)
  {
    rpt[-1].pos++;
    if (rpt[-1].pos >= rule_len)
      return Rule::TOTAL_FAILURE;
    if (in[rpt[-1].pos] == Rule::GROUP_END)
      return Rule::TOTAL_FAILURE;
    if (in[rpt[-1].pos] == Rule::PIPE)
    { rpt[-1].pos++; return Rule::PARTIAL_FAILURE; }
  }
}

Rule::MatchDescriptor Rule::_match_str(
  Parser& parser,
  View<Unit> units,
  list<RulePt>& rpt,
  list<int>& upt
) {
  View<char> s;
  const View<char> big = units[upt[-1]].str(parser);
  const View<char> patt = parser.classes[in_class(rpt[-1].pos)].shorten(1,1);
  if ((s = _butch_str_match(big, patt)).start)
  {
    rpt[-1].pos++;
    _next(upt[-1], units);
    return {units[upt[-1]].str_begin, units[upt[-1]].str_len, true};
  }
  else
  { // failure
    return _butch_failure(rpt, upt, in_rules, in_len);
  }
  return Rule::TOTAL_FAILURE;
}
Rule::MatchDescriptor Rule::_match_rgx(
  Parser& parser,
  View<Unit> units,
  list<int>& rpt,
  list<int>& upt
) {
  View<char> s;
  const View<char> big = units[upt[-1]].str(parser);
  const View<char> patt = parser.classes[in_class(rpt[-1])].shorten(1,1);
  if ((s = parser._rgx_match(big, patt)).start)
  {
    rpt[-1].pos++;
    _next(upt[-1], units);
    return {units[upt[-1]].str_begin, units[upt[-1]].str_len, true};
  }
  else
  { // failure
    return _butch_failure(rpt, upt, in_rules, in_len);
  }
  return Rule::TOTAL_FAILURE;
}

Rule::MatchDescriptor Rule::_sequence_match(Parser& parser, View<Unit> units)
{
  constexpr MatchDescriptor failure = TOTAL_FAILURE;
  MatchDescriptor ret = {.str_begin = units[-1].str_end(), .str_len=0, .matched=true};
  list<RulePt> u;
  u.init();
  u.push({0, 0}).push({0, 0});
  /*
  How the algorithm works:
  Each (parenthesis) depth level is split in "sectors" (split by PIPE unit).
  Each sector is a sequence that could potentially match.
  rpt stack stores options before sector failure (sector failure means one element in the sequence did not match)
    When an element fails to match:
      If rpt.len() > 1: rpt.pop()
      Else: skip until next sector with the same depth (triggers sector failure in 'upt' conditional section)
        If GROUP_END is reached, then: return failure
    When a PIPE is reached: skip until next unit after GROUP_END of current depth
    When a PLUS is reached:
      If units[rpt[-1]-1] is GROUP_END:
        find GROUP_BEGIN with same depth at 'position';
        rpt.push(position);
        rpt.swap(-1,-2);
        rpt[-1] += (next)
      Else:
        rpt.push(rpt[-1]);
        rpt[-2]--;
        rpt[-1] += (next)
    When a DOT is reached: rpt[-1] += (next)
  upt stack stores start of each depth:
    When a sector fails: upt[-1] = upt[-2].
    When a new depth level is entered: upt.push(upt[-1])
    When a depth level is exited: upt[-2] = upt[-1]; upt.pop()

  */
  while (u[-1].r < in_len && u[])
  {
    switch (in_rule(rpt[-1]))
    {
      case GROUP_BEGIN:
        upt.push(upt[-1]);
        rpt[-1]++;
        break;
      case PIPE:
        {
        int depth = 0;
        rpt.push(rpt[-1]+1);
        while (in_rule(rpt[-1]) != GROUP_END || depth > 0)
        {
          if (in_rule(rpt[-1]) == GROUP_BEGIN)
            depth++;
          if (in_rule(rpt[-1]) == GROUP_END)
            depth--;
          rpt[-1]++;
        }
        [[fallthrough]];
        }
      case GROUP_END:
        upt[-2] = upt[-1];
        upt.pop();
        rpt[-1]++;
        break;
      case DOT:
        rpt[-1]++;
        break;
      case PLUS:
        if (in_rule(rpt[-1]-1) == GROUP_END)
        {
          int p = rpt[-1]-2;
          int depth = 0;
          while (p > 0 && (in_rule(p) != GROUP_BEGIN || depth != 0))
          {
            if (in_rule(p) == GROUP_END)
              depth++;
            else if (in_rule(p) == GROUP_BEGIN)
              depth--;
            p--;
          }
          rpt.push(p);
          rpt.swap(-1, -2);
        }
        else
          rpt.push(rpt[-1]);
          rpt[-2]--;
        rpt[-1]++;
        break;
      case STRING:
        if (ret.include(_match_str(parser, units, rpt, upt)))
          return failure;
        break;
      case REGEX:
        if (ret.include(_match_rgx(parser, units, rpt, upt)))
          return failure;
        break;
      default:
        if (
          units[upt[-1]].id != in_rule(rpt[-1])
      &&  parser.rules[units[upt[-1]].id].parent == in_rule(rpt[-1])
        ) {
          if (_butch_failure(rpt, upt, in_rules, in_len) == TOTAL_FAILURE)
            return failure;
        } else {
          rpt[-1]++;
          _next(upt[-1], units);
        }
        break;
    }
  }
  return ret;
}

bool Parser::_is_to_be_trimmed(uint32_t begin, uint32_t len)
{
  if (!(flags & FLAG_TRIM_SPACE_LITERALS))
    return false;
  for (uint32_t i = 0; i < len; i++)
  {
    if (!isspace(parsed_code[begin+i]))
      return false;
  }
  return true;
}

bool Parser::_check_and_do_rule(Rule& r, list<Unit>& out)
{
  Rule::MatchDescriptor match;
  bool did_something = false;
  Rule::AtomType atomt;

  if (
     r.in_len == 1
  && ((atomt=atom_type(r.in[0])) == Rule::ATOM_REGEX_MATCH
    || atomt == Rule::ATOM_STRING_MATCH)
  ) {
    // token
    for (int i = 0; i < out.len(); _next(i, out.to_view()))
    {
      if (out[i].id == Unit::LITERAL)
      {
        match = r._sequence_match(*this, out.slice(i, 1));
        if (match.matched)
        {
          const uint32_t initial_length = out[i].str_len;
          const uint32_t pre_len = match.str_begin - out[i].str_begin;
          out[i].str_len = pre_len;
          out.insert(Unit(r.out, match.str_begin, match.str_len, 0), i+1);
          if (
             initial_length - pre_len - match.str_len > 0
          && !(_is_to_be_trimmed(
                match.str_begin+match.str_len,
                initial_length - pre_len - match.str_len
              ))
          ) {
            out.insert(Unit(
              Unit::LITERAL,
              match.str_begin+match.str_len,
              initial_length - pre_len - match.str_len,
              0
            ), i+2);
          }
          if (pre_len == 0 || _is_to_be_trimmed(out[i].str_begin, pre_len))
            out.rmv(i);
          did_something = true;
        }
      }
    }
  }
  else if (r.parent == NO_PARENT)
  {
    // top-class abstract syntax node type
    for (int i = 0; i < out.len()-1; _next(i, out.to_view()))
    {
      int j = i+1;
      while (j <= out.len())
      {
        match = r._sequence_match(*this, out.slice(i, j-i));
        if (match.matched)
        {
          out.insert(Unit(r.out, match.str_begin, match.str_len, j-i),i);
          did_something = true;
        }
        _next(j, out.to_view());
      }
    }
  } else {
    // subclass
    for (int i = 0; i < out.len(); _next(i, out.to_view()))
    {
      if (out[i].id == r.parent)
      {
        match = r._sequence_match(*this, out.slice(i+1, out[i].children_len));
        if (match.matched)
        {
          out[i].id = r.out;
          did_something = true;
        }
      }
    }
  }
  return did_something;
}

list<Unit> Parser::parse(Ref<char*> s)
{
  list<Unit> out;
  out.init();
  parsed_code = s;

  out.push(Unit(Unit::LITERAL, 0, str_len(s), 0));

  for (;;)
  {
    bool did_something = false;
    for (Rule& r : rules)
    {
      did_something = did_something
                      || _check_and_do_rule(r, out);
    }
    if (!did_something) break;
  }

  return out;
}

