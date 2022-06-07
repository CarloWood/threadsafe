/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Implementation of AIReadWriteSpinLock.
 *
 * @Copyright (C) 2017, 2018, 2022  Carlo Wood.
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

#ifndef DEBUG_STATIC_ASSERTS
#pragma once
#endif

#ifdef CWDEBUG
// Set to 1 to enable very extentive debug output with regard to this class; as well as enable static_asserts at the end.
#define DEBUG_RWSPINLOCK 0
#endif

#include "utils/cpu_relax.h"
#include "utils/macros.h"
#include "debug.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <exception>
#include <array>

#if (defined(__clang__) && __clang_major__ <= 14) || (defined(CWDEBUG) && defined(DEBUG_STATIC_ASSERTS))
// Broken compiler - or we're debugging the static_asserts.
#define consteval constexpr
#endif

#ifdef CWDEBUG
#ifdef DEBUG_STATIC_ASSERTS
#undef DEBUG_RWSPINLOCK
#define DEBUG_RWSPINLOCK 1
#endif // DEBUG_STATIC_ASSERTS
#if DEBUG_RWSPINLOCK
#include <iomanip>
#include <array>
#define RWSLDout(a, b) Dout(a, b)
#define RWSLDoutEntering(a, b) DoutEntering(a, b)
#else // DEBUG_RWSPINLOCK
#define RWSLDout(a, b) do { } while(0)
#define RWSLDoutEntering(a, b) do { } while(0)
#endif // DEBUG_RWSPINLOCK
#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
#include "ThreadPermuter.h"
#endif // DEBUG_RWSPINLOCK_THREADPERMUTER
#else // CWDEBUG
#define DEBUG_RWSPINLOCK 0
#undef DEBUG_RWSPINLOCK_THREADPERMUTER
#define RWSLDout(a, b) do { } while(0)
#define RWSLDoutEntering(a, b) do { } while(0)
#endif // CWDEBUG

#ifndef DEBUG_RWSPINLOCK_THREADPERMUTER
#undef TPY
#undef TPB
#undef TPP
#define TPY
#define TPB
#define TPP
#endif // DEBUG_RWSPINLOCK_THREADPERMUTER

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

  // Used in certain test.
  static constexpr int64_t sign_bit_r = r << (shift - 1);
  static constexpr int64_t sign_bit_v = v << (shift - 1);
  static constexpr int64_t sign_bit_c = c << (shift - 1);
  static constexpr int64_t sign_bit_w = w << (shift - 1);

  // The bits used for R.
  static constexpr int64_t R_mask = w - 1;
  static constexpr int64_t W_mask = R_mask << shift;
  static constexpr int64_t C_mask = W_mask << shift;
  static constexpr int64_t CW_mask = C_mask | W_mask;
  static constexpr int64_t V_mask = C_mask << shift;

  // Possible transitions.
  static constexpr int64_t one_rdlock           =      r;
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

#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
  using mutex_t = thread_permuter::Mutex;
  using condition_variable_t = thread_permuter::ConditionVariable;
#else
  using mutex_t = std::mutex;
  using condition_variable_t = std::condition_variable;
