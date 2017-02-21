/**
 * @file
 * @brief Implementation of AIMutex.
 *
 * Copyright (C) 2017 Carlo Wood.
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

#include <mutex>
#include "debug.h"

class AIMutex {
  protected:
    std::mutex m_mutex;
    std::thread::id m_id;

  public:
    void lock()
    {
      // AIMutex is not recursive.
      ASSERT(m_id != std::this_thread::get_id());
      m_mutex.lock();
      m_id = std::this_thread::get_id();
    }
    bool try_lock()
    {
      // AIMutex is not recursive.
      ASSERT(m_id != std::this_thread::get_id());
      bool success = m_mutex.try_lock();
      if (success) m_id = std::this_thread::get_id();
      return success;
    }
    void unlock()
    {
      m_mutex.unlock();
      m_id = std::thread::id();
    }
    bool self_locked() const
    {
      return m_id == std::this_thread::get_id();
    }
};
