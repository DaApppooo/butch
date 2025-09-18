#include "butch.hpp"
#include <cctype>

#define assume(X) do { assert(X); [[assume(X)]]; } while(false)

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
  const uint32_t init_len = rulesets_rules.len();
  while (*s != '\n' && *s != '#')
  {
    if (*s == '"')
    {
      start = s;
      SKIP_STRING('"')
      _push_rc(Rule::STRING, _find_or_make_class(View(start, ++s)));
    }
    else if (*s == '\'')
    {
      start = s;
      SKIP_STRING('\'')
      _push_rc(Rule::REGEX, _find_or_make_class(View(start, ++s)));
    }
    else
    {
      switch (*s)
      {
#define PUSH_SYM(CHAR, SYM) \
  case CHAR: \
    _push_rc(SYM, 0); \
    s++; break;
        PUSH_SYM('(', Rule::GROUP_BEGIN);
        PUSH_SYM(')', Rule::GROUP_END);
        PUSH_SYM('.', Rule::DOT);
        PUSH_SYM('+', Rule::PLUS);
        PUSH_SYM('|', Rule::PIPE);
        PUSH_SYM('!', Rule::NOT);
        case '[':
          if (r.has_postpeep)
            return Error::MISPLACED_PREPEEP;
          if (r.has_prepeep)
            return Error::DOUBLE_PREPEEP;
          r.has_prepeep = true;
          _push_rc(Rule::PREPEEP, 0); s++; break;
        case ']':
          if (r.has_postpeep)
            return Error::DOUBLE_POSTPEEP;
          r.has_postpeep = true;
          _push_rc(Rule::POSTPEEP, 0); s++; break;
        default:
          start = s;
          while (!isspace(*s) && *s != '\n')
            s++;
          const class_id cid = _find_or_make_class(View(start, s));
          const rule_id rid = _find_rule(cid);
          if (rid == Rule::UNKNOWN_RULE)
          {
            const ctx_id ctid = _find_ctx(cid);
            if (ctid == GLOBAL_CONTEXT)
              _push_rc(rid, cid);
            else
              _push_rc(Rule::CONTEXT, ctid);
          }
          else
            _push_rc(rid, cid);
          break;
      }
    }
    SKIP_SPACES_ONLY;
  }
  if (*s == '#')
    while (*s != 0 && *s != '\n') s++;
  r.input_len = rulesets_rules.len() - init_len;
  return Error::NONE;
}

