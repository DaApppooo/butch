#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#ifndef H_LIST
#define H_LIST
#include <cassert>
#include <cstddef>
#include <cstring>

#include <experimental/type_traits>
#include <initializer_list>
#include <utility>
#ifdef FILE_DIALOG
#include "raylib.h"
#endif
// #define DEBUG
#ifdef DEBUG
#include <cstdio>
#define assertm(COND, ...) if (!(COND)) { fprintf(stderr, __VA_ARGS__); assert(COND); }
#else
#define assertm(COND, ...)
#endif


template <class InitIterator, class Output>
struct TreeIter
{
  using II = InitIterator;
  using O = Output;
  using Self = TreeIter<II, O>;
  InitIterator _v;
  std::function<void(InitIterator&)> _next;
  constexpr inline Self& operator ++ () { _next(_v); return *this; }
  constexpr inline Self begin() { return *this; }
  constexpr inline Self end() { return {_v.end()}; }
  constexpr inline Output& operator * () { return *_v; }
  constexpr inline bool operator == (Self other)
  { return _v == other._v; }
  constexpr inline bool operator != (Self other)
  { return _v != other._v; }
  constexpr inline bool operator < (Self other)
  { return _v < other._v; }
};

template <class T = int32_t>
struct DirRange
{
  using Self = DirRange<T>;
  T _i;
  T _end;
  int dir;
  static constexpr inline Self init(T start, T end, int dir)
  { return {start, end, dir}; }
  static constexpr inline Self init(T start, T end)
  { return {start, end, 1}; }
  static constexpr inline Self init(T end)
  { return {0, end, 1}; }
  constexpr inline Self& operator ++ () { _i+=dir; return *this; }
  constexpr inline Self begin() { return *this; }
  constexpr inline Self end() { return {_end, _end, dir,}; }
  constexpr inline T& operator * () { return _i; }
  constexpr inline bool operator == (Self other)
  { return _i == other._i; }
  constexpr inline bool operator != (Self other)
  { return _i*dir < other._i*dir; }
  constexpr inline bool operator < (Self other)
  { return _i*dir < other._i*dir; }
  constexpr inline operator T& () const { return _i; }
  constexpr inline operator T& () { return _i; }
  constexpr inline TreeIter<Self, T> iter_with(
    std::function<void(Self&)> f
  ) const
  { return TreeIter<Self, T>{*this, f}; }
  constexpr inline Self reverse_if(bool x) const
  { return Self::init(_end, _i, x ? -dir : dir); }
};

template <class T = int32_t>
struct DynDirRange
{
  using Self = DynDirRange<T>;
  T _i;
  const T& _end;
  int dir;
  static constexpr inline Self init(T start, const T& end, int dir)
  { return {start, end, dir}; }
  static constexpr inline Self init(T start, const T& end)
  { return {start, end, 1}; }
  static constexpr inline Self init(const T& end)
  { return {0, end, 1}; }
  constexpr inline Self& operator ++ () { _i+=dir; return *this; }
  constexpr inline Self begin() { return *this; }
  constexpr inline Self end() { return {_end, _end, dir,}; }
  constexpr inline T& operator * () { return _i; }
  constexpr inline bool operator == (Self other)
  { return _i == other._i; }
  constexpr inline bool operator != (Self other)
  { return _i*dir < other._i*dir; }
  constexpr inline bool operator < (Self other)
  { return _i*dir < other._i*dir; }
  constexpr inline operator T& () const { return _i; }
  constexpr inline operator T& () { return _i; }
  constexpr inline TreeIter<Self, T> iter_with(
    std::function<void(Self&)> f
  ) const
  { return TreeIter<Self, T>{*this, f}; }
  constexpr inline DirRange<T> reverse_if(bool x) const
  { return DirRange<T>::init(_end, _i, x ? -dir : dir); }
};


