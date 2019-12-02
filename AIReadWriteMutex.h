/**
 * @file
 * @brief Implementation of AIReadWriteMutex.
 *
 * @Copyright (C) 2010, 2016, 2017  Carlo Wood.
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
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   2015/03/01
 *   - Moved code from Singularity to separate repository.
 *   - Changed the license to the GNU Affero General Public License.
 *   - Major rewrite to make it more generic and use C++11 thread support.
 *
 *   2016/12/17
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <exception>

class AIReadWriteMutex
{
  public:
    AIReadWriteMutex() : m_readers_count(0), m_waiting_writers(0), m_rd2wr_count(0) { }

  private:
    std::mutex m_state_mutex;					///< Guards all member variables below.
    std::condition_variable m_condition_unlocked;		///< Condition variable used to wait for no readers or writers left (to tell waiting writers).
    std::condition_variable m_condition_no_writer_left;		///< Condition variable used to wait for no writers left (to tell waiting readers).
    std::condition_variable m_condition_one_reader_left;	///< Condition variable used to wait for one reader left (to tell that reader that it can become a writer).
    std::condition_variable m_condition_rd2wr_count_zero;	///< Condition variable used to wait until m_rd2wr_count is zero.
    int m_readers_count;					///< Number of readers or -1 if a writer locked this object.
    int m_waiting_writers;					///< Number of threads that are waiting for a write lock. Used to block readers from waking up.
    int m_rd2wr_count;						///< Number of threads that try to go from a read lock to a write lock.

  public:
    void rdlock()
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      m_condition_no_writer_left.wait(lk, [this]{return m_readers_count >= 0;});	// Wait till m_readers_count is no longer -1.
      ++m_readers_count;								// One more reader.
    }

    void rdunlock()
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      if (--m_readers_count <= 1)							// Decrease reader count. Was this the (second) last reader?
      {
	bool one_reader_left = m_readers_count == 1;
	// In most practical cases there are no race conditions, so it is more efficient to first unlock m_state_mutex and only then kick waiting threads:
	// if we did that the other way around then threads woken up would immediately block again on trying to obtain m_state_mutex before leaving wait().
	// In the case that we try to wake up threads who can't be woken up because another thread now locked this object then the wait predicate will stop
	// them from being woken up.
	lk.unlock();

	if (one_reader_left)								// Still one reader left, so only notify m_condition_one_reader_left.
	  m_condition_one_reader_left.notify_one();
	else
	  m_condition_unlocked.notify_one();						// No readers left, tell waiting writers.
      }
    }

    void wrlock()
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      ++m_waiting_writers;								// Stop readers from being woken up.
      m_condition_unlocked.wait(lk, [this]{return m_readers_count == 0;});		// Wait untill m_readers_count is 0 (nobody else has the lock).
      --m_waiting_writers;
      m_readers_count = -1;								// We are a writer now.
    }

    void rd2wrlock()
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      if (++m_rd2wr_count > 1)								// Only the first thread that calls rd2wrlock will get passed this.
      {
	--m_rd2wr_count;
	// It is impossible to recover from this: two threads have a read lock
	// and require to turn that into a write lock. The only way out of this
	// is to throw an exception and let the caller solve the mess.
	// To solve this situation, either only use rd2wrlock() from a single
	// thread (other threads may still call the other member functions), or
	// catch this exception and call rdunlock() and then rd2wryield().
	// After that a new attempt can be made.
	throw std::exception();
      }
      ++m_waiting_writers;								// Stop readers from being woken up.
      m_condition_one_reader_left.wait(lk, [this]{return m_readers_count == 1;});	// Wait till m_readers_count is 1 (only this thead has its read lock).
      --m_waiting_writers;
      m_readers_count = -1;								// We are a writer now.
      if (--m_rd2wr_count == 0)
	m_condition_rd2wr_count_zero.notify_one();					// Allow additional calls to rd2wrlock().
    }

    void rd2wryield()
    {
      std::this_thread::yield();
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      m_condition_rd2wr_count_zero.wait(lk, [this]{return m_rd2wr_count == 0;});
    }

    void wrunlock()
    {
      m_state_mutex.lock();								// Get exclusive access.
      m_readers_count = 0;								// We have no writer anymore.
      int waiting_writer = m_waiting_writers;
      m_state_mutex.unlock();								// Release m_state_mutex so that threads can leave their respective wait() immediately.

      if (waiting_writer)
	m_condition_unlocked.notify_one();						// Tell waiting writers.
      else
	m_condition_no_writer_left.notify_all();					// Tell waiting readers.
    }

    void wr2rdlock()
    {
      m_state_mutex.lock();								// Get exclusive access.
      m_readers_count = 1;								// Turn writer into a reader.
      int waiting_writer = m_waiting_writers;
      m_state_mutex.unlock();								// Release m_state_mutex so that threads can leave their respective wait() immediately.

      // Don't call m_condition_one_reader_left.notify_one() because it is impossible that any thread
      // is waiting there: they'd need to have been a reader before and that is not allowed while
      // we had the write lock.
      if (!waiting_writer)
	m_condition_no_writer_left.notify_all();					// Tell waiting readers.
    }
};