#endif

  std::atomic<int64_t> m_state;
  mutex_t m_readers_cv_mutex;
  condition_variable_t m_readers_cv;
  mutex_t m_writers_cv_mutex;
  condition_variable_t m_writers_cv;

  // This condition is used to detect if a reader is allowed to grab a read-lock.
  //
  // Returns true if there is an actual writer (W > 0), but also when there are
  // threads that are waiting to get a write lock. All those threads have V decremented.
  [[gnu::always_inline]] static constexpr bool writer_present(int64_t state)
  {
    // See the use of -v above.
    return state < 0;
  }

  // This condition is used to detect if a (waiting) writer is allowed to grab a write-lock.
  //
  // Returns true only when there are no readers (R = 0) and no (waiting) writers (V == 0,
  // which implies that W == 0 and C == 0).
  [[gnu::always_inline]] static constexpr bool reader_or_writer_present(int64_t state)
  {
    // No read-lock or write-locks are allowed: R = 0 and W = 0. But also no waiting writers.
    return state != 0;
  }

  // This condition is used to detect if a spinning writer can proceed with waiting for other writers,
  // and no longer worry about readers (at the moment).
  //
  // Returns true when R is larger than zero.
  [[gnu::always_inline]] static constexpr bool reader_present(int64_t state)
  {
    // Note that R can't be negative.
    return (state & R_mask) != 0;
  }

  // This condition is used to detect if a writer has to wait for other writers before it can grab the write-lock.
  // That includes the C count, because threads that try to convert their read-lock into a write-lock have
  // a higher priority (after all, we can't get a write-lock while they have their read-lock, so there is no other way).
  //
  // Returns true if either C or W is larger than zero.
  [[gnu::always_inline]] static constexpr bool converting_or_actual_writer_present(int64_t state)
  {
    // Note that C and W (and R) can't be negative.
    return (state & CW_mask) != 0;
  }

  // This condition is used to detect if a writer is allowed to grab a write-lock after having already waited for other writers a bit.
  //
  // It returns true iff reader_present or converting_or_actual_writer_present.
  [[gnu::always_inline]] static constexpr bool reader_or_converting_or_actual_writer_present(int64_t state)
  {
    return (state & (R_mask | CW_mask)) != 0;
  }

  // This condition is used to detect if it is safe for a reader to continue converting to a write-lock,
  // after it incremented C and V (the state passed is the previous state).
  //
  // It returns true iff a converting writer is present, aka C > 0.
  [[gnu::always_inline]] static constexpr bool converting_writer_present(int64_t state)
  {
    // C can never be less than zero, so testing for non-zero is the same as testing for larger than zero.
    // Also uses the fact that counters below this (W and R) are never negative.
    return (state & C_mask) != 0;
  }

  // Used in rd2wrlock to wait for all other readers to release their lock.
  //
  // Returns true iff R > 1.
  [[gnu::always_inline]] static constexpr bool other_readers_present(int64_t state)
  {
    return (state & R_mask) > 1;
  }

  // Used in rd2wrlock to wait until an actual writer released their write-lock.
  //
  // Returns true iff W > 0.
  [[gnu::always_inline]] static constexpr bool actual_writer_present(int64_t state)
  {
    // W can never be less than zero, so testing for non-zero is the same as testing for larger than zero.
    // Also uses the fact that the counter below this (R) is never negative.
    return (state & W_mask) != 0;
  }

  static consteval std::array<int, 4> decode_increment(int64_t increment)
  {
    std::array<int, 4> i;
    i[3] = (increment & R_mask) - ((increment & sign_bit_r) << 1);
    if (i[3] < 0)
      increment += w;
    i[2] = ((increment & W_mask) - ((increment & sign_bit_w) << 1)) >> shift;
    if (i[2] < 0)
      increment += c;
    i[1] = ((increment & C_mask) - ((increment & sign_bit_c) << 1)) >> (2 * shift);
    if (i[1] < 0)
      increment += v;
    i[0] = ((increment & V_mask) - ((increment & sign_bit_v) << 1)) >> (3 * shift);
    return i;
  }

  // This test is used in do_transition and tests if adding `increment` to m_state can cause a (waiting) writer to be removed.
  // Returns true if V > 0 || C < 0 || W < 0.
  static consteval bool removes_writer(int64_t increment)
  {
    std::array<int, 4> i = decode_increment(increment);
    return i[0] > 0 || i[1] < 0 || i[2] < 0;
  }

  // This test is used in do_transition and tests if adding `increment` to m_state can cause the number of converting/actual writers to become zero.
  // What this means is that this transition might change the value of converting_or_actual_writer_present from true to false.
  // Returns true if (C < 0 || W < 0) && !(C > 0 || W > 0).
  static consteval bool removes_converting_or_actual_writer(int64_t increment)
  {
    std::array<int, 4> i = decode_increment(increment);
    return (i[1] < 0 || i[2] < 0) && !(i[1] > 0 || i[2] > 0);
  }

  // Returns true if C < 0.
  static consteval bool removes_converting_writer(int64_t increment)
  {
    std::array<int, 4> i = decode_increment(increment);
    return i[1] < 0;
  }

  // Returns true if W < 0.
  static consteval bool removes_actual_writer(int64_t increment)
  {
    std::array<int, 4> i = decode_increment(increment);
    return i[2] < 0;
  }

