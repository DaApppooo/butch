#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
      r.in[i++] = _find_or_make_class(View(start, ++s));
    }
    else if (*s == '\'')
    {
      start = s;
      SKIP_STRING('\'')
      r.in[i++] = _find_or_make_class(View(start, ++s));
    }
    else
    {
      switch (*s)
      {
        case '(':
          r.in[i++] = GROUP_BEGIN; s++; break;
        case ')':
          r.in[i++] = GROUP_END; s++; break;
        case '.':
          r.in[i++] = DOT; s++; break;
        case '+':
          r.in[i++] = PLUS; s++; break;
        default:
          start = s;
          while (!isspace(*s) && *s != '\n')
            s++;
          r.in[i++] = _find_or_make_class(View(start, s));
          break;
      }
    }
    SKIP_SPACES_ONLY;
  }
  if (*s == '#')
    while (*s != 0 && *s != '\n') s++;
}

rule_id Parser::_find_or_make_class(View<char> s)
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
    if (r.parent != NO_PARENT)
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
      if (r.in[i] < 0)
      {
        const char* p = nullptr;
        switch (r.in[i])
        {
          case GROUP_BEGIN: p = " ("; break;
          case GROUP_END: p = " )"; break;
          case PLUS: p = " +"; break;
          case DOT: p = " ."; break;
          case PIPE: p = " |"; break;
          default:
            fprintf(f, "<??? %i \?\?\?>", r.in[i]);
            assert(!"Unknown symbol token.");
            break;
        }
        fprintf(f, "%s", p);
      } else {
        c = classes[r.in[i]].fix();
        fprintf(f, " %s", classes[r.in[i]].start);
        classes[r.in[i]].mov(c);
      }
    }
    fprintf(f, "\n");
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
{ start += units[start].children_len; }


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
    if (in[rpt[-1].pos] == GROUP_END)
      return Rule::TOTAL_FAILURE;
    if (in[rpt[-1].pos] == PIPE)
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
  const View<char> patt = parser.classes[in[rpt[-1].pos]].shorten(1,1);
  if ((s = _butch_str_match(big, patt)).start)
  {
    _next(rpt[-1].pos, units);
    upt[-1]++;
    return {units[upt[-1]].str_begin, units[upt[-1]].str_len, true};
  }
  else
  { // failure
    return _butch_failure(rpt, upt, in, in_len);
  }
  return Rule::TOTAL_FAILURE;
}
Rule::MatchDescriptor Rule::_match_rgx(
  Parser& parser,
  View<Unit> units,
  list<RulePt>& rpt,
  list<int>& upt
) {
  View<char> s;
  const View<char> big = units[upt[-1]].str(parser);
  const View<char> patt = parser.classes[in[rpt[-1].pos]].shorten(1,1);
  if ((s = _butch_rgx_match(big, patt)).start)
  {
    _next(rpt[-1].pos, units);
    upt[-1]++;
    return {units[upt[-1]].str_begin, units[upt[-1]].str_len, true};
  }
  else
  { // failure
    return _butch_failure(rpt, upt, in, in_len);
  }
  return Rule::TOTAL_FAILURE;
}

Rule::MatchDescriptor Rule::_sequence_match(Parser& parser, View<Unit> units)
{
  constexpr MatchDescriptor failure = TOTAL_FAILURE;
  MatchDescriptor ret = {.str_begin = units[-1].str_end(), .str_len=0, .matched=true};
  list<RulePt> rpt; // rule pointer
  list<int> upt; // units pointer
  rpt.init();
  upt.init();
  rpt.push({0, 0});
  upt.push(0).push(0);
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
  while (upt[-1] < units.len)
  {
    switch (parser.atom_type(in[rpt[-1].pos]))
    {
      case ATOM_SYMBOL:
        assert(false); // TODO
        break;
      case ATOM_CLASS:
        if (units[upt[-1]].id != in[rpt[-1].pos])
        {
          if (_butch_failure(rpt, upt, in, in_len) == TOTAL_FAILURE)
            return failure;
        } else {
          _next(rpt[-1].pos, units);
          upt[-1]++;
        }
        break;
      case ATOM_STRING_MATCH:
        if (ret.include(_match_str(parser, units, rpt, upt)))
          return failure;
        break;
      case Rule::ATOM_REGEX_MATCH:
        if (ret.include(_match_rgx(parser, units, rpt, upt)))
          return failure;
        break;
    }
  }
}


bool Parser::_check_and_do_rule(Rule& r, list<Unit>& out)
{
  Rule::MatchDescriptor match;
  bool did_something = false;
  for (int i = 0; i < out.len()-1; i++)
  {
    int j = i+1;
    while (j <= out.len())
    {
      match = r._sequence_match(*this, out.slice(i, j-i));
      if (match.matched)
      {
        out.insert(Unit(r.out, match.str_begin, match.str_len, j-i), i);
        did_something = true;
      }
    }
  }
  return did_something;
}

list<Unit> Parser::parse(Ref<char*> s)
{
  list<Unit> out;
  out.init();

  out.push(Unit(DOT, 0, strlen(s), 0));

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

