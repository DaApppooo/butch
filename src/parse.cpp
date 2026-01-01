#include "butch.hpp"
#include <cstdio>
// #include <regex.h>
#include <regex>
#include <cctype>
#include "stuff.hpp"
#include "mystd/iters.hpp"

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

View<const char> _butch_eq_iso(
  int i,
  View<const char> big,
  View<const char> pattern,
  bool iso
) {
  assert(big.len()-i >= pattern.len());
  if (memcmp(big.begin()+i, pattern.begin(), pattern.len()) == 0)
  {
    if (!iso)
      return {big.begin()+i, pattern.len()};
    bool left = i == 0,
         right = u32(i) == big.len() - pattern.len() - 1;
    if (!left) // ensure that no deref is done if i == 0
      left = isspace(big.begin()[i-1])
             || !isalnum(big.begin()[i-1])
             || !isalnum(big.begin()[i]);
    if (!right) // same thing
      right = isspace(big.begin()[i+pattern.len()])
              || !isalnum(big.begin()[i+pattern.len()])
              || !isalnum(big.begin()[i+pattern.len()-1]);
    if (left && right)
      return {big.begin()+i, pattern.len()};
  }
  return View<const char>::empty();
}

View<const char> _butch_str_match(
  View<const char> big,
  View<const char> pattern,
  u8 iso
) {
  assert(iso < 3);
  if (big.len() < pattern.len())
    return View<const char>::empty();
  View<const char> r;
  if (iso == 2)
  {
    if (pattern == (r = View<const char>::range(big.begin(), pattern.len())))
      return r;
    else
     return View<const char>::empty();
  }
  for (u32 i : range(big.len() - pattern.len()))
  {
    if ((r = _butch_eq_iso(i, big, pattern, iso)).begin())
      return r;
  }
  return View<const char>::empty();
}

View<char> Parser::_rgx_match(View<char> big, RegexPatt& pattern)
{
  constexpr View<char> failed = View<char>::empty();
  RegexMatch m;
  if (std::regex_search(big.begin(), big.end(), m, *pattern))
  {
    return View<char>::from(m[0].first).to(m[0].second);
  }
  return failed;
}

constexpr void _butch_units_next(View<Unit>& units)
{ units._begin += units.begin()->children_len+1; }
#define UITER _butch_units_next
inline void _next(u32& i, View<const Unit> units)
{ i += units[i].children_len+1; }
inline void _next(u32& i, View<Unit> units)
{ _next(i, units.to_const()); }

Match _butch_failure(
  rule_id* in,
  View<Unit> units,
  List<Rule::Pt>& u,
  int rule_len
) {
  const u32 old_u = u[-1].u;
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
  // deactivate any reparsing
  for (u32 i = u[-1].u; i < old_u; _next(i, units))
    units[i].reparse_ctx = Unit::NO_REPARSE;
  return Rule::PARTIAL_FAILURE;
}

inline View<Unit> to_view(View<Unit> v) { return v; }
inline View<Unit> to_view(Pack<Unit>& v) { return v.to_view(); }
#define BUTCH_FAIL \
  _butch_failure( \
      p.rulesets_rules.data()+ruleset_start, \
      to_view(units), \
      u, \
      input_len \
    )

Match Rule::_match_str(
  const Parser& p,
  View<Unit> units,
  List<Pt>& u,
  const class_id cid
) {
  View<const char> s;
  const u8 iso_mode =
    bool(p.flags & Parser::FLAG_ISOLATED_MODE_STRINGS)
  | (bool(p.flags & Parser::_FLAG_START_FIXED_MATCH) << 1);
  const View<const char> big = units[u[-1].u].str(p);
  const View<const char> patt
    = p.classes[cid].shortened(1,1).to_const();
  if (units[u[-1].u].reparse_ctx != Unit::NO_REPARSE)
    return BUTCH_FAIL;
  if ((s = _butch_str_match(big, patt, iso_mode)).begin())
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.begin() - p.parsed_code.begin()), s.len(), true};
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
  List<Pt>& u,
  const class_id cid
) {
  View<char> s;
  const View<const char> big = units[u[-1].u].str(p);
  const View<const char> patt = p.classes[cid].to_const();
  if (units[u[-1].u].reparse_ctx != Unit::NO_REPARSE)
    return BUTCH_FAIL;
  if ((s = p._rgx_match(big.to_mut(), p.regex_patterns[patt.len()])).begin())
  {
    u[-1].r++;
    _next(u[-1].u, units);
    return {uint32_t(s.begin() - p.parsed_code.begin()), s.len(), true};
  }
  else
  { // failure
    return BUTCH_FAIL;
  }
  return Rule::TOTAL_FAILURE;
}