#if DEBUG_RWSPINLOCK
  // Works for state and increment.
  static consteval int64_t make_state(std::array<int, 4> const& s)
  {
    std::array<int64_t, 4> const b = { v, c, w, r };
    int64_t res = 0;
    for (int i = 0; i < 4; ++i)
    {
      int64_t sign = (s[i] < 1) ? -1UL : 1UL;
      for (int j = 0; j < sign * s[i]; ++j)
        res += sign * b[i];
    }
    return res;
  }

  static consteval bool test_removes_converting_or_actual_writer()
  {
    // Test that removes_converting_or_actual_writer does what its comment says: return true iff (c < 0 || w < 0) && !(c > 0 || w > 0).
    for (int v = -1; v <= 1; ++v)
      for (int c = -2; c <= 2; ++c)
        for (int w = -2; w <= 2; ++w)
          for (int r = -2; r <= 2; ++r)
          {
            // All transitions do c and v, or w and v in pairs.
            std::array<int, 4> i = { v - c - w, c, w, r };
            int64_t increment = make_state(i);
            bool expected = (c < 0 || w < 0) && !(c > 0 || w > 0);
            // Test removes_converting_or_actual_writer.
            if (removes_converting_or_actual_writer(increment) != expected)
            {
#ifdef DEBUG_STATIC_ASSERTS
              std::cout << "Error: " << std::dec << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; removes_converting_or_actual_writer(" <<
                std::hex << std::setfill('0') << std::setw(16) << increment << ") = " << std::boolalpha << !expected << "; expected: " << std::boolalpha << expected << std::endl;
#endif
              return false;
            }
          }
    // In the broader picture, any transition (increment) that might cause converting_or_actual_writer_present
    // to become false must satisfy removes_converting_or_actual_writer(increment).
    for (int pure_waiting = 0; pure_waiting <= 2; ++pure_waiting)
      for (int converting = 0; converting <= 3; ++converting)
        for (int writing = 0; writing <= 2; ++writing)
          for (int reading = 0; reading <= 2; ++reading)
          {
            std::array<int, 4> s = { -pure_waiting - converting - writing, converting, writing, reading };
#ifdef DEBUG_STATIC_ASSERTS
            assert(s[0] <= -(s[1] + s[2]));                             // A state always must have V <= -(C + W).
#endif
            int64_t state = make_state(s);
            // Counters may never become negative (or v positive).
            for (int v = -1; v <= 1; ++v)
              for (int c = std::max(-2, -converting); c <= 2; ++c)      // c + converting >= 0 --> c >= -converting.
                for (int w = std::max(-2, -writing); w <= 2; ++w)
                  for (int r = std::max(-2, -reading); r <= 2; ++r)
                  {
                    std::array<int, 4> i = { v - c - w, c, w, r };
                    if (!(s[0] + i[0] <= -(s[1] + i[1] + s[2] + i[2]))) // Also the resulting state must always have V <= -(C + W).
                      continue;
                    int64_t increment = make_state(i);
                    bool converting_or_actual_writer_present_becomes_false = converting_or_actual_writer_present(state) && !converting_or_actual_writer_present(state + increment);
                    if (converting_or_actual_writer_present_becomes_false && !removes_converting_or_actual_writer(increment))
                    {
#ifdef DEBUG_STATIC_ASSERTS
                      std::cout << "Error: " << std::dec << "pure_waiting = " << pure_waiting << ", converting = " <<
                        converting << ", writing = " << writing << ", reading = " << reading << "; state = " <<
                        std::hex << std::setfill('0') << std::setw(16) << state << std::dec << std::endl;
                      std::cout << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; increment = " <<
                        std::hex << std::setfill('0') << std::setw(16) << increment << std::dec << std::endl;
                      std::cout << "converting_or_actual_writer_present_becomes_false = " << std::boolalpha << converting_or_actual_writer_present_becomes_false <<
                        "; state + increment = " << std::hex << std::setfill('0') << std::setw(16) << (state + increment) << std::dec << std::endl;
#endif
                      return false;     // Logic error.
                    }

                  }
          }
    return true;  // Success.
  }

  static consteval bool test_removes_converting_writer()
  {
    // Test that removes_converting_writer does what its comment says: return true iff c < 0.
    for (int v = -1; v <= 1; ++v)
      for (int c = -2; c <= 2; ++c)
        for (int w = -2; w <= 2; ++w)
          for (int r = -2; r <= 2; ++r)
          {
            // All transitions do c and v, or w and v in pairs.
            std::array<int, 4> i = { v - c - w, c, w, r };
            int64_t increment = make_state(i);
            bool expected = c < 0;
            // Test removes_converting_writer.
            if (removes_converting_writer(increment) != expected)
            {
#ifdef DEBUG_STATIC_ASSERTS
              std::cout << "Error: " << std::dec << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; removes_converting_writer(" <<
                std::hex << std::setfill('0') << std::setw(16) << increment << ") = " << std::boolalpha << !expected << "; expected: " << std::boolalpha << expected << std::endl;
#endif
              return false;
            }
          }
    // In the broader picture, any transition (increment) that might cause converting_writer_present
    // to become false must satisfy removes_converting_writer(increment).
    for (int pure_waiting = 0; pure_waiting <= 2; ++pure_waiting)
      for (int converting = 0; converting <= 3; ++converting)
        for (int writing = 0; writing <= 2; ++writing)
          for (int reading = 0; reading <= 2; ++reading)
          {
            std::array<int, 4> s = { -pure_waiting - converting - writing, converting, writing, reading };
#ifdef DEBUG_STATIC_ASSERTS
            assert(s[0] <= -(s[1] + s[2]));                             // A state always must have V <= -(C + W).
#endif
            int64_t state = make_state(s);
            // Counters may never become negative (or v positive).
            for (int v = -1; v <= 1; ++v)
              for (int c = std::max(-2, -converting); c <= 2; ++c)      // c + converting >= 0 --> c >= -converting.
                for (int w = std::max(-2, -writing); w <= 2; ++w)
                  for (int r = std::max(-2, -reading); r <= 2; ++r)
                  {
                    std::array<int, 4> i = { v - c - w, c, w, r };
                    if (!(s[0] + i[0] <= -(s[1] + i[1] + s[2] + i[2]))) // Also the resulting state must always have V <= -(C + W).
                      continue;
                    int64_t increment = make_state(i);
                    bool converting_writer_present_becomes_false = converting_writer_present(state) && !converting_writer_present(state + increment);
                    if (converting_writer_present_becomes_false && !removes_converting_writer(increment))
                    {
#ifdef DEBUG_STATIC_ASSERTS
                      std::cout << "Error: " << std::dec << "pure_waiting = " << pure_waiting << ", converting = " <<
                        converting << ", writing = " << writing << ", reading = " << reading << "; state = " <<
                        std::hex << std::setfill('0') << std::setw(16) << state << std::dec << std::endl;
                      std::cout << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; increment = " <<
                        std::hex << std::setfill('0') << std::setw(16) << increment << std::dec << std::endl;
                      std::cout << "converting_writer_present_becomes_false = " << std::boolalpha << converting_writer_present_becomes_false <<
                        "; state + increment = " << std::hex << std::setfill('0') << std::setw(16) << (state + increment) << std::dec << std::endl;
#endif
                      return false;     // Logic error.
                    }

                  }
          }
    return true;  // Success.
  }

  static consteval bool test_removes_actual_writer()
  {
    // Test that removes_actual_writer does what its comment says: return true iff w < 0.
    for (int v = -1; v <= 1; ++v)
      for (int c = -2; c <= 2; ++c)
        for (int w = -2; w <= 2; ++w)
          for (int r = -2; r <= 2; ++r)
          {
            // All transitions do c and v, or w and v in pairs.
            std::array<int, 4> i = { v - c - w, c, w, r };
            int64_t increment = make_state(i);
            bool expected = w < 0;
            // Test removes_actual_writer.
            if (removes_actual_writer(increment) != expected)
            {
#ifdef DEBUG_STATIC_ASSERTS
              std::cout << "Error: " << std::dec << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; removes_actual_writer(" <<
                std::hex << std::setfill('0') << std::setw(16) << increment << ") = " << std::boolalpha << !expected << "; expected: " << std::boolalpha << expected << std::endl;
#endif
              return false;
            }
          }
    // In the broader picture, any transition (increment) that might cause actual_writer_present
    // to become false must satisfy removes_actual_writer(increment).
    for (int pure_waiting = 0; pure_waiting <= 2; ++pure_waiting)
      for (int converting = 0; converting <= 3; ++converting)
        for (int writing = 0; writing <= 2; ++writing)
          for (int reading = 0; reading <= 2; ++reading)
          {
            std::array<int, 4> s = { -pure_waiting - converting - writing, converting, writing, reading };
#ifdef DEBUG_STATIC_ASSERTS
            assert(s[0] <= -(s[1] + s[2]));                             // A state always must have V <= -(C + W).
#endif
            int64_t state = make_state(s);
            // Counters may never become negative (or v positive).
            for (int v = -1; v <= 1; ++v)
              for (int c = std::max(-2, -converting); c <= 2; ++c)      // c + converting >= 0 --> c >= -converting.
                for (int w = std::max(-2, -writing); w <= 2; ++w)
                  for (int r = std::max(-2, -reading); r <= 2; ++r)
                  {
                    std::array<int, 4> i = { v - c - w, c, w, r };
                    if (!(s[0] + i[0] <= -(s[1] + i[1] + s[2] + i[2]))) // Also the resulting state must always have V <= -(C + W).
                      continue;
                    int64_t increment = make_state(i);
                    bool converting_writer_present_becomes_false = converting_writer_present(state) && !converting_writer_present(state + increment);
                    if (converting_writer_present_becomes_false && !removes_converting_writer(increment))
                    {
#ifdef DEBUG_STATIC_ASSERTS
                      std::cout << "Error: " << std::dec << "pure_waiting = " << pure_waiting << ", converting = " <<
                        converting << ", writing = " << writing << ", reading = " << reading << "; state = " <<
                        std::hex << std::setfill('0') << std::setw(16) << state << std::dec << std::endl;
                      std::cout << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; increment = " <<
                        std::hex << std::setfill('0') << std::setw(16) << increment << std::dec << std::endl;
                      std::cout << "converting_writer_present_becomes_false = " << std::boolalpha << converting_writer_present_becomes_false <<
                        "; state + increment = " << std::hex << std::setfill('0') << std::setw(16) << (state + increment) << std::dec << std::endl;
#endif
                      return false;     // Logic error.
                    }

                  }
          }
    return true;  // Success.
  }

  static consteval bool test_removes_writer()
  {
    // Test that removes_writer does what its comment says: return true iff c < 0 || w < 0.
    for (int v = -1; v <= 1; ++v)
      for (int c = -2; c <= 2; ++c)
        for (int w = -2; w <= 2; ++w)
          for (int r = -2; r <= 2; ++r)
          {
            // All transitions do c and v, or w and v in pairs.
            std::array<int, 4> i = { v - c - w, c, w, r };
            int64_t increment = make_state(i);
            // Test decode_increment.
            std::array<int, 4> read_back = decode_increment(increment);
            if (read_back != i)
            {
#ifdef DEBUG_STATIC_ASSERTS
              std::cout << "Error: decode_increment(" << std::hex << std::setfill('0') << std::setw(16) << increment << std::dec << ") = ";
              for (int n : read_back)
                std::cout << n << ' ';
              std::cout << std::endl;
#endif
              return false;
            }
            bool expected = i[0] > 0 || c < 0 || w < 0;
            // Test removes_writer.
            if (removes_writer(increment) != expected)
            {
#ifdef DEBUG_STATIC_ASSERTS
              std::cout << "Error: " << std::dec << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; removes_writer(" <<
                std::hex << std::setfill('0') << std::setw(16) << increment << ") = " << std::boolalpha << !expected << "; expected: " << std::boolalpha << expected << std::endl;
#endif
              return false;
            }
          }
    // In the broader picture, any transition (increment) that might cause writer_present, converting_or_actual_writer_present,
    // actual_writer_present or converting_writer_present to become false must satisfy removes_writer(increment).
    for (int pure_waiting = 0; pure_waiting <= 2; ++pure_waiting)
      for (int converting = 0; converting <= 3; ++converting)
        for (int writing = 0; writing <= 2; ++writing)
          for (int reading = 0; reading <= 2; ++reading)
          {
            std::array<int, 4> s = { -pure_waiting - converting - writing, converting, writing, reading };
#ifdef DEBUG_STATIC_ASSERTS
            assert(s[0] <= -(s[1] + s[2]));                             // A state always must have V <= -(C + W).
#endif
            int64_t state = make_state(s);
            // Counters may never become negative (or v positive).
            for (int v = -1; v <= 1; ++v)
              for (int c = std::max(-2, -converting); c <= 2; ++c)      // c + converting >= 0 --> c >= -converting.
                for (int w = std::max(-2, -writing); w <= 2; ++w)
                  for (int r = std::max(-2, -reading); r <= 2; ++r)
                  {
                    std::array<int, 4> i = { v - c - w, c, w, r };
                    if (!(s[0] + i[0] <= -(s[1] + i[1] + s[2] + i[2]))) // Also the resulting state must always have V <= -(C + W).
                      continue;
                    int64_t increment = make_state(i);
                    bool writer_present_becomes_false = writer_present(state) && !writer_present(state + increment);
                    bool converting_or_actual_writer_present_becomes_false = converting_or_actual_writer_present(state) && !converting_or_actual_writer_present(state + increment);
                    bool actual_writer_present_becomes_false = actual_writer_present(state) && !actual_writer_present(state + increment);
                    bool converting_writer_present_becomes_false = converting_writer_present(state) && !converting_writer_present(state + increment);
                    if ((writer_present_becomes_false ||
                         converting_or_actual_writer_present_becomes_false ||
                         actual_writer_present_becomes_false ||
                         converting_writer_present_becomes_false) && !removes_writer(increment))
                    {
#ifdef DEBUG_STATIC_ASSERTS
                      std::cout << "Error: " << std::dec << "pure_waiting = " << pure_waiting << ", converting = " <<
                        converting << ", writing = " << writing << ", reading = " << reading << "; state = " <<
                        std::hex << std::setfill('0') << std::setw(16) << state << std::dec << std::endl;
                      std::cout << "v = " << v << ", c = " << c << ", w = " << w << ", r = " << r << "; increment = " <<
                        std::hex << std::setfill('0') << std::setw(16) << increment << std::dec << std::endl;
                      std::cout << "writer_present_becomes_false = " << std::boolalpha << writer_present_becomes_false <<
                        "; converting_or_actual_writer_present_becomes_false = " << std::boolalpha << converting_or_actual_writer_present_becomes_false <<
                        "; actual_writer_present_becomes_false = " << std::boolalpha << actual_writer_present_becomes_false <<
                        "; converting_writer_present_becomes_false = " << std::boolalpha << converting_writer_present_becomes_false <<
                        "; state + increment = " << std::hex << std::setfill('0') << std::setw(16) << (state + increment) << std::dec << std::endl;
#endif
                      return false;     // Logic error.
                    }
                  }
          }
    return true;  // Success.
  }

  // The sanity of all of the above is tested in AIReadWriteSpinLock_static_assert, see at the bottom of this file.
  friend struct AIReadWriteSpinLock_static_assert;
#endif // DEBUG_RWSPINLOCK

  template<int64_t increment>
  [[gnu::always_inline]] int64_t do_transition()
  {
    RWSLDoutEntering(dc::notice|continued_cf, "do_transition<" << get_counters_as_increment(increment) << ">() ");
    // `increment == 0` is a no-op. The return value in that case should not be used; but since we can't check that,
    // just don't allow the function to be called at all (since that doesn't make sense anyway).
    static_assert(increment != 0, "Don't call do_transition<0>()");
    // If the result of `writer_present` might change from true to false, we should wake up possible threads that are waiting for that.
    if constexpr (removes_writer(increment))
    {
#if CW_DEBUG
      bool m_writers_cv_mutex_was_locked = false;
#endif
      int64_t previous_state;
      {
        std::lock_guard<mutex_t> lk(m_readers_cv_mutex);
        RWSLDout(dc::notice, "m_readers_cv_mutex is locked.");
        if constexpr (removes_converting_or_actual_writer(increment) ||
                      removes_converting_writer(increment) ||
                      removes_actual_writer(increment))
        {
          {
            std::lock_guard<mutex_t> lk(m_writers_cv_mutex);
#if CW_DEBUG
            m_writers_cv_mutex_was_locked = true;
#endif
            RWSLDout(dc::notice, "m_writers_cv_mutex is locked.");
            previous_state = m_state.fetch_add(increment, std::memory_order::release);  // Synchronize with rdunlock.
            RWSLDout(dc::finish, get_counters(previous_state) <<  " --> " << get_counters(previous_state + increment));
            TPY;
            RWSLDout(dc::notice, "Unlocking m_writers_cv_mutex...");
          }
          TPY;
        }
        else
        {
          // Remove a waiting writer.
          previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
          RWSLDout(dc::finish, get_counters(previous_state) << " --> " << get_counters(previous_state + increment));
          TPY;
        }
        RWSLDout(dc::notice, "Unlocking m_readers_cv_mutex...");
      }
      TPY;

      // If writer_present changed from true to false, wake up all threads that are waiting for a read-lock.
      if (writer_present(previous_state) && !writer_present(previous_state + increment))
      {
        RWSLDout(dc::notice, "Calling m_readers_cv.notify_all()");
        m_readers_cv.notify_all();
      }
      else
        RWSLDout(dc::notice, "Not calling m_readers_cv.notify_all() because writer_present() didn't change.");

      // If converting_writer_present changed from true to false, wake up all threads that are possibly waiting in rd2wryield.
      if (converting_writer_present(previous_state) && !converting_writer_present(previous_state + increment))
      {
        ASSERT(m_writers_cv_mutex_was_locked);
        RWSLDout(dc::notice, "Calling m_writers_cv.notify_all()");
        m_writers_cv.notify_all();
      }
      // Otherwise, if converting_or_actual_writer_present or actual_writer_present changed from true to false, wake up one thread waiting on m_writers_cv.
      else if ((converting_or_actual_writer_present(previous_state) && !converting_or_actual_writer_present(previous_state + increment)) ||
               (actual_writer_present(previous_state) && !actual_writer_present(previous_state + increment)))
      {
        ASSERT(m_writers_cv_mutex_was_locked);
        RWSLDout(dc::notice, "Calling m_writers_cv.notify_one()");
        m_writers_cv.notify_one();
      }
      else
        RWSLDout(dc::notice, "Not calling m_writers_cv.notify_all() because converting_writer_present, converting_or_actual_writer_present and actual_writer_present all didn't change.");

      return previous_state;
    }
    else
    {
      // removes_converting_or_actual_writer can't be true when removes_writer is false.
      static_assert(!removes_converting_or_actual_writer(increment), "Logic error");

      RWSLDout(dc::notice, "Not calling notify_one()");
      int64_t previous_state;
      if constexpr (increment == one_rdlock)
      {
        // Need to sync with the last wrunlock.
        previous_state = m_state.fetch_add(increment, std::memory_order::acquire);
      }
      else
      {
        // This change might cause threads to leave their spin-loop, but no notify_one is required.
        previous_state = m_state.fetch_add(increment, std::memory_order::relaxed);
      }
      RWSLDout(dc::finish, get_counters(previous_state) << " --> " << get_counters(previous_state + increment));
      TPY;
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
      {
        std::unique_lock<mutex_t> lk(m_readers_cv_mutex);
        TPY;
        RWSLDout(dc::notice, "Calling m_readers_cv.wait(lk, lambda)");
        m_readers_cv.wait(lk, [this, &read_locked](){
          RWSLDout(dc::notice, "Inside m_readers_cv.wait()'s lambda; m_readers_cv_mutex is locked.");
          int64_t state = 0; // If m_state is in the "unlocked" state (0), then replace it with one_rdlock (1).
          read_locked = m_state.compare_exchange_weak(state, one_rdlock, std::memory_order::relaxed, std::memory_order::relaxed);
          RWSLDout(dc::notice|continued_cf, "compare_exchange_weak(0, 1, ...) = " << read_locked << "... ");
          TPY;
          // If this returned true, then m_state was 0 and is now 1,
          // which means we successfully obtained a read lock.
          //
          // If it returned false and at the moment V is negative then we are still write locked
          // and it is safe to enter wait() again because we have the lock on m_readers_cv_mutex
          // and therefore the condition variable is guaranteed to be notified again.
#if DEBUG_RWSPINLOCK
          if (read_locked)
            RWSLDout(dc::finish, "transition: " << get_counters(0) << " --> " << get_counters(one_rdlock));
          else
            RWSLDout(dc::finish, "state was " << get_counters(state));
#endif
          // In other words: since the following returns false when there were writers present,
          // we must have m_readers_cv_mutex locked whenever we do a transition that causes
          // writer_present to become false - and do a notify_all after that.
          RWSLDout(dc::notice|flush_cf, "Returning " << std::boolalpha << (read_locked || !writer_present(state)) << "; unlocking m_readers_cv_mutex...");
          bool exit_wait = read_locked || !writer_present(state);
#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
          if (!exit_wait)
            TPP;  // For the unlock of m_readers_cv_mutex.
#endif
          return exit_wait;
        });
        RWSLDout(dc::notice, "Left m_readers_cv.wait() with read_locked = " << read_locked << "; m_readers_cv_mutex is locked. Unlocking it...");
      }
      TPY; // Because we left the scope of the std::unique_lock.
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
    if (!reader_or_writer_present(do_transition<one_wrlock>()))
    {
      RWSLDout(dc::notice, "Success");
      return; // Success.
    }

    do
    {
      // Transition into `waiting_writer`.
      int64_t state = do_transition<failed_wrlock>();
      TPP;

      // From now on no new reader will succeed. Begin with spinning until all current readers are gone.
      //
      // Note that because this only reads, without trying to write anything -- because of the widespread use
      // of MESI caching protocols -- this should cause the cache line for the lock to become "Shared" with no bus
      // traffic while the CPU waits for the lock (on architectures with a cache per CPU).
      // Nevertheless, we add a call to cpu_relax() in the loop because that is common practise and highly
      // recommended anyway (by the intel user manual) for performance reasons.
      RWSLDout(dc::notice|continued_cf|flush_cf, "spinning... ");
      while (reader_present(state = m_state.load(std::memory_order::relaxed)))
      {
        cpu_relax();
        TPB;
      }
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
      {
        std::unique_lock<mutex_t> lk(m_writers_cv_mutex);
        TPY;
        RWSLDout(dc::notice, "Calling m_writers_cv.wait(lk, lambda)");
        m_writers_cv.wait(lk, [this, state, &write_locked]() mutable {
          RWSLDout(dc::notice, "Inside m_writers_cv.wait()'s lambda; m_writers_cv_mutex is locked.");
          state &= V_mask;        // Demand C = W = R = 0.
          write_locked = m_state.compare_exchange_weak(state, state + finalize_wrlock, std::memory_order::relaxed, std::memory_order::relaxed);
          RWSLDout(dc::notice|continued_cf, "compare_exchange_weak(" << get_counters(state) << ", " << get_counters(state + finalize_wrlock) << ", ...) = " << write_locked << "... ");
          TPY;
          // If this returned true, then m_state was state (C == W == R == 0) and is now state + finalize_wrlock,
          // which means we successfully obtained a write lock.
          //
          // If it returned false and at the moment W or C are larger than zero, then it is
          // safe to enter wait() again because we have the lock on m_writers_cv_mutex
          // and therefore it is guaranteed that the condition variable will be notified again when
          // either changes towards zero.
#if DEBUG_RWSPINLOCK
          if (write_locked)
            RWSLDout(dc::finish, "transition: " << get_counters(state) << " --> " << get_counters(state + finalize_wrlock));
          else
            RWSLDout(dc::finish, "state was " << get_counters(state));
#endif
          // In other words: since the following returns false when there were converting/actual writers present,
          // we must have m_writers_cv_mutex locked whenever we do a transition that causes
          // converting_or_actual_writer_present to become false - and do a notify_one after that.
          RWSLDout(dc::notice|flush_cf, "Returning " << std::boolalpha << (write_locked || !converting_or_actual_writer_present(state)) << "; unlocking m_writers_cv_mutex...");
          bool exit_wait = write_locked || !converting_or_actual_writer_present(state);
#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
          if (!exit_wait)
            TPP;    // For the unlock of m_writers_cv_mutex.
#endif
          return exit_wait;
        });
        RWSLDout(dc::notice, "Left m_writers_cv.wait() with write_locked = " << write_locked << "; m_writers_cv_mutex is locked. Unlocking it...");
      }
      TPY; // Because we left the scope of the std::unique_lock.
      // If write_locked was set, then we effectively added one_wrlock to m_state and we're done.
      if (write_locked)
        break;
      // If we get here then converting_or_actual_writer_present(state) was false, which means W == C == 0,
      // but m_state was not equal to the value passed as first argument, with W == C == R == 0.
      // This can mean that R > 0 (there are now readers that we have to wait for again) and/or
      // that V changed (ie, another thread did one_wrlock and failed_wrlock).
      // In neither case we can rely on the condition variable, so it is correct that we left wait().
      // Now we no longer care about the value of V however: we will grab the write lock regardless.
    }
    while (reader_or_converting_or_actual_writer_present(do_transition<finalize_wrlock>()));
    RWSLDout(dc::notice, "Leaving wrlock()");
  }

  void rd2wrlock()
  {
    RWSLDoutEntering(dc::notice, "rd2wrlock()");
    int64_t state;

    // Converting a read- to write-lock should only immediately succeed if
    // there are no readers, actual writers or other converting writers.
    if (!reader_or_converting_or_actual_writer_present(state = do_transition<one_rd2wrlock>()))
    {
      // Finalize the conversion. After this the read-lock is released and we are fully converted into a write-lock.
      do_transition<successful_rd2wrlock>();
      RWSLDout(dc::notice, "Success");
      return;
    }

    // If another thread tried to convert their read-lock into a write-lock at the same time, we have a dead-lock and must throw an exception.
    if (converting_writer_present(state))
    {
      // Revert what we just did.
      do_transition<-one_rd2wrlock>();
      throw std::exception();
    }

    // failed_rd2wrlock is a no-op and not necessary (otherwise it would be done here).
    TPP;

    // From now on no new reader or writer will succeed. Begin with spinning until all, other, current readers are gone.
    RWSLDout(dc::notice|continued_cf|flush_cf, "spinning... ");
    while (other_readers_present(state = m_state.load(std::memory_order::relaxed)))
    {
      cpu_relax();
      TPB;
    }
    RWSLDout(dc::finish, "done (state = " << get_counters(state) << ")");

    {
      std::unique_lock<mutex_t> lk(m_writers_cv_mutex);
      TPY;
      // Finally, wait until a possible actual writer released their write-lock.
      // Note that m_writers_cv is notified each time W or C is decremented.
      // C == 1 (this thread) and will not be decremented to zero; so we can use the same condition variable.
      RWSLDout(dc::notice, "Calling m_writers_cv.wait(lk, lambda)");
      m_writers_cv.wait(lk, [this, state]() mutable {
        RWSLDout(dc::notice, "Inside m_writers_cv.wait()'s lambda; m_writers_cv_mutex is locked.");
        bool write_locked;
        do
        {
          state &= ~W_mask;       // Demand W = 0.
          write_locked = m_state.compare_exchange_weak(state, state + successful_rd2wrlock, std::memory_order::relaxed, std::memory_order::relaxed);
          RWSLDout(dc::notice|continued_cf, "compare_exchange_weak(" << get_counters(state) << ", " << get_counters(state + successful_rd2wrlock) << ", ...) = " << write_locked << "... ");
#if DEBUG_RWSPINLOCK
          if (write_locked)
            RWSLDout(dc::finish, "transition: " << get_counters(state) << " --> " << get_counters(state + successful_rd2wrlock));
          else
            RWSLDout(dc::finish, "state was " << get_counters(state));
#endif
          TPY;
          if (write_locked)
          {
            // This simulated a do_transition<successful_rd2wrlock>() which requires a m_writers_cv.notify_all() if C became zero.
            if (!converting_writer_present(state + successful_rd2wrlock))
            {
              // Wake up all threads that are potentially waiting in rd2wryield().
              m_writers_cv.notify_all();
            }
          }
        }
        while (!write_locked && !actual_writer_present(state)); // Only exit this loop if we succeeded to get the write-lock, or when there are no actual writers present.

        // Here, either `write_locked` is true and we succeeded (and will leave this function), or
        // actual_writer_present(state) is true. In that case it is safe to return false and continue
        // to wait for m_writers_cv because that actual writer will call notify_one.
        // Of course that means, again, that we must have m_writers_cv_mutex locked whenever we do a
        // transition that causes actual_writer_present to become false - and do a notify_one after that.
        RWSLDout(dc::notice|flush_cf, "Returning " << std::boolalpha << write_locked << "; unlocking m_writers_cv_mutex...");
        bool exit_wait = write_locked;
#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
        if (!exit_wait)
          TPP;    // For the unlock of m_writers_cv_mutex.
#endif
        return exit_wait;
      });
      RWSLDout(dc::notice, "Left m_writers_cv.wait(); m_writers_cv_mutex is locked. Unlocking it...");
    }
    TPY; // Because we left the scope of the std::unique_lock.
    RWSLDout(dc::notice, "Leaving rd2wrlock()");
  }

  void rd2wryield()
  {
    RWSLDoutEntering(dc::notice, "rd2wryield()");
#ifndef DEBUG_RWSPINLOCK_THREADPERMUTER
    std::this_thread::yield();
#endif
    // Wait until C became zero again.
    {
      std::unique_lock<mutex_t> lk(m_writers_cv_mutex);
      TPY;
      RWSLDout(dc::notice, "Calling m_writers_cv.wait(lk, lambda)");
      m_writers_cv.wait(lk, [this](){
        RWSLDout(dc::notice, "Inside m_writers_cv.wait()'s lambda; m_writers_cv_mutex is locked.");
#if DEBUG_RWSPINLOCK
        int64_t state;
        bool leave_rd2wryield = !converting_writer_present(state = m_state.load(std::memory_order::relaxed));
        RWSLDout(dc::notice, "converting_writer_present(" << std::hex << std::setfill('0') << std::setw(16) << state << std::dec << ") = " << std::boolalpha << !leave_rd2wryield);
#else
        bool leave_rd2wryield = !converting_writer_present(m_state.load(std::memory_order::relaxed));
#endif
        bool exit_wait = leave_rd2wryield;
#ifdef DEBUG_RWSPINLOCK_THREADPERMUTER
        if (!exit_wait)
          TPP;    // For the unlock of m_writers_cv_mutex.
#endif
        // Since this returns false when there were converting writers present,
        // we must have m_writers_cv_mutex locked whenever we do a transition that causes
        // converting_writer_present to become false - and do a notify_all after that.
        return exit_wait;
      });
    }
    TPY; // Because we left the scope of the std::unique_lock.
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

#undef RWSLDout
#undef RWSLDoutEntering

// Absolutely ridiculous - but the whole class definition must be finished
// before you can use a static constexpr member function in a static_assert?!
struct AIReadWriteSpinLock_static_assert : AIReadWriteSpinLock
{
  static constexpr int64_t unlocked = 0;
  static constexpr int64_t one_waiting_converter = c;

  //===========================================================================================================
  // Test writer_present; this includes waiting writers: anything that should cause new rdlock()'s to block.

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
      writer_present(one_wrlock + one_wrlock + failed_wrlock + failed_wrlock),
      "writer_present logic error - depth 2 with two failures");

  // Test tripple transition.
  static_assert(
      writer_present(one_rdlock + one_wrlock + one_rd2wrlock),
      "writer_present logic error - depth 3");

  //===========================================================================================================
  // Test converting_or_actual_writer_present; this includes converting writers: anything that should cause new wrlock()'s to block.

  // Test single transitions.
  static_assert(
     !converting_or_actual_writer_present(one_rdlock) &&
      converting_or_actual_writer_present(one_wrlock),
      "converting_or_actual_writer_present logic error - depth 1");

  // Test double transitions.
  static_assert(
     !converting_or_actual_writer_present(one_rdlock + one_rdlock) &&
      converting_or_actual_writer_present(one_rdlock + one_wrlock) &&
      converting_or_actual_writer_present(one_wrlock + one_wrlock) &&
      converting_or_actual_writer_present(one_rdlock + one_rd2wrlock) &&
      converting_or_actual_writer_present(one_rdlock + one_rd2wrlock + successful_rd2wrlock),
      "converting_or_actual_writer_present logic error - depth 2");

  // Test single transitions with failure.
  static_assert(
     !converting_or_actual_writer_present(one_rdlock + failed_rdlock) &&
     !converting_or_actual_writer_present(one_wrlock + failed_wrlock),
      "converting_or_actual_writer_present logic error - depth 1 with failure");

  // Test double transitions with one failure.
  static_assert(
     !converting_or_actual_writer_present(one_rdlock + one_rdlock + failed_rdlock) &&
      converting_or_actual_writer_present(one_rdlock + one_wrlock + failed_rdlock) &&
     !converting_or_actual_writer_present(one_rdlock + one_wrlock + failed_wrlock) &&
      converting_or_actual_writer_present(one_wrlock + one_wrlock + failed_wrlock) &&
      converting_or_actual_writer_present(one_rdlock + one_rd2wrlock + failed_rd2wrlock),
      "converting_or_actual_writer_present logic error - depth 2 with one failure");

  // Test double transitions with two failures.
  static_assert(
     !converting_or_actual_writer_present(one_rdlock + one_rdlock + failed_rdlock + failed_rdlock) &&
     !converting_or_actual_writer_present(one_rdlock + one_wrlock + failed_rdlock + failed_wrlock) &&
     !converting_or_actual_writer_present(one_wrlock + one_wrlock + failed_wrlock + failed_wrlock),
      "converting_or_actual_writer_present logic error - depth 2 with two failures");

  // Test tripple transition.
  static_assert(
      converting_or_actual_writer_present(one_rdlock + one_wrlock + one_rd2wrlock),
      "writer_present logic error - depth 3");

  //===========================================================================================================
  // do_transition tests.

#ifndef DEBUG_STATIC_ASSERTS
  static_assert(test_removes_converting_or_actual_writer(), "removes_converting_or_actual_writer is broken!");
  static_assert(test_removes_writer(), "removes_writer is broken!");
  static_assert(test_removes_converting_writer(), "removes_converting_writer is broken!");
#else
  static void run_test_removes_converting_or_actual_writer() { test_removes_converting_or_actual_writer(); }
  static void run_test_removes_writer() { test_removes_writer(); }
  static void run_test_removes_converting_writer() { test_removes_converting_writer(); }
#endif
};

#endif

#ifdef DEBUG_STATIC_ASSERTS
// Compile as: g++ -x c++ -std=c++20 -I.. -I../cwds -DCWDEBUG -DDEBUG_STATIC_ASSERTS -include "sys.h" AIReadWriteSpinLock.h
int main()
{
  AIReadWriteSpinLock_static_assert::run_test_removes_converting_or_actual_writer();
  AIReadWriteSpinLock_static_assert::run_test_removes_writer();
  AIReadWriteSpinLock_static_assert::run_test_removes_converting_writer();
}
#endif

// Clean up macros.
#ifndef DEBUG_RWSPINLOCK_THREADPERMUTER
#undef TPY
#undef TPB
#undef TPP
#endif
#if (defined(__clang__) && __clang_major__ <= 14) || (defined(CWDEBUG) && defined(DEBUG_STATIC_ASSERTS))
#undef consteval
#endif
