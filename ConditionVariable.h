// SPDX-FileCopyrightText: 2015-2020, 2022-2023 Carlo Wood
// SPDX-License-Identifier: MIT

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
// using foo_type = threadsafe::Unlocked<Foo, threadsafe::policy::Primitive<threadsafe::ConditionVariable>>;
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
