#include "butch.hpp"
#include "list.hpp"
#include <functional>
#include <regex.h>
#include <cctype>
#include "stuff.hpp"

#define expect(X) do { assert(X); [[assume(X)]]; } while(false)

bool _butch_alias_match(const Parser& p, const Rule& alias, const Unit& u)
{
  assert(alias.is_alias);
  const rule_id* test = p.rulesets_rules.data() + alias.ruleset_start;
  const rule_id* const end = test + alias.input_len;
  for (; test < end; test++)
  {
    if (*test == u.id)
      return true;
    else if (p.rules[*test].is_alias)
      if (_butch_alias_match(p, p.rules[*test], u))
        return true;
  }
  return false;
}

View<char> _butch_eq_iso(int i, View<char> big, View<char> pattern, bool iso)
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
  return {nullptr, nullptr};
}

View<char> _butch_str_match(View<char> big, View<char> pattern, u8 iso)
{
  assert(iso != 3);
  if (big.len < pattern.len)
    return View<char>(nullptr, nullptr);
  View<char> r;
  if (iso == 2)
  {
    if (pattern == (r = View<char>(big.start, big.start+pattern.len)))
      return r;
    else
     return View<char>(nullptr, nullptr);
  }
  for (u32 i : range(0U, big.len - pattern.len))
  {
    if ((r = _butch_eq_iso(i, big, pattern, iso)).start)
      return r;
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
  const View<char> start = flags & _FLAG_START_FIXED_MATCH
                           ? View<char>((char*)"^", 1)
                           : View<char>(nullptr,nullptr);
  const char* const cstr_patt = _load_str(pattern, surround, start);
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

constexpr void _butch_units_next(View<Unit>& units)
{ units.start += units.start->children_len+1; }
#define UITER _butch_units_next
inline void _next(u32& i, View<Unit> units)
{ i += units[i].children_len+1; }


Rule::MatchDescriptor _butch_failure(
  list<Rule::Pt>& u,
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
#define BUTCH_FAIL \
  _butch_failure( \
      u, \
      p.rulesets_rules.data()+ruleset_start, \
      input_len \
    )

Rule::MatchDescriptor Rule::_match_str(
  const Parser& p,
  View<Unit> units,
  list<Pt>& u,
  const class_id cid
) {
  View<char> s;
  const bool iso_mode =
    bool(p.flags & Parser::FLAG_ISOLATED_MODE_STRINGS)
  | (bool(p.flags & Parser::_FLAG_START_FIXED_MATCH) << 1);
  const View<char> big = units[u[-1].u].str(p);
  const View<char> patt = p.classes[cid].shorten(1,1);
  if ((s = _butch_str_match(big, patt, iso_mode)).start)
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.start - p.parsed_code.start), s.len, true};
  }
  else
  { // failure
    return BUTCH_FAIL;
  }
  return Rule::TOTAL_FAILURE;
}

Rule::MatchDescriptor Rule::_match_rgx(
  Parser& p,
  View<Unit> units,
  list<Pt>& u,
  const class_id cid
) {
  View<char> s;
  const View<char> big = units[u[-1].u].str(p);
  const View<char> patt = p.classes[cid].shorten(1,1);
  if ((s = p._rgx_match(big, patt)).start)
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.start - p.parsed_code.start), s.len, true};
  }
  else
  { // failure
    return BUTCH_FAIL;
  }
  return Rule::TOTAL_FAILURE;
}

void _butch_split_token(
  Parser& p,
  Pack<Unit>& out, // offseted pack
  Rule::MatchDescriptor match,
  u32 rule_idx
) {
  const u32 initial_length = out[0].s_len;
  const u32 pre_len = match.s_start - out[0].s_start;
  const u32 post_len = initial_length - pre_len - match.s_len;
  const u32 post_start = match.s_start+match.s_len;
  out[0].s_len = pre_len;
  out.insert(Unit(rule_idx, match.s_start, match.s_len, 0), 1);
  if (post_len > 0 && !p._is_to_be_trimmed(post_start, post_len))
    out.insert(Unit(Unit::LITERAL, post_start, post_len, 0), 2);
  if (pre_len == 0 || p._is_to_be_trimmed(out[0].s_start, pre_len))
    out.rmv(0);
}

