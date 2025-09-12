#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <regex.h>
// #include <regex>
#include "butch.hpp"

#define assume(X) do { assert(X); [[assume(X)]]; } while(false)

#define ALIAS_CHAR '?'


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

Own<char*> _butch_allocpy_view(const View<char> v)
{
  char* ret = (char*)malloc(v.len+1);
  memcpy(ret, v.start, v.len);
  ret[v.len] = 0;
  return ret;
}

#define SKIP_SPACES_ONLY \
    { \
      while ((*s == ' ' || *s == '\t') && *s != 0) s++; \
    }
#define SKIP_STRING(str_char) \
  while (*(++s) != str_char) \
    if (*s == '\\') \
      s++;

View<char> Unit::str(const Parser& p)
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
        case '|':
        case '[':
        case ']':
        case '!':
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

Parser::Error::Type Parser::_make_rule(Rule& r, char*& s)
{
  char* start = s;
  int i = 0;
  while (*s != '\n' && *s != '#')
  {
    if (*s == '"')
    {
      start = s;
      SKIP_STRING('"')
      r.in_rule(i) = Rule::STRING;
      r.in_class(i++) = _find_or_make_class(View(start, ++s));
    }
    else if (*s == '\'')
    {
      start = s;
      SKIP_STRING('\'')
      r.in_rule(i) = Rule::REGEX;
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
        case '|':
          r.in_rule(i++) = Rule::PIPE; s++; break;
        case '!':
          if (i+1 == r.in_len)
            return Error::MISPLACED_NOT;
          r.in_rule(i++) = Rule::NOT; s++; break;
        case '[':
          if (r.has_postpeep)
            return Error::MISPLACED_PREPEEP;
          if (r.has_prepeep)
            return Error::DOUBLE_PREPEEP;
          r.has_prepeep = true;
          r.in_rule(i++) = Rule::PREPEEP; s++; break;
        case ']':
          if (r.has_postpeep)
            return Error::DOUBLE_POSTPEEP;
          r.has_postpeep = true;
          r.in_rule(i++) = Rule::POSTPEEP; s++; break;
        default:
          start = s;
          while (!isspace(*s) && *s != '\n')
            s++;
          r.in_class(i) = _find_or_make_class(View(start, s));
          r.in_rule(i) = _find_rule(r.in_class(i));
          i++;
          break;
      }
    }
    SKIP_SPACES_ONLY;
  }
  if (*s == '#')
    while (*s != 0 && *s != '\n') s++;
  return Error::NONE;
}

