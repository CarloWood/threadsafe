#include "sys.h"
#include "PointerStorage.h"
#include  <algorithm>

namespace utils::threading {

void VoidPointerStorage::increase_size(uint32_t initial_size)
{
  index_type size = m_size;
  m_size = std::max(static_cast<index_type>(initial_size), static_cast<index_type>(memory_grow_factor * size));
  if (AI_UNLIKELY(m_size == size))
    m_size += 1;

  m_storage.reserve(m_size);
  m_storage.resize(m_size);
  m_free_indices.reserve(m_size);
  m_free_indices.resize(m_size);

  for (index_type i = size; i < m_size; ++i)
    m_free_indices[i] = i;
}

} // namespace utils::threading
