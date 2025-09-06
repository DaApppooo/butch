#pragma once
#include <cstdint>
#include <cstdlib>
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
#define poff(PTR, OFF) ((decltype(PTR))((char*)(PTR) + OFF))
#define DEBUG
#ifdef DEBUG
#include <cstdio>
#define assertm(COND, ...) if (!(COND)) { fprintf(stderr, __VA_ARGS__); assert(COND); }
#else
#define assertm(COND, ...)
#endif


template <class T>
struct View
{
  using mut_T = std::remove_const<T>;
  T* start; uint32_t len;
  constexpr View() : start(nullptr), len(0) {}
  constexpr View(T* start, uint32_t len) : start(start), len(len) {}
  constexpr View(T* start, T* end) : start(start), len(end-start) {}
  constexpr View(const View<T>& v) : start(v.start), len(v.len) {}
  constexpr View(const View<T>&& v) : start(v.start), len(v.len) {}
  T& operator [] (int index) const
  { if (index < 0) index += len; return start[index]; }
  View<T>& operator = (const View<T>& v)
  { start = v.start; len = v.len; return *this; }
  View<mut_T> mut() const
  { return {const_cast<mut_T>(start), len}; }
  View<T> shorten(int start_off, int end_off)
  { return View<T>(start+start_off, len-start_off-end_off); }
};
template <>
struct View<char>
{
  char* start; uint32_t len;
  constexpr View() : start(nullptr), len(0) {}
  constexpr View(char* start, uint32_t len) : start(start), len(len) {}
  constexpr View(char* start, char* end) : start(start), len(end-start) {}
  constexpr View(const View<char>& v) : start(v.start), len(v.len) {}
  constexpr View(const View<char>&& v) : start(v.start), len(v.len) {}
  char& operator [] (int index) const
  { if (index < 0) index += len; return start[index]; }
  View<char>& operator = (const View<char>& v)
  { start = v.start; len = v.len; return *this; }
  char fix() const
  { char tmp = start[len]; start[len] = 0; return tmp; }
  void mov(char c) const
  { start[len] = c; }
  View<char> shorten(int start_off, int end_off)
  { return View<char>(start+start_off, len-start_off-end_off); }
};

template <class T> class list
{
public:
  using Self = list<T>;
  template <class U>
  using _temp_has_destroyer = decltype(std::declval<U&>().destroy());
  static constexpr bool has_destroyer
      = std::experimental::is_detected_exact_v<void, _temp_has_destroyer, T>;
  T* _data;
  size_t _cap;

  T* begin() const { return _data; }
  T* end() const { return _data+len(); }
  const T* cbegin() const { return _data; }
  const T* cend() const { return _data+len(); }

  list();
  list(const T*);
  list(const list<T>& other) { memcpy(this, &other, sizeof(list<T>)); }
  list(const list<T>&& other) { memcpy(this, &other, sizeof(list<T>)); }
  inline Self& init();
  inline Self& push(T element);
  Self& prextend(View<T> ref_pre);
  Self& extend(View<T> ref_ext);
  inline Self& insert(T element, int index);
  // Return value is not destroyed !!!
  inline T pop();
  inline Self& prealloc(size_t size);
  inline Self& set_len(size_t new_len);
  inline Self& realloc();
  inline Self& force_realloc();
  inline void destroy();
  inline Self copy() const;
  inline Self& rmv(int index);
  inline View<T> to_view() const;
  inline View<T> slice(int start, int len);
  Self& rmv_range(std::initializer_list<int> start_end);
  inline Self& clear();
  inline Self& swap(int idx_a, int idx_b);
  inline Self& ref(const View<T>& other);
  inline Self& ref(const list<T>& other);
  inline Self& operator=(const list<T>& other);
  inline Self& operator=(std::initializer_list<T> init_ls);
  T& operator[](const int index) const;
  inline View<T> operator[](std::initializer_list<int> start_end);

  // Modifies the state of the argument as disowned
  //  => other.cap := 0 (other.is_owned() := true)
  inline Self& own(list<T>& other);

  inline T* data() const;
  inline size_t len() const;
  inline size_t cap() const;

  // len == 0
  inline bool is_empty() const;
  // cap != 0
  inline bool is_owned() const;
   
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
  //     size_t cap;
  //   };
  //   const size_t _len = len();
  //   fake_list_char ret;
  //   ret._p = (char*)malloc(_len);
  //   memcpy(ret._p, _data, sizeof(char)*(_len+1));
  //   // Copying memory with length bytes.
  //   return *(list<char>*)&ret;
  // }
};