int _butch_split_token(
  Parser& p,
  Pack<Unit>& out, // offseted pack
  Match match,
  u32 rule_idx
) {
  const u32 initial_length = out[0].s_len;
  const u32 pre_len = match.s_start - out[0].s_start;
  const u32 post_len = initial_length - pre_len - match.s_len;
  const u32 post_start = match.s_start+match.s_len;
  int offset = 0;
  out[0].s_len = pre_len;
  out.insert(1, Unit::init(rule_idx, match.s_start, match.s_len, 0));
  if (!p._should_be_trimmed(post_start, post_len))
    out.insert(2, Unit::init(Unit::LITERAL, post_start, post_len, 0));
  if (p._should_be_trimmed(out[0].s_start, pre_len))
    out.rmv(0); // TODO : optimize this rmv out
  else
    offset++;
  return offset;
}

void _debug(Parser& p, Pack<Unit>& units)
{
  p.debug_output(stdout, units.ls().to_view());
}

void _butch_apply_context(
  Parser& p,
  Pack<Unit>& units,
  List<Rule::Pt>& u,
  const Context& ctx,
  rule_id rid,
  Match m
) {
  assert(units[0].id == Unit::LITERAL);
  assert(units.len() == 1);
  Unit* parent = &units[0];
  bool common_malcom_do_somethin = true;
  assert(m.matched);
  const u32 units_s_start = parent->s_start;
  const u32 units_s_len = parent->s_len;
  const i32 begin_len = m.u_start;
  const i32 head_len = i32(m.s_start) - units_s_start - begin_len;
  if (!p._should_be_trimmed(units_s_start, head_len))
  {
    assert(m.s_start - units_s_start - begin_len > 0);
    units.insert(0, Unit::init(
      Unit::LITERAL,
      units_s_start,
      head_len,
      0
    ));
    units.eat();
  }
  parent = &units[0];
  parent->s_start = m.s_start;
  parent->s_len = m.s_len;
  parent->id = rid;
  parent->children_len = m.s_len > 0;
  if (m.s_len > 0)
  {
    units.insert(1, Unit::init( // <-- the child
      Unit::LITERAL,
      m.s_start,
      m.s_len,
      0
    ));
    units.eat();
    while (common_malcom_do_somethin)
    {
      common_malcom_do_somethin = false;
      u32 i = 0;
      for (Rule& r : p.rules.slice(ctx.rules_start, ctx.rule_count))
      {
        if (r.out == Rule::BEGIN || r.out == Rule::END)
        { i++; continue; }
        printf("Sub-check rule %i\n", i+ctx.rules_start);
        common_malcom_do_somethin =
          p._check_and_do_rule(r, units, u)
          || common_malcom_do_somethin;
        i++;
      }
    }
    units.burp();
    units[0].children_len = units.len()-1;
  }
  const i32 total_end = units_s_start+units_s_len;
  const i32 ctx_end = m.s_start+m.s_len+m.u_end;
  const i32 tail_len = total_end - ctx_end;
  if (!p._should_be_trimmed(ctx_end, tail_len))
  {
    units.insert(units.len(), Unit::init(
      Unit::LITERAL,
      ctx_end,
      tail_len,
      0
    ));
  }
}

