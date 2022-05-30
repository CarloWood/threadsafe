/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Implementation of AIReadWriteSpinLock.
 *
 * @Copyright (C) 2017, 2018  Carlo Wood.
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

#ifdef CWDEBUG
// Set to 1 to enable very extentive debug output with regard to this class.
#define DEBUG_RWSPINLOCK 0
#endif

#include "utils/cpu_relax.h"
#include "utils/macros.h"
#include "debug.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

#ifdef CWDEBUG
#if DEBUG_RWSPINLOCK
#include <iomanip>
#include <array>
#define RWSLDout(a, b) Dout(a, b)
#define RWSLDoutEntering(a, b) DoutEntering(a, b)
#else
#define RWSLDout(a, b) do { } while(0)
#define RWSLDoutEntering(a, b) do { } while(0)
#endif
#else // CWDEBUG
#define DEBUG_RWSPINLOCK 0
#endif // CWDEBUG

class AIReadWriteSpinLock
{
 private:
  static constexpr int shift = 16;    // Such that `1 << shift` is much larger than the maximum number of threads.

  // The atomic, m_state is divided into four counters that shouldn't interfere because they each get 16 bits.
  //                           V               C               W               R
  //                   ╭───────┴──────╮╭───────┴──────╮╭───────┴──────╮╭───────┴──────╮
  // state (64 bits) = vvvvvvvvvvvvvvvvccccccccccccccccwwwwwwwwwwwwwwwwrrrrrrrrrrrrrrrr
  //                   ↑              ↑               ↑               ↑               ↑
  //                 bit 63         bit 48          bit 32          bit 16          bit 0
  //
  // The units of each counter.
  static constexpr int64_t r = 1;
  static constexpr int64_t w = r << shift;
  static constexpr int64_t c = w << shift;
  static constexpr int64_t v = c << shift;

  // The bits used for R.
  static constexpr int64_t R_mask = w - 1;
  static constexpr int64_t W_mask = (c - 1) & ~R_mask;
  static constexpr int64_t CW_mask = (v - 1) & ~R_mask;
  static constexpr int64_t V_mask = ~int64_t{1} & ~(CW_mask | R_mask);

  // Possible transitions.
  static constexpr int64_t one_rdlock           =      1;
  static constexpr int64_t one_wrlock           = -v + w;     // A negative value, significantly less than one_rdlock * max_number_of_threads.
  static constexpr int64_t one_rd2wrlock        = -v + c;     // A negative value, significantly less than w * max_number_of_threads.
  static constexpr int64_t one_waiting_writer   = -v;         // A negative value, significantly less than c * max_number_of_threads.

  // Follow up transitions after examining the previous state of the above operations.
  static constexpr int64_t failed_rdlock        = -one_rdlock;                                // Wait for !writer_present().
  static constexpr int64_t failed_wrlock        = -one_wrlock + one_waiting_writer;           // Spin till C == W == R == 0.
  static constexpr int64_t failed_rd2wrlock     = 0;                                          // Spin till -V == C == R == 1 and W == 0.
  static constexpr int64_t successful_rd2wrlock = -one_rd2wrlock - one_rdlock + one_wrlock;
  static constexpr int64_t finalize_wrlock      = -failed_wrlock;                             // Revert the failure == success.

  // Transitions that can't fail.
  static constexpr int64_t one_rdunlock         = -one_rdlock;
  static constexpr int64_t one_wrunlock         = -one_wrlock;
  static constexpr int64_t one_wr2rdlock        = one_wrunlock + one_rdlock;

#if DEBUG_RWSPINLOCK
  struct Counters
  {
    std::array<int16_t, 4> counters;

    friend std::ostream& operator<<(std::ostream& os, Counters const& counters)
    {
      os << '{' << -counters.counters[0] << ", " << counters.counters[1] << ", " << counters.counters[2] << ", " << counters.counters[3] << '}';
      return os;
    }
  };

