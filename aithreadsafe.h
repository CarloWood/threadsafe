/**
 * @file aithreadsafe.h
 * @brief Implementation of AIThreadSafe.
 *
 * Copyright (c) 2010 - 2015, Aleric Inglewood.
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
 *   31/03/2010
 *   Initial version, written by Aleric Inglewood @ SL
 *
 *   14/03/2012
 *   Added AIThreadSafeSingleThread and friends.
 *   Added AIAccessConst (and derived AIAccess from it) to allow read
 *   access to a const AIThreadSafeSimple.
 *
 *   26/01/2013
 *   Added support for LLCondition to AIThreadSafeSimple.
 *
 *   24/02/2015
 *   Moved code from Singularity to separate repository.
 *   Changed the license to the GNU Affero General Public License.
 *   Did a major rewrite to make it more generic and use C++11
 *   std::thread support.
 */

// This file defines a wrapper template class (AIThreadSafe<T, Policy>) for
// arbitrary types T adding locking to the instance and shielding it from
// access without first being locked.
//
// Locking and getting access works by creating a scoped access object that
// takes the wrapper class as argument. Creating the access object obtains
// the lock, while destructing it releases the lock.
//
// There are three types of policies: ReadWrite, Primitive and OneThread.
// The latter doesn't use any mutex and doesn't do any locking, it does
// check however if all accesses are done by the same (one) thread.
//
// policy::ReadWrite is for use with the access classes:
// AIConstReadAccess, AIReadAccess and AIWriteAccess.
//
// policy::Primitive is for use with the access classes:
// AIAccessConst and AIAccess.
//
// policy::OneThread is for use with the access classes:
// AIOTAccessConst and AIOTAccess.
//
// For generality it is advised to always make the distincting between
// read-only access and read/write access, even for the primitive (and
// one thread) policies. The typical declaration of a ThreadSafe object
// should include a typedef. For example:
//
// typedef AIThreadSafe<MyData, policy::Primitive<std::mutex>> mydata_t;
// mydata_t data;
//
// After which the following access types can be used:
//
// mydata_t::crat : const read access type
// mydata_t::rat  : read access type
// mydata_::wat  : (read/)write access type.
//
// crat provides read access to a const AIThreadSafe.
// rat provides read access to a non-const AIThreadSafe (as well as const).
// wat provides read/write access to a non-const AIThreadSafe.
//
// The AIThreadSafe wrapper allows its wrapped object to be constructed
// with arbitrary parameters by using operator new with placement;
// for example, to instantiate a class Foo with read/write locking:
//
// typedef AIThreadSafe<Foo, policy::ReadWrite<AIReadWriteMutex>> foo_t;
// foo_t foo(new (foo.storage()) Foo(param1, param2, ...));
//
// Default and a general one-parameter constructor are provided
// directly however. For example:
//
// foo_t foo;		// Default constructed Foo.
// foo_t foo(3.4);	// Foo with one constructor parameter.

#ifndef AITHREADSAFE_H
#define AITHREADSAFE_H

#include "aithreadid.h"

#include <new>
#include <cstddef>
#include <memory>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <mutex>
#include <condition_variable>

#define THREADSAFE_DEBUG defined(CWDEBUG)

namespace thread_safe
{

template<typename T>
class Bits
{
  private:
    // AIThreadSafe is a wrapper around an instance of T.
    // Because T might not have a default constructor, it is constructed
    // 'in place', with placement new, in the storage reserved here.
    //
    // Properly aligned uninitialized storage for T.
    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage;

  public:
    // The wrapped objects are constructed in-place with placement new *outside*
    // of this object (by AITHREADSAFE macro(s) or derived classes).
    // However, we are responsible for the destruction of the wrapped object.
    ~Bits() { ptr()->~T(); }

    // Only for use by AITHREADSAFE, see below.
    void* storage() const { return std::addressof(m_storage); }

    // Cast a T* back to Bits<T>. This is the inverse of storage().
    // This assumes that addressof(m_storage) == this, in storage().
    static Bits<T>* wrapper_cast(T* ptr) { return reinterpret_cast<Bits<T>*>(ptr); }
    static Bits<T> const* wrapper_cast(T const* ptr) { return reinterpret_cast<Bits<T> const*>(ptr); }