class_name_id Parser::_find_or_make_class(View<char> s)
{
  const uint32_t l = classes.len();
  for (uint32_t i = 0; i < l; i++)
  {
    View<char> v = classes.atp(i);
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
  for (uint32_t i = 0; i < rules.len(); i++)
  {
    if (rules.atp(i).out == class_name)
      return i;
  }
  return Rule::UNKNOWN_RULE;
}
rule_id Parser::_find_next(rule_id rid)
{
  const class_name_id class_name = rules.atp(rid).out;
  for (uint32_t i = rid+1; i < rules.len(); i++)
  {
    if (rules.atp(i).out == class_name)
      return i;
  }
  return Rule::UNKNOWN_RULE;
}

Result<Parser, Parser::Error> Parser::read(Own<char*> s)
{
#define ALLOCPY_CN(CLASS_ID) _butch_allocpy_view(self.classes[CLASS_ID])
#define ERROR_CN(ERR, CLASS_ID) \
          { start = ALLOCPY_CN(CLASS_ID); \
          self.destroy(); \
          return err<Parser>(Error{ \
            .type=ERR, \
            .name=start \
          }); }
  constexpr int CPY_NEXT = 1;
  constexpr int CPY_SELF = 2;
  if (s == nullptr)
    return err<Parser>(Error{Error::CANT_OPEN_FILE, 0});
  Parser self;
  self.butch_code = s;
  
  View<char> class_name;
  View<char> recurse_in;
  char* start = s;
  int cpy_flag = 0;
#define CHECK_COMMENTS \
    if (*s == '#') \
    { \
      while (*s != '\n' && *s != 0) s++; \
      s++; \
      continue; \
    }

  while (*s != 0)
  {
    SKIP_SPACES_ONLY;
    if (*s == '\n')
    { s++; continue; }
    CHECK_COMMENTS;

    { // class name (and parent class)
      start = s;
      recurse_in.start = nullptr;
      while (*s != '=' && *s != ':' && !isspace(*s) && *s != '/' && *s != 0)
        s++;
      char* const temp = s;
      SKIP_SPACES_ONLY;
      if (*s == '/')
      {
        recurse_in = View<char>(start, temp);
        s++;
        SKIP_SPACES_ONLY;
        start = s;
        while (*s != '=' && *s != ':' && !isspace(*s) && *s != 0)
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
    
    if (*class_name.start == '*' && class_name.len == 1)
    { // recurse everything
      if (recurse_in.len == 0)
      {
        self.destroy();
        return err<Parser>(Error{
          Error::RECURSION_SYNTAX_ERROR,
          uint32_t(s - self.butch_code)
        });
      }
      const class_name_id cn = self._find_or_make_class(recurse_in);
      rule_id rid = self._find_rule(cn);
      if (rid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::UNDECLARED_RULE, cn);
      while (
         rid != Rule::UNKNOWN_RULE
      && self.rules[rid].recurse != Rule::NO_RECURSE
      )
      { rid = self._find_next(rid); }
      if (rid == Rule::UNKNOWN_RULE)
      {
        self.rules.push(Rule::init(0));
        Rule& r = self.rules.atn(-1);
        r.out = cn;
        r.recurse = Rule::RECURSE_ALL;
        r.are_inputs_owned = false;
      }
      else
        self.rules.atp(rid).recurse = Rule::RECURSE_ALL;
      continue;
    }
    
    if (*s == ':')
    {
      if (*(s+1) == ':')
        cpy_flag = CPY_SELF;
      else
        cpy_flag = CPY_NEXT;
      SKIP_SPACES_ONLY;
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
      if (cpy_flag)
        self.rules.push(Rule::init(0));
      else
        self.rules.push(Rule::init(res.ok));
      Rule& r = self.rules[-1];
      r.out = self._find_or_make_class(class_name);
      if (recurse_in.start)
        r.recurse = self._find_or_make_class(recurse_in);
      if (cpy_flag == 0)
      {
        Error::Type error = self._make_rule(r, s);
        if (error != Error::NONE)
          ERROR_CN(error, r.out);
      }
      else
      {
        rule_id rid;
        if (cpy_flag == CPY_SELF)
          rid = self._find_rule(r.out);
        else
        {
          start = s;
          while (*s != '\n' && *s != ' ' && *s != 0)
          { s++; }
          const class_name_id cn = self._find_or_make_class(View<char>(start, s));
          rid = self._find_rule(cn);
        }
        if (rid == Rule::UNKNOWN_RULE)
          ERROR_CN(Error::UNDECLARED_RULE, r.out);
        const Rule& src = self.rules[rid];
        r.in_rules = src.in_rules;
        r.in_class_names = src.in_class_names;
        r.is_alias = src.is_alias;
        r.is_token = src.is_token;
        r.has_prepeep = src.has_prepeep;
        r.has_postpeep = src.has_postpeep;
        r.are_inputs_owned = false;
        cpy_flag = 0;
      }
    }
    assume(*s == '\n' || *s == 0);
    if (*s == '\n')
      s++;
  }

  // resolve rules with Rule.in_rule(x) == UNKNOWN_RULE
  for (uint32_t i = 0; i < self.rules.len(); i++)
  {
    Rule& r = self.rules[i];
    if (r.in_len == 0)
      continue;
    if (r.in_rule(0) == Rule::PLUS)
      ERROR_CN(Error::MISPLACED_PLUS, r.out);
    r.is_token = true;
    for (int j = 0; j < r.in_len; j++)
    {
      if (
        r.is_alias
        && r.in_rule(j) < 0
        && r.in_rule(j) != Rule::UNKNOWN_RULE
      )
        ERROR_CN(Error::ALIAS_SYNTAX_ERROR, r.out);
      if (r.in_rule(j) == Rule::UNKNOWN_RULE)
      {
        const rule_id find_test = self._find_rule(r.in_class(j));
        if (find_test == Rule::UNKNOWN_RULE)
          ERROR_CN(Error::UNDECLARED_RULE, r.in_class(j));
        r.in_rule(j) = find_test;
      }
      if (j > 0 && r.is_rule(j-1, {Rule::NOT}) && r.in_rule(j) < 0)
        ERROR_CN(Error::MISPLACED_NOT, r.in_class(j));
      if (!r.is_rule(j, {Rule::STRING, Rule::REGEX, Rule::PIPE}))
        r.is_token = false;
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
  int i = 0;
  for (Rule& r : rules)
  {
    fprintf(f, "%04i ", i++);
    if (r.recurse == Rule::RECURSE_ALL)
    {
      c = classes[r.out].fix();
      fprintf(f, "%s/* =", classes[r.out].start);
      classes[r.out].mov(c);
    }
    else
    {
      if (r.recurse != Rule::NO_RECURSE)
      {
        c = classes[r.recurse].fix();
        fprintf(f, "%s/", classes[r.recurse].start);
        classes[r.recurse].mov(c);
      }
      c = classes[r.out].fix();
        fprintf(f, "%s =", classes[r.out].start);
      classes[r.out].mov(c);
    }
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
          case Rule::NOT: p = " !"; break;
          case Rule::PREPEEP: p = " ["; break;
          case Rule::POSTPEEP: p = " ]"; break;
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
      if (r.in_rule(i) >= 0)
        fprintf(f, " [%i]", r.in_rule(i));
    }
    fprintf(f, "\n");
  }
}

bool _butch_alias_match(const Parser& p, const Rule& alias, const Unit& u)
{
  assert(alias.is_alias);
  for (int i = 0; i < alias.in_len; i++)
  {
    if (alias.in_rule(i) == u.id)
      return true;
    else if (p.rules[alias.in_rule(i)].is_alias)
      if (_butch_alias_match(p, p.rules[alias.in_rule(i)], u))
        return true;
  }
  return false;
}

bool Unit::is(const Parser& p, const View<char> s)
{
  if (s.start == nullptr) return false;
  if (id == LITERAL) return false;
  Rule* r = nullptr;
  rule_id rid;
  for (int i = 0; i < p.rules.len(); i++)
  {
    if (p.classes[p.rules[i].out] == s)
    { r = p.rules.data() + i; rid = i; break; }
  }
  if (!r) return false;
  if (r->is_alias)
    return _butch_alias_match(p, *r, *this);
  else
    return id == rid;
}

void Parser::debug_output(FILE* f, const View<Unit> ls)
{
  int8_t depth_pt = 0;
  int32_t depth_stack[127];
  for (int i = 0; i < ls.len; i++)
  {
    for (int j = 0; j < depth_pt; j++)
      fprintf(f, "  ");
    const View<char> s =
      ls[i].id == Unit::LITERAL ? ls[i].str(*this)
                                : classes[rules[ls[i].id].out];
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    for (int j = 0; j < s.len; j++)
      putc(s[j], f);
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    putc(' ', f);
    fprintf(f, "(%i) ", ls[i].children_len);
    if (ls[i].id != Unit::LITERAL && rules[ls[i].id].is_token)
      fprintf(f, "'%s'\n", _load_str(ls[i].str(*this)));
    else
      fprintf(f, "[%i:+%i]\n", ls[i].str_begin, ls[i].str_len);
    if (ls[i].children_len > 0)
    {
      depth_stack[depth_pt++] = ls[i].children_len+1;
    }
    for (int j = depth_pt-1; j >= 0; j--)
    {
      depth_stack[j]--;
      if (depth_stack[j] <= 0)
      {
        depth_pt--;
      }
    }
  }
}

const char* Parser::_load_str(View<char> s, View<char> surround)
{
  assume(s.len > 0); // undefined behavior otherwise
  assume(bool(surround.start) != (surround.len == 0));
  if (buff._cap < s.len+1+surround.len*2)
    buff.prealloc(s.len+1);
  if (surround.start)
    memcpy(buff.data(), surround.start, surround.len);
  memcpy(buff.data()+surround.len, s.start, s.len);
  if (surround.start)
    memcpy(buff.data()+s.len+surround.len, surround.start, surround.len);
  buff.data()[s.len+surround.len*2] = 0;
  return buff.data();
}

int _butch_hex2byte(const char* const p)
{
  int res = 0;
  for (int i = 0; i < 2; i++)
  {
    const char c= *(p+i);
    if ('0' <= c && c <= '9')
    { res |= (c - '0')<<(4*(1-i)); }
    else if ('a' <= c && c <= 'f')
    { res |= (c - 'a' + 10)<<(4*(1-i)); }
    else if ('A' <= c && c <= 'F')
    { res |= (c - 'A' + 10)<<(4*(1-i)); }
    else
      return -1;
  }
  return res;
}

const char* Parser::_comp_str(View<char> s)
{
  _load_str(s);
  int off = 0;
  int suc;
  const int len = buff.len();
  for (int i = 0; i < len-off; i++)
  {
    if (i == len-off-1)
    { buff[i] = buff[i+off]; break; }
    if (buff[i+off] == '\\')
    {
      switch (buff[i+off+1])
      {
        case 'n': buff[i] = '\n'; break;
        case 't': buff[i] = '\t'; break;
        case 'e': buff[i] = '\e'; break;
        case 'r': buff[i] = '\r'; break;
        case 'a': buff[i] = '\a'; break;
        case 'x':
          if (i >= len-off-4)
          { buff[i] = buff[i+off]; off--; }
          else
          {
            if ((suc = _butch_hex2byte(buff.data()+i+off+2)) >= 0)
            {
              buff[i] = suc;
              off += 2;
            }
            else { buff[i] = buff[i+off]; off--; }
          }
        default:
          buff[i] = buff[i+off];
          off--;
      }
      off++;
    }
    else
      buff[i] = buff[i+off];
  }
  buff.data()[len-off] = 0;
  return buff.data();
}

View<char> _butch_str_match(View<char> big, View<char> pattern, bool iso)
{
  if (big.len < pattern.len)
    return View<char>(nullptr, nullptr);
  for (int i = 0; i < big.len - pattern.len; i++)
  {
    if (memcmp(big.start+i, pattern.start, pattern.len) == 0)
    {
      if (!iso)
        return {big.start+i, pattern.len};
      bool left = i == 0,
           right = i == big.len - pattern.len - 1;
      if (!left) // ensure that no deref is done if i == 0
        left = isspace(big.start[i-1])
               || !isalnum(big.start[i-1])
               || !isalnum(big.start[i]);
      if (!right) // same thing
        right = isspace(big.start[i+pattern.len])
                || !isalnum(big.start[i+pattern.len])
                || !isalnum(big.start[i+pattern.len-1]);
      if (left && right)
        return {big.start+i, pattern.len};
    }
  }
  return View<char>(nullptr, nullptr);
}
View<char> Parser::_rgx_match(View<char> big, View<char> pattern)
{
  constexpr View<char> failed = View<char>(nullptr, nullptr);
  regex_t patt;
  int res;
  regmatch_t match;
  const View<char> surround = flags & FLAG_ISOLATED_MODE_REGEX
                              ? View<char>((char*)"\\b", 2)
                              : View<char>(nullptr, nullptr);
  const char* const cstr_patt = _load_str(pattern, surround);
  res = regcomp(&patt, cstr_patt, REG_EXTENDED);
  if (res)
  {
    buff.prealloc(256);
    regerror(res, &patt, buff.data(), buff._cap);
    fprintf(stderr, "WARN: Failure to compile regex '%s': %s\n",
            cstr_patt, buff.data());
    return failed;
  }
  res = regexec(&patt, _load_str(big), 1, &match, 0);
  regfree(&patt);
  if (!res)
    return View<char>(big.start+match.rm_so, big.start+match.rm_eo);
  return failed;
}

inline void _next(uint32_t& start, View<Unit> units)
{ start += units[start].children_len+1; }

Rule::MatchDescriptor _butch_failure(
  list<Rule::RulePt>& u,
  rule_id* in,
  int rule_len
) {
  int m = -1, depth;
  while (m < 0)
  {
    m = u[-1].r;
    depth = 0;
    while (in[m] != Rule::PIPE || depth > 0)
    {
      if (in[m] == Rule::GROUP_BEGIN)
        depth++;
      else if (in[m] == Rule::GROUP_END)
        depth--;
      m++;
      if (m >= rule_len || (in[m] == Rule::GROUP_END && depth == 0))
      {
        m = -1; // failed to find a new m
        break;
      }
    }
    if (m == -1)
    {
      if (u.len() > 2)
      {
        u[-2] = u[-1];
        u.pop();
      }
      else
        return Rule::TOTAL_FAILURE;
    }
    else
    {
      u[-2].r = u[-1].r = m+1;
      u[-1].u = u[-2].u;
    }
  }
  return Rule::PARTIAL_FAILURE;
}

Rule::MatchDescriptor Rule::_match_str(
  Parser& parser,
  View<Unit> units,
  list<RulePt>& u
) {
  View<char> s;
  const bool iso_mode = parser.flags&Parser::FLAG_ISOLATED_MODE_STRINGS;
  const View<char> big = units[u[-1].u].str(parser);
  const View<char> patt = parser.classes[in_class(u[-1].r)].shorten(1,1);
  if ((s = _butch_str_match(big, patt, iso_mode)).start)
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.start - parser.parsed_code), s.len, true};
  }
  else
  { // failure
    return _butch_failure(u, in_rules, in_len);
  }
  return Rule::TOTAL_FAILURE;
}
Rule::MatchDescriptor Rule::_match_rgx(
  Parser& parser,
  View<Unit> units,
  list<RulePt>& u
) {
  View<char> s;
  const View<char> big = units[u[-1].u].str(parser);
  const View<char> patt = parser.classes[in_class(u[-1].r)].shorten(1,1);
  if ((s = parser._rgx_match(big, patt)).start)
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.start - parser.parsed_code), s.len, true};
  }
  else
  { // failure
    return _butch_failure(u, in_rules, in_len);
  }
  return Rule::TOTAL_FAILURE;
}