#define GEN template <class T>
#define Self list<T>
#define self (*this)
#define FETCH_LEN() *(((size_t*)_data) - 1)

GEN list<T>::list()
{
  _data = NULL;
  _cap = 0;
}

GEN inline size_t list<T>::len() const
{
  if (_data == nullptr)
    return 0;
  return FETCH_LEN();
}
GEN inline Self& list<T>::set_len(size_t new_len) 
{
  if (new_len > 0)
  {
    assert(_data != nullptr);
    assert(_cap >= new_len);
    FETCH_LEN() = new_len;
  }
  return *this;
}
GEN inline Self& list<T>::clear()
{
  if (_data == nullptr || !is_owned())
  {
    init();
    return *this;
  }
  const size_t size = FETCH_LEN();
  if (size == 0)
    return *this;
  if constexpr (has_destroyer)
  {
    for (size_t i = 0; i < size; i++)
    {
      _data[i].destroy();
    }
  }
  FETCH_LEN() = 0;
  return self;
}
GEN inline View<T> list<T>::slice(int start, int len)
{
  return View<T>(_data+start, len);
}
GEN inline size_t list<T>::cap() const { return _cap; }
GEN inline T* list<T>::data() const { return _data; }
GEN inline bool list<T>::is_empty() const
{
  if (_data == nullptr)
  {
    return true;
  }
  return FETCH_LEN() == 0;
}
GEN inline bool list<T>::is_owned() const
{
  if (_data == nullptr)
    assert(_cap == 0);
  return _data != nullptr || _cap != 0;
}

GEN inline Self& list<T>::init()
{
  _data = nullptr;
  _cap = 0;
  return *this;
}

GEN inline Self& list<T>::prealloc(size_t size)
{
  const size_t _len = len();
  if (size <= _cap)
    return self;
  if (is_owned())
  {
    _cap = size;
    _data = (T*)poff(::realloc(
      poff(_data, -sizeof(size_t)),
      sizeof(T) * _cap + sizeof(size_t)
    ), sizeof(size_t));
  }
  else
  {
    const void* old = _data;
    _cap = size;
    _data = (T*)poff(malloc(sizeof(T) * _cap + sizeof(size_t)), sizeof(size_t));
    if (old)
      memcpy(
        poff(_data, -sizeof(size_t)),
        poff(old, -sizeof(size_t)),
        sizeof(T)*_len + sizeof(size_t)
      );
    else
      FETCH_LEN() = 0;
  }
  return self;
}

// content of element is owned
GEN inline Self& list<T>::push(T element)
{
  size_t _len = len();
  _len++;
  if (_len > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = (void*)_data; // Pointing to content, not length
      _data = (T*)(((size_t*)malloc(sizeof(T) * _len + sizeof(size_t))) + 1);
      memcpy((void*)_data, old, _len - 1); // Copying old content, not length
      _cap = _len;
    }
    else
    {
      _cap *= 2;
      _data = (T*)poff(
          ::realloc(
              poff(_data, -sizeof(size_t)),
              sizeof(T) * _cap + sizeof(size_t)
          ),
          sizeof(size_t)
      );
    }
  }
  _data[_len - 1] = element;
  FETCH_LEN() = _len;
  return self;
}