template <class T>
constexpr inline DirRange<T> range(T start, T end)
{ return DirRange<T>::init(start, end); }
template <class T>
constexpr inline DynDirRange<T> dyn_range(T start, T& end)
{ return DynDirRange<T>::init(start, end); }
template <class T>
constexpr inline DirRange<T> range(T start, T end, int step)
{ return DirRange<T>::init(start, end, step); }
template <class T>
constexpr inline DirRange<T> range(T end)
{ return DirRange<T>::init(end); }

template <class T>
struct View
{
  using mut_T = std::remove_const<T>;
  T* start; uint32_t len;
  static constexpr View<T> range(T* s, uint32_t len)
  { return {s, len}; }
  static constexpr View<T> from(T* s)
  { return {s, 0U}; }
  static constexpr View<T> empty() { return {nullptr, 0}; }
  View<T>& to(T* end)
  { len = end - start; return *this; }

  T& operator [] (int index) const
  { if (index < 0) index += len; return start[index]; }
  View<mut_T> mut() const
  { return {const_cast<mut_T>(start), len}; }
  View<T> shorten(int start_off, int end_off)
  { return View<T>(start+start_off, len-start_off-end_off); }
  View<T> slice(int sub_start, int sub_len) const
  { return View<T>(start+sub_start, sub_len); }
  constexpr inline View<T>& operator ++ () { start++; len--; return *this; }
  constexpr inline T& operator * () const { return *start; }
  constexpr inline View<T> begin() const { return *this; }
  constexpr inline View<T> end() const
  { return View::from(start+len).to(start+len); }
  constexpr inline bool operator < (View<T> other) const
  { return start < other.start; }
  constexpr inline bool operator != (View<T> other) const
  { return start < other.start; }
  constexpr inline TreeIter<View<T>, T> iter_with(void(*f)(View<T>&)) const
  { return TreeIter{*this, f}; }
  constexpr inline DirRange<decltype(len)> range() const
  { return DirRange<decltype(len)>::init(len); }
};
template <>
struct View<char>
{
  char* start; uint32_t len;
  // constexpr View() : start(nullptr), len(0) {}
  // constexpr View(char* start, uint32_t len) : start(start), len(len) {}
  // constexpr View(char* start, char* end) : start(start), len(end-start) {}
  // constexpr View(const View<char>& v) : start(v.start), len(v.len) {}
  // constexpr View(const View<char>&& v) : start(v.start), len(v.len) {}
  static constexpr View<char> range(char* s, uint32_t len)
  { return {s, len}; }
  static constexpr View<char> from(char* s)
  { return {s, 0U}; }
  static constexpr View<char> empty() { return {nullptr, 0}; }
  View<char>& to(char* end)
  { len = end - start; return *this; }

  char* end() { return start + len; }
  char& operator [] (int index) const
  { if (index < 0) index += len; return start[index]; }
  constexpr inline bool operator == (const View<char>& v)
  { return !(len != v.len || memcmp(start, v.start, len)); }
  char fix() const
  { char tmp = start[len]; start[len] = 0; return tmp; }
  void mov(char c) const
  { start[len] = c; }
  View<char> shorten(int start_off, int end_off)
  { return View<char>::range(start+start_off, len-start_off-end_off); }
};

template <class T>
bool operator == (const View<T> a, const View<T> b)
{
  if (a.len != b.len) return false;
  return memcmp(a.start, b.start, a.len*sizeof(T)) == 0;
}

template <class T> struct Pack;

