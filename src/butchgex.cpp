#include "list.hpp"
#include "stuff.hpp"
#include <expected>

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
  
  constexpr bool is(Type t)
  { return t == type; }
  constexpr bool match(View<char>& v);
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
  { tmp = cb(pt, size, data); \
    if (size && !tmp) { self.type = DESER_ERROR; return self; } \
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
        CB(&self._group.ors._len, sizeof(_group.ors._len));
        CB(self._group.ors.data(), _group.ors.len()*sizeof(Group::OrStartPt));
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
  }
  void destroy()
  {
    if (is(GROUP) || is(UNCAPTURED_GROUP) || is(UNCAPTURED_NOT_GROUP))
      _group.ors.destroy();
    if (is(RANGE) || is(NOT_RANGE))
      _range.subranges.destroy();
  }
};

// matches in normal group mode, 'uncaptured' and 'not' can be matched later
constexpr bool match(Token::Group g, View<char>& v)
{
  return false;
}
// same for ranges here
constexpr bool match(Token::Range g, View<char>& v)
{
  return false;
}
constexpr bool match(Token::Literal g, View<char>& v)
{
  return false;
}

// 'NOT' is applied, but the 'UNCAPTURED' flag is not
constexpr bool Token::match(View<char>& v)
{
  assert(!is(REPEAT) && !is(LAZY_REPEAT));
  switch (type)
  {
    case GROUP:
    case UNCAPTURED_GROUP:
      return ::butchgex::match(_group, v);
    case UNCAPTURED_NOT_GROUP:
      return !::butchgex::match(_group, v);
    case LITERAL:
      return ::butchgex::match(_lit, v);
    case RANGE:
      return ::butchgex::match(_range, v);
    case NOT_RANGE:
      return !::butchgex::match(_range, v);
    case LAZY_REPEAT:
    case REPEAT:
    case DESER_ERROR:
      assert(false);
  }
  return false;
}

struct Regex
{
  
};

}

