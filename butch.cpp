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
      while (*s == ' ' || *s == '\t' || *s == 0) s++; \
      continue; \
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
  for (int i = 0; i < (int)classes.size(); i++)
  {
    View<char> v = classes[i];
    if (v.len != s.len)
      continue;
    if (memcmp(v.begin, s.begin, v.len))
      return i;
  }
  classes.push_back(s);
  return classes.size()-1;
}


Result<Parser, Parser::Error> Parser::read(Own<char*> s)
{
  if (s == nullptr)
    return err<Parser>(Error{Error::CANT_OPEN_FILE, 0});
  Result<Parser, Error> ret;
  ret.type = OK;
  Parser& self = ret.ok;
  self.butch_code = s;
  
  View<char> class_name;
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
    CHECK_COMMENTS;

    { // class name
      start = s;
      while (*s != '=' && !isspace(*s))
        s++;
      class_name = View<char>(start, s+1);
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
      // if (res.ok == 0)
      //   continue;
      self.rules.push_back(Rule::init(res.ok));
      Rule& r = self.rules.back();
      r.out = self._find_or_make_class(class_name);
      self._make_rule(r, s);
    }
    assert(*s == '\n' || *s == 0);
    if (*s == '\n')
      s++;
  }
  
  return ret;
}

void Parser::debug(FILE* f)
{
  char c;
  for (Rule& r : rules)
  {
    c = classes[r.out].fix();
    fprintf(f, "%s =", classes[r.out].begin);
    classes[r.out].mov(c);
    for (int i = 0; i < r.in_len; i++)
    {
      c = classes[r.in[i]].fix();
      fprintf(f, " %s", classes[r.in[i]].begin);
      classes[r.in[i]].mov(c);
    }
    fprintf(f, "\n");
  }
}
