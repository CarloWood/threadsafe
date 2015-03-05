/**
 * @file aithreadsafe.h
 * @brief Implementation of the aithreadsafe namespace.
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
 *   std::thread support: now only one AIThreadSafe class is left,
 *   everything else is in the namespace thread_safe. Introduced
 *   the policy classes.
 *
 *   03/03/2015
 *   Renamed thread_safe to aithreadsafe and
 *   renamed AIThreadSafe to aithreadsafe::Wrapper.
 */

// This file defines a wrapper template class for arbitrary types T
// (aithreadsafe::Wrapper<T, PolicyMutex>) adding a mutex and locking
// policy (through PolicyMutex) to the instance and shielding it from
// access without first being locked.
//
// Locking and getting access works by creating a scoped "access object"
// whose constructor takes the wrapper object as argument. Creating the
// access object obtains the lock, while destructing it releases the lock.
//
// There are three types of policies: ReadWrite, Primitive and OneThread.
// The latter doesn't use any mutex and doesn't do any locking, it does
// however check that all accesses are done by the same (one) thread.
//
// policy::ReadWrite<RWMUTEX> allows read/write locking. RWMUTEX needs
// to provide the following member functions: rdlock, rdunlock, wrlock,
// wrunlock, wr2rdlock, rd2wrlock and rd2wryield.
//
// policy::Primitive<MUTEX> allows primitive locking. MUTEX needs to
// provide the following member functions: lock and unlock.
//
// policy::OneThread does no locking but allows testing that an object
// is really only accessed by a single thread (in debug mode).
//
// For generality it is advised to always make the distincting between
// read-only access and read/write access, even for the primitive (and
// one thread) policies.
//
// The typical declaration of a Wrapper object should involve a typedef.
// For example:
//
// typedef aithreadsafe::Wrapper<MyData, aithreadsafe::policy::Primitive<std::mutex>> mydata_t;
// mydata_t data;
//
// After which the following access types can be used:
//
// mydata_t::crat : Const Read Access Type (cannot be converted to a wat).
// mydata_t::rat  : Read Access Type.
// mydata_t::wat  : (read/)Write Access Type.
//
// crat (const read access type) provides read-only access to a const Wrapper
// and cannot be converted to a wat (write access type).
//
// rat (read access type) provides read access to a non-const Wrapper and can
// be converted to a wat; however such conversion can throw a std::exception.
// If that happens then the rat must be destroyed (catch the exception outside
// of its scope) followed by calling rd2wryield(). After that one can loop
// back and recreate the rat. See the documentation of Wrapper for more
// details.
//
// wat (write access type) provides (read and) write access to a non-const
// Wrapper. It can safely be converted to a rat, for example by passing it
// to a function that takes a rat, but that doesn't release the write lock
// of course. The write lock is only released when the wat is destructed.
//
// The need to start with a write lock which then needs to be converted to
// a read lock gives rise to a third layer: the w2rCarry (write->read carry).
// A w2rCarry cannot be used to access the underlaying data, nor does it contain
// a mutex itself, but it allows one to convert a write access into a read
// access without the risk of having an exception being thrown. See the
// documentation of Wrapper for more details.

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

namespace aithreadsafe
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