template <class T> class list
{
public:
  using Self = list<T>;
  template <class U>
  using _temp_has_destroyer = decltype(std::declval<U&>().destroy());
  static constexpr bool has_destroyer
      = std::experimental::is_detected_exact_v<void, _temp_has_destroyer, T>;
  T* _data;
  uint32_t _len;
  uint32_t _cap;

  T* begin() const { return _data; }
  T* end() const { return _data+len(); }
  const T* cbegin() const { return _data; }
  const T* cend() const { return _data+len(); }

  list();
  list(const T*);
  list(const list<T>& other) { memcpy(this, &other, sizeof(list<T>)); }
  list(const list<T>&& other) { memcpy(this, &other, sizeof(list<T>)); }
  inline Self& init();
  inline Self& push(const T element)
  { push_ref(element); return *this; }
  inline Self& push_ref(const T& element);
  Self& prextend(View<T> ref_pre);
  Self& extend(View<T> ref_ext);
  inline Self& insert(T element, int index);
  // Return value is not destroyed !!!
  inline T pop();
  inline Self& prealloc(uint32_t size);
  inline Self& set_len(uint32_t newl);
  inline Self& realloc();
  inline Self& force_realloc();
  inline void destroy();
  inline Self copy() const;
  inline Self& rmv(int index);
  inline View<T> to_view() const;
  inline Pack<T> to_pack() const;
  inline View<T> slice(int start, int len) const;
  Self& rmv(int start, int end);
  inline Self& clear();
  inline Self& swap(int idx_a, int idx_b);
  inline Self& ref(const View<T>& other);
  inline Self& ref(const list<T>& other);
  inline Self& operator=(const list<T>& other);
  inline Self& operator=(std::initializer_list<T> init_ls);
  inline T& operator[](const int index) const;
  inline View<T> operator[](std::initializer_list<int> start_end);
  inline T& atp(const uint32_t index) const;
  inline T& atn(const int index) const;
  // this one is to avoid billions of call to .len()
  inline T& atn(const int index, const uint32_t length) const;
  

  // Modifies the state of the argument as disowned
  //  => other.cap := 0 (other.is_owned() := true)
  inline Self& own(list<T>& other);

  inline T* data() const;
  inline uint32_t len() const;
  inline uint32_t cap() const;

  // len == 0
  inline bool is_empty() const;
   
  inline Self& operator += (const Self&& other)
  {
    extend(other);
  }
  inline Self& operator += (const T&& obj)
  {
    push(obj);
  }
  // inline Self copy() const { // fuck C++
  //   struct fake_list_char
  //   {
  //     char* _p;
  //     uint32_t cap;
  //   };
  //   const uint32_t l = len();
  //   fake_list_char ret;
  //   ret._p = (char*)malloc(l);
  //   memcpy(ret._p, _data, sizeof(char)*(l+1));
  //   // Copying memory with length bytes.
  //   return *(list<char>*)&ret;
  // }
};

template <class T>
struct Pack
{
  using Self = Pack<T>;
  struct State { uint32_t s; uint32_t l; uint32_t il; };
  list<T> _data;
  uint32_t start;
  uint32_t len;
  void init() { _data.init(); start = len = 0; }
  T* data() {  _data.data() + start; }
  T& operator [] (uint32_t index) const
  { return _data.atp(start+index); }
  T& operator [] (uint32_t index)
  { return _data.atp(start+index); }
  Self& insert(T element, uint32_t index)
  { _data.insert(element, start + index); len++; return *this; }
  Self& push(T element)
  { _data.push(element); len++; return *this; }
  View<T> to_view() const
  { return _data.slice(start, len); }
  State set(uint32_t start_, uint32_t len_)
  {
    const State ret = {start, len, _data.len()};
    start += start_; len = len_;
    return ret;
  }
  State set_abs(uint32_t start_, uint32_t len_)
  {
    const State ret = {start, len, _data.len()};
    start = start_; len = len_;
    return ret;
  }
  int len_diff(State s)
  { return len - s.il; }
  void reset(State s)
  { start = s.s; len = s.l+(_data.len()-s.il); }
  Self& rmv(int i)
  { _data.rmv(start+i); len--; return *this; }
  Self& rmv(int s, int e)
  {
    _data.rmv(s, e);
    len -= e - s;
    return *this;
  }
  list<T>& ls() const { return _data; }
  list<T>& ls() { return _data; }
  constexpr inline DynDirRange<decltype(len)> dyn_range() const
  { return DynDirRange<decltype(len)>::init(len); }
};

#define GEN template <class T>
#define Self list<T>
#define self (*this)

