#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
  return View<char>(p.parsed_code.start+s_start, s_len);
}

bool Unit::is(const Parser& p, const View<char> s)
{
  if (s.start == nullptr) return false;
  if (id == LITERAL) return false;
  Rule* r = nullptr;
  rule_id rid;
  for (int i = 0; i < p.rules.len(); i++)
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


size_t Parser::serialize(
  size_t(*callback)(const void*,size_t,void*),
  void* data
) {
#define STORE { if (!temp) return 0; else total += temp; }
  constexpr size_t length_size = sizeof(ctxs._len);
  size_t total = 0, temp;
  char* s;
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
  for (View<char> v : classes)
  {
    struct { size_t s; decltype(v.len) l; } obj;

    obj.s = len;
    obj.l = v.len;
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
  temp = callback(&flags, sizeof(flags)+sizeof(flags_data), data);
  STORE;
  
  temp = callback(ctxs.data(), sizeof(Context)*ctxs.len(), data);
  STORE;
  temp = callback(rules.data(), sizeof(Rule)*rules.len(), data);
  STORE;
  temp = callback(rulesets_rules.data(), sizeof(rule_id)*rulesets_rules.len(), data);
  STORE;
  temp = callback(rulesets_classes.data(), sizeof(class_id)*rulesets_classes.len(), data);
  STORE;

  return total;
}