  protected:
    // Accessors.
    T const* ptr() const { return static_cast<T const*>(std::addressof(m_storage)); }
    T* ptr() { return reinterpret_cast<T*>(std::addressof(m_storage)); }
};

template<class WRAPPER> struct ConstReadAccess;
template<class WRAPPER> struct ReadAccess;
template<class WRAPPER> struct WriteAccess;
template<class WRAPPER> struct AccessConst;
template<class WRAPPER> struct Access;
template<class WRAPPER> struct OTAccessConst;
template<class WRAPPER> struct OTAccess;

namespace policy
{

template<class RWMUTEX>
class ReadWrite
{
  protected:
    template<class WRAPPER>
    struct access_types
    {
      typedef ConstReadAccess<WRAPPER> read_access_const_type;
      typedef ReadAccess<WRAPPER> read_access_type;
      typedef WriteAccess<WRAPPER> write_access_type;
    };

    template<class WRAPPER> friend struct ConstReadAccess;
    template<class WRAPPER> friend struct ReadAccess;
    template<class WRAPPER> friend struct WriteAccess;

    RWMUTEX m_read_write_mutex;

  public:
    void rd2wryield(void) { m_read_write_mutex.rd2wryield(); }
};

template<class MUTEX>
class Primitive
{
  protected:
    template<class WRAPPER>
    struct access_types
    {
      typedef AccessConst<WRAPPER> read_access_const_type;
      typedef Access<WRAPPER> read_access_type;
      typedef Access<WRAPPER> write_access_type;
    };

    template<class WRAPPER> friend struct AccessConst;
    template<class WRAPPER> friend struct Access;

    MUTEX m_primitive_mutex;
};

class OneThread
{
  protected:
    template<class WRAPPER>
    struct access_types
    {
      typedef OTAccessConst<WRAPPER> read_access_const_type;
      typedef OTAccess<WRAPPER> read_access_type;
      typedef OTAccess<WRAPPER> write_access_type;
    };

#if THREADSAFE_DEBUG
    mutable std::thread::id m_thread_id;
#endif // THREADSAFE_DEBUG
};

} // namespace policy
} // namespace thread_safe

/**
 * @brief A wrapper class for objects that need to be accessed by more than one thread, allowing concurrent readers.
 *
 * For example,
 *
 * <code>
 * class Foo { public: Foo(int, int); };
 *
 * using namespace thread_safe;
 *
 * typedef AIThreadSafe<Foo, policy::ReadWrite<AIReadWriteMutex>> foo_t;
 * foo_t foo(new (foo.storage()) Foo(2, 3));
 *
 * foo_t::rat foo_r(foo);
 * // Use foo_r-> for read access.
 *
 * foo_t::wat foo_w(foo);
 * // Use foo_w-> for write access.
 * </code>
 *
 * If <code>foo</code> is constant, you have to use <code>crat</code>.
 *
 * It is possible to pass access objects to a function that
 * downgrades the access, for example:
 *
 * <code>
 * void readfunc(foo_t::rat const& read_access);
 *
 * foo_t::wat foo_w(foo);
 * readfunc(foo_w);	// readfunc will perform read access to foo_w.
 * </code>
 *
 * If a <code>rat</code> object is non-const, you can upgrade the access by
 * creating a <code>wat</code> object from it. For example:
 *
 * <code>
 * foo_t::wat foo_w(foo_r);
 * </code>
 *
 * This API is Robust(tm). If you try anything that could result in problems,
 * it simply won't compile. The only mistake you can still easily make is
 * to obtain write access to an object when it is not needed, or to unlock
 * an object in between accesses while the state of the object should be
 * preserved. For example:
 *
 * <code>
 * // This resets foo to point to the first file and then returns that.
 * std::string filename = foo_t::wat(foo)->get_first_filename();
 *
 * // WRONG! The state between calling get_first_filename and get_next_filename should be preserved!
 *
 * foo_t::wat foo_w(foo);	// Wrong. The code below only needs read-access.
 * while (!filename.empty())
 * {
 *   something(filename);
 *   filename = foo_w->next_filename();
 * }
 * </code>
 *
 * Correct would be
 *
 * <code>
 * foo_t::rat foo_r(foo);
 * std::string filename = foo_t::wat(foo_r)->get_first_filename();
 * while (!filename.empty())
 * {
 *   something(filename);
 *   filename = foo_r->next_filename();
 * }
 * </code>
 */