GEN list<T>::list()
{
  _data = NULL;
  _cap = 0;
}

GEN inline T& list<T>::atp(const uint32_t index) const
{ return _data[index]; }
GEN inline T& list<T>::atn(const int index) const
{
  [[assume(index < 0)]];
  return _data[len() + index];
}
GEN inline T& list<T>::atn(const int index, const uint32_t len) const
{
  [[assume(index < 0)]];
  return _data[len + index];
}

GEN inline uint32_t list<T>::len() const
{
  if (_data == nullptr)
    return 0;
  return _len;
}

GEN inline Self& list<T>::set_len(uint32_t newl) 
{
  if (newl > 0)
  {
    assert(_data != nullptr);
    assert(_cap >= newl);
    _len = newl;
  }
  return *this;
}
GEN inline Self& list<T>::clear()
{
  if (_data == nullptr)
  {
    init();
    return *this;
  }
  const uint32_t size = _len;
  if (size == 0)
    return *this;
  if constexpr (has_destroyer)
  {
    for (uint32_t i = 0; i < size; i++)
    {
      _data[i].destroy();
    }
  }
  _len = 0;
  return self;
}
GEN inline View<T> list<T>::slice(int start, int len) const
{
  return View<T>::range(_data+start, len);
}
GEN inline uint32_t list<T>::cap() const { return _cap; }
GEN inline T* list<T>::data() const { return _data; }
GEN inline bool list<T>::is_empty() const
{
  if (_data == nullptr)
  {
    return true;
  }
  return _len == 0;
}

GEN inline Self& list<T>::init()
{
  _data = nullptr;
  _cap = 0;
  return *this;
}

GEN inline Self& list<T>::prealloc(uint32_t size)
{
  if (size <= _cap)
    return self;
  _cap = size;
  _data = (T*)::realloc(
    _data,
    sizeof(T) * _cap
  );
  return self;
}

// content of element is owned
GEN inline Self& list<T>::push_ref(const T& element)
{
  uint32_t l = len();
  l++;
  if (l > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = (void*)_data; // Pointing to content, not length
      _data = (T*)malloc(sizeof(T) * l);
      memcpy((void*)_data, old, l - 1); // Copying old content, not length
      _cap = l;
    }
    else
    {
      while (_cap < l) _cap += _cap/2+(_cap<3);
      _data = (T*)::realloc(_data, sizeof(T) * _cap);
    }
  }
  memcpy(_data + (l-1), &element, sizeof(T));
  _len = l;
  return self;
}

GEN Self& list<T>::extend(const View<T> ref_ext)
{
  uint32_t initl = len();
  uint32_t l = initl;
  l += ref_ext.len;
  if (l > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = _data; // Pointing to content, not length
      _data = (T*)malloc(sizeof(T) * l);
      memcpy(
          _data,
          old,
          l - ref_ext.len
      ); // Copying old content, not length
      _cap = l;
    }
    else
    {
      _cap = l;
      _data = (T*)::realloc(_data, sizeof(T) * _cap);
    }
  }
  // 2. Push others
  for (uint32_t i = initl; i < l; i++)
  {
    _data[i] = ref_ext[i - initl];
  }
  if (l > 0)
    _len = l;
  return self;
}

GEN Self& list<T>::prextend(View<T> ref_pre)
{
  uint32_t initl = len();
  uint32_t l = initl;
  l += ref_pre.len;
  if (l == 0)
  {
    return *this;
  }
  if (initl == 0)
  {
    return extend(ref_pre);
  }
  if (l > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = (void*)_data; // Pointing to content, not length
      _data = (T*)malloc(sizeof(T) * l);
      memcpy(
          _data,
          old,
          l - ref_pre.len
      ); // Copying old content, not length
      _cap = l;
    }
    else
    {
      _cap = l;
      _data = (T*)::realloc(_data, sizeof(T) * _cap);
    }
  }
  // 1. Move everything
  for (int i = 0; i < initl; i++)
  {
    _data[l - i - 1] = _data[initl - i - 1];
  }
  // 2. Push others
  for (uint32_t i = 0; i < ref_pre.len; i++)
  {
    _data[i] = ref_pre.start[i];
  }
  _len = l;
  return self;
}

