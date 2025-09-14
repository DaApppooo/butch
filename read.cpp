#include "butch.hpp"

#define SKIP_SPACES_ONLY \
    { \
      while ((*s == ' ' || *s == '\t') && *s != 0) s++; \
    }
#define SKIP_STRING(str_char) \
  while (*(++s) != str_char) \
    if (*s == '\\') \
      s++;

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