template<typename T, typename POLICY_MUTEX>
class AIThreadSafe : public thread_safe::Bits<T>, public POLICY_MUTEX
{
  public:
    // The access types.
    typedef typename POLICY_MUTEX::template access_types<AIThreadSafe<T, POLICY_MUTEX>>::read_access_const_type crat;
    typedef typename POLICY_MUTEX::template access_types<AIThreadSafe<T, POLICY_MUTEX>>::read_access_type rat;
    typedef typename POLICY_MUTEX::template access_types<AIThreadSafe<T, POLICY_MUTEX>>::write_access_type wat;

  protected:
    // Only these may access the object (through ptr()).
    friend crat;
    friend rat;
    friend wat;

    typedef T data_type;
    typedef POLICY_MUTEX policy_type;

  public:
    // Allow arbitrary parameters to be passed for construction.
    template<typename... ARGS>
    AIThreadSafe(ARGS... args)
#if THREADSAFE_DEBUG
      : m_ref(0)
#endif // THREADSAFE_DEBUG
    {
      new (thread_safe::Bits<T>::ptr()) T(args ...);
    }

#if THREADSAFE_DEBUG
  private:
    std::atomic<int> m_ref;

  public:
    // Can only be locked when there still exists an Access object that
    // references this object and will access it upon destruction.
    // If the assertion fails, make sure that such Access object is
    // destructed before the deletion of this object.
    ~AIThreadSafe()
    {
      assert(m_ref == 0);
    }
#endif
};

namespace thread_safe
{

/**
 * @brief Read lock object and provide read access.
 */
template<class WRAPPER>
struct ConstReadAccess
{
  public:
    //! Internal enum for the lock-type of the Access object.
    enum state_type
    {
      readlocked,	//!< A ConstReadAccess or ReadAccess.
      read2writelocked,	//!< A WriteAccess constructed from a ReadAccess.
      writelocked,	//!< A WriteAccess constructed from a ThreadSafe.
      write2writelocked	//!< A WriteAccess constructed from (the ReadAccess base class of) a WriteAccess.
    };

    //! Construct a ConstReadAccess from a constant AIThreadSafe.
    ConstReadAccess(WRAPPER const& wrapper) : m_wrapper(const_cast<WRAPPER&>(wrapper)), m_state(readlocked)
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref++;
#endif // THREADSAFE_DEBUG
      m_wrapper.m_read_write_mutex.rdlock();
    }

    //! Destruct the Access object.
    // These should never be dynamically allocated, so there is no need to make this virtual.
    ~ConstReadAccess()
    {
      if (m_state == readlocked)
	m_wrapper.m_read_write_mutex.rdunlock();
      else if (m_state == writelocked)
	m_wrapper.m_read_write_mutex.wrunlock();
      else if (m_state == read2writelocked)
	m_wrapper.m_read_write_mutex.wr2rdlock();
#if THREADSAFE_DEBUG
      m_wrapper.m_ref--;
#endif // THREADSAFE_DEBUG
    }

    //! Access the underlaying object for read access.
    typename WRAPPER::data_type const* operator->() const { return m_wrapper.ptr(); }

    //! Access the underlaying object for read access.
    typename WRAPPER::data_type const& operator*() const { return *m_wrapper.ptr(); }

  protected:
    //! Constructor used by ReadAccess.
    ConstReadAccess(WRAPPER& wrapper, state_type state) : m_wrapper(wrapper), m_state(state)
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref++;
#endif // THREADSAFE_DEBUG
    }

    WRAPPER& m_wrapper;		//!< Reference to the object that we provide access to.
    state_type const m_state;	//!< The lock state that m_wrapper is in.

  private:
    // Disallow copy constructing directly.
    ConstReadAccess(ConstReadAccess const&);
};

/**
 * @brief Read lock object and provide read access, with possible promotion to write access.
 */
template<class WRAPPER>
struct ReadAccess : public ConstReadAccess<WRAPPER>
{
  public:
    typedef typename ConstReadAccess<WRAPPER>::state_type state_type;
    using ConstReadAccess<WRAPPER>::readlocked;

    //! Construct a ReadAccess from a non-constant AIThreadSafe.
    ReadAccess(WRAPPER& wrapper) : ConstReadAccess<WRAPPER>(wrapper, readlocked)
    {
      this->m_wrapper.m_read_write_mutex.rdlock();
    }

