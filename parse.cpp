#include "butch.hpp"
#include "list.hpp"
#include <cstdio>
#include <functional>
// #include <regex.h>
#include <regex>
#include <cctype>
#include "stuff.hpp"

#define expect(X) do { assert(X); [[assume(X)]]; } while(false)

bool _butch_alias_match(const Parser& p, const Rule& alias, const Unit& u)
{
  assert(alias.is_alias());
  const rule_id* test = p.rulesets_rules.data() + alias.ruleset_start;
  const rule_id* const end = test + alias.input_len;
  for (; test < end; test++)
  {
    if (*test == u.id)
      return true;
    else if (p.rules[*test].is_alias())
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
  assert(iso < 3);
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

View<char> Parser::_rgx_match(View<char> big, const RegexPatt& pattern)
{
  constexpr View<char> failed = View<char>(nullptr, nullptr);
  const View<char> surround = flags & FLAG_ISOLATED_MODE_REGEX
                              ? View<char>((char*)"\\b", 2)
                              : View<char>(nullptr, nullptr);
  const View<char> start = flags & _FLAG_START_FIXED_MATCH
                           ? View<char>((char*)"^", 1)
                           : View<char>(nullptr,nullptr);
  // const char* const cstr_patt = _load_str(pattern, surround, start);
  // RegexPatt regex_patt(cstr_patt);
  RegexMatch m;
  if (std::regex_search(big.start, big.end(), m, pattern)) // std::regex_constants::nosubs
  {
    return View<char>(m[0].first, m[0].second);
  }
  return failed;
}

constexpr void _butch_units_next(View<Unit>& units)
{ units.start += units.start->children_len+1; }
#define UITER _butch_units_next
inline void _next(u32& i, View<Unit> units)
{ i += units[i].children_len+1; }

Match _butch_failure(
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

Match Rule::_match_str(
  const Parser& p,
  View<Unit> units,
  list<Pt>& u,
  const class_id cid
) {
  View<char> s;
  const u8 iso_mode =
    bool(p.flags & Parser::FLAG_ISOLATED_MODE_STRINGS)
  | (bool(p.flags & Parser::_FLAG_START_FIXED_MATCH) << 1);
  const View<char> big = units[u[-1].u].str(p);
  const View<char> patt = p.classes[cid].shorten(1,1);
  if (units[u[-1].u].ctx != -1)
    return BUTCH_FAIL;
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

Match Rule::_match_rgx(
  Parser& p,
  View<Unit> units,
  list<Pt>& u,
  const class_id cid
) {
  View<char> s;
  const View<char> big = units[u[-1].u].str(p);
  const View<char> patt = p.classes[cid];
  expect(patt.start == nullptr);
  if (units[u[-1].u].ctx != -1)
    return BUTCH_FAIL;
  if ((s = p._rgx_match(big, p.regex_patterns[patt.len])).start)
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
  Match match,
  u32 rule_idx
) {
  const u32 initial_length = out[0].s_len;
  const u32 pre_len = match.s_start - out[0].s_start;
  const u32 post_len = initial_length - pre_len - match.s_len;
  const u32 post_start = match.s_start+match.s_len;
  out[0].s_len = pre_len;
  out.insert(Unit(rule_idx, match.s_start, match.s_len, 0), 1);
  if (!p._should_be_trimmed(post_start, post_len))
    out.insert(Unit(Unit::LITERAL, post_start, post_len, 0), 2);
  if (p._should_be_trimmed(out[0].s_start, pre_len))
    out.rmv(0); // TODO : optimize this rmv out
}

void _debug(Parser& p, Pack<Unit>& units)
{
  p.debug_output(stdout, units.ls().to_view());
}

void Rule::_apply_context(
  Parser& p,
  Pack<Unit>& units,
  list<Rule::Pt>& u,
  Match m
) {
  assert(is_context());
  assert(units[0].id == Unit::LITERAL);
  assert(units.len == 1);
  const ctx_id ctid = get_context();
  const Context& ctx = p.ctxs[ctid];
  Unit* parent = &units[0];
  bool common_malcom_do_somethin = true;
  assert(m.matched);
  const u32 units_s_start = parent->s_start;
  const u32 units_s_len = parent->s_len;
  const u16 begin_len = m.u_start;
  const u16 end_len = m.u_end;
  const i32 head_len = i32(m.s_start) - units_s_start - begin_len;
  if (!p._should_be_trimmed(units_s_start, head_len))
  {
    assert(m.s_start - units_s_start - begin_len > 0);
    units.insert(Unit(
      Unit::LITERAL,
      units_s_start,
      head_len,
      0
    ), 0);
    units.start++;
    units.len--;
  }
  parent = &units[0];
  parent->s_start = m.s_start;
  parent->s_len = m.s_len;
  parent->id = this - p.rules.data();
  parent->children_len = m.s_len > 0;
  if (m.s_len > 0)
  {
    units.insert(Unit( // <-- the child
      Unit::LITERAL,
      m.s_start,
      m.s_len,
      0
    ), 1);
    units.start++;
    units.len--;
    while (common_malcom_do_somethin)
    {
      common_malcom_do_somethin = false;
      for (Rule& r : p.rules.slice(ctx.rules_start, ctx.rule_count))
      {
        if (r.out == Rule::BEGIN || r.out == Rule::END)
          continue;
        common_malcom_do_somethin =
          p._check_and_do_rule(r, units, u)
          || common_malcom_do_somethin;
      }
    }
    units.start--;
    units[0].children_len = units.len;
    units.len++;
  }
  const i32 total_end = units_s_start+units_s_len;
  const i32 ctx_end = m.s_start+m.s_len+m.u_end;
  const i32 tail_len = total_end - ctx_end;
  if (!p._should_be_trimmed(ctx_end, tail_len))
  {
    units.insert(Unit(
      Unit::LITERAL,
      ctx_end,
      tail_len,
      0
    ), units.len);
  }
}

Match _butch_find_first_depth_stopper(
  Parser& p,
  Pack<Unit>& units,
  list<Rule::Pt>& u,
  const Context& ctx
) {
  Match ds_m, sub_m;
  ds_m.matched = false;
  for (u8 i = 0; i < ctx.ds_count; i++)
  {
    sub_m = p.rules[ctx.depth_stoppers[i]]._sequence_match(p, units, u);
    if (!sub_m.matched)
      continue;
    if (!ds_m.matched)
      ds_m = sub_m;
    else if (sub_m.s_start < ds_m.s_start)
      ds_m = sub_m;
  }
  return ds_m;
}

Match Rule::_match_context(
  Parser& p,
  Pack<Unit>& units,
  list<Rule::Pt>& u
) {
  // welcome to cache-misses hell
  //
  // idea to support non-literals:
  //  convert temporarily the first unit into a literal, just to test things
#define LD_BEGIN(IDX) \
  p._comp_str(p.classes[p.in_class(p.rules[ctx.begin[0]], 0)]);
  
  const Context& ctx = p.ctxs[get_context()];
  Pack<Unit>::State state;
  Match m, sub_m, ds_m;
  list<u8> depth; // indicies in the ctx.begin and ctx.end
  u32 ctx_content_len, abs_index;
  assert(units[0].id == Unit::LITERAL);
  assert(p.rules.atp(ctx.begin[0]).is_token());
  assert(p.rules.atp(ctx.end[0]).is_token());
  assert(units.len == 1);
  const u32 units_s_start = units[0].s_start;
  const u32 units_s_end = units_s_start + units[0].s_len;
  depth.init();
  do
  {
    m = p.rules[ctx.begin[0]]._sequence_match(p, units, u);
    if (!m.matched)
    {
      m = TOTAL_FAILURE;
      goto FAILURE;
    }
    const u32 temp = units[0].s_start;
    do
    {
      ds_m = _butch_find_first_depth_stopper(p, units, u, ctx);
    } while (ds_m.matched && ds_m.s_end() < m.s_start);
    if (ds_m.matched && m.in(ds_m))
    {
      units[0].s_len -= ds_m.s_end() - units[0].s_start;
      units[0].s_start = ds_m.s_end();
    }
    if ((ds_m.matched && !m.in(ds_m)) || !ds_m.matched)
      break;
  } while (true);
  units[0].s_len = m.s_start - units[0].s_start + 1;
  for (u8 i = 1; i < ctx.be_count; i++)
  {
    sub_m = p.rules[ctx.begin[i]]._sequence_match(p, units, u);
    if (sub_m.matched && sub_m.s_start < m.s_start)
    {
      m = TOTAL_FAILURE;
      goto FAILURE;
    }
  }
  // we temporarily enable _FLAG_START_FIXED_MATCH
  p.flags |= Parser::_FLAG_START_FIXED_MATCH;
  depth.push(0);
  units[0].s_start = m.s_start;
  units[0].s_start += m.s_len;
  units[0].s_len = units_s_end - units[0].s_start;
  ds_m.matched = false;
  while (depth.len() > 0)
  {
    const u32 len = units[0].s_len;
    do // units[0].s_len stores the minimum length between units[0].s_start and
    { // the next depth stopper
      if (!ds_m.matched || units[0].s_start > ds_m.s_start)
        ds_m = _butch_find_first_depth_stopper(p, units, u, ctx);
      if (ds_m.matched && ds_m.s_start == units[0].s_start)
      {
        units[0].s_start += ds_m.s_len;
        units[0].s_len -= ds_m.s_len;
      }
      else
      {
        units[0].s_len = ds_m.s_start - units[0].s_start;
        break;
      }
    } while (true);
    sub_m.matched = false;
    for (u8 i : range(ctx.be_count))
    {
      sub_m = p.rules[ctx.end[i]]._sequence_match(p, units, u);
      if (sub_m.matched)
      {
        if (depth.atn(-1) != i)
        {
          units[0].id = Unit::ERROR_CONTEXT_MISMATCH;
          m = BUTCH_FAIL;
          goto FAILURE;
        }
        depth.pop();
        units[0].s_start += sub_m.s_len;
        units[0].s_len = len - sub_m.s_len;
        break;
      }
      sub_m = p.rules[ctx.begin[i]]._sequence_match(p, units, u);
      if (sub_m.matched)
      {
        depth.push(i);
        units[0].s_start += sub_m.s_len;
        units[0].s_len = len - sub_m.s_len;
        break;
      }
    }
    if (!sub_m.matched)
    {
      units[0].s_start++;
      units[0].s_len = len-1;
    }
    if (units[0].s_len <= 0 && depth.len() > 0)
    {
      m = BUTCH_FAIL;
      goto FAILURE;
    }
  }
  // successfuly matched
  m = Match{
    .s_start = m.s_end(),
    .s_len = sub_m.s_start - m.s_end(),
    .matched = true,
    .u_start = u16(m.s_len),
    .u_end = u16(sub_m.s_len)
  };
  units[0].s_start = units_s_start;
  units[0].s_len = units_s_end - units_s_start;
  p.flags &= ~p._FLAG_START_FIXED_MATCH;
  depth.destroy();
  return m;
FAILURE:
  assert(!m.matched);
  units[0].s_start = units_s_start;
  units[0].s_len = units_s_end - units_s_start;
  units.reset(state);
  p.flags &= ~p._FLAG_START_FIXED_MATCH;
  depth.destroy();
  return m;
}

Match Rule::_sequence_match(
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
  
  constexpr Match failure = TOTAL_FAILURE;
  expect(units.len > 0);
  Match ret = {
    .s_start = units[units.len-1].s_end(),
    .s_len=0,
    .matched=true,
    .u_start=0,
    .u_end=(uint16_t)units.len
  };
  bool not_flag = false;
  u.clear();
  u.push({0, 0}).push({0, 0});
  // printf("===== Rule %s : units.len=%u =====\n",
  //        out >= 0 ? p._load_str(p.classes[out]) : "Begin/End",
  //        units.len
  // );
  // p.debug_output(stdout, units.ls().to_view());
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
      case PREPEEP: [[unlikely]];
        assert(!is_token());
        ret.u_start = u[-1].u;
        u[-1].r++;
        break;
      case POSTPEEP: [[unlikely]];
        assert(!is_token());
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
      case REPARSE:
        assert(false);
        break;
      case STRING:
        if (ret.include(_match_str(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      case REGEX:
        if (ret.include(_match_rgx(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      default: [[likely]];
        if (p.rules[*RPTR(-1)].is_alias())
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
        else if (p.rules[units[u[-1].u].id].out != *CPTR(-1))
        {
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

bool Parser::_should_be_trimmed(uint32_t begin, uint32_t len)
{
  if (len == 0)
    return true;
  if (!(flags & FLAG_TRIM_SPACE_LITERALS))
    return false;
  for (uint32_t i = 0; i < len; i++)
  {
    if (!isspace(parsed_code[begin+i]))
      return false;
  }
  return true;
}

i32 _butch_prev(const Pack<Unit>& units, i32 i)
{
  for (u32 j = 0; j < i; _next(j, units.to_view()))
  {
    if (j + units[j].children_len + 1 >= i)
      return j;
  }
  return -1;
}

bool Parser::_check_and_do_rule(
  Rule& r,
  Pack<Unit>& out,
  list<Rule::Pt>& pt_buffer
) {
  Match match;
  bool did_something = false;
  const rule_id rule_idx = &r - rules.data();
  bool test;
  auto dyn_out_iter = [&out](DynDirRange<u32>& i){ _next(i, out.to_view()); };
  auto dyn_out_rev_iter = [&out](DynDirRange<u32>& i){ _butch_prev(out, i); };
  Pack<Unit>::State state;

  char c = classes[r.out].fix();
  printf(">>>> %s <<<<\n", classes[r.out].start);
  classes[r.out].mov(c);
  debug_output(stdout, out.to_view());

  assert(!(flags & _FLAG_START_FIXED_MATCH)); // TEMP

  if (r.is_context())
  {
    for (u32 i : out.dyn_range().iter_with(dyn_out_iter))
    {
      if (out[i].id == Unit::LITERAL)
      {
        state = out.set(i, 1);
        match = r._match_context(*this, out, pt_buffer);
        if (match.matched)
        {
          r._apply_context(*this, out, pt_buffer, match);
          did_something = true;
        }
        out.reset(state);
      }
    }
  }
  else if (r.is_alias())
    return false; // this is an alias
  else if (r.is_token())
  {
    // token (no peeping can happen here)
    for (u32 i = 0; i < out.len; _next(i, out.to_view()))
    {
      if (out[i].id == Unit::LITERAL)
      {
        state = out.set(i, 1);
        match = r._sequence_match(*this, out, pt_buffer);
        out.reset(state);
        if (match.matched)
        {
          state = out.set(i, 1);
          _butch_split_token(*this, out, match, rule_idx);
          out.reset(state);
          did_something = true;
        }
      }
    }
  }
  else if (r.has_rl_parse())
  {
    for (i32 i = _butch_prev(out, out.len); i >= 0; i=_butch_prev(out,i))
    {
      u32 j = i;
      _next(j, out.to_view());
      for (; j <= out.len; _next(j,out.to_view()))
      {
        assert(j > i);
        state = out.set(i, j-i);
        match = r._sequence_match(*this, out, pt_buffer);
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
          j++;
        }
        _next(j, out.to_view());
      }
    }
  }
  else
  {
    // top-class abstract syntax node type
    for (u32 i = 0; i < out.len; _next(i, out.to_view()))
    {
      u32 j = i;
      _next(j, out.to_view());
      for (; j < out.len+1; _next(j, out.to_view())) {
        assert(j > i);
        state = out.set(i, j-i);
        match = r._sequence_match(*this, out, pt_buffer);
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
          j++;
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

  if (_should_be_trimmed(0, parsed_code.len))
    return out.ls();

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

  pt_buffer.destroy();

  return out.ls();
}


void Parser::debug_output(FILE* f, const View<Unit> ls)
{
  int8_t depth_pt = 0;
  int32_t depth_stack[127];
  char c;
  for (int i = 0; i < ls.len; i++)
  {
    for (int j = 0; j < depth_pt; j++)
      fprintf(f, "  ");
    const View<char> s =
      ls[i].id == Unit::LITERAL ? ls[i].str(*this)
                                : classes[rules[ls[i].id].out];
    if (ls[i].ctx >= 0)
    {
      const class_id cid = ctxs[ls[i].ctx].name;
      c = classes[cid].fix();
      fprintf(f, "(%s)", classes[cid].start);
      classes[cid].mov(c);
    }
    else if (ls[i].ctx == Unit::ERROR_CONTEXT_MISMATCH)
      fprintf(f, "(!CONTEXT_MISMATCH)");
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    for (int j = 0; j < s.len; j++)
      putc(s[j], f);
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    putc(' ', f);
    // fprintf(f, "(%i) ", ls[i].children_len);
    if (ls[i].id != Unit::LITERAL && rules[ls[i].id].is_token())
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

