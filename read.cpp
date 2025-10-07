#include "butch.hpp"
#include "stuff.hpp"
#include <cctype>
#include <regex>

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
      SKIP_STRING('\'');
      const auto pattern = View<char>(start+1, s++);
      const View<char> surround =
        flags & FLAG_ISOLATED_MODE_REGEX
        ? View((char*)"\\b", 3) : View<char>(nullptr, nullptr);
      const View<char> start =
        r.out == Rule::BEGIN || r.out == Rule::END
        ? View((char*)"^", 1) : View<char>(nullptr, nullptr);
      const char* const cstr_patt = _load_str(pattern, surround, start);
      regex_patterns.push(RegexPatt());
      try
      {
        regex_patterns[-1] = RegexPatt(cstr_patt);
      }
      catch (std::regex_error& err)
      {
         classes.push(View<char>((char*)err.what(), 0U));
         r.out = classes.len()-1;
         return Error::REGEX_SYNTAX_ERROR;
      }
      const View<char> v = View<char>(nullptr, regex_patterns.len()-1);
      classes.push(v);
      _push_rc(Rule::REGEX, classes.len()-1);
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
          if (r.is_alias())
            return Error::ALIAS_SYNTAX_ERROR;
          if (r.has_postpeep())
            return Error::MISPLACED_PREPEEP;
          if (r.has_prepeep())
            return Error::DOUBLE_PREPEEP;
          r.has_prepeep(true);
          _push_rc(Rule::PREPEEP, 0); s++; break;
        case ']':
          if (r.is_alias())
            return Error::ALIAS_SYNTAX_ERROR;
          if (r.has_postpeep())
            return Error::DOUBLE_POSTPEEP;
          r.has_postpeep(true);
          _push_rc(Rule::POSTPEEP, 0); s++; break;
        default:
          start = s;
          while (!isspace(*s) && *s != '\n')
            s++;
          const class_id cid = _find_or_make_class(View(start, s));
          const rule_id rid = _find_rule(cid);
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
  if (s.len == 5 && memcmp(s.start, "Begin", 5) == 0)
    return Rule::BEGIN;
  if (s.len == 3 && memcmp(s.start, "End", 3) == 0)
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
  bool rl_parse = false; // lr by default
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
      while (isalnum(*s)) s++;
      cctx = self.ctxs.len();
      self.ctxs.push(Context{});
      Context& ctx = self.ctxs.atn(-1);
      ctx.rulesets_start = self.rulesets_rules.len();
      ctx.rules_start = self.rules.len();
      ctx.rule_count = 0;
      ctx.name = self._find_or_make_class(View<char>(start, s));
      ctx.begin[0] = ctx.end[0] = Rule::UNKNOWN_RULE;
      ctx.be_count = 1;
      SKIP_SPACES_ONLY;
      while (*s != '\n' && *s != '#' && *s != 0)
      {
        if (ctx.be_count >= 4)
          ERROR_CN(Error::CONTEXT_TOO_MANY_INHERITS, ctx.name);
        start = s;
        while (isalnum(*s)) s++;
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
        SKIP_SPACES_ONLY;
      }
      continue;
    }
    else if (*s == '+')
    {
      if (cctx == GLOBAL_CONTEXT)
      {
        const u32 offset = s - self.butch_code;
        self.destroy();
        return err<Parser>(Error{
          .type=Error::MISPLACED_COPY,
          .offset=offset
        });
      }
      s++;
      SKIP_SPACES_ONLY;
      if (*s == '\n')
      {
        const u32 glob_len = self.ctxs[0].rules_start;
        for (u32 i = 0; i < glob_len; i++)
        {
          if (self.rules[i].is_alias())
            continue;
          self.rules.push(self.rules[i]);
          self.ctxs[cctx].rule_count++;
        }
      }
      do
      {
        if (*s == '#')
          break;
        start = s;
        while (!isspace(*s))
          s++;
        class_name = View<char>(start, s);
        const class_id cid = self._find_or_make_class(class_name);
        const ctx_id ctid = self._find_ctx(cid);
        if (ctid != GLOBAL_CONTEXT)
        {
          const Context& ctx = self.ctxs[ctid];
          const u32 ie = ctx.rules_start+ctx.rule_count;
          for (u32 i = ctx.rules_start; i < ie; i++)
          {
            if (self.rules[i].out > 0 && !self.rules[i].is_alias())
            {
              self.rules.push(self.rules[i]);
              self.ctxs[cctx].rule_count++;
            }
          }
        }
        else
        {
          const rule_id rid = self._find_rule(cid);
          if (rid == Rule::UNKNOWN_RULE)
            ERROR_CN(Error::UNDECLARED_RULE, cid);
          if (!self.rules[rid].is_alias())
          {
            self.rules.push(self.rules[rid]);
            self.ctxs[cctx].rule_count++;
          }
        }
        SKIP_SPACES_ONLY;
      } while (*s != '\n');
      continue;
    }
    else if (*s == '!')
    {
      s++;
      SKIP_SPACES_ONLY;
      do
      {
        start = s;
        while (!isspace(*s))
          s++;
        class_name = View<char>(start, s);
        const class_id cid = self._find_or_make_class(class_name);
        const ctx_id ctid = self._find_ctx(cid);
        if (ctid == GLOBAL_CONTEXT)
          self.rules.push(Rule::init_unknown_context(cid));
        else
          self.rules.push(Rule::init_context(cid, ctid));
        if (cctx >= 0)
          self.ctxs[cctx].rule_count++;
        SKIP_SPACES_ONLY;
      } while (*s != '\n');
      continue;
    }

    { // class name (and parent class)
      start = s;
      while (isalnum(*s) || *s == '?')
        s++;
      class_name = View<char>(start, s);
    }
    
    { // handle midway spaces, direction, then '='
      SKIP_SPACES_ONLY;
      if (*s == '<')
      {
        rl_parse = true;
        while (*s != '=')
          s++;
      }
      else if (*s == '>' || *s == '=')
      {
        rl_parse = false;
        while (*s != '=')
          s++;
      }
      else
      {
        start = _butch_allocpy_view(class_name);
        self.destroy();
        return err<Parser>(Error{
          .type=Error::RULE_SYNTAX_ERROR,
          .name=start
        });
      }
      s++; // skip the '='
      SKIP_SPACES_ONLY;
    }
    
    { // class rule
      self.rules.push(Rule::init());
      Rule& r = self.rules[-1];
      r.ruleset_start = self.rulesets_rules.len();
      r.out = self._find_or_make_class(class_name);
      if (*class_name.start == '?')
      {
        if (rl_parse)
          ERROR_CN(Error::ALIAS_SYNTAX_ERROR, r.out);
        r.is_alias(true);
      }
      else if (rl_parse)
        r.has_rl_parse(true);
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
  bool is_token;
  for (uint32_t i = 0; i < self.rules.len(); i++)
  {
    Rule& r = self.rules[i];
    if (r.input_len == 0)
      continue;
    if (self.in_rule(r, 0) == Rule::PLUS)
      ERROR_CN(Error::MISPLACED_PLUS, r.out);
    if (r.is_context())
    {
      if (r.is_unknown_context())
      {
        const ctx_id ctid = self._find_ctx(r.out);
        if (ctid == GLOBAL_CONTEXT)
          ERROR_CN(Error::UNDECLARED_CONTEXT, r.out)
        r.set_context(ctid);
      }
      continue;
    }
    is_token = !(r.is_alias() || r.is_context() || r.has_rl_parse());
    rule_id* rids = self.rulesets_rules.data()+r.ruleset_start;
    rule_id* cids = self.rulesets_classes.data()+r.ruleset_start;
    for (int j = 0; j < r.input_len; j++)
    {
      if (
        r.is_alias()
        && rids[j] < 0
        && rids[j] != Rule::UNKNOWN_RULE
      ) {
        ERROR_CN(Error::ALIAS_SYNTAX_ERROR, r.out);
      }
      if (rids[j] == Rule::UNKNOWN_RULE)
      {
        const rule_id find_test = self._find_rule(cids[j]);
        if (find_test == Rule::UNKNOWN_RULE)
        { ERROR_CN(Error::UNDECLARED_RULE, cids[j]) }
        else
          rids[j] = find_test;
      }
      if (j > 0 && rids[j-1] == Rule::NOT && rids[j] < 0)
        ERROR_CN(Error::MISPLACED_NOT, cids[j]);
      if (!self.is_rule(r, j, {Rule::STRING, Rule::REGEX, Rule::PIPE}))
        is_token = false;
    }
    r.is_token(is_token);
  }

  // Resolve contexts
  for (uint32_t i = 0; i < self.ctxs.len(); i++)
  {
    if (self.ctxs[i].begin[0] == Rule::UNKNOWN_RULE)
    {
      ERROR_CN(Error::CONTEXT_MISSING_BEGIN, self.ctxs[i].name);
    }
    if (self.ctxs[i].end[0] == Rule::UNKNOWN_RULE)
      ERROR_CN(Error::CONTEXT_MISSING_END, self.ctxs[i].name);
    for (uint8_t j = 1; j < self.ctxs[i].be_count; j++)
    {
      if (self.ctxs[i].begin[j] != Rule::UNKNOWN_RULE)
        continue;
      const class_id cid = self.ctxs[i].end[j];
      const ctx_id ctid = self._find_ctx(cid);
      if (ctid == GLOBAL_CONTEXT)
        ERROR_CN(Error::UNDECLARED_CONTEXT, cid);
      const Context& src = self.ctxs[ctid];
      const rule_id brid = src.begin[0];
      const rule_id erid = src.end[0];
      if (brid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::CONTEXT_MISSING_BEGIN, src.name);
      if (erid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::CONTEXT_MISSING_END, src.name);
      self.ctxs[i].begin[j] = brid;
      self.ctxs[i].end[j] = erid;
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
    if (t.start)
    {
      c = t.fix();
      fprintf(f, "  %s", t.start);
      t.mov(c);
    }
    else
      fprintf(f, "  <compiled regex>");
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
      fprintf(f,
        "= %s (%04i, %04i)\n",
        classes[ctx.name].start,
        ctx.begin[0],
        ctx.end[0]
      );
      classes[ctx.name].mov(c);
      k_end = k + ctx.rule_count;
    }
    for (; k < k_end; k++)
    {
      const Rule& r = rules[k];
      fprintf(f, "  %04i ", k);
      if (r.is_context())
      {
        c = classes[r.out].fix();
        fprintf(f, "! %s\n", classes[r.out].start);
        classes[r.out].mov(c);
        continue;
      }
      if (r.out == Rule::BEGIN)
        fprintf(f, "Begin =");
      else if (r.out == Rule::END)
        fprintf(f, "End =");
      else
      {
        c = classes[r.out].fix();
        fprintf(f, "%s =", classes[r.out].start);
        classes[r.out].mov(c);
      }
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
        } else if (in_rule(r, i) == Rule::REGEX) {
          fprintf(f, " <compiled regex>"); 
        } else {
          c = classes[in_class(r, i)].fix();
          fprintf(f, " %s", classes[in_class(r, i)].start);
          classes[in_class(r, i)].mov(c);
        }
        if (in_rule(r, i) >= 0)
          fprintf(f, " [%i]", in_rule(r, i));
      }
      if (r.is_alias())
        fprintf(f, " (alias)");
      if (r.is_token())
        fprintf(f, " (token)");
      fprintf(f, "\n");
    }
  }
}
