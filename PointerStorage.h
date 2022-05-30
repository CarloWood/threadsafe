#pragma once

#include "AIReadWriteSpinLock.h"
#include "utils/macros.h"
#include <boost/lockfree/stack.hpp>
#include <cstdint>
#include <mutex>
#include <vector>

namespace aithreadsafe {

// Fast storage for pointers.
//
// This container is intended to keep track of existing objects,
// where the constructors / destructors call insert / erase.
//
// This allows one to call a member function on all existing
// objects (for example, at program termination).
//
// Insertion and erase take constant time, except when the
// memory allocated for the stored pointers is too small,
// which causes a reallocation.
//
// Such reallocation might move the storage in memory, therefore
// indices are used to refer to the place in the storage where
// a pointer is stored, for fast erasure. That in turn requires
// that pointers are never moved relative to the storage however,
// so that an additional accounting is necessary to keep track
// of free entries in the storage in order to achieve constant
// insertion.
//
// The initial state is as follows:
//
//             pos:
//                 .------------.                  .-----.
// m_storage --> 0 | free       |  m_free_indices: |  0  |
//               1 | free       |                  |  1  |
//               2 | free       |                  |  2  |
//               3 | free       |                  |  3  |
//               4 | free       |                  |  4  |
//               5 | free       |                  |  5  |
//               6 | free       |                  |  6  |
//               7 | free       |                  |  7  |
//                 `------------'                  `-----'
//
// Then suppose the following calls happen:
//
//  pos0 = insert(ptr0)
//  pos1 = insert(ptr1)
//  pos2 = insert(ptr2)
//  pos3 = insert(ptr3)
//  pos4 = insert(ptr4)
//  pos5 = insert(ptr5)
//  pos6 = insert(ptr6)
//  pos7 = insert(ptr7)
//
// At that point the situation is:
//
//                 m_size == 8
//                 .------------.                  .-----.
// m_storage --> 0 | ptr0       |  m_free_indices: |  0  |
//               1 | ptr1       |                  |  1  |
//               2 | ptr2       |                  |  2  |
//               3 | ptr3       |                  |  3  |
//               4 | ptr4       |                  |  4  |
//               5 | ptr5       |                  |  5  |
//               6 | ptr6       |                  |  6  |
//               7 | ptr7       |                  |  7  |
//                 `------------'                  `-----' <-- m_last_freed_index == 8
//
// Next suppose the following calls happen:
//
//  erase(pos0)
//  erase(pos7)
//  erase(pos5)
//  erase(pos4)
//  erase(pos6)
//  erase(pos1)
//
// At that point the situation is:
//
//                 m_size == 8
//                 .------------.                  .-----.
// m_storage --> 0 |            |  m_free_indices: |  0  |
//               1 |            |                  |  1  |
//               2 | ptr2       |                  |  1  | <-- m_last_freed_index == 8
//               3 | ptr3       |                  |  6  |
//               4 |            |                  |  4  |
//               5 |            |                  |  5  |
//               6 |            |                  |  7  |
//               7 |            |                  |  0  |
//                 `------------'                  `-----'
//
// In other words, any element of m_storage can end up used or free; and
// all elements in m_free_indices at m_last_freed_index and higher are relevant:
// free indices of m_storage in reverse order that they were erased.
//
// This means that an erase followed by an insert, writes and then reads
// the same memory location in m_free_indices, which is cache friendly.
// m_storage is only written to.
//
class VoidPointerStorage
{
 public:
  using index_type = uint_fast32_t;
  static constexpr float memory_grow_factor = 1.414f;

 protected:
  mutable AIReadWriteSpinLock m_rwlock;
  index_type m_size;
  std::vector<void*> m_storage;
  mutable boost::lockfree::stack<index_type> m_free_indices;

 public:
  VoidPointerStorage(uint32_t initial_size) : m_size(0), m_free_indices(initial_size)
  {
    increase_size(initial_size);
  }

  void increase_size(uint32_t initial_size = 0);

  index_type insert(void* value)
  {
    m_rwlock.rdlock();
    index_type index;
    while (AI_UNLIKELY(!m_free_indices.pop(index)))
    {
      m_rwlock.rdunlock();
      increase_size();
      m_rwlock.rdlock();
    }
    m_storage[index] = value;
    m_rwlock.rdunlock();
    return index;
  }

  void erase(index_type pos)
  {
    m_rwlock.rdlock();
    m_free_indices.bounded_push(pos);
    m_rwlock.rdunlock();
  }

  void* get(index_type pos) const
  {
    return m_storage[pos];
  }

#ifdef CWDEBUG
  // Extremely expensive function.
  bool debug_empty() const;
#endif
};

template<typename T>
struct PointerStorage : public VoidPointerStorage
{
  using VoidPointerStorage::VoidPointerStorage;

  [[gnu::always_inline]] index_type insert(T* value) { return VoidPointerStorage::insert(value); }
  [[gnu::always_inline]] T* get(index_type pos) { return static_cast<T*>(VoidPointerStorage::get(pos)); }

  void copy(std::vector<T*>& output);
};

template<typename T>
void PointerStorage<T>::copy(std::vector<T*>& output)
{
  m_rwlock.wrlock();
  m_free_indices.consume_all([this](index_type index){
    m_storage[index] = nullptr;
  });
  for (void* ptr : m_storage)
    if (ptr)
      output.push_back(static_cast<T*>(ptr));
  m_rwlock.wrunlock();
}

} // namespace aithreadsafe