Rule::MatchDescriptor Rule::_match_context(
  Parser& p,
  Pack<Unit>& units,
  list<Rule::Pt>& u,
  ctx_id cid
) {
  // welcome to cache-misses hell
#define LD_BEGIN(IDX) \
  p._comp_str(p.classes[p.in_class(p.rules[ctx.begin[0]], 0)]);
  
  const Context& ctx = p.ctxs[cid];
  Pack<Unit>::State state;
  Rule::MatchDescriptor m;
  list<u8> depth; // indicies in the ctx.begin and ctx.end
  u32 off, unit_s_start;
  if (units[0].id != Unit::LITERAL)
    return BUTCH_FAIL;
  assert(p.rules.atp(ctx.begin[0]).is_token);
  assert(p.rules.atn(ctx.begin[0]).input_len == 1);
  assert(p.rules.atp(ctx.end[0]).is_token);
  assert(p.rules.atn(ctx.end[0]).input_len == 1);
  LD_BEGIN(0);
  m.s_start = units[0].s_start;
  m.s_len = p.buff.len();
  m.u_start = units.start;
  m.u_end = units.start+units.len;
  depth.init();
  while (m.s_end() <= p.parsed_code.len)
  {
    if (m.s_view(p) == p.buff.to_view())
      break;
    m.s_start++;
  }
  if (m.s_end() > p.parsed_code.len)
    return BUTCH_FAIL;
  if (m.s_start > units[0].s_start)
  {
    units.insert(
      Unit(
        Unit::LITERAL,
        units[0].s_start,
        m.s_start - units[0].s_start,
        0
      ), 0
    );
    units[1].s_start = m.s_start+m.s_len;
  }
  depth.push(0);
  off = m.s_start+1;
  // we temporarily enable _FLAG_START_FIXED_MATCH
  p.flags |= p._FLAG_START_FIXED_MATCH;
  while (depth.len() > 0)
  {
    for (u32 i : range(ctx.be_count))
    {
      
    }
    off++;
  }
  p.flags &= ~p._FLAG_START_FIXED_MATCH;
  return m;
}