/**
 * @brief A wrapper class for objects that need to be accessed by more than one thread, allowing concurrent readers.
 *
 * For example,
 *
 * <code>
 * class Foo { public: Foo(int, int); };	// Some user defined type.
 * typedef aithreadsafe::Wrapper<Foo, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>> foo_t;	// Wrapper type for Foo.
 * foo_t foo(2, 3);				// Instantiation of a Foo(2, 3).
 *
 * {
 *   foo_t::rat foo_r(foo);			// Scoped read-lock for foo.
 *   // Use foo_r-> for read access (returns a Foo const*).
 * }
 *
 * {
 *   foo_t::wat foo_w(foo);			// Scoped write-lock for foo.
 *   // Use foo_w-> for write access (returns a Foo*).
 * }
 * </code>
 *
 * If <code>foo</code> is constant, you have to use <code>crat</code>.
 *
 * It is possible to pass access objects to a function that
 * downgrades the access (wat -> rat -> crat (crat is a base
 * class of rat which in turn is a base class of wat)).
 *
 * For example:
 *
 * <code>
 * void readfunc(foo_t::crat const& read_access);
 *
 * foo_t::wat foo_w(foo);
 * readfunc(foo_w);	// readfunc will perform read access on foo_w
 *                      // (without releasing the write lock!).
 * </code>
 *
 * It is therefore highly recommended to use <code>f(foo_t::crat const& foo_r)</code>
 * as signature for functions that only read foo, unless that function (sometimes)
 * needs to convert its argument to a wat for writing. The latter implies that
 * it might throw a std::exception however (at least that is what AIReadWriteMutex
 * does when two threads call rd2wrlock() simultaneously), in which case the user
 * has to call rd2wryield() (after destruction of all access objects).
 *
 * For example,
 *
 * <code>
 * typedef aithreadsafe::Wrapper<Foo, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>> foo_t;
 * foo_t foo;
 *
 * void f(foo_t::rat& foo_r)		// Sometimes need to write to foo_r.
 * {
 *   // Read access here.
 *   foo_t::wat foo_w(foo_r);		// This might throw.
 *   // Write access here.
 * }
 *
 * ...
 *   for(;;)
 *   {
 *     try
 *     {
 *       foo_t::rat foo_r(foo);		// This must be inside the try block.
 *       // Read access here.
 *       f(foo_r);			// This might throw.
 *       // Read access here.
 *       foo_t::wat foo_w(foo_r);	// This might throw.
 *       // Write access here.
 *     }
 *     catch (std::exception const&)
 *     {
 *       foo.rd2wryield();
 *       continue;
 *     }
 *     break;
 *   }
 * </code>
 *
 * Note that you can only upgrade a read access type (<code>rat</code>) to a
 * write access type (<code>wat</code>) when the rat is non-const.
 *
 * To summarize, the following function arguments make sense:
 *
 * <code>
 * void f(foo_t::crat const& foo_r);	// Only ever reads.
 * void f(foo_t::rat& foo_r);		// Mostly reads, but sometimes acquires write access in some code path (which might throw).
 * void f(foo_t::wat const& foo_w);	// Writes (most likely, not necessarily always of course).
 * </code>
 *
 * This API is pretty robust; if you try anything that could result in problems
 * it simply won't compile. The only mistake one can still easily make is
 * to obtain write access to an object when that is not needed, or to unlock
 * an object in between accesses while the state of the object should be
 * preserved. For example:
 *
 * <code>
 * // This resets foo to point to the first file and then returns that.
 * std::string filename = foo_t::wat(foo)->get_first_filename();
 *
 * // WRONG! The foo_t::wat is destructed and thus foo becomes unlocked,
 *           but the state between calling get_first_filename and
 *           get_next_filename should be preserved!
 *
 * foo_t::wat foo_w(foo);	// Wrong. The code below only needs read-access.
 * while (!filename.empty())
 * {
 *   something(filename);
 *   filename = foo_w->next_filename();
 * }
 * </code>
 *
 * Where we assume that next_filename is a const member function (using
 * a mutable internally or something). The only-needs-read-access problem
 * can easily be solved by changing the second wat into a rat of course.
 * But to keep the object locked while going from write access to read
 * access we'd have to do the following:
 *
 * <code>
 * for(;;)
 * {
 *   try
 *   {
 *     foo_t::rat foo_r(foo);
 *     std::string filename = foo_t::wat(foo_r)->get_first_filename();
 *     while (!filename.empty())
 *     {
 *       something(filename);
 *       filename = foo_r->next_filename();
 *     }
 *   }
 *   catch()
 *   {
 *     foo.rd2wryield();
 *     continue;
 *   }
 *   break;
 * }
 * </code>
 *
 * And while for most practical cases this will perform perfectly,
 * it is slightly annoying that the construction of the wat from
 * foo_r can throw an exception while we did even use the foo_r
 * yet!
 *
 * If this is the case (no need for read access before the write)
 * then one can do the following instead:
 *
 * <code>
 * foo_t::w2rCarry carry(foo);
 * std::string filename = foo_t::wat(carry)->get_first_filename();
 * foo_t::rat foo_r(carry);
 * while (!filename.empty())
 * {
 *   something(filename);
 *   filename = foo_r->next_filename();
 * }
 * </code>
 *
 * where the construction of the carry does not obtain a lock,
 * but causes the object to remain read-locked after the destruction
 * of the wat that it was passed to. Passing it subsequently to
 * a rat then allows the user to perform read access.
 *
 * A w2rCarry must be immediately passed to a wat (as opposed to to
 * a rat) and can subsequently be passed to one or more rat objects
 * (one will do) in order to access the object read-only. It is not
 * possible to pass the carry to a second wat object as that would
 * still require actual read to write locking and therefore could
 * throw anyway: if that is needed then just use the try / catch
 * block approach.
 */
template<typename T, typename POLICY_MUTEX>
class Wrapper : public aithreadsafe::Bits<T>, public POLICY_MUTEX
{
  public:
    // The access types.
    typedef typename POLICY_MUTEX::template access_types<Wrapper<T, POLICY_MUTEX>>::const_read_access_type crat;
    typedef typename POLICY_MUTEX::template access_types<Wrapper<T, POLICY_MUTEX>>::read_access_type       rat;
    typedef typename POLICY_MUTEX::template access_types<Wrapper<T, POLICY_MUTEX>>::write_access_type      wat;
    typedef typename POLICY_MUTEX::template access_types<Wrapper<T, POLICY_MUTEX>>::write_to_read_carry    w2rCarry;

  protected:
    // Only these may access the object (through ptr()).
    friend crat;
    friend rat;
    friend wat;
    friend w2rCarry;