  static Counters get_counters(int64_t state)
  {
    return {{ static_cast<int16_t>(state >> 48), static_cast<int16_t>(state >> 32), static_cast<int16_t>(state >> 16), static_cast<int16_t>(state) }};
  }

  static constexpr Counters get_counters_as_increment(int64_t increment)
  {
    int16_t R = static_cast<int16_t>(increment);
    if (R < 0)
      increment += w;
    int16_t W = static_cast<int16_t>(increment >> 16);
    if (W < 0)
      increment += c;
    int16_t C = static_cast<int16_t>(increment >> 32);
    if (C < 0)
      increment += v;
    int16_t V = static_cast<int16_t>(increment >> 48);
    return {{ V, C, W, R }};
  }
#endif

  std::atomic<int64_t> m_state;
  std::mutex m_readers_cv_mutex;
  std::condition_variable m_readers_cv;
  std::mutex m_writers_cv_mutex;
  std::condition_variable m_writers_cv;

  // This condition is used to detect if a reader is allowed to grab a read-lock.
  [[gnu::always_inline]] static constexpr bool writer_present(int64_t state)
  {
    // See the use of -v above.
    return state < 0;
  }

  // This condition is used to detect if a writer is allowed to grab a write-lock.
  [[gnu::always_inline]] static constexpr bool no_reader_or_writer_present(int64_t state)
  {
    // No read-lock or write-locks are allowed: R = 0 and W = 0. But also no waiting writers.
    return state == 0;
  }

  // This condition is used to detect if a spinning writer can proceed with waiting for other writers,
  // and no longer worry about readers.
  [[gnu::always_inline]] static constexpr bool reader_present(int64_t state)
  {
    return (state & R_mask) != 0;
  }

  // This condition is used to detect if a writer has to wait for other writers before it can grab the write-lock.
  // That includes the C count, because threads that try to convert their read-lock into a write-lock have
  // a higher priority (after all, we can't get a write-lock while they have their read-lock, so there is no other way).
  [[gnu::always_inline]] static constexpr bool no_real_writer_present(int64_t state)
  {
    return (state & CW_mask) == 0;
  }

  // This condition is used to detect if a writer is allowed to grab a write-lock after having already waited for other writers a bit.
  [[gnu::always_inline]] static constexpr bool reader_or_real_writer_present(int64_t state)
  {
    return (state & (R_mask | CW_mask)) != 0;
  }

  // This test is used in do_transition and tests if adding `increment` to m_state can cause a (waiting) writer to be removed.
  // Since real writers also have V decremented, it is sufficient to only look for a positve value of V here.
  // The division by 2 is necessary because a negative increment of C (or W if C == 0, etc) could have borrowed from V.
  // In that case V would be 0, but the high bit of C would be set.
  [[gnu::always_inline]] static constexpr bool removes_writer(int64_t increment)
  {
    return increment >= v / 2;
  }

  // Used in removes_real_writer.
  static constexpr int64_t sign_bit_c = c << (shift - 1);
  static constexpr int64_t sign_bit_w = w << (shift - 1);

