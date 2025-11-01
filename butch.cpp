#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "list.hpp"
#include "stuff.hpp"
#include "butch.hpp"

#define expect(X) do { assert(X); [[assume(X)]]; } while(false)
#define ALIAS_CHAR '?'

Own<char*> _butch_read_file(Ref<const char*> filename)
{
  FILE* f = fopen(filename, "r");
  if (f == nullptr) return nullptr;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  auto s = (char*)malloc(len+1);
  fread(s, 1, len, f);
  fclose(f);
  s[len] = 0;
  return s;
}

Own<char*> _butch_allocpy_view(const View<char> v)
{
  char* ret = (char*)malloc(v.len+1);
  memcpy(ret, v.start, v.len);
  ret[v.len] = 0;
  return ret;
}

View<char> Unit::str(const Parser& p)
{
  return View<char>::range(p.parsed_code.start+s_start, s_len);
}

bool Unit::is(const Parser& p, const View<char> s)
{
  if (s.start == nullptr) return false;
  if (id == LITERAL) return false;
  Rule* r = nullptr;
  rule_id rid;
  for (u32 i = 0; i < p.rules.len(); i++)
  {
    if (p.classes[p.rules[i].out] == s)
    { r = p.rules.data() + i; rid = i; break; }
  }
  if (!r) return false;
  if (r->is_alias())
    return _butch_alias_match(p, *r, *this);
  else
    return id == rid;
}

const char* Parser::_load_str(
  View<char> s,
  View<char> surround,
  View<char> start
) {
  expect(s.len > 0); // undefined behavior otherwise
  expect(bool(surround.start) != (surround.len == 0));
  if (buff._cap < s.len+1+surround.len*2+start.len)
    buff.prealloc(s.len+1+surround.len*2+start.len);
  if (start.start)
    memcpy(buff.data(), start.start, start.len);
  if (surround.start)
    memcpy(buff.data()+start.len, surround.start, surround.len);
  memcpy(buff.data()+start.len+surround.len, s.start, s.len);
  if (surround.start)
    memcpy(
      buff.data()+start.len+s.len+surround.len,
      surround.start, surround.len
    );
  buff.data()[start.len+s.len+surround.len*2] = 0;
  return buff.data();
}

i32 Parser::_comp_rgx(const View<char> patt, rule_id rout)
{
  const View<char> surround =
    flags & FLAG_ISOLATED_MODE_REGEX
    ? View<char>::range((char*)"\\b", 3) : View<char>::empty();
  const View<char> ld_start =
    rout == Rule::BEGIN || rout == Rule::END
    ? View<char>::range((char*)"^", 1) : View<char>::empty();
  const char* const cstr_patt = _load_str(patt, surround, ld_start);
  regex_patterns.push_ref(RegexPatt());
  try
  {
    regex_patterns[-1] = RegexPatt(cstr_patt);
  }
  catch (std::regex_error& err)
  {
     classes.push(View<char>::range((char*)err.what(), 0U));
     return -1;
  }
  return regex_patterns.len()-1;
}

int _butch_hex2byte(const char* const p)
{
  int res = 0;
  for (int i = 0; i < 2; i++)
  {
    const char c= *(p+i);
    if ('0' <= c && c <= '9')
    { res |= (c - '0')<<(4*(1-i)); }
    else if ('a' <= c && c <= 'f')
    { res |= (c - 'a' + 10)<<(4*(1-i)); }
    else if ('A' <= c && c <= 'F')
    { res |= (c - 'A' + 10)<<(4*(1-i)); }
    else
      return -1;
  }
  return res;
}

