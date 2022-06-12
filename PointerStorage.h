/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Declaration of class PointerStorage.
 *
 * @Copyright (C) 2022  Carlo Wood.
 *
 * pub   dsa3072/C155A4EEE4E527A2 2018-08-16 Carlo Wood (CarloWood on Libera) <carlo@alinoe.com>
 * fingerprint: 8020 B266 6305 EE2F D53E  6827 C155 A4EE E4E5 27A2
 *
 * This file is part of threadsafe.
 *
 * Threadsafe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Threadsafe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with threadsafe.  If not, see <http://www.gnu.org/licenses/>.
 */

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

 private:
  void increase_size(uint32_t initial_size = 0);

 public:
  VoidPointerStorage(uint32_t initial_size) : m_size(0), m_free_indices(initial_size)
  {
    increase_size(initial_size);
  }

  index_type insert(void* value)
  {
    index_type index;
    for (;;)
    {
      m_rwlock.rdlock();
      try
      {
        while (AI_UNLIKELY(!m_free_indices.pop(index)))
          increase_size();      // Converts m_rwlock from read to write lock (which might throw) and back.
      }
      catch (std::exception const&)
      {
        m_rwlock.rdunlock();
        m_rwlock.rd2wryield();  // Wait until the other thread is done increasing the size.
        continue;
      }
      m_storage[index] = value;
      m_rwlock.rdunlock();
      break;
    }
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

// Thread-safe pointer storage.
//
// Use insert to add new pointers, and erase(pos) to remove them again - where pos is an index returned by insert.
// That index can also be used to read back the pointer value if needed, using get(pos).
//
template<typename T>
struct PointerStorage : public VoidPointerStorage
{
  // Pass the initial size (number of pointers) of the storage to the constructor.
  using VoidPointerStorage::VoidPointerStorage;

  index_type insert(T* value) { return VoidPointerStorage::insert(value); }
  T* get(index_type pos) { return static_cast<T*>(VoidPointerStorage::get(pos)); }

  // Copy all currently stored pointers to `output`.
  void for_each(std::function<void(T*)> callback);
};

template<typename T>
void PointerStorage<T>::for_each(std::function<void(T*)> callback)
{
  std::vector<index_type> free_indices;
  m_rwlock.wrlock();
  m_free_indices.consume_all([this, &free_indices](index_type index){
    m_storage[index] = nullptr;
    free_indices.push_back(index);
  });
  for (void* ptr : m_storage)
    if (ptr)
      callback(static_cast<T*>(ptr));
  for (index_type index : free_indices)
    m_free_indices.bounded_push(index);
  m_rwlock.wrunlock();
}

} // namespace aithreadsafe