    typedef T data_type;
    typedef POLICY_MUTEX policy_type;

  public:
    // Allow arbitrary parameters to be passed for construction.
    template<typename... ARGS>
    Wrapper(ARGS... args)
#if THREADSAFE_DEBUG
      : m_ref(0)
#endif // THREADSAFE_DEBUG
    {
      new (aithreadsafe::Bits<T>::ptr()) T(args ...);
    }

#if THREADSAFE_DEBUG
  private:
    std::atomic<int> m_ref;

  public:
    // Can only be locked when there still exists an Access object that
    // references this object and will access it upon destruction.
    // If the assertion fails, make sure that such Access object is
    // destructed before the deletion of this object.
    ~Wrapper()
    {
      assert(m_ref == 0);
    }
#endif
};

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
      write2writelocked,//!< A WriteAccess constructed from (the ReadAccess base class of) a WriteAccess.
      carrylocked	//!< A ReadAccess constructed from a Write2ReadCarry.
    };

    //! Construct a ConstReadAccess from a constant Wrapper.
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

template<class WRAPPER> struct ReadAccess;
template<class WRAPPER> struct WriteAccess;

/**
 * @brief Allow to carry the read access from a wat to a rat.
 */
template<class WRAPPER>
class Write2ReadCarry
{
  private:
    WRAPPER& m_wrapper;
    bool m_used;

  public:
    Write2ReadCarry(WRAPPER& wrapper) : m_wrapper(wrapper), m_used(false)
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref++;
#endif // THREADSAFE_DEBUG
    }
    ~Write2ReadCarry()
    {
#if THREADSAFE_DEBUG
      m_wrapper.m_ref--;
#endif // THREADSAFE_DEBUG
      if (m_used)
	m_wrapper.m_read_write_mutex.rdunlock();
    }

    friend struct WriteAccess<WRAPPER>;
    friend struct ReadAccess<WRAPPER>;
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
    using ConstReadAccess<WRAPPER>::carrylocked;

    //! Construct a ReadAccess from a non-constant Wrapper.
    ReadAccess(WRAPPER& wrapper) : ConstReadAccess<WRAPPER>(wrapper, readlocked)
    {
      this->m_wrapper.m_read_write_mutex.rdlock();
    }

    //! Construct a ReadAccess from a Write2ReadCarry object containing an read locked Wrapper. Upon destruction leave the Wrapper read locked.
    explicit ReadAccess(Write2ReadCarry<WRAPPER> const& w2rc) : ConstReadAccess<WRAPPER>(w2rc.m_wrapper, carrylocked)
    {
      assert(w2rc.m_used); // Always pass a w2rCarry to a wat first.
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

    //! Construct a WriteAccess from a non-constant Wrapper.
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

    //! Construct a WriteAccess from a Write2ReadCarry object containing an unlocked Wrapper. Upon destruction leave the Wrapper read locked.
    explicit WriteAccess(Write2ReadCarry<WRAPPER>& w2rc) : ReadAccess<WRAPPER>(w2rc.m_wrapper, read2writelocked)
    {
      assert(!w2rc.m_used); // Always pass a w2rCarry to the wat first. There can only be one wat.
      w2rc.m_used = true;
      this->m_wrapper.m_read_write_mutex.wrlock();
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
    //! Construct a AccessConst from a constant Wrapper.
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
    //! Construct a Access from a non-constant Wrapper.
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
    //! Construct a OTAccessConst from a constant Wrapper.
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
    //! Construct a OTAccess from a non-constant Wrapper.
    OTAccess(WRAPPER& wrapper) : OTAccessConst<WRAPPER>(wrapper) { }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type* operator->() const { return this->m_wrapper.ptr(); }

    //! Access the underlaying object for (read and) write access.
    typename WRAPPER::data_type& operator*() const { return *this->m_wrapper.ptr(); }
};

namespace policy
{

template<class RWMUTEX>
class ReadWrite
{
  protected:
    template<class WRAPPER>
    struct access_types
    {
      typedef ConstReadAccess<WRAPPER> const_read_access_type;
      typedef ReadAccess<WRAPPER> read_access_type;
      typedef WriteAccess<WRAPPER> write_access_type;
      typedef Write2ReadCarry<WRAPPER> write_to_read_carry;
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
      typedef AccessConst<WRAPPER> const_read_access_type;
      typedef Access<WRAPPER> read_access_type;
      typedef Access<WRAPPER> write_access_type;
      typedef void write_to_read_carry;
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
      typedef OTAccessConst<WRAPPER> const_read_access_type;
      typedef OTAccess<WRAPPER> read_access_type;
      typedef OTAccess<WRAPPER> write_access_type;
      typedef void write_to_read_carry;
    };

#if THREADSAFE_DEBUG
    mutable std::thread::id m_thread_id;
#endif // THREADSAFE_DEBUG
};

} // namespace policy
} // namespace aithreadsafe

#endif // AITHREADSAFE_H