  protected:
    //! Constructor used by WriteAccess.
    ReadAccess(WRAPPER& wrapper, state_type state) : ConstReadAccess<WRAPPER>(wrapper, state) { }

    friend struct WriteAccess<WRAPPER>;
};

/**
 * @brief Write lock object and provide read/write access.
 */
template<class WRAPPER>
struct WriteAccess : public ReadAccess<WRAPPER>
{
  public:
    using ConstReadAccess<WRAPPER>::readlocked;
    using ConstReadAccess<WRAPPER>::read2writelocked;
    using ConstReadAccess<WRAPPER>::writelocked;
    using ConstReadAccess<WRAPPER>::write2writelocked;

    //! Construct a WriteAccess from a non-constant AIThreadSafe.
    WriteAccess(WRAPPER& wrapper) : ReadAccess<WRAPPER>(wrapper, writelocked) { this->m_wrapper.m_read_write_mutex.wrlock();}

    //! Promote read access to write access.
    explicit WriteAccess(ReadAccess<WRAPPER>& access) :
        ReadAccess<WRAPPER>(access.m_wrapper, (access.m_state == readlocked) ? read2writelocked : write2writelocked)
    {
      if (this->m_state == read2writelocked)
      {
	this->m_wrapper.m_read_write_mutex.rd2wrlock();
      }
    }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type& operator*() const { return *this->m_wrapper.ptr(); }
};

/**
 * @brief Write lock object and provide read access.
 */
template<class WRAPPER>
struct AccessConst
{
    //! Construct a AccessConst from a constant AIThreadSafe.
    AccessConst(WRAPPER const& wrapper) : m_wrapper(const_cast<WRAPPER&>(wrapper))
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref++;
#endif // THREADSAFE_DEBUG
      this->m_wrapper.m_primitive_mutex.lock();
    }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type const* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type const& operator*() const { return *this->m_wrapper.ptr(); }

    ~AccessConst()
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref--;
#endif // THREADSAFE_DEBUG
      this->m_wrapper.m_primitive_mutex.unlock();
    }

    // If m_primitive_mutex is a Condition, then this can be used to wait for a signal.
    void wait() { this->m_wrapper.m_primitive_mutex.wait(); }
    // If m_primitive_mutex is a Condition then this can be used to wake up the waiting thread.
    void signal() { this->m_wrapper.m_primitive_mutex.signal(); }

  protected:
    WRAPPER& m_wrapper;		//!< Reference to the object that we provide access to.

  private:
    // Disallow copy constructing directly.
    AccessConst(AccessConst const&);
};

/**
 * @brief Write lock object and provide read/write access.
 */
template<class WRAPPER>
struct Access : public AccessConst<WRAPPER>
{
  public:
    //! Construct a Access from a non-constant AIThreadSafe.
    Access(WRAPPER& wrapper) : AccessConst<WRAPPER>(wrapper) { }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type& operator*() const { return *this->m_wrapper.ptr(); }
};

/**
 * @brief Access single threaded object for read access.
 */
template<class WRAPPER>
struct OTAccessConst
{
  public:
    //! Construct a OTAccessConst from a constant AIThreadSafe.
    OTAccessConst(WRAPPER const& wrapper) : m_wrapper(const_cast<WRAPPER&>(wrapper))
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref++;
      assert(is_single_threaded(wrapper.m_thread_id));
#endif // THREADSAFE_DEBUG
    }

#if THREADSAFE_DEBUG
    ~OTAccessConst()
    {
      m_wrapper.m_ref--;
    }
#endif // THREADSAFE_DEBUG

    //! Access the underlaying object for read access.
    typename WRAPPER::data_type const* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for read write access.
    typename WRAPPER::data_type const& operator*() const { return *this->m_wrapper.ptr(); }

  protected:
    WRAPPER& m_wrapper;		//!< Reference to the object that we provide access to.

  private:
    // Disallow copy constructing directly.
    OTAccessConst(OTAccessConst const&);
};

/**
 * @brief Access single threaded object for read/write access.
 */
template<class WRAPPER>
struct OTAccess : public OTAccessConst<WRAPPER>
{
  public:
    //! Construct a OTAccess from a non-constant AIThreadSafe.
    OTAccess(WRAPPER& wrapper) : OTAccessConst<WRAPPER>(wrapper) { }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type& operator*() const { return *this->m_wrapper.ptr(); }
};

} // namespace thread_safe

#endif // AITHREADSAFE_H
