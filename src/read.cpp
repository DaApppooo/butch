#include "butch.hpp"
#include "mystd/iters.hpp"
#include "stuff.hpp"
#include "mystd/algo.hpp"
#include <cctype>
#include <cstdio>

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
  i32 regex_idx;
  while (*s != '\n' && *s != '#')
  {
    if (*s == '"')
    {
      start = s;
      SKIP_STRING('"')
      _push_rc(Rule::STRING, _find_or_make_class(
        View<char>::from(start).to(++s)
      ));
    }
    else if (*s == '\'')
    {
      start = s;
      SKIP_STRING('\'');
      const auto pattern = View<char>::from(start+1).to(s++);
      regex_idx = _comp_rgx(pattern, r.out);
      if (regex_idx < 0)
      {
       r.out = classes.len()-1;
       return Error::REGEX_SYNTAX_ERROR;
      }
      const View<char> v = View<char>::range(start, regex_idx);
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
        case '{':
        {
          s++; // skip '{'
          start = s;
          while (*s != 0 && *s != '}' && *s != '#')
            s++;
          if (*s == 0 || *s == '#')
            return Error::REPARSE_SYNTAX_ERROR;
          const class_id cid =
            _find_or_make_class(View<char>::from(start).to(s));
          const ctx_id ctid = _find_ctx(cid);
          if (ctid == GLOBAL_CONTEXT)
            _push_rc(Rule::UNKNOWN_REPARSE, cid);
          else
            _push_rc(Rule::REPARSE, ctid);
          s++; // skip '}'
          break;
        }
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
        {
          start = s;
          if (!isalnum(*s))
          { return Error::RULE_SYNTAX_ERROR; }
          while (isalnum(*s) || *s == '?')
            s++;
          const class_id cid =
            _find_or_make_class(View<char>::from(start).to(s));
          const rule_id rid = _find_rule(cid);
          _push_rc(rid, cid);
          break;
        }
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
  if (s.len() == 5 && memcmp(s.begin(), "Begin", 5) == 0)
    return Rule::BEGIN;
  if (s.len() == 3 && memcmp(s.begin(), "End", 3) == 0)
    return Rule::END;
  for (uint32_t i = 0; i < l; i++)
  {
    View<char> v = classes[i];
    if (v == s)
      return i;
  }
  classes.push(s);
  return classes.len()-1;
}

rule_id Parser::_find_rule(class_id class_name)
{
  for (uint32_t i = 0; i < rules.len(); i++)
  {
    if (rules[i].out == class_name && !rules[i].is_unknown_copy())
      return i;
  }
  return Rule::UNKNOWN_RULE;
}

ctx_id Parser::_find_ctx(class_id class_name)
{
  for (uint32_t i = 0; i < ctxs.len(); i++)
  {
    if (ctxs[i].name == class_name)
      return i;
  }
  return GLOBAL_CONTEXT;
}

#define ERROR_CN(ERR, CLASS_ID) \
          { start = ALLOCPY_CN(CLASS_ID); \
          self.drop(); \
          return Err(Error{ \
            .type=ERR, \
            .name=start \
          }); }

Parser::Error::Type _butch_ctx_change(
  Parser& self,
  char*& s,
  char* start,
  ctx_id& cctx
) {
  s++;
  SKIP_SPACES_ONLY;
  start = s;
  while (isalnum(*s)) s++;
  cctx = self.ctxs.len();
  self.ctxs.push(Context{});
  Context& ctx = self.ctxs[-1];
  ctx.rulesets_start = self.rulesets_rules.len();
  ctx.rules_start = self.rules.len();
  ctx.rule_count = 0;
  ctx.name = self._find_or_make_class(View<char>::from(start).to(s));
  ctx.begin[0] = ctx.end[0] = Rule::UNKNOWN_RULE;
  ctx.be_count = 1;
  SKIP_SPACES_ONLY;
  // load friend contexts
  while (*s != '\n' && *s != '#' && *s != 0 && *s != '!')
  {
    if (ctx.be_count >= 4)
      return Parser::Error::CONTEXT_SPECS_ERROR;
    start = s;
    while (isalnum(*s)) s++;
    const class_id cid
      = self._find_or_make_class(View<char>::from(start).to(s));
    // mark every one of them as unknown and resolve them once every
    // copy is done.
    ctx.begin[ctx.be_count] = Rule::UNKNOWN_RULE;
    ctx.end[ctx.be_count++] = cid;
    SKIP_SPACES_ONLY;
  }
  if (*s == '!') // depth-stoppers
  {
    s++;
    SKIP_SPACES_ONLY;
    do {
      if (ctx.ds_count >= 4)
        return Parser::Error::CONTEXT_SPECS_ERROR;
      start = s;
      while (isalnum(*s)) s++;
      const class_id cid
        = self._find_or_make_class(View<char>::from(start).to(s));
      if (cid == Rule::BEGIN || cid == Rule::END)
        return Parser::Error::CONTEXT_SPECS_ERROR;
      // resolve it later, once copies are made.
      ctx.depth_stoppers[ctx.ds_count++] = -cid;
      SKIP_SPACES_ONLY;
    } while (*s != '\n' && *s != '#' && *s != 0);
  }
  return Parser::Error::NONE;
}

class_id _butch_copy_object(
  Parser& self,
  char*& s,
  char* start,
  const ctx_id cctx
) {
  View<char> class_name;

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
      if (cctx >= 0)
        self.ctxs[cctx].rule_count++;
    }
    return -1;
  }
  do
  {
    if (*s == '#')
      break;
    start = s;
    while (!isspace(*s))
      s++;
    class_name = View<char>::from(start).to(s);
    const class_id cid = self._find_or_make_class(class_name);
    const rule_id rid = self._find_rule(cid);
    if (rid == Rule::UNKNOWN_RULE)
    {
      const ctx_id ctid = self._find_ctx(cid);
      if (ctid == GLOBAL_CONTEXT)
      {
        self.rules.push(Rule::init_unknown_copy(cid));
        return -1;
      }
      const Context& ctx = self.ctxs[ctid];
      const u32 ie = ctx.rules_start+ctx.rule_count;
      for (u32 i = ctx.rules_start; i < ie; i++)
      {
        if (self.rules[i].out > 0 && !self.rules[i].is_alias())
        {
          self.rules.push(self.rules[i]);
          if (cctx >= 0)
            self.ctxs[cctx].rule_count++;
        }
      }
    }
    else
    {
      if (!self.rules[rid].is_alias())
      {
        const Rule r = self.rules[rid];
        self.rules.push(r);
        if (cctx >= 0)
          self.ctxs[cctx].rule_count++;
      }
    }
    SKIP_SPACES_ONLY;
  } while (*s != '\n');
  return -1;
}