GEN inline Self& list<T>::insert(T element, int index)
{
  if (index < 0)
  {
    index = len() + index;
  }
  if (len() < 1 || uint32_t(index) == len())
  {
    self.push(element);
    return self;
  }
  const T temp = self[-1]; // references to self in a push
                           // can cause illegal reads
  self.push(temp);
  for (int i = len() - 2; i > index; i--)
  {
    self._data[i] = self._data[i - 1];
  }
  self._data[index] = element;
  return self;
}

GEN inline Self& list<T>::rmv(int index)
{
  uint32_t l = len();
  if constexpr (has_destroyer)
  {
    _data[index].destroy();
  }
  l--;
  for (uint32_t i = index; i < l; i++)
  {
    _data[i] = _data[i + 1];
  }
  _len = l;
  return self;
}

GEN Self& list<T>::rmv(int start, int end)
{
  if (start == end) return *this;
  uint32_t l = len();
  assert(start < end);
  if constexpr (has_destroyer)
  {
    for (int i = start; i < end; i++)
    {
      _data[i].destroy();
    }
  }
  const uint32_t dist = end - start;
  l -= dist;
  for (uint32_t i = start; i < l; i++)
  {
    _data[i] = _data[i + dist];
  }
  _len = l;
  return self;
}


GEN inline View<T> list<T>::to_view() const
{
  return View<T>::range(_data, len());
}

// Object is not destroyed !!!
GEN inline T list<T>::pop()
{
  const uint32_t l = len();
  assertm(l > 0, "You tried to pop an empty list!\n");
  _len = l - 1;
  return _data[l - 1];
}

GEN inline Self& list<T>::swap(int a, int b)
{
  T temp = self[a];
  self[a] = self[b];
  self[b] = temp;
  return self;
}

GEN inline Self& list<T>::realloc()
{
  assertm(
      _cap == 0,
      "[MEMCHECK] You tried to realloc a list, but it was already allocated "
      "and you forgot to destroy it !\n"
  );
  return self.force_realloc();
}

GEN inline Self& list<T>::force_realloc()
{
  if (_data == nullptr)
    return self;
  uint32_t l = _len;
  if (l == 0)
  {
    _data = nullptr;
    _cap = 0;
    return self;
  }
  T* old = _data;
  _data = calloc(l, sizeof(T));
  _cap = l;
  memcpy(_data, old, l);
  // NOTE: this function doesn't free memory
  return self;
}

GEN inline Self& list<T>::ref(const View<T>& other)
{
  assert(other.is_full()
  ); // Or else accessing length will cause undefined behavior
  assertm(
      _cap == 0,
      "[MEMCHECK] You tried to reference a list, but you forgot to destroy it "
      "! (1)\n"
  );
  _data = other.start;
  _cap = 0;
  return self;
}

GEN inline Self& list<T>::ref(const list<T>& other)
{
  assertm(
      _cap == 0,
      "[MEMCHECK] You tried to reference a list, but you forgot to destroy it "
      "! (2)\n"
  );
  _data = other._data;
  _cap = 0;
  return self;
}

// Default C behavior, nothing weird
GEN inline Self& list<T>::operator=(const list<T>& other)
{
  _data = other._data;
  _cap = other._cap;
  return self;
}

GEN inline Self& list<T>::operator=(std::initializer_list<T> init_ls)
{
  assertm(
      _cap == 0,
      "[MEMCHECK] You tried to assign to a list, but you forgot to destroy it "
      "! (with an initializer list)\n"
  )
  _data = (T*)malloc(sizeof(T) * init_ls.size());
  _cap = _len = (uint32_t)(init_ls.size());
  return self;
}