GEN Self& list<T>::extend(const View<T> ref_ext)
{
  size_t init_len = len();
  size_t _len = init_len;
  _len += ref_ext.len;
  if (_len > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = (void*)_data; // Pointing to content, not length
      _data = (T*)(((size_t*)malloc(sizeof(T) * _len + sizeof(size_t))) + 1);
      memcpy(
          (void*)_data,
          old,
          _len - ref_ext.len
      ); // Copying old content, not length
      _cap = _len;
    }
    else
    {
      _cap = _len;
      _data = (T*)poff(
          ::realloc(
              poff(_data, -sizeof(size_t)),
              sizeof(T) * _cap + sizeof(size_t)
          ),
          sizeof(size_t)
      );
    }
  }
  // 2. Push others
  for (size_t i = init_len; i < _len; i++)
  {
    _data[i] = ref_ext[i - init_len];
  }
  if (_len > 0)
    FETCH_LEN() = _len;
  return self;
}

GEN Self& list<T>::prextend(View<T> ref_pre)
{
  size_t init_len = len();
  size_t _len = init_len;
  _len += ref_pre.len;
  if (_len == 0)
  {
    return *this;
  }
  if (init_len == 0)
  {
    return extend(ref_pre);
  }
  if (_len > _cap)
  {
    if (_cap == 0 || _data == NULL)
    {
      const void* const old = (void*)_data; // Pointing to content, not length
      _data = (T*)(((size_t*)malloc(sizeof(T) * _len + sizeof(size_t))) + 1);
      memcpy(
          (void*)_data,
          old,
          _len - ref_pre.len
      ); // Copying old content, not length
      _cap = _len;
    }
    else
    {
      _cap = _len;
      _data = (T*)poff(
          ::realloc(
              poff(_data, -sizeof(size_t)),
              sizeof(T) * _cap + sizeof(size_t)
          ),
          sizeof(size_t)
      );
    }
  }
  // 1. Move everything
  for (int i = 0; i < init_len; i++)
  {
    _data[_len - i - 1] = _data[init_len - i - 1];
  }
  // 2. Push others
  for (size_t i = 0; i < ref_pre.len; i++)
  {
    _data[i] = ref_pre.start[i];
  }
  FETCH_LEN() = _len;
  return self;
}

GEN inline Self& list<T>::insert(T element, int index)
{
  if (index < 0)
  {
    index = len() + index;
  }
  if (len() < 1)
  {
    self.push(element);
    return self;
  }
  self.push(self[-1]);
  for (int i = len() - 2; i > index; i--)
  {
    self._data[i] = self._data[i - 1];
  }
  self._data[index] = element;
  return self;
}

GEN inline Self& list<T>::rmv(int index)
{
  size_t _len = len();
  if constexpr (has_destroyer)
  {
    _data[index].destroy();
  }
  _len--;
  for (size_t i = index; i < _len; i++)
  {
    _data[i] = _data[i + 1];
  }
  FETCH_LEN() = _len;
  return self;
}

GEN Self& list<T>::rmv_range(std::initializer_list<int> start_end)
{
  size_t _len = len();
  int start = *start_end.begin();
  int end = *(start_end.begin() + 1);
  assert(start < end);
  if constexpr (has_destroyer)
  {
    for (int i = start; i < end; i++)
    {
      _data[i].destroy();
    }
  }
  const size_t dist = end - start;
  _len -= dist;
  for (size_t i = start; i < _len; i++)
  {
    _data[i] = _data[i + dist];
  }
  FETCH_LEN() = _len;
  return self;
}


GEN inline View<T> list<T>::to_view() const
{
  return View<T>(_data, len());
}

// Object is not destroyed !!!
GEN inline T list<T>::pop()
{
  const size_t _len = len();
  assertm(_len > 0, "You tried to pop an empty list!\n");
  FETCH_LEN() = _len - 1;
  return _data[_len - 1];
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
  size_t _len = FETCH_LEN();
  if (_len == 0)
  {
    _data = nullptr;
    _cap = 0;
    return self;
  }
  T* old = _data;
  _data = calloc(_len, sizeof(T));
  _cap = _len;
  memcpy(_data, old, _len);
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
  ) _data
      = (T*)poff(
          malloc(sizeof(T) * init_ls.size() + sizeof(size_t)),
          sizeof(size_t)
      );
  FETCH_LEN() = (size_t)(init_ls.size());
  _cap = (size_t)(init_ls.size());
  return self;
}

