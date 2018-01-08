/**
 * @file
 * @brief Implementation of AIReadWriteSpinLock.
 *
 * Copyright (C) 2017  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/macros.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

class AIReadWriteSpinLock
{
  public:
    AIReadWriteSpinLock() { }

  private:
    std::atomic_int m_state;    // < 0: write locked, 0: unlocked, > 0: read locked.
    std::mutex m_cv_mutex;
    std::condition_variable m_cv;

  public:
    static constexpr int max_concurrent_accesses = 0x8000;   // Something larger than the number of cores.

    void rdlock()
    {
      bool read_locked;
      do
      {
	if (std::atomic_fetch_add_explicit(&m_state, 1, std::memory_order_relaxed) >= 0)    // Very likely to be true.
	  break;

	// Apparently one or more threads are trying to obtain a write lock or one already has it.
	// Undo the increment of m_state.
	std::atomic_fetch_sub_explicit(&m_state, 1, std::memory_order_relaxed);

	// Next we're going to wait until m_state becomes positive again.
	read_locked = false;
	std::unique_lock<std::mutex> lk(m_cv_mutex);
	m_cv.wait(lk, [&] {
	  int expected = 0;
	  read_locked = std::atomic_compare_exchange_weak_explicit(&m_state, &expected, 1, std::memory_order_relaxed, std::memory_order_relaxed);
	  return read_locked || (m_state.load(std::memory_order_relaxed) >= 0);
	});
      }
      while (!read_locked); // Try again if didn't obtain a read lock yet.
    }

    void rdunlock()
    {
      std::atomic_fetch_sub_explicit(&m_state, 1, std::memory_order_relaxed);
    }

    void wrlock()
    {
      for (;;)
      {
        int state = std::atomic_fetch_sub_explicit(&m_state, max_concurrent_accesses, std::memory_order_relaxed);
        if (state > 0)
        {
          // Read locked. Spin lock until all readers are done.
          while (m_state.load(std::memory_order_relaxed) > -max_concurrent_accesses);
          break;
        }
        else (state < 0)
        {
          // Write locked. Wait for other write-lock to be released.
          std::atomic_fetch_add_explicit(&m_state, max_concurrent_accesses, std::memory_order_relaxed);
          std::unique_lock<std::mutex> lk(m);
          m_cv.wait(lk, [this](){ return m_state.load(std::memory_order_relaxed) >= 0; });
        }
      }
    }

    void rd2wrlock()
    {
      // It is not supported to convert a read lock into a write lock.
      DoutFatal(dc::core, "Calling AIReadWriteSpinLock::rd2wrlock()");
    }

    void rd2wryield()
    {
      // It is not supported to convert a read lock into a write lock.
      DoutFatal(dc::core, "Calling AIReadWriteSpinLock::rd2wryield()");
    }

    void wrunlock()
    {
      std::atomic_fetch_add_explicit(&m_state, max_concurrent_accesses, std::memory_order_relaxed);
      m_cv.notify_all();
    }

    void wr2rdlock()
    {
      std::atomic_fetch_add_explicit(&m_state, max_concurrent_accesses + 1, std::memory_order_relaxed);
      m_cv.notify_all();
    }
};
