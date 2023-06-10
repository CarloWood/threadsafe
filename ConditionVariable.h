/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Declaration of class ConditionVariable.
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

#include "AIMutex.h"
#include <condition_variable>

namespace threadsafe
{

// Like std::condition_variable but derived from AIMutex and using that as mutex.
//
// Usage:
//
// Declaration:
//
// using foo_type = threadsafe::Wrapper<Foo, threadsafe::policy::Primitive<threadsafe::ConditionVariable>>;
// foo_type foo_cv;
//
// Waiting:
//
// foo_type::wat foo_w(foo_cv);
// foo_w.wait([&](){ return foo_w->done(); });
//
// Notifying:
//
// foo_type::wat foo_w(foo_cv);
// foo_w->set_done();
// foo_w.notify_one();
//
class ConditionVariable : public AIMutex
{
 private:
  std::condition_variable_any m_condition_variable;

 public:
  template<typename Predicate>
  void wait(Predicate pred)
  {
    // Usage:
    //
    // threadsafe::ConditionVariable cv;
    //
    //   cv.lock();
    //   cv.wait([](){ return done; });
    //   cv.unlock();
    //
    // For prefered usage, see above.
    ASSERT(is_self_locked());
    m_condition_variable.wait(*this, pred);
  }

  void notify_one()
  {
    m_condition_variable.notify_one();
  }
};

} // namespace threadsafe