GEN T& list<T>::operator[](const int index) const
{
  const size_t _len = len();
  if (index < 0)
  {
    return _data[_len + index];
  }
#ifdef DEBUG
  assertm(0 <= index && index < _len,
          "list out of bound access with index %i (len=%i)\n",
          index,
          (int)_len
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
  if (is_owned())
  {
    if constexpr (has_destroyer)
    {
      const size_t _len = FETCH_LEN();
      for (size_t i = 0; i < _len; i++)
      {
        _data[i].destroy();
      }
    }
    if (_data != nullptr)
    {
      free(((size_t*)_data) - 1);
    }
    _data = nullptr;
    _cap = 0;
  }
}

GEN inline Self list<T>::copy() const
{
  const size_t _len = len();
  list<T> ret;
  ret.init();
  ret.prealloc(_len);
  memcpy(
      ((size_t*)ret._data) - 1,
      ((size_t*)_data) - 1,
      sizeof(size_t) + sizeof(T) * _len
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
#undef FETCH_LEN
#undef Self
#undef self

#include <cstdio>
// NOTE - Using abreviation: Ld0 <=> Leading zero
// NOTE - For strings,
//  the len is stored the C-string way, not in a preceding block (to the content of the array)
//  The cap doesn't count the Ld0
#define Self list<char>
#define self (*this)

inline constexpr size_t str_len(const char* const s)
{
  if (s == nullptr) return 0;
  if (*s == 0) return 0;
  return strlen(s);
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
  // Return value is not destroyed !!!
  inline T pop();
  inline Self& prealloc(size_t size);
  inline Self& set_len(size_t new_len) = delete;
  inline Self& realloc()
  {
    assert(_cap == 0);
    const char* old = _data;
    const int _len = len();
    _cap = _len;
    _data = (char*)malloc(_cap+1);
    memcpy(_data, old, _cap+1);
    return *this;
  }
  inline Self& force_realloc();
  inline void destroy();
  Self copy() const;
  inline Self& rmv(int index);
  inline View<T> to_view() const
  {
    return View<T>(_data, len());
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


inline Self& list<char>::clear() {
	self._data[0] = 0;
	return self;
}

inline size_t list<char>::len() const {
	return str_len(self._data);
}

inline Self& list<char>::hold(Self& other)
{
  self = other;
  other._cap = 0;
  return self;
}

inline Self& list<char>::push(char element) {
	size_t _len = len();
	_len++;
	if (_len > _cap) {
		if (_cap == 0 || _data == NULL) {
			const void* const old = (void*)_data; // Pointing to content, not length
			_data = (char*)malloc(sizeof(char)*(_len+1));
			memcpy((void*)_data, old, _len); // Copying old content (+Ld0), not length
			_cap = _len;
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
	_data[_len-1] = element;
	_data[_len] = 0; // could be useful, idk O_O
	return self;
}
// Return value is not destroyed !!!

inline char list<char>::pop() {
	const size_t _len = len();
	assertm(_len > 0, "You tried to pop an empty list !");
	const char out = _data[_len-1];
	_data[_len-1] = 0;
	return out;
}

inline Self& list<char>::prealloc(size_t size) {
  if (is_owned())
  {
  	const size_t _len = len();
  	assertm(_cap == 0 && _len == 0, "List is already set: cannot prealloc.");
	}
	_cap = size;
	_data = (char*)malloc(sizeof(char)*(_cap+1));
	memset(_data, 0, sizeof(char)*(_cap+1));
	return self;
}

inline Self& list<char>::force_realloc() {
	if (_data == nullptr)
		return self;
	const size_t _len = len();
	if (_len == 0) {
		_data = nullptr;
		_cap = 0;
		return self;
	}
	char* old = _data;
	_data = (char*)malloc((_len+1)*sizeof(char));
	memset(_data, 0, (_len+1)*sizeof(char));
	_cap = _len;
	memcpy(_data, old, _len);
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
	const size_t _len = len();
	memmove(_data + index, _data + index + 1, sizeof(char)*_len);
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