GEN T& list<T>::operator[](const int index) const
{
  const uint32_t l = len();
  if (index < 0)
  {
    return _data[l + index];
  }
#ifdef DEBUG
  assertm(0 <= index && index < l,
          "list out of bound access with index %i (len=%i)\n",
          index,
          (int)l
  );
#endif
  return _data[index];
}

GEN inline View<T> list<T>::operator[](std::initializer_list<int> start_end)
{
  assert(
      start_end.size() == 2
  ); // because C++ is a piece of scheit (am very angy)
  int* p = (int*)start_end.begin();
  return View<T>{_data + p[0], p[1] - p[0], len()};
}

GEN inline Self& list<T>::own(list<T>& other)
{
  assertm(
      self._cap == 0,
      "You cannot overwrite ownership without destroying the first owner."
  );
  self._cap = other._cap;
  other._cap = 0;
  self._data = other._data;
}

GEN inline void list<T>::destroy()
{
  if constexpr (has_destroyer)
  {
    const uint32_t l = _len;
    for (uint32_t i = 0; i < l; i++)
    {
      _data[i].destroy();
    }
  }
  if (_data != nullptr)
    free(_data);
  _data = nullptr;
  _cap = 0;
}

GEN inline Self list<T>::copy() const
{
  const uint32_t l = len();
  list<T> ret;
  ret.init();
  ret.prealloc(l);
  memcpy(
      ret._data,
      _data,
      sizeof(T) * l
  );
  // Copying memory with length bytes.
  return ret;
}

GEN inline Self operator + (const Self a, const Self b)
{
  return a.copy().extend(b.to_view());
}
GEN inline Self operator + (const Self a, const T&& obj)
{
  return a.copy().push(obj);
}

#undef GEN
#undef Self
#undef self

#include <cstdio>
// NOTE - Using abreviation: Ld0 <=> Leading zero
// NOTE - For strings,
//  the len is stored the C-string way, not in a preceding block (to the content of the array)
//  The cap doesn't count the Ld0
#define Self list<char>
#define self (*this)

inline constexpr size_t str_len(const char* s)
{
  if (s == nullptr) return 0;
  size_t l = 0;
  while (*(s++) != 0) { l++; }
  return l;
}
inline bool str_eq(const char* a, const char* b)
{
  if (a == b)
    return true;
  else if (a == nullptr || b == nullptr)
    return false;
  while (*a != 0 && *b != 0)
    if (*(a++) != *(b++))
      return false;
  return *a == *b;
}

template <>
class list<char>
{
public:
  using T = char;
  T* _data;
  size_t _cap;

  constexpr list() : _data(nullptr), _cap(0) {}
  constexpr list(T* data, size_t cap) : _data(data), _cap(cap) {}
  constexpr list(const list<T>& other)
    : _data(other._data), _cap(other._cap) {}
  constexpr list(const list<T>&& other)
    : _data(other._data), _cap(other._cap) {}
  inline Self& init() { _data = nullptr; _cap = 0; return *this; }
  static constexpr inline Self ref(char* cstr)
  { return {cstr, 0}; }
  static constexpr inline Self own(char* cstr)
  { return {cstr, str_len(cstr)+1}; }
  inline Self& push(T element);
  Self& prextend(View<T> ref_pre);
  Self& extend(View<T> ref_ext);
  inline Self& hold(Self& other);
  inline Self& insert(T element, int index);
  inline T pop();
  inline Self& prealloc(size_t size);
  inline Self& setl(size_t newl) = delete;
  inline Self& realloc() = delete;
  // {
  //   assert(_cap == 0);
  //   const char* old = _data;
  //   const int l = len();
  //   _cap = l;
  //   _data = (char*)malloc(_cap+1);
  //   memcpy(_data, old, _cap+1);
  //   return *this;
  // }
  inline Self& force_realloc();
  inline void destroy();
  Self copy() const;
  inline Self& rmv(int index);
  inline View<T> to_view() const
  {
    return View<T>::range(_data, len());
  }
  Self& rmv_range(std::initializer_list<int> start_end);
  inline Self& clear();
  inline Self& assign(const View<T>& other);
  inline Self& ref(const list<T>& other)
  {
    _data = other._data;
    _cap = 0;
    return *this;
  }
  inline Self& operator=(const list<T>& other)
  {
    _data = other._data;
    _cap = other._cap;
    return *this;    
  }
  inline Self& operator=(std::initializer_list<T> init_ls);
  inline T& operator[](int index) const
  {
    if (index < 0) index = len() + index; // quite expensive but can be useful
#ifdef DEBUG
    assert(index >= 0);
    assert(index < len());
#endif
    return _data[index];
  }
  inline View<T> operator[](std::initializer_list<int> start_end);

