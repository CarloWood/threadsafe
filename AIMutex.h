// SPDX-FileCopyrightText: 2015-2017, 2019, 2022 Carlo Wood
// SPDX-License-Identifier: MIT

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
