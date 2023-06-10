#include "sys.h"
#include "PointerStorage.h"
#include  <algorithm>

namespace threadsafe {

void VoidPointerStorage::increase_size(uint32_t initial_size)
{
  m_rwlock.rd2wrlock(); // This might throw if another thread is already trying to convert its read lock into a write lock.

  index_type size = m_size;
  m_size = std::max(static_cast<index_type>(initial_size), static_cast<index_type>(memory_grow_factor * size));
  if (AI_UNLIKELY(m_size == size))
    m_size += 1;

  m_storage.reserve(m_size);
  m_storage.resize(m_size);

  std::vector<index_type> tmp;
  m_free_indices.consume_all([&tmp](index_type index){
    tmp.push_back(index);
  });
  m_free_indices.reserve(m_size);

  ASSERT(m_free_indices.empty());
  index_type index = m_size;
//  Dout(dc::notice|continued_cf, "Pushing:");
  while (index > size)
  {
//    Dout(dc::continued, ' ' << (index - 1));
    m_free_indices.bounded_push(--index);
  }
  for (auto iter = tmp.rbegin(); iter != tmp.rend(); ++iter)
  {
//    Dout(dc::continued, ' ' << *iter);
    m_free_indices.bounded_push(*iter);
  }
//  Dout(dc::finish, '.');

  m_rwlock.wr2rdlock();
}

#ifdef CWDEBUG
bool VoidPointerStorage::debug_empty() const
{
  // The storage contains no pointers when all indices are free.
  // The only way we can check this is by emptying the stack :/.
  m_rwlock.wrlock();
  std::vector<index_type> tmp;
  m_free_indices.consume_all([&tmp](index_type index){
    tmp.push_back(index);
  });
  bool empty = tmp.size() == m_size;
  for (auto iter = tmp.rbegin(); iter != tmp.rend(); ++iter)
    m_free_indices.bounded_push(*iter);
  m_rwlock.wrunlock();
  return empty;
}
#endif

} // namespace threadsafe
