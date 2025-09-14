#include "butch.hpp"

bool _butch_alias_match(const Parser& p, const Rule& alias, const Unit& u)
{
  assert(alias.is_alias);
  const rule_id* test = p.rulesets_rules[alias.ruleset_start];
  for (int i = 0; i < ; i++)
  {
    if (alias.in_rule(i) == u.id)
      return true;
    else if (p.rules[alias.in_rule(i)].is_alias)
      if (_butch_alias_match(p, p.rules[alias.in_rule(i)], u))
        return true;
  }
  return false;
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
    fprintf(stderr, "WARN: Failure to compile regex '%s': ",
            buff.data());
    buff.prealloc(256);
    const size_t w = regerror(res, &patt, buff.data(), buff._cap);
    if (w == 0) { strcpy(buff.data(), "???"); }
    fprintf(stderr, "%s\n", buff.data());
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
  bool test;
  seq_match_stack.init();

  // char c = classes[r.out].fix();
  // printf("%s : %i:%i\n", classes[r.out].start, start, end);
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
            _check_and_do_rule(r, out, i+1, i+1+out[i].children_len)
            || did_something;
        const uint32_t diff = out.len() - init_len;
        out[i].children_len += diff;
        end += diff;
      }
    }
  }
  else if (r.recurse == Rule::RECURSE_ALL)
  {
    const int cb = 1, ce = 1;
    for (uint32_t i = start; i < end; _next(i, out.to_view()))
    {
      if (out[i].id == rule_idx)
      {
        uint32_t init_len  = out.len();
        if (r.is_token && out[i].children_len == 0)
        { // Generate sub-literal if needed
          if (out[i].str_len-cb-ce == 0)
            continue;
          out[i].children_len = 1;
          out.insert(
            Unit(Unit::LITERAL, out[i].str_begin+cb, out[i].str_len-cb-ce, 0),
            i+1
          );
          init_len++;
        }
        // puts("BEFORE");
        // debug_output(stdout, out.to_view());
        for (Rule& subr : rules)
          did_something =
            _check_and_do_rule(subr, out, i+1, i+1+out[i].children_len)
            || did_something;
        const int32_t diff = int(out.len()) - init_len;
        out[i].children_len += diff;
        end += diff;
        // puts("AFTER");
        // debug_output(stdout, out.to_view());
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
      did_something =
        _check_and_do_rule(r, out, 0, out.len())
        || did_something;
    }
    if (!did_something) break;
  }

  return out;
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

