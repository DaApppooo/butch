#include "list.hpp"
#include "stuff.hpp"
#include <cstring>

namespace butchgex
{

struct Token
{
  enum Type : u8
  {
    GROUP, // <-> Group
    UNCAPTURED_GROUP, // <-> Group
    UNCAPTURED_NOT_GROUP, // <-> Group
    REPEAT, // <-> Repeat
    LAZY_REPEAT, // <-> Repeat
    LITERAL, // <-> Literal
    RANGE, // <-> Range
    NOT_RANGE, // <-> Range
    DESER_ERROR
  };
  /*
    Encodes:
      - Whole regex expression
      - Explicit groups: '(a|b|c)'
      - Lookaheads and lookbehinds: '(?=hello)'
      - Negative lookaheads and lookbehinds: '(?!hello)'
  */
  struct Group
  {
    using OrStartPt = u32;
    /*
      - empty: always matches
      - n>=2 elements:
        - the [1,n-1[-th elements are where the ors are in the group
        - the n-th element is the end of the group
    */
    list<OrStartPt> ors;
    static constexpr Group init()
    { Group self;
      self.ors.init();
      return self; }
  };
  /*
    Encodes:
      - Explicit ranges: '[a-zA-Z]'
      - Character groups: '\w'
      - Exclusion ranges: '[^a-z]'
      - Character exclusion groups: '\W'
  */
  struct Range
  {
    using Char = u8;
    static constexpr u8 EXPAND = 0; // the dash in the [  ]
    // Syntax:
    // - Single matches are next to each other.
    // - When EXPAND symbol is found, the two next characters indicate an ascii
    //   range.
    list<Char> subranges;
    static constexpr Range init()
    { Range self;
      self.subranges.init();
      return self; }
  };
  /*
    Encodes:
      - Star: 'x*'
      - Plus: 'x+'
      - Explicit repeats: '(hello){5,9}'

    Note: The token must be placed before what is repeated
  */
  struct Repeat
  {
    static constexpr u32 INF = 0;
    u32 min, max;
  };
  union MaxSizeUtil { Group _0; Range _1; Repeat _2; };
  using Literal = i8[sizeof(MaxSizeUtil)];
  union
  {
    Group _group;
    Range _range;
    Repeat _repeat;
    Literal _lit;
  };
  Type type;

  constexpr Token() : _lit{}, type(LITERAL) {}
  constexpr Token(const Token& o)
    : _lit{}, type(LITERAL)
  {
    type = o.type;
    switch (o.type)
    {
      case GROUP:
      case UNCAPTURED_GROUP:
      case UNCAPTURED_NOT_GROUP:
        _group = o._group;
        break;
      case RANGE:
      case NOT_RANGE:
        _range = o._range;
        break;
      case REPEAT:
      case LAZY_REPEAT:
        _repeat = o._repeat;
        break;
      case LITERAL:
        std::copy(o._lit, o._lit+sizeof(Literal), _lit);
        break;
      case DESER_ERROR:
        assert(false);
    }
  }
  
  static constexpr Token new_group()
  {
    Token t;
    t.type = GROUP;
    t._group = Group::init();
    return t;
  }
  static constexpr Token new_uncaptured_group()
  {
    Token t;
    t.type = UNCAPTURED_GROUP;
    t._group = Group::init();
    return t;
  }
  static constexpr Token new_uncaptured_not_group()
  {
    Token t;
    t.type = UNCAPTURED_NOT_GROUP;
    t._group = Group::init();
    return t;
  }
  static constexpr Token new_repeat(u32 start, u32 end)
  {
    Token t;
    t.type = REPEAT;
    t._repeat = Repeat{start, end};
    return t;
  }
  /*
    Fetch up to sizeof(MaxSizeUtil) bytes and subtracts it from the total
    length. If the resulting length is not 0, then another literal token
    must be created, and that until len == 0.
  */
  static constexpr Token new_literal(char* str, int& len)
  {
    Token t;
    t.type = LITERAL;
    if (len <= sizeof(Literal))
    {
      std::copy(str, str+len, t._lit);
      memset(t._lit+len, 0, sizeof(Literal)-len);
      len = 0;
    }
    else
    {
      std::copy(str, str+sizeof(Literal), t._lit);
      len -= sizeof(Literal);
    }
    return t;
  }
  static constexpr Token new_range()
  {
    Token t;
    t.type = RANGE;
    t._range = Range::init();
    return t;
  }
  static constexpr Token new_not_range()
  {
    Token t;
    t.type = NOT_RANGE;
    t._range = Range::init();
    return t;
  }