const char* Parser::_comp_str(View<char> s)
{
  _load_str(s);
  int off = 0;
  int suc;
  const int len = buff.len();
  for (int i = 0; i < len-off; i++)
  {
    if (i == len-off-1)
    { buff[i] = buff[i+off]; break; }
    if (buff[i+off] == '\\')
    {
      switch (buff[i+off+1])
      {
        case 'n': buff[i] = '\n'; break;
        case 't': buff[i] = '\t'; break;
        case 'e': buff[i] = '\e'; break;
        case 'r': buff[i] = '\r'; break;
        case 'a': buff[i] = '\a'; break;
        case 'x':
          if (i >= len-off-4)
          { buff[i] = buff[i+off]; off--; }
          else
          {
            if ((suc = _butch_hex2byte(buff.data()+i+off+2)) >= 0)
            {
              buff[i] = suc;
              off += 2;
            }
            else { buff[i] = buff[i+off]; off--; }
          }
        default:
          buff[i] = buff[i+off];
          off--;
      }
      off++;
    }
    else
      buff[i] = buff[i+off];
  }
  buff.data()[len-off] = 0;
  return buff.data();
}

size_t Parser::serialize_file(FILE* f)
{
  const auto callback = [](const void* src, size_t size, void* data)
  {
    return fwrite(src, size, 1, (FILE*)data);
  };
  return serialize(callback, f);
}

char* Parser::serialize(size_t* s)
{
  list<u8> data;
  data.init();
  const auto callback = [](const void* src, size_t size, void* data)
  {
    auto& ls = *(list<u8>*)data;
    ls.prealloc(ls.len() + size);
    if (ls.data() == nullptr)
      return 0UL;
    ls.set_len(ls.len() + size);
    memcpy(ls.data(), src, size);
    return size;
  };
  *s = serialize(callback, (void*)&data);
  return (char*)data.data();
}

struct SerializedClass
{
  size_t s;
  decltype(View<char>::len) l;
  u32 flags; // << this only exists because we can't serialize regex for now
};

size_t Parser::serialize(
  size_t(*callback)(const void*,size_t,void*),
  void* data
) {
#define STORE { if (!temp) return 0; else total += temp; }
  constexpr size_t length_size = sizeof(ctxs._len);
  size_t total = 0, temp;
  char* s;
  // 0. flags
  temp = callback(&flags, sizeof(flags)+sizeof(flags_data), data);
  STORE;
  // 1. compute total code length
  size_t len = 0;
  for (View<char> v : classes)
  {
    if (*v.start != '\'')
    {
      len += v.len;
      continue;
    }

    s = v.start;
    while (*s != '\'' && *s != 0) s++;
    len += s - v.start;
  }
  temp = callback(&len, sizeof(size_t), data);
  STORE;
  // 2. copy only the necessary butch code
  for (View<char> v : classes)
  {
    if (*v.start == '\'')
    {
      s = v.start;
      while (*s != '\'' && *s != 0) s++;
      len = s - v.start;
    }
    else
      len = v.len;
    temp = callback(v.start, len, data);
    STORE;
  }
  // 3. copy classes (with offsets fixed)
  temp = callback(&classes._len, length_size, data);
  STORE;
  len = 0;
  const u32 cl_len = classes.len();
  for (u32 i = 0; i < cl_len; i++)
  {
    View<char> v = classes[i];
    SerializedClass obj;

    obj.s = len;
    obj.l = v.len;
    obj.flags = 0;
    for (Rule& r : rules)
    {
      if (
        (r.out == Rule::BEGIN
      || r.out == Rule::END)
      && in_class(r, 0) == class_id(i)
      ) {
        obj.flags = 1;
        break;
      }
    }
    temp = callback(&obj, sizeof(obj), data);
    STORE;

    if (*v.start != '\'')
    {
      len += v.len;
      continue;
    }
    s = v.start;
    while (*s != '\'' && *s != 0) s++;
    len += s - v.start;
  }
  // 4. Then copy everything else in bulk
  temp = callback(&ctxs._len, length_size, data);
  STORE;
  temp = callback(&rules._len, length_size, data);
  STORE;
  temp = callback(&rulesets_rules._len, length_size, data);
  STORE;
  // this is the same as rulesets_rules._len so we skip
  // temp = callback(&rulesets_classes._len, length_size, data);
  // STORE;
  
  temp = callback(ctxs.data(), sizeof(Context)*ctxs.len(), data);
  STORE;
  temp = callback(rules.data(), sizeof(Rule)*rules.len(), data);
  STORE;
  temp = callback(
    rulesets_rules.data(),
    sizeof(rule_id)*rulesets_rules.len(),
    data
  );
  STORE;
  temp = callback(
    rulesets_classes.data(),
    sizeof(class_id)*rulesets_classes.len(),
    data
  );
  STORE;

  return total;
#undef STORE
}