  // This test is used in do_transition and tests if adding `increment` to m_state can cause a real writer to be removed.
  // "Real writers" here means literally that their removal might change the value of no_real_writer_present from false to true.
  [[gnu::always_inline]] static constexpr bool removes_real_writer(int64_t increment)
  {
    // This sign_bit must always be set, because either C is negative,
    // or C is "zero" but W is negative, borrowing 1 from C making it
    // appear to be negative.
    if ((increment & sign_bit_c) == 0)
      return false;

    // At this point sign_bit_c is set, which means that:
    // 1. C < 0, OR
    // 2. C == 0 and W < 0, OR
    // 3. C == 0 and W == 0 and R < 0.

    // Detect the case C < 0 (point 1), W == 0, R >= 0 (no borrow from W).
    // That C is negative is implied by the first test, if W == 0.
    // Using w / 2 instead of sign_bit_r, because this is not about R but
    // about whatever counter is below W that might have borrowed 1 from W.
    if ((increment & (W_mask | w / 2)) == 0)
      return true;

    // At this point we have either
    // 1a. C < 0 and W != 0, OR
    // 1b. C < 0, W == 0 and R < 0, OR
    // 2. C == 0 and W < 0, OR
    // 3. C == 0 and W == 0 and R < 0.

    // Detect the case where W > 0 (point 1a). If the sign bit is not
    // set then that rules out 1b and 3 because if R < 0 it would borrow
    // from W (that is zero in those cases) and cause the bit be set.
    // It also rules out 2 that has a negative W. Therefore this then
    // must be point 1a with a non-negative W, hence a positive W and
    // we can return false.
    if ((increment & sign_bit_w) == 0)
      return false;

    // At this point we have either
    // 1a. C < 0 and W < 0, OR
    // 1b. C < 0, W == 0 (and R < 0), OR
    // 1c. C == 0 and W < 0, OR
    // 2. C == 0 and W < 0, OR
    // 3. C == 0 and W == 0 (and R < 0).

    // In all but the last case we should return true.
    // Hence return false for case 3. Again, using w / 2 instead of sign_bit_r,
    // because this is not about R but about whatever counter is below W that
    // might have borrowed 1 from W.
    return (increment & (CW_mask | w / 2)) != (CW_mask | w / 2);
  }

#if DEBUG_RWSPINLOCK
  // The sanity of all of the above is tested in AIReadWriteSpinLock_static_assert, see at the bottom of this file.
  friend struct AIReadWriteSpinLock_static_assert;
#endif

  template<int64_t increment>
  [[gnu::always_inline]] int64_t do_transition()
  {
    RWSLDoutEntering(dc::notice|continued_cf, "do_transition<" << get_counters_as_increment(increment) << ">() ");
    // This is a no-op.
    if constexpr (increment == 0)
    {
      // The return value should not be used; but since we can't check that, just don't allow the function to be called at all (which clearly makes no sense).
      ASSERT(false);
      return 0;
    }
    // If the result of `writer_present` might to change from true to false, we should wake up possible threads that are waiting for that.
    else if constexpr (removes_writer(increment))
    {
      bool writer_present_became_false;
      int64_t previous_state;
      {
        std::lock_guard<std::mutex> lk(m_readers_cv_mutex);
        RWSLDout(dc::notice, "m_readers_cv_mutex is locked.");
        if constexpr (!removes_real_writer(increment))
        {
          previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
          RWSLDout(dc::finish, get_counters(previous_state) << " --> " << get_counters(previous_state + increment));
          writer_present_became_false = writer_present(previous_state) && !writer_present(previous_state + increment);
        }
        else
        {
          {
            std::lock_guard<std::mutex> lk(m_writers_cv_mutex);
            previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
            RWSLDout(dc::finish, get_counters(previous_state) <<  " --> " << get_counters(previous_state + increment));
            writer_present_became_false = writer_present(previous_state) && !writer_present(previous_state + increment);
          }
          RWSLDout(dc::notice, "Calling m_writers_cv.notify_one()");
          m_writers_cv.notify_one();
        }
        RWSLDout(dc::notice, "Unlocking m_readers_cv_mutex...");
      }
      if (writer_present_became_false)
      {
        RWSLDout(dc::notice, "Calling m_readers_cv.notify_all()");
        m_readers_cv.notify_all();
      }
      else
        RWSLDout(dc::notice, "Not calling notify_all() because writer_present() didn't change.");
      return previous_state;
    }
    else if constexpr (removes_real_writer(increment))
    {
      int64_t previous_state;
      {
        std::lock_guard<std::mutex> lk(m_writers_cv_mutex);
        previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
        RWSLDout(dc::finish, get_counters(previous_state) << " --> " << get_counters(previous_state + increment));
      }
      RWSLDout(dc::notice, "Calling m_writers_cv.notify_one()");
      m_writers_cv.notify_one();
      return previous_state;
    }
    else
    {
      RWSLDout(dc::notice, "Not calling notify_one()");
      // This change might cause threads to leave their spin-loop, but no notify_one is required.
      int64_t previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
      RWSLDout(dc::finish, get_counters(previous_state) << " --> " << get_counters(previous_state + increment));
      return previous_state;
    }
  }