  // Modifies the state of the argument as disowned
  //  => other.cap := 0 (other.is_owned() := true)
  inline Self& own(list<T>& other);

  inline constexpr T* data() const
  {
    return _data;
  }
  inline size_t len() const;

  // len == 0
  inline bool is_empty() const
  {
    return len() == 0;
  }
  // cap != 0
  inline bool is_owned() const
  {
    return _cap != 0 && _data != nullptr;
  }
   
  inline Self& operator += (const Self&& other)
  {
    extend(other.to_view());
    return *this;
  }
  inline Self& operator += (const T&& obj)
  {
    push(obj);
    return *this;
  }
};


inline size_t list<char>::len() const {
	return str_len(_data);
}

inline Self& list<char>::clear() {
	self._data[0] = 0;
	return self;
}
inline Self& list<char>::hold(Self& other)
{
  self = other;
  other._cap = 0;
  return self;
}

inline Self& list<char>::push(char element) {
	size_t l = len();
	l++;
	if (l > _cap) {
		if (_cap == 0 || _data == NULL) {
		  assert(l == 0);
			_data = (char*)malloc(8);
			_cap = 7;
		} else {
			_cap *= 2;
			// C++ really can be ugly sometimes ngl
			// btw i critize but my language has the same kind of shitty features
			_data = (char*)(::realloc(
				_data,
				sizeof(char)*(_cap+1)
			));
		}
	}
	_data[l-1] = element;
	_data[l] = 0; // could be useful, idk O_O
	return self;
}

// Return value is not destroyed !!!
inline char list<char>::pop() {
	const size_t l = len();
	assertm(l > 0, "You tried to pop an empty list !");
	const char out = _data[l-1];
	_data[l-1] = 0;
	return out;
}

inline Self& list<char>::prealloc(size_t size) {
  if (size <= _cap)
    return self;
  if (is_owned())
  {
    _cap = size;
    _data = (char*)::realloc(
      _data,
      sizeof(T) * (_cap+1)
    );
  }
  else
  {
    size = std::max(8UL, size);
    _cap = size-1;
    _data = (char*)malloc(size);
    _data[0] = 0;
  }
  return self;
}

inline Self& list<char>::force_realloc() {
	if (_data == nullptr)
		return self;
	const size_t l = len();
	if (l == 0) {
		_data = nullptr;
		_cap = 0;
		return self;
	}
	char* old = _data;
	_data = (char*)malloc((l+1)*sizeof(char));
	memset(_data, 0, (l+1)*sizeof(char));
	_cap = l;
	memcpy(_data, old, l);
	// NOTE: this function doesn't free memory
	return self;
}

inline void list<char>::destroy()
{
  if (is_owned())
  {
    if (_data != nullptr)
    {
      free(_data);
    }
    _data = nullptr;
    _cap = 0;
  }
}

inline Self& list<char>::rmv(int index) {
	const size_t l = len();
	memmove(_data + index, _data + index + 1, sizeof(char)*l);
	return self;
}

using string = list<char>;
inline bool operator == (string a, string b)
{
  return str_eq(a.data(), b.data());
}
inline bool operator != (string a, string b)
{
  return !(a == b);
}

#undef Self
#undef self

#endif /* H_LIST */


