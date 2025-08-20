#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        case '*':
          r.in[i++] = STAR; s++; break;
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
          case STAR: p = " *"; break;
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

View<char> _butch_str_match(View<char> big, View<char> pattern)
{
  for (int i = 0; i < big.len - pattern.len; i++)
  {
    if (memcmp(big.start+i, pattern.start, pattern.len))
    {
      
    }
  }
}

inline int _next(int start, View<Unit> units)
{ return start+units[start].children_len; }

Rule::MatchDescriptor Rule::match(Parser& parser, View<Unit> units)
{
  constexpr MatchDescriptor failure = {.matched=false};
  int rpt = 0; // rule pointer
  int upt = 0; // units pointer
  while (upt < units.len)
  {
    switch (parser.atom_type(in[rpt]))
    {
      case ATOM_SYMBOL:
        assert(false); // TODO
        break;
      case ATOM_CLASS:
        if (units[upt].id != in[rpt])
          return failure;
        break;
      case ATOM_STRING_MATCH:
        
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
      match = r.match(*this, out.slice(i, j-i));
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

