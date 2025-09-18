#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex.h>
// #include <regex>
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
    auto s = new char[len+1];
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
  return View<char>(p.parsed_code+str_begin, str_len);
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
  if (r->is_alias)
    return _butch_alias_match(p, *r, *this);
  else
    return id == rid;
}

const char* Parser::_load_str(View<char> s, View<char> surround)
{
  expect(s.len > 0); // undefined behavior otherwise
  expect(bool(surround.start) != (surround.len == 0));
  if (buff._cap < s.len+1+surround.len*2)
    buff.prealloc(s.len+1);
  if (surround.start)
    memcpy(buff.data(), surround.start, surround.len);
  memcpy(buff.data()+surround.len, s.start, s.len);
  if (surround.start)
    memcpy(buff.data()+s.len+surround.len, surround.start, surround.len);
  buff.data()[s.len+surround.len*2] = 0;
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