 public:
  AIReadWriteSpinLock() : m_state(0) { }

  // Fast path. Implement taking a read-lock with a single RMW operation.
  void rdlock()
  {
    RWSLDoutEntering(dc::notice, "rdlock()");
    // Write locks have a higher priority in this class, therefore back-off any new read-lock
    // even when there are waiting writers (but no real succeeded write-lock yet).
#if DEBUG_RWSPINLOCK
    int64_t state;
    if (AI_UNLIKELY(writer_present(state = do_transition<one_rdlock>())))
      rdlock_blocked(state);
#else
    if (AI_UNLIKELY(writer_present(do_transition<one_rdlock>())))
      rdlock_blocked();
#endif
  }

  // Fast path. Implement releasing a read-lock with a single RMW operation.
  void rdunlock()
  {
    RWSLDoutEntering(dc::notice, "rdunlock()");
    // If this results in R == 0 and there are waiting writers, then those have to pick that up by reading m_state in a spin loop.
    do_transition<one_rdunlock>();
  }

private:
#if DEBUG_RWSPINLOCK
  void rdlock_blocked(int64_t state)
#else
  void rdlock_blocked()
#endif
  {
    RWSLDoutEntering(dc::notice, "rdlock_blocked(" << get_counters(state) << ")");
    do
    {
      RWSLDout(dc::notice, "Top of do/while loop.");
      // Apparently one or more threads are trying to obtain a write lock or one already has it.
      // Undo the increment of m_state.
      do_transition<failed_rdlock>();

      // Next we're going to wait until m_state becomes positive again.
      bool read_locked = false;
      RWSLDout(dc::notice, "Entering m_readers_cv.wait()");
      std::unique_lock<std::mutex> lk(m_readers_cv_mutex);
      RWSLDout(dc::notice, "m_readers_cv_mutex is locked.  Unlocking it...");
      m_readers_cv.wait(lk, [this, &read_locked](){
        RWSLDout(dc::notice, "Inside m_readers_cv.wait()'s lambda: m_readers_cv_mutex is locked.");
        int64_t state = 0; // If m_state is in the "unlocked" state (0), then replace it with one_rdlock (1).
        read_locked = m_state.compare_exchange_weak(state, one_rdlock, std::memory_order::relaxed, std::memory_order::relaxed);
        RWSLDout(dc::notice|continued_cf, "compare_exchange_weak(0, 1, ...) = " << read_locked);
        // If this returned true, then m_state was 0 and is now 1.
#if DEBUG_RWSPINLOCK
        if (read_locked)
          RWSLDout(dc::finish, "; transition: " << get_counters(0) << " --> " << get_counters(one_rdlock));
        else
          RWSLDout(dc::finish, "; state was " << get_counters(state));
#endif
        //
        // If it returned false and at the moment m_state is negative then we are still
        // write locked and it is safe to enter wait() again because we have the lock on
        // m_readers_cv_mutex and therefore the condition variable is guaranteed to be notified
        // again.
        //
        // In other words: since this returns false when there are writers present, we must have
        // m_readers_cv_mutex locked whenever we do a transition that removes a writer,
        // and do a notify_one after that.
        RWSLDout(dc::notice, "Returning " << std::boolalpha << (read_locked || !writer_present(state)) << "; unlocking m_readers_cv_mutex...");
        return read_locked || !writer_present(state);
      });
      RWSLDout(dc::notice, "Left m_readers_cv.wait() with read_locked = " << read_locked << "; m_readers_cv_mutex is locked.  Unlocking it.");
      // If read_locked was set, then we effectively added one_rdlock to m_state and we're done.
      if (read_locked)
        break;
    }
    while (writer_present(do_transition<one_rdlock>()));      // Try to get the read-lock again (see rdlock()) since we didn't obtain a read lock yet.
    RWSLDout(dc::notice, "Leaving rdlock_blocked()");
  }

