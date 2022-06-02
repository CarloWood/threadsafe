/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Implementation of AIMutex.
 *
 * @Copyright (C) 2017  Carlo Wood.
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

#include "debug.h"
#include <mutex>
#include <atomic>
#include <thread>

class AIMutex
{
 protected:
  std::mutex m_mutex;
  std::atomic<std::thread::id> m_id;    // Must be atomic because of the access in is_self_locked().
                                        // It is not possible to protect m_id with m_mutex in that function.

  // This is actually not REALLY required, but it would be very silly if a mutex would be used here.
  static_assert(std::atomic<std::thread::id>::is_always_lock_free, "Get a real OS");

 public:
  AIMutex() : m_id(std::thread::id{}) { }

  void lock()
  {
    // AIMutex is not recursive.
    ASSERT(m_id.load(std::memory_order_relaxed) != std::this_thread::get_id());
    m_mutex.lock();
    m_id.store(std::this_thread::get_id(), std::memory_order_relaxed);
  }

  bool try_lock()
  {
    // AIMutex is not recursive.
    ASSERT(m_id.load(std::memory_order_relaxed) != std::this_thread::get_id());
    bool success = m_mutex.try_lock();
    if (success)
      m_id.store(std::this_thread::get_id(), std::memory_order_relaxed);
    return success;
  }

  void unlock()
  {
    m_id.store(std::thread::id(), std::memory_order_relaxed);
    m_mutex.unlock();
  }

  bool is_self_locked() const
  {
    return m_id.load(std::memory_order_relaxed) == std::this_thread::get_id();
  }
};
