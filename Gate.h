/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Declaration of class Gate.
 *
 * @Copyright (C) 2017  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
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

#include "AIMutex.h"
#include <condition_variable>

namespace aithreadsafe
{

// Block (multiple) thread(s) until open() is called.
//
// If open() was already called before wait() then
// wait() also doesn't block anymore.
//
class Gate : public AIMutex
{
 private:
  std::condition_variable_any m_condition_variable;
  bool m_open;

 public:
  Gate() : m_open(false) { }

  void wait()
  {
    std::lock_guard<AIMutex> lock(*this);
    m_condition_variable.wait(*this, [this](){ return m_open; });
  }

  void open()
  {
    {
      std::lock_guard<AIMutex> lock(*this);
      m_open = true;
    }
    m_condition_variable.notify_all();
  }
};

} // namespace aithreadsafe