 public:
  void wrlock()
  {
    RWSLDoutEntering(dc::notice, "wrlock()");
    // Taking a write lock should succeed only when no other thread has a read-lock or a write-lock.
    //
    // We also fail when nobody has a write-lock but there are (other) threads waiting on a write lock;
    // those should get a fair chance to get it since they were first. Aka, the "writer_present" here
    // has the same meaning as that of `writer_present`.
    if (no_reader_or_writer_present(do_transition<one_wrlock>()))
    {
      RWSLDout(dc::notice, "Success");
      return; // Success.
    }

    do
    {
      // Transition into `waiting_writer`.
      int64_t state = do_transition<failed_wrlock>();

      // From now on no new reader will succeed. Begin with spinning until all current readers are gone.
      //
      // Note that because this only reads, without trying to write anything -- because of the widespread use
      // of MESI caching protocols -- this should cause the cache line for the lock to become "Shared" with no bus
      // traffic while the CPU waits for the lock (on architectures with a cache per CPU).
      // Nevertheless, we add a call to cpu_relax() in the loop because that is common practise and highly
      // recommended anyway (by the intel user manual) for performance reasons.
      RWSLDout(dc::notice|continued_cf|flush_cf, "spinning... ");
      while (reader_present(state = m_state.load(std::memory_order::relaxed)))
        cpu_relax();
      RWSLDout(dc::finish, "done (state = " << get_counters(state) << ")");

      // Even though a call to rdlock() might still shortly increment R, this is no longer
      // our concern: they will fail and subtract 1 again.
      //
      // Note that also C will be zero at this point, because R is zero: C counts the number
      // of threads that try to convert a read-lock into a write-lock.
      //
      // However, there is no guarantee that we will win a race against another thread
      // that attempts to get a write lock; and if they do - they are allowed to convert
      // that into a read-lock despite that we're waiting to grab a write lock; therefore
      // it can still happen that at some point all writers are gone, but new readers
      // appeared. Therefore we now will wait until there are no real-writers anymore (W == 0)
      // and also C and R are zero; and then attempt to get a write lock again.

      // Note that m_writers_cv is notified each time W or C is decremented.
      bool write_locked = false;
      RWSLDout(dc::notice, "Entering m_writers_cv.wait()");
      std::unique_lock<std::mutex> lk(m_writers_cv_mutex);
      RWSLDout(dc::notice, "m_writers_cv_mutex is locked.  Unlocking it...");
      m_writers_cv.wait(lk, [this, state, &write_locked]() mutable {
        RWSLDout(dc::notice, "Inside m_writers_cv.wait()'s lambda: m_writers_cv_mutex is locked.");
        state &= V_mask;        // Set C = W = R = 0.
        write_locked = m_state.compare_exchange_weak(state, state + finalize_wrlock, std::memory_order::relaxed, std::memory_order::relaxed);
        RWSLDout(dc::notice|continued_cf, "compare_exchange_weak(" << get_counters(state) << ", " << get_counters(state + finalize_wrlock) << ", ...) = " << write_locked);
        // If this returned true, then m_state was state (C == W == R == 0) and is now state + finalize_wrlock,
#if DEBUG_RWSPINLOCK
        if (write_locked)
          RWSLDout(dc::finish, "; transition: " << get_counters(state) << " --> " << get_counters(state + finalize_wrlock));
        else
          RWSLDout(dc::finish, "; state was " << get_counters(state));
#endif
        // which means we successfully obtained a write lock.
        //
        // If it returned false and at the moment W or C are larger than zero, then it is
        // safe to enter wait() again because we have the lock on m_writers_cv_mutex
        // and therefore it is guaranteed that the condition variable will be notified again when
        // either changes towards zero.
        //
        // In other words: since this returns false when there were real writers present, we must
        // have m_writers_cv_mutex locked whenever we do a transition that removes a
        // real writer.
        RWSLDout(dc::notice, "Returning " << std::boolalpha << (write_locked || no_real_writer_present(state)) << "; unlocking m_writers_cv_mutex...");
        return write_locked || no_real_writer_present(state);
      });
      RWSLDout(dc::notice, "Left m_writers_cv.wait() with write_locked = " << write_locked << "; m_writers_cv_mutex is locked.  Unlocking it.");
      // If write_locked was set, then we effectively added one_wrlock to m_state and we're done.
      if (write_locked)
        break;
      // If we get here then no_real_writer_present(state) was true, which means W == C == 0,
      // but m_state was not equal to the value passed as first argument, with W == C == R == 0.
      // This can mean that R > 0 (there are now readers that we have to wait for again) and/or
      // that V changed (ie, another thread did one_wrlock and failed_wrlock).
      // In neither case we can rely on the condition variable, so it is correct that we left wait().
      // Now we no longer care about the value of V however: we will grab the write lock regardless.
    }
    while (reader_or_real_writer_present(do_transition<finalize_wrlock>()));
    RWSLDout(dc::notice, "Leaving wrlock()");
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
    RWSLDoutEntering(dc::notice, "wrunlock()");
    do_transition<one_wrunlock>();
  }