Rule::MatchDescriptor Rule::_sequence_match(
  Parser& p,
  Pack<Unit>& units,
  list<Pt>& u
) {
#define RPTR(INDEX) (p.rulesets_rules.data()+ruleset_start+u.atn(INDEX).r)
#define RPTR_OFFSET(INDEX,OFF) \
(p.rulesets_rules.data()+ruleset_start+u.atn(INDEX).r + OFF)
#define CPTR(INDEX) (p.rulesets_classes.data()+ruleset_start+u.atn(INDEX).r)
#define NEXT(UU) _next(UU, units.to_view())
#define FAIL \
  if ( \
    _butch_failure(u, \
    p.rulesets_classes.data()+ruleset_start, \
    input_len \
  ) == TOTAL_FAILURE) \
    return failure;
  
  constexpr MatchDescriptor failure = TOTAL_FAILURE;
  expect(units.len > 0);
  MatchDescriptor ret = {
    .s_start = units[-1].str_end(),
    .s_len=0,
    .matched=true,
    .u_start=0,
    .u_end=(uint16_t)units.len
  };
  bool not_flag = false;
  u.clear();
  u.push({0, 0}).push({0, 0});
  // printf("===== Rule %s : units.len=%u =====\n",
  //        p._load_str(p.classes[out]),
  //        units.len
  // );
  // p.debug_output(stdout, units);
  // puts("=== vvv ===");
  while (u[-1].r < input_len && u[-1].u < units.len)
  {
    // printf(">>> |u|=%u u[-1]={.r=%i,.u=%i}\n",
    //   u.len(),
    //   u[-1].r,
    //   u[-1].u
    // );
    if (not_flag)
      not_flag = false;
    switch (*RPTR(-1))
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
        expect(!is_token);
        ret.u_start = u[-1].u;
        u[-1].r++;
        break;
      case POSTPEEP:
        expect(!is_token);
        ret.u_end = u[-1].u;
        u[-1].r++;
        break;
      case PIPE:
        {
        int depth = 0;
        u[-1].r++;
        u[-2].r = u[-1].r;
        while ((*RPTR(-1) != GROUP_END || depth > 0) && u[-1].r < input_len)
        {
          if (*RPTR(-1) == GROUP_BEGIN)
            depth++;
          if (*RPTR(-1) == GROUP_END)
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
        NEXT(u[-1].u);
        break;
      case PLUS:
        {
          uint32_t rp;
          if (*RPTR_OFFSET(-1, -1) == GROUP_END)
          {
            rp = u[-1].r-2;
            int depth = 0;
            while (rp > 0 && (*RPTR(rp) != GROUP_BEGIN || depth != 0))
            {
              if (*RPTR(rp) == GROUP_END)
                depth++;
              else if (*RPTR(rp) == GROUP_BEGIN)
                depth--;
              rp--;
            }
          }
          else
            rp = u[-1].r-1;
          u.push({.r=rp, .u=u[-1].u});
          u.swap(-1, -2);
          u[-1].r++;
          break;
        }
      case STRING:
        if (ret.include(_match_str(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      case REGEX:
        if (ret.include(_match_rgx(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      case CONTEXT:
        if (ret.include(_match_context(p, units, u, *CPTR(-1))))
          return failure;
        break;
      default:
        if (p.rules[*RPTR(-1)].is_alias)
        { // aliases
          
          if (_butch_alias_match(
            p,
            p.rules[*RPTR(-1)],
            units[u[-1].u]
          )) {
            ret.include(units[u[-1].u]);
            u[-1].r++;
            NEXT(u[-1].u);
          }
          else
            FAIL;
        }
        else if (units[u[-1].u].id != *RPTR(-1))
        {
          printf("%u != %u\n", units[u[-1].u].id, *RPTR(-1));
          FAIL;
        } else {
          ret.include(units[u[-1].u]);
          u[-1].r++;
          NEXT(u[-1].u);
        }
        break;
    }
  }
  if (u[-1].r < input_len || u[-1].u < units.len)
    return failure;
  return ret;
#undef NEXT
#undef RPTR
#undef RPTR_OFFSET
#undef CPTR
#undef FAIL
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
  Pack<Unit>& out,
  list<Rule::Pt>& pt_buffer
) {
  Rule::MatchDescriptor match;
  bool did_something = false;
  const rule_id rule_idx = &r - rules.data();
  bool test;
  auto out_iter = [&out](Range<u32>& i){ _next(i, out.to_view()); };
  Pack<Unit>::State state;

  // char c = classes[r.out].fix();
  // printf("%s : %i:%i\n", classes[r.out].start, start, end);
  // classes[r.out].mov(c);

  if (r.is_alias)
    return false; // this is an alias
  else if (r.is_token) {
    // token (no peeping can happen here)
    for (u32 i : out.range().iter_with(out_iter))
    {
      if (out[i].id == Unit::LITERAL)
      {
        state = out.save();
        match = r._sequence_match(*this, out.set(i, 1), pt_buffer);
        out.reset(state);
        if (match.matched)
        {
          state = out.save();
          _butch_split_token(*this, out.set(i, out.len), match, rule_idx);
          out.reset(state);
          did_something = true;
        }
      }
    }
  }
  else
  {
    // top-class abstract syntax node type
    for (u32 i : out.to_view().shorten(0, 1).range().iter_with(out_iter))
    {
      for (u32 j : Range<u32>::init(i, out.len))
      {
        state = out.save();
        match = r._sequence_match(*this, out.set(i, j-i), pt_buffer);
        out.reset(state);
        if (match.matched)
        {
          out.insert(
            Unit(rule_idx,
                 match.s_start, match.s_len,
                 match.u_end-match.u_start
            ),
            i+match.u_start
          );
          did_something = true;
        }
        _next(j, out.to_view());
      }
    }
  }
  return did_something;
}

list<Unit> Parser::parse(Ref<char*> s)
{
  Pack<Unit> out;
  list<Rule::Pt> pt_buffer;
  out.init();
  pt_buffer.init();
  parsed_code = {s, (u32)str_len(s)};

  out.push(Unit(Unit::LITERAL, 0, str_len(s), 0));

  for (;;)
  {
    bool did_something = false;
    const uint32_t global_end =
      ctxs.len() > 0
      ? ctxs[0].rules_start
      : rules.len();
    for (uint32_t i = 0; i < global_end; i++)
    {
      did_something =
        _check_and_do_rule(rules[i], out, pt_buffer)
        || did_something;
    }
    if (!did_something) break;
  }

  return out.ls();
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
      fprintf(f, "[%i:+%i]\n", ls[i].s_start, ls[i].s_len);
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