Match _butch_find_first_depth_stopper(
  Parser& p,
  Pack<Unit>& units,
  List<Rule::Pt>& u,
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
  List<Rule::Pt>& u
) {
#define LD_BEGIN(IDX) \
  p._comp_str(p.classes[p.in_class(p.rules[ctx.begin[0]], 0)]);
  
  const Context& ctx = p.ctxs[get_context()];
  Match m, sub_m, ds_m;
  List<u8> depth; // indicies in the ctx.begin and ctx.end
  assert(units[0].id == Unit::LITERAL);
  assert(p.rules[ctx.begin[0]].is_token());
  assert(p.rules[ctx.end[0]].is_token());
  assert(units.len() == 1);
  const u32 units_s_start = units[0].s_start;
  const u32 units_s_end = units_s_start + units[0].s_len;
  depth = List<u8>::init();
  do
  {
    m = p.rules[ctx.begin[0]]._sequence_match(p, units, u);
    if (!m.matched)
    {
      m = TOTAL_FAILURE;
      puts("C");
      goto FAILURE;
    }
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
          puts("B");
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
    for (u8 i : range<u8>(u8(ctx.be_count)))
    {
      sub_m = p.rules[ctx.end[i]]._sequence_match(p, units, u);
      if (sub_m.matched)
      {
        if (depth[-1] != i)
        {
          units[0].id = Unit::ERROR_CONTEXT_MISMATCH;
          m = BUTCH_FAIL;
          puts("A");
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
  depth.drop();
  return m;
FAILURE:
  puts("FAILED");
  assert(!m.matched);
  units[0].s_start = units_s_start;
  units[0].s_len = units_s_end - units_s_start;
  p.flags &= ~p._FLAG_START_FIXED_MATCH;
  depth.drop();
  return m;
}

Match Rule::_sequence_match(
  Parser& p,
  Pack<Unit>& units,
  List<Pt>& u
) {
#define RPTR(INDEX) (p.rulesets_rules.data()+ruleset_start+u[INDEX].r)
#define RPTR_OFFSET(INDEX,OFF) \
(p.rulesets_rules.data()+ruleset_start+u[INDEX].r + OFF)
#define CPTR(INDEX) (p.rulesets_classes.data()+ruleset_start+u[INDEX].r)
#define NEXT(UU) _next(UU, units.to_view())
#define FAIL \
  if ( \
    _butch_failure( \
    p.rulesets_classes.data()+ruleset_start, \
    to_view(units), \
    u, \
    input_len \
  ) == TOTAL_FAILURE) \
    return failure;
  
  constexpr Match failure = TOTAL_FAILURE;
  need(units.len() > 0);
  Match ret = {
    .s_start = units[units.len()-1].s_end(),
    .s_len=0,
    .matched=true,
    .u_start=0,
    .u_end=(uint16_t)units.len()
  };
  bool not_flag = false;
  u32 prev_u[2] = {0,0};
  u.clear();
  u.push(0, 0);
  u.push(0, 0);
  printf("===== Rule %s : units.len=%u =====\n",
         out >= 0 ? p._load_str(p.classes[out].to_const()) : "Begin/End",
         units.len()
  );
  p.debug_output(stdout, units.ls().to_view());
  puts("=== vvv ===");
  while (u[-1].r < input_len)
  {
    // printf(">>> |u|=%u u[-1]={.r=%i,.u=%i}\n",
    //   u.len(),
      //   u[-1].r,
    //   u[-1].u
    // );
    prev_u[0] = prev_u[1];
    prev_u[1] = u[-1].u;
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
          if (u[-1].u >= units.len())
          {
            if (u[-1].r == input_len-1)
              return ret;
            else
              FAIL;
          }

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
          u.push(rp, u[-1].u);
          std::swap(u[-1], u[-2]);
          u[-1].r++;
          break;
        }
      case REPARSE:
        units[prev_u[0]].reparse_ctx = *CPTR(-1);
        u[-1].r++;
        break;
      case STRING:
        if (u[-1].u >= units.len())
          FAIL;
        if (ret.include(_match_str(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      case REGEX:
        if (u[-1].u >= units.len())
          FAIL;
        if (ret.include(_match_rgx(p, units.to_view(), u, *CPTR(-1))))
          return failure;
        break;
      default: [[likely]];
        if (u[-1].u >= units.len())
          FAIL;
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
  if (u[-1].r < input_len || u[-1].u < units.len())
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
  for (u32 j = 0; i32(j) < i; _next(j, units.to_view()))
  {
    if (i32(j + units[j].children_len + 1) >= i)
      return j;
  }
  return -1;
}

void _butch_reparse(
  Parser& p,
  Pack<Unit>& out,
  List<Rule::Pt>& pt_buffer
) {
  assert(out.a > 0);
  Pack<Unit>::State state;
  for (u32 i = 0; i < out.len(); _next(i, out.to_view()))
  {
    if (out[i].reparse_ctx >= 0)
    {
      const rule_id prev = out[i].id;
      const i32 prev_cc = out[i].children_len;
      out.rmv(i+1, i+1+prev_cc);
      out[i].id = Unit::LITERAL;
      state = out.set(i, 1);
      _butch_apply_context(
        p,
        out,
        pt_buffer,
        p.ctxs[out[0].reparse_ctx],
        prev,
        Match{out[0].s_start, out[0].s_len, true, 0, 0}
      );
      out.reset(state);
      out[i].id = prev;
      out[i].reparse_ctx = Unit::NO_REPARSE;
      out.ls()[out.a-1].children_len += out[i].children_len - prev_cc;
    }
  }
}

bool Parser::_check_and_do_rule(
  Rule& r,
  Pack<Unit>& out,
  List<Rule::Pt>& pt_buffer
) {
  Match match;
  bool did_something = false;
  const rule_id rule_idx = &r - rules.data();
  Pack<Unit>::State state;

  println(">>>> {} <<<<\n", _load_str(classes[r.out].to_const()));
  debug_output(stdout, out.to_view());

  if (r.is_context())
  {
    for (u32 i = 0; i < out.len(); _next(i, out.to_view()))
    {
      if (out[i].id == Unit::LITERAL)
      {
        state = out.set(i, 1);
        match = r._match_context(*this, out, pt_buffer);
        if (match.matched)
        {
          _butch_apply_context(
            *this,
            out,
            pt_buffer,
            ctxs[r.get_context()],
            rule_idx,
            match
          );
          did_something = true;
          i += out.len()-1;
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
    for (u32 i = 0; i < out.len(); _next(i, out.to_view()))
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
    for (i32 i = _butch_prev(out, out.len()); i >= 0; i=_butch_prev(out,i))
    {
      u32 j = i;
      _next(j, out.to_view());
      for (; j <= out.len(); _next(j,out.to_view()))
      {
        assert(i32(j) > i);
        state = out.set(i, j-i);
        match = r._sequence_match(*this, out, pt_buffer);
        out.reset(state);
        if (match.matched)
        {
          out.insert(
            i+match.u_start,
            Unit::init(
              rule_idx,
              match.s_start, match.s_len,
              match.u_end-match.u_start
            )
          );
          state = out.set(i+match.u_start+1, match.u_end-match.u_start-1);
          _butch_reparse(*this, out, pt_buffer);
          out.reset(state);
          did_something = true;
          j++;
        }
        _next(j, out.to_view());
      }
    }
  }
  else
  {
    for (u32 i = 0; i < out.len(); _next(i, out.to_view()))
    {
      u32 j = i;
      _next(j, out.to_view());
      for (; j < out.len()+1; _next(j, out.to_view())) {
        assert(j > i);
        state = out.set(i, j-i);
        match = r._sequence_match(*this, out, pt_buffer);
        out.reset(state);
        if (match.matched)
        {
          out.insert(
            i+match.u_start,
            Unit::init(rule_idx,
                 match.s_start, match.s_len,
                 match.u_end-match.u_start
            )
          );
          state = out.set(i+match.u_start+1, match.u_end-match.u_start-1);
          _butch_reparse(*this, out, pt_buffer);
          out.reset(state);
          did_something = true;
          j++;
        }
      }
    }
  }
  return did_something;
}

Parser::Output Parser::parse(Ref<char*> s)
{
  Pack<Unit> out;
  mut pt_buffer = List<Rule::Pt>::init();
  out.init();
  parsed_code = {s, (u32)str_len_s(s)};

  if (_should_be_trimmed(0, parsed_code.len()))
    return { out.data(), out.len() };

  out.push(Unit::init(Unit::LITERAL, 0, str_len_s(s), 0));

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

  pt_buffer.drop();

  return {out.data(), out.len()};
}


void Parser::debug_output(FILE* f, const View<Unit> ls)
{
  int8_t depth_pt = 0;
  int32_t depth_stack[127];
  for (u32 i = 0; i < ls.len(); i++)
  {
    for (i8 j = 0; j < depth_pt; j++)
      fprintf(f, "  ");
    const View<char> s =
      ls[i].id == Unit::LITERAL ? ls[i].str(*this).to_mut()
                                : classes[rules[ls[i].id].out];
    if (ls[i].reparse_ctx == Unit::ERROR_CONTEXT_MISMATCH)
      fprintf(f, "(!CONTEXT_MISMATCH)");
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    for (u32 j = 0; j < s.len(); j++)
      putc(s[j], f);
    if (ls[i].id == Unit::LITERAL)
      putc('"', f);
    putc(' ', f);
    // fprintf(f, "(%i) ", ls[i].children_len);
    if (ls[i].id != Unit::LITERAL && rules[ls[i].id].is_token())
      fprintf(f, "'%s'", _load_str(ls[i].str(*this)));
    else
      fprintf(f, "[%i:+%i]", ls[i].s_start, ls[i].s_len);
    fprintf(f, " (%i)\n", ls[i].children_len);
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