  constexpr bool is_group() const
  {
    return type == GROUP
           || type == UNCAPTURED_GROUP
           || type == UNCAPTURED_NOT_GROUP;
  }
  constexpr bool is_uncaptured_group() const
  {
    return type == UNCAPTURED_GROUP
           || type == UNCAPTURED_NOT_GROUP;
  }
  constexpr bool is_repeat() const
  {
    return type == REPEAT
           || type == LAZY_REPEAT;
  }
  constexpr bool is_range() const
  {
    return type == RANGE
           || type == NOT_RANGE;
  }
  constexpr Group::OrStartPt group_end() const
  {
    assert(is_group());
    return _group.ors[-1];
  }
  constexpr u32 min() const
  {
    assert(is_repeat());
    return _repeat.min;
  }
  constexpr u32 max() const
  {
    assert(is_repeat());
    return _repeat.max;
  }
  constexpr bool is(Type t) const
  { return t == type; }
  constexpr bool match(const View<Token> tree, View<char>& v);
  size_t serialize(
    size_t(*cb)(const void* src, size_t size, void* data),
    void* data
  ) {
#define CB(pt, size) \
  { tmp = cb(pt, size, data); if (size && !tmp) { return 0; } total += tmp; }
    size_t tmp, total = 0;
    CB(&type, sizeof(type));
    switch (type)
    {
      case LITERAL:
        CB(_lit, sizeof(_lit));
        break;
      case REPEAT:
      case LAZY_REPEAT:
        CB(&_repeat, sizeof(_repeat));
        break;
      case GROUP:
      case UNCAPTURED_GROUP:
      case UNCAPTURED_NOT_GROUP:
        tmp = _group.ors.serialize(cb, data);
        if (tmp == 0)
          return 0;
        total += tmp;
        break;
      case RANGE:
      case NOT_RANGE:
        CB(&_range.subranges._len, sizeof(_range.subranges._len));
        CB(
           _range.subranges.data(),
           _range.subranges.len()*sizeof(Range::Char)
        );
        break;
      case DESER_ERROR:
        assert(false);
    }
    return total;
#undef CB
  }
  static Token deserialize(
    size_t(*cb)(void* dst, size_t size, void* data),
    void* data
  ) {
#define CB(pt, size) \
  { if (size && !cb(pt, size, data)) \
    { self.type = DESER_ERROR; return self; } \
    total += tmp; }
    Token self;
    size_t tmp, total = 0;
    CB(&self.type, sizeof(type));
    switch (self.type)
    {
      case LITERAL:
        CB(self._lit, sizeof(_lit));
        break;
      case REPEAT:
      case LAZY_REPEAT:
        CB(&self._repeat, sizeof(_repeat));
        break;
      case GROUP:
      case UNCAPTURED_GROUP:
      case UNCAPTURED_NOT_GROUP:
        self._group.ors = self._group.ors.deserialize(cb, data);
        if (self._group.ors.deserialization_failed())
        { self.type = DESER_ERROR; return self; }
        break;
      case RANGE:
      case NOT_RANGE:
        self._range.subranges = self._range.subranges.deserialize(cb, data);
        if (self._range.subranges.deserialization_failed())
        { self.type = DESER_ERROR; return self; }
        break;
      case DESER_ERROR:
        assert(false);
    }
    return self;
  }
  void destroy()
  {
    if (is_group())
      _group.ors.destroy();
    if (is_range())
      _range.subranges.destroy();
  }
};

// matches in normal group mode, 'uncaptured' and 'not' can be matched later
/*
  Iters through every section of the group separated by a '|'.
  Returns wether at least one section matched and outputs
  in v the matched string (excluding uncaptured start and end if present).
  Note that this behavior is different from other 'match' functions where
  characters from v are consumed if there's a match. This requires special
  handling when facing a group.
*/
constexpr bool match(Token::Group g, View<Token> tree, View<char>& v);
// same for ranges here
constexpr bool match(Token::Range g, const View<Token> tree, View<char>& v);
constexpr bool match(Token::Literal g, const View<Token> tree, View<char>& v);

// 'NOT' is applied, but the 'UNCAPTURED' flag is not
constexpr bool Token::match(const View<Token> tree, View<char>& v)
{
  assert(!is(REPEAT) && !is(LAZY_REPEAT));
  switch (type)
  {
    case GROUP:
    case UNCAPTURED_GROUP:
      return ::butchgex::match(_group, tree, v);
    case UNCAPTURED_NOT_GROUP:
      return !::butchgex::match(_group, tree, v);
    case LITERAL:
      return ::butchgex::match(_lit, tree, v);
    case RANGE:
      return ::butchgex::match(_range, tree, v);
    case NOT_RANGE:
      return !::butchgex::match(_range, tree, v);
    case LAZY_REPEAT:
    case REPEAT:
    case DESER_ERROR:
      assert(false);
  }
  return false;
}

struct Regex
{
  list<Token> script;
};

// Returns true on failure
constexpr bool _unit_match(
  u32& pt,
  const u32 end_pt,
  View<Token> tree,
  const View<char> save,
  View<char>& v,
  View<char>& str_match
) {
  if (tree[pt].match(tree.slice(pt, end_pt-pt), v))
  {
    if (tree[pt].is_group())
    {
      pt = tree[pt].group_end();
      v.start = v.end();
    }
    if (tree[pt].is_uncaptured_group())
    {
      if (str_match.start == save.start)
        str_match.start = v.start;
      else
        str_match.set_end(v.start);
    }
    return false;
  }
  else
  {
    v = save;
    return true;
  }
}

/*
*/

constexpr bool match(Token::Group g, View<Token> tree, View<char>& v)
{
  const u32 or_c = g.ors.len();
  if (or_c == 0)
    return true;
  assert(or_c > 1);
  for (u32 i : range(or_c-1))
  {
    u32 pt = g.ors[i];
    const u32 end_pt = g.ors[i+1];
    View<char> save = v;
    View<char> str_match = save;
    while (pt < end_pt)
    {
      if (tree[pt].is(Token::REPEAT))
      {
        const Token& repeat = tree[pt];
        pt++;
        u32 reps = 0, temp_pt = pt;
        while (!_unit_match(temp_pt, end_pt, tree, save, v, str_match))
        {
          reps++;
          if (repeat.max() && reps > repeat.max())
            break;
          temp_pt = pt;
        }
        if (reps < repeat.min())
          break;
      }
      else if (tree[pt].is(Token::LAZY_REPEAT))
      {
        const Token& repeat = tree[pt];
        pt++;
        u32 reps = 0, temp_pt = 0;
        for (; reps < repeat.min(); reps++)
        {
          temp_pt = pt;
          if (!_unit_match(temp_pt, end_pt, tree, save, v, str_match))
            break;
        }
        if (reps < repeat.min())
          break;
        assert()
      }
      else if (!_unit_match(pt, end_pt, tree, save, v, str_match))
        break;
      pt++;
    }
    if (pt >= end_pt)
    {
      if (str_match.end() == save.end())
        str_match.set_end(v.start);
      v = str_match;
      return true;
    }
  }
  return false;
}

constexpr bool match(Token::Range g, const View<Token> tree, View<char>& v)
{
  assert(v.len > 0);
  const u32 l = g.subranges.len();
  for (u32 i = 0; i < l; i++)
  {
    if (g.subranges[i] == Token::Range::EXPAND)
    {
      assert(i < l-2);
      if (v[0] < g.subranges[i+1] || v[0] > g.subranges[i+2])
        return false;
      i+=2;
    }
    else if (v[0] != g.subranges[i])
      return false;
  }
  return true;
}

constexpr bool match(Token::Literal g, const View<Token> tree, View<char>& v)
{
  const u32 l = strnlen((const char*)g, sizeof(Token::Literal));
  if (v.len < l)
    return false;
  if (memcmp(v.start, g, l))
    return false;
  v.eat(l);
  return true;
}

}