class_id Parser::_find_or_make_class(View<char> s)
{
  const uint32_t l = classes.len();
  if (l == 5 && memcmp(s.start, "Begin", 5) == 0)
    return Rule::BEGIN;
  if (l == 3 && memcmp(s.start, "End", 3) == 0)
    return Rule::END;
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

rule_id Parser::_find_rule(class_id class_name)
{
  for (uint32_t i = 0; i < rules.len(); i++)
  {
    if (rules.atp(i).out == class_name)
      return i;
  }
  return Rule::UNKNOWN_RULE;
}

ctx_id Parser::_find_ctx(class_id class_name)
{
  for (uint32_t i = 0; i < ctxs.len(); i++)
  {
    if (ctxs.atp(i).name == class_name)
      return i;
  }
  return GLOBAL_CONTEXT;
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
  if (s == nullptr)
    return err<Parser>(Error{Error::CANT_OPEN_FILE, 0});
  Parser self;
  self.butch_code = s;
  
  View<char> class_name;
  ctx_id cctx = GLOBAL_CONTEXT;
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
    SKIP_SPACES_ONLY;
    if (*s == '\n')
    { s++; continue; }
    CHECK_COMMENTS;
    SKIP_SPACES_ONLY;
    if (*s == 0) break;

    if (*s == '=')
    { // context change
      s++;
      SKIP_SPACES_ONLY;
      start = s;
      while (isalnum(*(s++)));
      Context ctx;
      ctx.rulesets_start = self.rulesets_rules.len();
      ctx.rules_start = self.rules.len();
      ctx.rule_count = 0;
      ctx.name = self._find_or_make_class(View<char>(start, s-1));
      ctx.begin[0] = ctx.end[0] = Rule::UNKNOWN_RULE;
      ctx.be_count = 1;
      while (*s != '\n' && *s != 0)
      {
        SKIP_SPACES_ONLY;
        start = s;
        while (isalnum(*(s++)));
        const class_id cid
          = self._find_or_make_class(View<char>(start, s));
        const ctx_id ctxid = self._find_ctx(cid);
        if (ctxid == GLOBAL_CONTEXT
            || self.ctxs[ctxid].begin[0] == Rule::UNKNOWN_RULE
            || self.ctxs[ctxid].end[0] == Rule::UNKNOWN_RULE
        ) { // failed to find context, will be resolved later
          ctx.begin[ctx.be_count] = Rule::UNKNOWN_RULE;
          ctx.end[ctx.be_count++] = cid;
        }
        else
        {
          ctx.begin[ctx.be_count] = self.ctxs[ctxid].begin[0];
          ctx.end[ctx.be_count++] = self.ctxs[ctxid].end[0];
        }
      }
      self.ctxs.push(ctx);
      continue;
    }
    else if (*s == '+')
    {
      assert(false); // TODO
    }

    { // class name (and parent class)
      start = s;
      while (*s != '=' && *s != ':' && !isspace(*s) && *s != '/' && *s != 0)
        s++;
      class_name = View<char>(start, s);
    }
    
    { // handle midway space and '='
      while (*s == '=' || isspace(*s))
        s++;
      start = s;
    }
    
    { // class rule
      self.rules.push(Rule::init());
      Rule& r = self.rules[-1];
      r.ruleset_start = self.rulesets_rules.len();
      r.out = self._find_or_make_class(class_name);
      if (*class_name.start == '?')
        r.is_alias = true;
      Error::Type error = self._make_rule(r, s);
      if (error != Error::NONE)
        ERROR_CN(error, r.out);
      if (cctx != GLOBAL_CONTEXT)
      {
        if (r.out == Rule::BEGIN)
          self.ctxs[cctx].begin[0] = self.rules.len()-1;
        else if (r.out == Rule::END)
          self.ctxs[cctx].end[0] = self.rules.len()-1;
        self.ctxs[cctx].rule_count++;
      }
    }
    assume(*s == '\n' || *s == 0);
    if (*s == '\n')
      s++;
  }

  // resolve rules with Rule.in_rule(x) == UNKNOWN_RULE
  rule_id* rids = self.rulesets_rules.data();
  class_id* cids = self.rulesets_classes.data();
  for (uint32_t i = 0; i < self.rules.len(); i++)
  {
    Rule& r = self.rules[i];
    if (r.input_len == 0)
      continue;
    if (self.in_rule(r, 0) == Rule::PLUS)
      ERROR_CN(Error::MISPLACED_PLUS, r.out);
    r.is_token = true;
    for (int j = 0; j < r.input_len; j++)
    {
      if (
        r.is_alias
        && rids[j] < 0
        && rids[j] != Rule::UNKNOWN_RULE
      ) {
        printf("????? %i\n", rids[j]);
        ERROR_CN(Error::ALIAS_SYNTAX_ERROR, r.out);
      }
      if (rids[j] == Rule::UNKNOWN_RULE)
      {
        const rule_id find_test = self._find_rule(cids[j]);
        if (find_test == Rule::UNKNOWN_RULE)
        {
          const ctx_id ctid = self._find_ctx(cids[j]);
          if (ctid == GLOBAL_CONTEXT)
            ERROR_CN(Error::UNDECLARED_RULE, cids[j]);
          rids[j] = Rule::CONTEXT;
        }
        else
          rids[j] = find_test;
      }
      if (j > 0 && rids[j-1] == Rule::NOT && rids[j] < 0)
        ERROR_CN(Error::MISPLACED_NOT, cids[j]);
      if (!self.is_rule(r, j, {Rule::STRING, Rule::REGEX, Rule::PIPE}))
        r.is_token = false;
    }
    rids += r.input_len;
    cids += r.input_len;
  }

  // Resolve contexts
  for (uint32_t i = 0; i < self.ctxs.len(); i++)
  {
    if (self.ctxs[i].begin[0] == Rule::UNKNOWN_RULE)
      ERROR_CN(Error::CONTEXT_MISSING_BEGIN, self.ctxs[i].name);
    if (self.ctxs[i].end[0] == Rule::UNKNOWN_RULE)
      ERROR_CN(Error::CONTEXT_MISSING_END, self.ctxs[i].name);
    for (uint8_t j = 1; j < self.ctxs[i].be_count; j++)
    {
      if (self.ctxs[i].begin[j] != Rule::UNKNOWN_RULE)
        continue;
      const Context& src = self.ctxs[self.ctxs[i].end[j]];
      const rule_id brid = src.begin[0];
      const rule_id erid = src.end[0];
      if (brid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::CONTEXT_MISSING_BEGIN, src.name);
      if (erid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::CONTEXT_MISSING_END, src.name);
      self.ctxs[i].begin[j] = brid;
      self.ctxs[i].end[j] = brid;
    }
  }
  
  return ok<Error>(self);
}


void Parser::debug(FILE* f)
{
  char c;
  int i = 0;
  uint32_t k = 0;
  fprintf(f, "=== Classes (4 columns) ===\n");
  for (View<char>& t : classes)
  {
    c = t.fix();
    fprintf(f, "  %s", t.start);
    t.mov(c);
    i++;
    if (i >= 4)
    {
      fprintf(f, "\n");
      i = 0;
    }
  }
  if (i != 0)
    fprintf(f, "\n");
  fprintf(f, "=== Contexts ===\n");
  for (int32_t ctid = -1; ctid < (int)ctxs.len(); ctid++)
  {
    uint32_t k_end;
    if (ctid == GLOBAL_CONTEXT)
    {
      fprintf(f, "= GLOBAL\n");
      k_end = ctxs.len() > 0
              ? ctxs[0].rules_start
              : rules.len();
    }
    else
    {
      const Context& ctx = ctxs[ctid];
      c = classes[ctx.name].fix();
      fprintf(f, "= %s\n", classes[ctx.name].start);
      classes[ctx.name].mov(c);
      k_end = k + ctx.rule_count;
    }
    for (; k < k_end; k++)
    {
      const Rule& r = rules[k];
      fprintf(f, "  %04i ", i++);
      c = classes[r.out].fix();
        fprintf(f, "%s =", classes[r.out].start);
      classes[r.out].mov(c);
      for (i = 0; i < r.input_len; i++)
      {
        if (in_rule(r, i) < 0 && in_rule(r, i) > Rule::STRING)
        {
          const char* p = nullptr;
          switch (in_rule(r, i))
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
              fprintf(f, "<??? %i \?\?\?>", in_rule(r, i));
              assert(!"Unknown symbol token.");
              break;
          }
          fprintf(f, "%s", p);
        } else {
          c = classes[in_class(r, i)].fix();
          fprintf(f, " %s", classes[in_class(r, i)].start);
          classes[in_class(r, i)].mov(c);
        }
        if (in_rule(r, i) >= 0)
          fprintf(f, " [%i]", in_rule(r, i));
      }
      if (r.is_alias)
        fprintf(f, " (alias)");
      fprintf(f, "\n");
    }
  }
}