void _butch_ctx_rule(
  Parser& self,
  char*& s,
  char* start,
  const ctx_id cctx
) {
  View<char> class_name;

  s++;
  SKIP_SPACES_ONLY;
  do
  {
    start = s;
    while (!isspace(*s))
      s++;
    class_name = View<char>::from(start).to(s);
    const class_id cid = self._find_or_make_class(class_name);
    const ctx_id ctid = self._find_ctx(cid);
    if (ctid == GLOBAL_CONTEXT)
      self.rules.push(Rule::init_unknown_block(cid));
    else
      self.rules.push(Rule::init_block(cid, ctid));
    if (cctx >= 0)
      self.ctxs[cctx].rule_count++;
    SKIP_SPACES_ONLY;
  } while (*s != '\n');
}

ctx_id _butch_update_ctx_idx(
  Parser& p,
  ctx_id c,
  u32 rule_index
) {
  if (c == -1)
  {
    if (p.ctxs.len() > 0 && rule_index >= p.ctxs[0].rules_start)
      c++;
  }
  else if (c+1 < p.ctxs.len() && rule_index >= p.ctxs[c+1].rules_start)
    c++;
  return c;
}

void _butch_offset_ctxs(Parser& self, ctx_id ctx, i32 off)
{
  if (ctx == GLOBAL_CONTEXT)
    // .to_view() to avoid additional code instantiation
    map(self.ctxs.to_view(),
      [off](Context& c){
        c.rules_start+=off;
        c.begin[0] += off;
        c.end[0] += off;
      }
    );
  else
    map(self.ctxs[ctx, self.ctxs.len()],
      [off](Context& c){
        c.rules_start+=off;
        c.begin[0] += off;
        c.end[0] += off;
      }
    );
}