Rule::MatchDescriptor Rule::_sequence_match(
  Parser& parser,
  View<Unit> units,
  list<RulePt>& u
) {
  constexpr MatchDescriptor failure = TOTAL_FAILURE;
  const std::function<void()> util = [&]() { parser.debug_output(stdout, units); };
  assume(units.len > 0);
  MatchDescriptor ret = {
    .str_begin = units[-1].str_end(),
    .str_len=0,
    .matched=true,
    .u_begin=0,
    .u_end=(uint16_t)units.len
  };
  bool not_flag = false;
  u.clear();
  u.push({0, 0}).push({0, 0});
  while (u[-1].r < in_len && u[-1].u < units.len)
  {
    // printf(">>> Rule '%s': |u|=%lu u[-1]={.r=%i,.u=%i}\n",
    //   parser._load_str(parser.classes[out]),
    //   u.len(),
    //   u[-1].r,
    //   u[-1].u
    // );
    if (not_flag)
      not_flag = false;
    switch (in_rule(u[-1].r))
    {
      case GROUP_BEGIN:
        u[-1].r++;
        u.push(u[-1]);
        break;
      case NOT:
        not_flag = true;
        u[-1].r++;
        break;
      case PREPEEP:
        assume(!is_token);
        ret.u_begin = u[-1].u;
        u[-1].r++;
        break;
      case POSTPEEP:
        assume(!is_token);
        ret.u_end = u[-1].u;
        u[-1].r++;
        break;
      case PIPE:
        {
        int depth = 0;
        u[-1].r++;
        u[-2].r = u[-1].r;
        while ((in_rule(u[-1].r) != GROUP_END || depth > 0) && u[-1].r < in_len)
        {
          if (in_rule(u[-1].r) == GROUP_BEGIN)
            depth++;
          if (in_rule(u[-1].r) == GROUP_END)
            depth--;
          u[-1].r++;
        }
        [[fallthrough]];
        }
      case GROUP_END:
        u[-1].r++;
        break;
      case DOT:
        u[-1].r++;
        _next(u[-1].u, units);
        break;
      case PLUS:
        {
          uint32_t p;
          if (in_rule(u[-1].r-1) == GROUP_END)
          {
            p = u[-1].r-2;
            int depth = 0;
            while (p > 0 && (in_rule(p) != GROUP_BEGIN || depth != 0))
            {
              if (in_rule(p) == GROUP_END)
                depth++;
              else if (in_rule(p) == GROUP_BEGIN)
                depth--;
              p--;
            }
          }
          else
            p = u[-1].r-1;
          u.push({.r=p, .u=u[-1].u});
          u.swap(-1, -2);
          u[-1].r++;
          break;
        }
      case STRING:
        if (ret.include(_match_str(parser, units, u)))
          return failure;
        break;
      case REGEX:
        if (ret.include(_match_rgx(parser, units, u)))
          return failure;
        break;
      default:
        if (parser.rules[in_rule(u[-1].r)].is_alias)
        { // aliases
          
          if (_butch_alias_match(
            parser,
            parser.rules[in_rule(u[-1].r)],
            units[u[-1].u]
          )) {
            ret.include(units[u[-1].u]);
            u[-1].r++;
            _next(u[-1].u, units);
          }
          else
            if (_butch_failure(u, in_rules, in_len) == TOTAL_FAILURE)
              return failure;
        }
        else if (units[u[-1].u].id != in_rule(u[-1].r))
        {
          if (_butch_failure(u, in_rules, in_len) == TOTAL_FAILURE)
            return failure;
        } else {
          ret.include(units[u[-1].u]);
          u[-1].r++;
          _next(u[-1].u, units);
        }
        break;
    }
  }
  if (u[-1].r < in_len || u[-1].u < units.len)
    return failure;
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

bool Parser::_check_and_do_rule(
  Rule& r,
  list<Unit>& out,
  uint32_t start,
  uint32_t end
) {
  Rule::MatchDescriptor match;
  bool did_something = false;
  list<Rule::RulePt> seq_match_stack;
  const uint32_t rule_idx = &r - rules.data();
  const rule_id parent =
    start > 0
    ? out[start-1].id
    : Rule::UNKNOWN_RULE;
  seq_match_stack.init();

  // char c = classes[r.out].fix();
  // printf("%s\n", classes[r.out].start);
  // classes[r.out].mov(c);

  if (r.is_alias)
    return false; // this is an alias
  else if (r.is_token) {
    // token (no peeping can happen here)
    for (uint32_t i = start; i < end; _next(i, out.to_view()))
    {
      if (out[i].id == Unit::LITERAL)
      {
        match = r._sequence_match(*this, out.slice(i, 1), seq_match_stack);
        if (match.matched)
        {
          const uint32_t initial_length = out[i].str_len;
          const uint32_t pre_len = match.str_begin - out[i].str_begin;
          out[i].str_len = pre_len;
          out.insert(Unit(rule_idx, match.str_begin, match.str_len, 0), i+1);
          end++;
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
            end++;
          }
          if (pre_len == 0 || _is_to_be_trimmed(out[i].str_begin, pre_len))
          {
            out.rmv(i);
            end--;
          }
          did_something = true;
          // debug_output(stdout, out.to_view());
        }
      }
    }
  }
  else if (r.recurse < 0 || parent == r.recurse)
  {
    // top-class abstract syntax node type
    for (uint32_t i = start; i < end-1; _next(i, out.to_view()))
    {
      uint32_t j = i;
      _next(j, out.to_view());
      while (j <= end)
      {
        match = r._sequence_match(*this, out.slice(i, j-i), seq_match_stack);
        if (match.matched)
        {
          out.insert(
            Unit(rule_idx,
                 match.str_begin, match.str_len,
                 match.u_end-match.u_begin
            ),
            i+match.u_begin
          );
          did_something = true;
          end++;
          // printf("%i <-> %i\n", match.u_begin, (int)match.u_end);
        }
        _next(j, out.to_view());
      }
    }
  }
  // NOTE: Offset in sub-literalization is arbitrary and should be handed to the user
  if (r.recurse >= 0)
  {
    for (uint32_t i = start; i < end; _next(i, out.to_view()))
    {
      if (out[i].id == r.recurse)
      {
        const uint32_t init_len  = out.len();
        if (rules[r.recurse].is_token && out[i].children_len == 0)
        { // Generate sub-literal if needed
          out[i].children_len = 1;
          out.insert(
            Unit(Unit::LITERAL, out[i].str_begin+1, out[i].str_len-2, 0),
            i+1
          );
        }
        did_something =
          did_something || _check_and_do_rule(r, out, i+1, i+1+out[i].children_len);
        const uint32_t diff = out.len() - init_len;
        out[i].children_len += diff;
        end += diff;
      }
    }
  }
  else if (r.recurse == Rule::RECURSE_ALL)
  {
    for (uint32_t i = start; i < end; _next(i, out.to_view()))
    {
      if (out[i].id == rule_idx)
      {
        const uint32_t init_len  = out.len();
        if (r.is_token && out[i].children_len == 0)
        { // Generate sub-literal if needed
          out[i].children_len = 1;
          out.insert(
            Unit(Unit::LITERAL, out[i].str_begin+1, out[i].str_len-2, 0),
            i+1
          );
        }
        for (Rule& subr : rules)
          did_something =
            did_something || _check_and_do_rule(r, out, i+1, i+1+out[i].children_len);
        const uint32_t diff = out.len() - init_len;
        out[i].children_len += diff;
        end += diff;
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
                      || _check_and_do_rule(r, out, 0, out.len());
    }
    if (!did_something) break;
  }

  return out;
}


