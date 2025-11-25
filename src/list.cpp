#include "list.hpp"
using namespace _butch_utils;

list<char> list<char>::copy() const {
	const size_t _len = len();
	list<char> ret;
	ret.init();
	ret.prealloc(_len);
	memcpy(ret._data, _data, sizeof(char)*(_len+1));
	// Copying memory with length bytes.
	return ret;
}

list<char>& list<char>::extend(View<char> ref_ext)
{
	size_t _len = len();
	_len+=ref_ext.len;
	if (_len > _cap) {
		if (_cap == 0 || _data == NULL) {
			const void* const old = (void*)_data; // Pointing to content, not length
			_data = (char*)malloc(sizeof(char)*(_len+1));
			if (old != NULL)
				memcpy((void*)_data, old, _len); // Copying old content (+Ld0), not length
			_cap = _len;
		} else {
			while (_len > _cap)
				_cap *= 2;
			_data = (char*)(::realloc(
				_data,
				sizeof(char)*(_cap+1)
			));
		}
	}
	memcpy(_data + (_len - ref_ext.len), ref_ext.start, sizeof(char)*ref_ext.len);
	_data[_len] = 0;
	return *this;
}

list<char>& list<char>::prextend(View<char> ref_ext)
{
	size_t _len = len();
	_len+=ref_ext.len;
	if (_len > _cap) {
		if (_cap == 0 || _data == NULL) {
			const void* const old = (void*)_data; // Pointing to content, not length
			_data = (char*)malloc(sizeof(char)*(_len+1));
			if (old != NULL)
				memcpy((void*)_data, old, _len); // Copying old content (+Ld0), not length
			_cap = _len;
		} else {
			while (_len > _cap)
				_cap *= 2;
			_data = (char*)(::realloc(
				_data,
				sizeof(char)*(_cap+1)
			));
		}
	}
	memmove(_data+ref_ext.len, _data, _len-ref_ext.len);
	memcpy(_data, ref_ext.start, sizeof(char)*ref_ext.len);
	_data[_len] = 0;
	return *this;
}

template <class T>
size_t list<T>::serialize(
  size_t(*cb)(const void*, size_t, void*),
	void* d
) const {
#define CB(pt, size) \
	{ tmp = cb(pt, size, d); if (size && !tmp) { return 0; } total += tmp; }
	size_t tmp, total = 0;
	CB(&_len, sizeof(_len));
	const uint32_t l = len();
	if constexpr(has_ser)
	{
		// NOTE: we cannot determine failure from this call
		for (int i = 0; i < l; i++)
			total += data()[i].serialize(cb, d);

	} else {
		CB(data(), sizeof(T)*len());
	}
	return total;
#undef CB
}

template <class T>
list<T> list<T>::deserialize(
  size_t(*cb)(void*, size_t, void*),
	void* d
) {
#define CB(pt, size) \
	{ tmp = cb(pt, size, d); \
		if (size && !tmp) \
			{ self._cap = 0; self._len = 1; return 0; } \
		total += tmp; \
	}
	size_t tmp, total = 0;
	list<T> self;
	CB(&self._len, sizeof(_len));
	self.prealloc(self._len);
	if (self.data() == nullptr)
	{
		self._cap = 0;
		self._len = 1;
		return self;
	}
	const uint32_t l = self.len();
	if constexpr(has_deser)
	{
		// NOTE: we cannot determine failure from this call
		for (int i = 0; i < l; i++)
		{
			self.atp(i) = T::deserialize(cb, d);
		}
		total += sizeof(T)*l;
	} else {
		CB(self.data(), sizeof(T)*self.len());
	}
	return total;
#undef CB
}


// list<char>& list<char>::rmv_range([[maybe_unused]] std::initializer_list<index_t> start_end) {
// 	todo();
// 	return *this;
// }


