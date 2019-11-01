/**
 * @file
 * @brief Declaration of class ConditionVariable.
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

#include "AIMutex.h"
#include <condition_variable>

namespace aithreadsafe
{

// Like std::condition_variable but derived from AIMutex and using that as mutex.
//
class ConditionVariable : public AIMutex
{
 private:
  std::condition_variable_any m_condition_variable;

 public:
  template<typename Predicate>
  void wait(Predicate pred)
  {
    ASSERT(is_self_locked());
    m_condition_variable.wait(*this, pred);
  }

  void notify_one()
  {
    m_condition_variable.notify_one();
  }
};

} // namespace aithreadsafe