Result<Own<Parser>, Parser::Error> Parser::deserialize_file(FILE* f)
{
  const auto callback = [](void* src, size_t size, void* data)
  {
    return fread(src, size, 1, (FILE*)data);
  };
  return Parser::deserialize(callback, f);
}

Result<Own<Parser>, Parser::Error> deserialize(const char* p, size_t size)
{
  struct P { const char* a, *b; };
  P data = {p, p+size};
  const auto callback = [](void* dst, size_t size, void* data)
  {
    auto& p = *(P*)data;
    [[assume(p.b >= p.a)]];
    if (u64(p.b - p.a) < size)
      return 0UL;
    memcpy(dst, p.a, size);
    p.a += size;
    return size;
  };
  return Parser::deserialize(callback, (void*)&data);
}

Result<Own<Parser>, Parser::Error> Parser::deserialize(
  size_t(*callback)(void*,size_t,void*),
  void* data
) {
#define STORE { \
    if (!temp) \
    { \
      self.destroy(); \
      return err<Parser>(Error{ \
        .type=Error::DESERIALIZATION_ERROR, .offset=0}); \
    } \
    else \
    { \
      total += temp; \
    } \
  }
  constexpr size_t length_size = sizeof(ctxs._len);
  Parser self;
  size_t total = 0, temp;
  size_t len = 0;
  decltype(ctxs._len) ls_len, i;
  i32 regex_index;
  // 0. flags
  temp = callback(&self.flags, sizeof(flags)+sizeof(flags_data), data);
  STORE;
  // 1. compute total code length
  temp = callback(&len, sizeof(size_t), data);
  STORE;
  self.butch_code = (char*)malloc(len);
  // 2. copy only the necessary butch code
  temp = callback(self.butch_code, len, data);
  STORE;
  // 3. copy classes (with offsets fixed)
  temp = callback(&ls_len, length_size, data);
  STORE;
  self.classes.prealloc(ls_len);
  len = 0;
  for (i = 0; i < ls_len; i++)
  {
    SerializedClass obj;

    temp = callback(&obj, sizeof(obj), data);
    STORE;

    const View<char> patt = View<char>::range(self.butch_code+obj.s, obj.l);
    if (self.butch_code[obj.s] != '\'')
    {
      self.classes.push(patt);
      continue;
    }
    regex_index = self._comp_rgx(patt, obj.flags ? Rule::BEGIN : 0);
    assert(regex_index >= 0);
    self.classes.push(View<char>::range(patt.start, regex_index));
  }
  // 4. Then copy everything else in bulk
  temp = callback(&ls_len, length_size, data);
  STORE;
  self.ctxs.prealloc(ls_len);
  self.ctxs.set_len(ls_len);
  temp = callback(&ls_len, length_size, data);
  STORE;
  self.rules.prealloc(ls_len);
  self.rules.set_len(ls_len);
  temp = callback(&ls_len, length_size, data);
  STORE;
  self.rulesets_rules.prealloc(ls_len);
  self.rulesets_rules.set_len(ls_len);
  self.rulesets_classes.prealloc(ls_len);
  self.rulesets_classes.set_len(ls_len);
  
  temp = callback(
    self.ctxs.data(),
    sizeof(Context)*self.ctxs.len(),
    data
  );
  STORE;
  temp = callback(
    self.rules.data(),
    sizeof(Rule)*self.rules.len(),
    data
  );
  STORE;
  temp = callback(
    self.rulesets_rules.data(),
    sizeof(rule_id)*self.rulesets_rules.len(),
    data
  );
  STORE;
  temp = callback(
    self.rulesets_classes.data(),
    sizeof(class_id)*self.rulesets_classes.len(),
    data
  );
  STORE;

  return ok<Error>(self);
#undef STORE
}