  void wr2rdlock()
  {
    RWSLDoutEntering(dc::notice, "wr2rdlock()");
    do_transition<one_wr2rdlock>();
  }
};

#if DEBUG_RWSPINLOCK
// Absolutely ridiculous - but the whole class definition must be finished
// before you can use a static constexpr member function in a static_assert?!
struct AIReadWriteSpinLock_static_assert : AIReadWriteSpinLock
{
  static constexpr int64_t unlocked = 0;
  static constexpr int64_t one_waiting_converter = c;

  //===========================================================================================================
  // Test writer_present; this includes waiting writers: anything that should prehibit new rdlock()'s to block.

  // Test single transitions.
  static_assert(
     !writer_present(one_rdlock) &&
      writer_present(one_wrlock),
      "writer_present logic error - depth 1");

  // Test double transitions.
  static_assert(
     !writer_present(one_rdlock + one_rdlock) &&
      writer_present(one_rdlock + one_wrlock) &&
      writer_present(one_wrlock + one_wrlock) &&
      writer_present(one_rdlock + one_rd2wrlock) &&
      writer_present(one_rdlock + one_rd2wrlock + successful_rd2wrlock) &&
                     one_rdlock + one_rd2wrlock + successful_rd2wrlock == one_wrlock,
      "writer_present logic error - depth 2");

  // Test single transitions with failure.
  static_assert(
     !writer_present(one_rdlock + failed_rdlock) &&
                     one_rdlock + failed_rdlock == unlocked &&
      writer_present(one_wrlock + failed_wrlock) &&
                     one_wrlock + failed_wrlock == one_waiting_writer,
      "writer_present logic error - depth 1 with failure");

  // Test double transitions with one failure.
  static_assert(
     !writer_present(one_rdlock + one_rdlock + failed_rdlock) &&
      writer_present(one_rdlock + one_wrlock + failed_rdlock) &&
      writer_present(one_rdlock + one_wrlock + failed_wrlock) &&
      writer_present(one_wrlock + one_wrlock + failed_wrlock) &&
      writer_present(one_rdlock + one_rd2wrlock + failed_rd2wrlock) &&
                     one_rdlock + one_rd2wrlock + failed_rd2wrlock == one_rdlock + one_waiting_writer + one_waiting_converter,
      "writer_present logic error - depth 2 with one failure");

  // Test double transitions with two failures.
  static_assert(
     !writer_present(one_rdlock + one_rdlock + failed_rdlock + failed_rdlock) &&
      writer_present(one_rdlock + one_wrlock + failed_rdlock + failed_wrlock) &&
      writer_present(one_wrlock + one_wrlock + failed_wrlock + failed_wrlock) &&
      writer_present(one_rdlock + one_rd2wrlock + failed_rdlock + failed_rd2wrlock) &&
      "writer_present logic error - depth 2 with two failures");
};

#undef RWSLDout
#undef RWSLDoutEntering

#endif