Result<Parser, Parser::Error> Parser::read(Own<char*> s)
{
#define ALLOCPY_CN(CLASS_ID) \
  _butch_allocpy_view(self.classes[CLASS_ID].to_const())
#define CHECK_COMMENTS \
    if (*s == '#') \
    { \
      while (*s != '\n' && *s != 0) s++; \
      s++; \
      continue; \
    }

  if (s == nullptr)
    return Err(Error{Error::CANT_OPEN_FILE, 0});
  Parser self;
  self.butch_code = s;
  self.parsed_code = View<char>::empty();
  self.flags = FLAG_TRIM_SPACE_LITERALS;
  self.flags_data = 0;
  self.blocks = List<rule_id>::init();
  self.ctxs = List<Context>::init();
  self.rules = List<Rule>::init();
  self.classes = List<View<char>>::init();
  self.rulesets_rules = self.rulesets_classes = List<i32>::init();
  self.regex_patterns = List<RegexPatt>::init();
  self.buff = CString<>::init();
  
  View<char> class_name;
  Error::Type errc;
  ctx_id cctx = GLOBAL_CONTEXT;
  class_id cid;
  char* start = s;
  bool rl_parse = false; // lr by default

  while (*s != 0)
  {
    SKIP_SPACES_ONLY;
    if (*s == '\n')
    { s++; continue; }
    CHECK_COMMENTS;
    SKIP_SPACES_ONLY;
    if (*s == 0) break;

    if (*s == '=')
    { // context change/decl
      if ((errc = _butch_ctx_change(self, s, start, cctx)) != Error::NONE)
        ERROR_CN(errc, self.ctxs[-1].name);
      continue;
    }
    else if (*s == '+')
    {
      if ((cid = _butch_copy_object(self, s, start, cctx)) >= 0)
        ERROR_CN(Error::UNDECLARED_RULE, cid);
      continue;
    }
    else if (*s == '!')
    {
      _butch_ctx_rule(self, s, start, cctx);
      continue;
    }

    { // class name (and parent class)
      start = s;
      while (isalnum(*s) || *s == '?')
        s++;
      class_name = View<char>::from(start).to(s);
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
        start = _butch_allocpy_view(class_name.to_const());
        self.drop();
        return Err(Error{
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
      if (*class_name.begin() == '?')
      {
        if (rl_parse)
          ERROR_CN(Error::ALIAS_SYNTAX_ERROR, r.out);
        r.is_alias(true);
      }
      else if (rl_parse)
        r.has_rl_parse(true);
      if ((errc = self._make_rule(r, s)) != Error::NONE)
        ERROR_CN(errc, r.out);
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
  bool has_unknown_copies = false;
  for (uint32_t i = 0; i < self.rules.len(); i++)
  {
    Rule& r = self.rules[i];
    if (r.is_unknown_copy())
    {
      has_unknown_copies = true;
      continue; // specific passes for this
    }
    if (r.input_len == 0)
      continue;
    if (r.is_block())
    {
      if (r.is_unknown_block())
      {
        const ctx_id ctid = self._find_ctx(r.out);
        if (ctid == GLOBAL_CONTEXT)
          ERROR_CN(Error::UNDECLARED_CONTEXT, r.out)
        r.set_block(ctid);
      }
      continue;
    }
    is_token = !(r.is_alias() || r.is_block() || r.has_rl_parse());
    rule_id* rids = self.rulesets_rules.data()+r.ruleset_start;
    rule_id* cids = self.rulesets_classes.data()+r.ruleset_start;
    if (*rids == Rule::PLUS)
      ERROR_CN(Error::MISPLACED_PLUS, r.out);
    for (u32 j = 0; j < r.input_len; j++)
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
      else if (rids[j] == Rule::UNKNOWN_REPARSE)
      {
        const ctx_id find_test = self._find_ctx(cids[j]);
        if (find_test == Rule::UNKNOWN_RULE)
        { ERROR_CN(Error::UNDECLARED_CONTEXT, cids[j]) }
        else
        {
          cids[j] = find_test;
          rids[j] = Rule::REPARSE;
        }
      }
      if (j > 0 && rids[j-1] == Rule::NOT && rids[j] < 0)
        ERROR_CN(Error::MISPLACED_NOT, cids[j]);
      if (!self.is_rule(r, j, {Rule::STRING, Rule::REGEX, Rule::PIPE}))
        is_token = false;
    }
    r.is_token(is_token);
  }

  // Resolve copies
  while (has_unknown_copies)
  {
    ctx_id ctx_idx = -1;
    has_unknown_copies = false;
    for (uint32_t i = 0; i < self.rules.len(); i++)
    {
      Rule& r = self.rules[i];
      ctx_idx = _butch_update_ctx_idx(self, ctx_idx, i);
      if (!r.is_unknown_copy())
        continue;
      has_unknown_copies = true;
      const rule_id rid = self._find_rule(r.out);
      if (rid == Rule::UNKNOWN_RULE)
      {
        const ctx_id ctid = self._find_ctx(r.out);
        if (ctid == GLOBAL_CONTEXT)
          ERROR_CN(Error::UNDECLARED_CONTEXT, r.out);
        const Context& ctx = self.ctxs[ctid];
        if (ctx.rule_count == 0) [[unlikely]]
        {
          _butch_offset_ctxs(self, ctx_idx, -1);
          self.rules.rm(i);
          i--;
        }
        else
        {
          const u32 ie = ctx.rules_start+ctx.rule_count;
          u32 k = 1;
          for (u32 j = ctx.rules_start; j < ie; j++)
          {
            if (self.rules[j].out > 0 && !self.rules[j].is_alias())
            {
              if (r.is_unknown_copy())
              {
                r = self.rules[j];
                assert(!r.is_unknown_copy());
              } else {
                self.rules.insert(i+k, self.rules[j+k-1]);
                k++;
              }
            }
          }
          _butch_offset_ctxs(self, ctx_idx, k-1);
          i += ctx.rule_count;
        }
      }
      else
      {
        if (rid == Rule::UNKNOWN_RULE)
          ERROR_CN(Error::UNDECLARED_RULE, r.out);
        r = self.rules[rid];
        assert(!r.is_unknown_copy());
      }
    }
  }

  // Register blocks
  for (u32 idx : range(self.rules.len()))
  {
    if (self.rules[idx].is_block())
      self.blocks.push(idx);
  }

  // Resolve contexts
  for (let i : range<u32>(self.ctxs.len()))
  {
    for (let j : range<u8>(1, self.ctxs[i].be_count))
    {
      assert(self.ctxs[i].begin[j] == Rule::UNKNOWN_RULE);
      const class_id cid = self.ctxs[i].end[j];
      const ctx_id ctid = self._find_ctx(cid);
      if (ctid == GLOBAL_CONTEXT)
        ERROR_CN(Error::UNDECLARED_CONTEXT, cid);
      const Context& src = self.ctxs[ctid];
      const rule_id brid = src.begin[0];
      const rule_id erid = src.end[0];
      self.ctxs[i].begin[j] = brid;
      self.ctxs[i].end[j] = erid;
    }
    for (let j : range<u8>(self.ctxs[i].ds_count))
    {
      assert(self.ctxs[i].depth_stoppers[j] < 0);
      const class_id cid = -self.ctxs[i].depth_stoppers[j];
      const rule_id rid = self._find_rule(cid);
      if (rid == Rule::UNKNOWN_RULE)
        ERROR_CN(Error::UNDECLARED_RULE, cid);
      self.ctxs[i].depth_stoppers[j] = rid;
    }
  }
  
  return Ok(self);
}

char fix(View<char> v) { char c = *v.end(); *v.end() = '\0'; return c; }
void mov(View<char> v, char c) { *v.end() = c; }

void Parser::debug(FILE* f)
{
  assume(rulesets_classes._len == rulesets_rules._len);
  char c;
  int i = 0;
  uint32_t k = 0;
  fprintf(f, "=== Classes (4 columns) ===\n");
  for (View<char>& t : classes)
  {
    if (*t.begin() != '\'')
    {
      c = fix(t);
      fprintf(f, "  %s", t.begin());
      mov(t, c);
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
      c = fix(classes[ctx.name]);
      fprintf(f, "= %s", classes[ctx.name].begin());
      mov(classes[ctx.name], c);
      k_end = k + ctx.rule_count;
      for (u8 i = 0; i < ctx.be_count; i++)
        fprintf(f, " (%04i, %04i)", ctx.begin[i], ctx.end[i]);
      fprintf(f, " !");
      for (u8 i = 0; i < ctx.ds_count; i++)
        fprintf(f, " (%04i)", ctx.depth_stoppers[i]);
      fprintf(f, "\n");
    }
    for (; k < k_end; k++)
    {
      const Rule& r = rules[k];
      fprintf(f, "  %04i ", k);
      if (r.is_block())
      {
        c = fix(classes[r.out]);
        fprintf(f, "! %s", classes[r.out].begin());
        if (in(k, blocks))
          fprintf(f, " (registered)\n");
        else
          fprintf(f, "\n");
        mov(classes[r.out], c);
        continue;
      }
      else if (r.is_unknown_copy())
      {
        c = fix(classes[r.out]);
        fprintf(f, "?+ %s\n", classes[r.out].begin());
        mov(classes[r.out], c);
        continue;
      }
      else if (r.is_unknown_block())
      {
        c = fix(classes[r.out]);
        fprintf(f, "?! %s\n", classes[r.out].begin());
        mov(classes[r.out], c);
        continue;
      }
      if (r.out == Rule::BEGIN)
        fprintf(f, "Begin =");
      else if (r.out == Rule::END)
        fprintf(f, "End =");
      else
      {
        c = fix(classes[r.out]);
        fprintf(f, "%s =", classes[r.out].begin());
        mov(classes[r.out], c);
      }
      for (i = 0; i < i32(r.input_len); i++)
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
        } else if (in_rule(r, i) == Rule::REPARSE) {
          const View<char>& v = classes[ctxs[in_class(r, i)].name];
          c = fix(v);
          fprintf(f, " {%s}", v.begin()); 
          mov(v, c);
        } else {
          c = fix(classes[in_class(r, i)]);
          fprintf(f, " %s", classes[in_class(r, i)].begin());
          mov(classes[in_class(r, i)], c);
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
