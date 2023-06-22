/**
 * threadsafe -- Threading utilities: object oriented (read/write) locking and more.
 *
 * @file
 * @brief Implementation of the threadsafe namespace.
 *
 * @Copyright (C) 2010, 2016, 2017  Carlo Wood.
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
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   2010/03/31
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2012/03/14
 *   - Added AIThreadSafeSingleThread and friends.
 *   - Added AIAccessConst (and derived AIAccess from it) to allow read
 *     access to a const AIThreadSafeSimple.
 *
 *   2013/01/26
 *   - Added support for Condition to AIThreadSafeSimple.
 *
 *   2015/02/24
 *   - Moved code from Singularity to separate repository.
 *   - Changed the license to the GNU Affero General Public License.
 *   - Did a major rewrite to make it more generic and use C++11
 *     std::thread support: now only one AIThreadSafe class is left,
 *     everything else is in the namespace thread_safe.
 *   - Introduced the policy classes.
 *
 *   2015/03/03
 *   - Renamed thread_safe to aithreadsafe and renamed AIThreadSafe
 *     to threadsafe::Wrapper.
 *
 *   2016/12/17
 *   - Transfered copyright to Carlo Wood.
 *
 *   2023/06/10
 *   - Renamed the repository from ai-threadsafe to threadsafe.
 *   - Renamed namespace aithreadsafe to threadsafe.
 *   - Renamed aithreadsafe.h to threadsafe.h.
 *   - Renamed Wrapper to Unlocked.
 *   - Renamed wrapper_cast to unlocked_cast.
 *
 *   2023/06/12
 *   - Unlocked no longer takes align and blocksize as template arguments.
 *   - Unlocked is now derived from protected from T instead of obscuring with Bits.
 */

// This file defines a wrapper template class for arbitrary types T
// (threadsafe::Unlocked<T, PolicyMutex>) adding a mutex and locking
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
// The typical declaration of an Unlocked object should involve a type alias.
// For example:
//
// using mydata_t = threadsafe::Unlocked<MyData, threadsafe::policy::Primitive<AIMutex>>;
// mydata_t data;
//
// After which the following access types can be used:
//
// mydata_t::crat : Const Read Access Type (cannot be converted to a wat).
// mydata_t::rat  : Read Access Type.
// mydata_t::wat  : (read/)Write Access Type.
//
// crat (const read access type) provides read-only access to a const Unlocked
// wrapper and cannot be converted to a wat (write access type).
//
// rat (read access type) provides read access to a non-const Unlocked wrapper
// and can be converted to a wat; however such conversion can throw a std::exception.
// If that happens then the rat must be destroyed (catch the exception outside
// of its scope) followed by calling rd2wryield(). After that one can loop
// back and recreate the rat. See the documentation of Unlocked for more details.
//
// wat (write access type) provides (read and) write access to a non-const
// Unlocked wrapper. It can safely be converted to a rat, for example by passing it
// to a function that takes a rat, but that doesn't release the write lock
// of course. The write lock is only released when the wat is destructed.
//
// The need to start with a write lock which then needs to be converted to
// a read lock gives rise to a third layer: the w2rCarry (write->read carry).
// A w2rCarry cannot be used to access the underlaying data, nor does it contain
// a mutex itself, but it allows one to convert a write access into a read
// access without the risk of having an exception being thrown. See the
// documentation of Unlocked for more details.

#pragma once

#include "utils/threading/aithreadid.h"
#include "utils/AIRefCount.h"
#include "utils/is_specialization_of.h"

#include <new>
#include <cstddef>
#include <memory>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <boost/integer/common_factor.hpp>

#ifdef CWDEBUG
// Set this to 1 to print tracking information about Unlocked and UnlockedBase to dc::tracked.
#define THREADSAFE_TRACK_UNLOCKED 1
#if THREADSAFE_TRACK_UNLOCKED
#include <cwds/tracked.h>
#include <libcwd/type_info.h>
#endif
#define THREADSAFE_DEBUG 1
#else
#define THREADSAFE_DEBUG 0
#endif

namespace threadsafe {

template<typename T, typename POLICY_MUTEX>
class Unlocked;

template<typename T, typename POLICY_MUTEX>
requires std::derived_from<T, AIRefCount>
void intrusive_ptr_add_ref(Unlocked<T, POLICY_MUTEX> const* ptr);

template<typename T, typename POLICY_MUTEX>
requires std::derived_from<T, AIRefCount>
void intrusive_ptr_release(Unlocked<T, POLICY_MUTEX> const* ptr);

/**
 * @brief A wrapper class for objects that need to be accessed by more than one thread, allowing concurrent readers.
 *
 * For example,
 *
 * <code>
 * class Foo { public: Foo(int, int); };	// Some user defined type.
 * using foo_t = threadsafe::Unlocked<Foo, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;	// Wrapper type for Foo.
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
 * using foo_t = threadsafe::Unlocked<Foo, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;
 * foo_t foo;
 *
 * void f(foo_t::rat& foo_r)		// Sometimes needs to write to foo_r.
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
 * Where we assume that next_filename is a const member function (using a mutable
 * internally or something). The only-needs-read-access problem above, can easily
 * be solved by changing the second wat into a rat of course. But to keep the
 * object locked while going from write access to read access we'd have to do the
 * following:
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
 * foo_r can throw an exception while we didn't even use the foo_r
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
#if THREADSAFE_TRACK_UNLOCKED
template<typename T, typename POLICY_MUTEX>
struct NameUnlocked {
  static char const* name;
};
template<typename T, typename POLICY_MUTEX>
char const* NameUnlocked<T, POLICY_MUTEX>::name =
  libcwd::type_info_of<Unlocked<T, POLICY_MUTEX>>().demangled_name();
#endif
template<typename T, typename POLICY_MUTEX>
class Unlocked : /* YOU NEED TO CREATE AN ACCESS TYPE TO ACCESS MEMBERS OF THIS PROTECTED T */ protected T, public POLICY_MUTEX
#if THREADSAFE_TRACK_UNLOCKED
                 , public tracked::Tracked<&NameUnlocked<T, POLICY_MUTEX>::name>
#endif
{
  public:
#if THREADSAFE_TRACK_UNLOCKED
    using tracked::Tracked<&NameUnlocked<T, POLICY_MUTEX>::name>::Tracked;
#endif

    using data_type = T;
    using policy_type = POLICY_MUTEX;
    using const_unlocked_type = Unlocked<T, POLICY_MUTEX>;

    // The access types.
    using crat = typename POLICY_MUTEX::template access_types<Unlocked<T, POLICY_MUTEX>>::const_read_access_type;
    using rat = typename POLICY_MUTEX::template access_types<Unlocked<T, POLICY_MUTEX>>::read_access_type;
    using wat = typename POLICY_MUTEX::template access_types<Unlocked<T, POLICY_MUTEX>>::write_access_type;
    using w2rCarry = typename POLICY_MUTEX::template access_types<Unlocked<T, POLICY_MUTEX>>::write_to_read_carry;
    using ratBase = typename POLICY_MUTEX::template access_types<Unlocked<T, POLICY_MUTEX>>::read_access_base_type;

    // Needs to access to T (and m_ref).
    template<typename BASE, typename PM> friend class ConstUnlockedBase;

    template<typename T2, typename POLICY_MUTEX2>
    requires std::derived_from<T2, AIRefCount>
    friend void intrusive_ptr_add_ref(Unlocked<T2, POLICY_MUTEX2> const* ptr);

    template<typename T2, typename POLICY_MUTEX2>
    requires std::derived_from<T2, AIRefCount>
    friend void intrusive_ptr_release(Unlocked<T2, POLICY_MUTEX2> const* ptr);

  public:
    // Allow arbitrary parameters to be passed for construction.
    template<typename... ARGS>
    Unlocked(ARGS&&... args) : T(std::forward<ARGS>(args)...)
#if THREADSAFE_DEBUG
      , m_ref(0)
#endif // THREADSAFE_DEBUG
    {
    }

  protected:
    // Only these may access the object (through ptr()).
    friend crat;
    friend rat;
    friend wat;
    friend w2rCarry;

    T const* ptr() const { return this; }
    T* ptr() { return this; }

#if THREADSAFE_DEBUG
  private:
    std::atomic<int> m_ref;

    void increment_ref() { m_ref++; }
    void decrement_ref() { m_ref--; }

  public:
    ~Unlocked()
    {
      // Can only be locked when there still exists an Access object that
      // references this object and will access it upon destruction.
      // If the assertion fails, make sure that such Access object is
      // destructed before the deletion of this object.
      // If the assert happens after main, did you join all threads --
      // that might still have such an Access object-- before leaving
      // main()?
      assert(m_ref == 0);
    }
#endif
};

/**
 * @brief A class that can be used to point to a base class of an object wrapped by Unlocked.
 *
 * Example usage:
 *
 * <code>
 * class B { public: void modify(); void print() const; };
 * class A : public B { ... };
 *
 * using UnlockedA = Unlocked<A, policy::ReadWrite<AIReadWriteMutex>>;
 * using UnlockedB = UnlockedBase<B, UnlockedA::policy_type>;
 * </code>
 *
 * Now UnlockedB can be created from an UnlockedA and then used in the usual way:
 *
 * <code>
 * void f(UnlockedB b)
 * {
 *   {
 *     UnlockedB::wat b_w(b);    // Get write-access.
 *     b_w->modify();
 *   }
 *   {
 *     UnlockedB::rat b_r(b);    // Get read-access.
 *     b_r->print();
 *   }
 * }
 * </code>
 *
 * Note that an UnlockedBase is a pointer/reference to the Unlocked that it was created from:
 * you may not move or destroy the Unlocked that it was created from!
 *
 * Moving and copying an UnlockedBase is prefectly fine; just like it would be to move/copy a
 * base class pointer.
 */

#if THREADSAFE_DEBUG
// Forward declarations.
template<typename BASE, typename POLICY_MUTEX>
class UnlockedBase;

template<typename BASE, typename POLICY_MUTEX>
class ConstUnlockedBase;

template<class UNLOCKED>
struct ConstReadAccess;

template<class UNLOCKED>
struct AccessConst;

template<class UNLOCKED>
struct OTAccessConst;
#endif

#if THREADSAFE_TRACK_UNLOCKED
template<typename BASE, typename POLICY_MUTEX>
class ConstUnlockedBase;
template<typename BASE, typename POLICY_MUTEX>
struct NameConstUnlockedBase {
  static char const* name;
};
template<typename BASE, typename POLICY_MUTEX>
char const* NameConstUnlockedBase<BASE, POLICY_MUTEX>::name = libcwd::type_info_of<ConstUnlockedBase<BASE, POLICY_MUTEX>>().demangled_name();
#endif
template<typename BASE, typename POLICY_MUTEX>
class ConstUnlockedBase : public POLICY_MUTEX::reference_type
#if THREADSAFE_TRACK_UNLOCKED
                 , public tracked::Tracked<&NameConstUnlockedBase<BASE, POLICY_MUTEX>::name>
#endif
{
  public:
#if THREADSAFE_TRACK_UNLOCKED
    using tracked::Tracked<&NameConstUnlockedBase<BASE, POLICY_MUTEX>::name>::Tracked;
#endif
    using data_type = BASE;
    using policy_type = typename POLICY_MUTEX::reference_type;
    using const_unlocked_type = ConstUnlockedBase<BASE, POLICY_MUTEX>;
    using non_const_unlocked_type = UnlockedBase<BASE, POLICY_MUTEX>;

    // The access types.
    using crat = typename policy_type::template access_types<ConstUnlockedBase<BASE, POLICY_MUTEX>>::const_read_access_type;
    using w2rCarry = typename policy_type::template access_types<UnlockedBase<BASE, POLICY_MUTEX>>::write_to_read_carry;
    using ratBase = typename policy_type::template access_types<ConstUnlockedBase<BASE, POLICY_MUTEX>>::read_access_base_type;

  public:
    template<typename T>
    requires std::derived_from<T, BASE>
    ConstUnlockedBase(Unlocked<T, POLICY_MUTEX> const& unlocked) :
      POLICY_MUTEX::reference_type(const_cast<Unlocked<T, POLICY_MUTEX>&>(unlocked).mutex()),
      m_base(const_cast<BASE*>(static_cast<BASE const*>(&unlocked)))
#if THREADSAFE_DEBUG
      , m_ref_ptr(&const_cast<Unlocked<T, POLICY_MUTEX>&>(unlocked).m_ref)
#endif // THREADSAFE_DEBUG
    {
      if constexpr (std::derived_from<BASE, AIRefCount>)
      {
        // Stop a destruction of a boost::intrusive_ptr that points to unlocked from destroying it.
        m_base->inhibit_deletion();
      }
    }

    template<typename T>
    requires std::derived_from<T, BASE>
    ConstUnlockedBase(ConstUnlockedBase<T, POLICY_MUTEX> const& unlocked_base) :
      POLICY_MUTEX::reference_type(unlocked_base.mutex()),
#if THREADSAFE_TRACK_UNLOCKED
        tracked::Tracked<&NameConstUnlockedBase<BASE, POLICY_MUTEX>::name>(unlocked_base),
#endif
        m_base(const_cast<BASE*>(unlocked_base.ptr()))
#if THREADSAFE_DEBUG
      , m_ref_ptr(unlocked_base.m_ref_ptr)
#endif // THREADSAFE_DEBUG
    {
      if constexpr (std::derived_from<BASE, AIRefCount>)
        m_base->inhibit_deletion();
    }

    // Copy constructor.
    ConstUnlockedBase(ConstUnlockedBase const& other) :
      POLICY_MUTEX::reference_type(other.mutex()),
#if THREADSAFE_TRACK_UNLOCKED
        tracked::Tracked<&NameConstUnlockedBase<BASE, POLICY_MUTEX>::name>(other),
#endif
        m_base(const_cast<BASE*>(other.ptr()))
#if THREADSAFE_DEBUG
      , m_ref_ptr(other.m_ref_ptr)
#endif // THREADSAFE_DEBUG
    {
      if constexpr (std::derived_from<BASE, AIRefCount>)
        m_base->inhibit_deletion();
    }

    ~ConstUnlockedBase()
    {
      if constexpr (std::derived_from<BASE, AIRefCount>)
        m_base->allow_deletion();
    }

    ConstUnlockedBase& operator=(ConstUnlockedBase const& other)
    {
      if (&other != this)
      {
        POLICY_MUTEX::reference_type::operator=(other);
        m_base = other.m_base;
#if THREADSAFE_DEBUG
        m_ref_ptr = other.m_ref_ptr;
#endif
      }
      return *this;
    }

    ConstUnlockedBase(ConstUnlockedBase&& orig) = default;
    ConstUnlockedBase& operator=(ConstUnlockedBase&& orig) = default;

  private:
    // Used by unlocked_cast.
    template<typename T>
    requires std::derived_from<BASE, T> && (!std::is_same_v<BASE, T>)
    ConstUnlockedBase(ConstUnlockedBase<T, POLICY_MUTEX> const& unlocked_base) : POLICY_MUTEX::reference_type(unlocked_base.mutex()),
#if THREADSAFE_TRACK_UNLOCKED
        tracked::Tracked<&NameConstUnlockedBase<BASE, POLICY_MUTEX>::name>(unlocked_base),
#endif
        m_base(static_cast<BASE*>(unlocked_base.ptr()))
#if THREADSAFE_DEBUG
      , m_ref_ptr(unlocked_base.m_ref_ptr)
#endif // THREADSAFE_DEBUG
    {
      if constexpr (std::derived_from<BASE, AIRefCount>)
        m_base->inhibit_deletion();
    }

  public:
    template<typename U>
    requires std::is_reference_v<U> && std::is_const_v<std::remove_reference_t<U>> &&
             utils::is_specialization_of_v<std::remove_cvref_t<U>, Unlocked> &&
             std::derived_from<typename std::remove_cvref<U>::type::data_type, BASE>
    friend U unlocked_cast(ConstUnlockedBase const& orig)
    {
      return static_cast<U>(*orig.m_base);
    }

    template<typename U>
    requires std::is_reference_v<U> && std::is_const_v<std::remove_reference_t<U>> &&
             utils::is_specialization_of_v<std::remove_cvref_t<U>, ConstUnlockedBase> &&
             std::derived_from<typename std::remove_cvref<U>::type::data_type, BASE>
    friend U unlocked_cast(ConstUnlockedBase const& orig)
    {
      return {orig};
    }

  protected:
    BASE* m_base;

  protected:
    // Only these may access the object (through ptr()).
    friend crat;
    // The crat type is ConstReadAccess<threadsafe::ConstUnlockedBase<BASE, POLICY_MUTEX>>.
    // But the base class of ReadAccess, AccessConst and OTAccessConst, which use UnlockedBase, also need
    // access to increment_ref/decrement_ref:
    friend ratBase;

    // Accessor.
    BASE const* ptr() const { return m_base; }

#if THREADSAFE_DEBUG
  private:
    std::atomic<int>* m_ref_ptr;

    // Requires access to increment_ref/decrement_ref.
    friend w2rCarry;

    void increment_ref() { (*m_ref_ptr)++; }
    void decrement_ref() { (*m_ref_ptr)--; }
#endif
};

template<typename BASE, typename POLICY_MUTEX>
class UnlockedBase : public ConstUnlockedBase<BASE, POLICY_MUTEX>
{
  public:
    using policy_type = typename ConstUnlockedBase<BASE, POLICY_MUTEX>::policy_type;
    using const_unlocked_type = ConstUnlockedBase<BASE, POLICY_MUTEX>;

    using rat = typename policy_type::template access_types<UnlockedBase<BASE, POLICY_MUTEX>>::read_access_type;
    using wat = typename policy_type::template access_types<UnlockedBase<BASE, POLICY_MUTEX>>::write_access_type;
    using w2rCarry = typename ConstUnlockedBase<BASE, POLICY_MUTEX>::w2rCarry;
    using ratBase = typename ConstUnlockedBase<BASE, POLICY_MUTEX>::ratBase;

  public:
    template<typename T>
    requires std::derived_from<T, BASE>
    UnlockedBase(Unlocked<T, POLICY_MUTEX>& unlocked) : ConstUnlockedBase<BASE, POLICY_MUTEX>(unlocked) { }

    using ConstUnlockedBase<BASE, POLICY_MUTEX>::ConstUnlockedBase;

    UnlockedBase(UnlockedBase const& other) = default;
    UnlockedBase& operator=(UnlockedBase const& other)
    {
      if (&other != this)
        ConstUnlockedBase<BASE, POLICY_MUTEX>::operator=(other);
      return *this;
    }

    UnlockedBase(UnlockedBase&& orig) = default;
    UnlockedBase& operator=(UnlockedBase&& orig) = default;

  private:
    // Used by unlocked_cast.
    template<typename T>
    requires std::derived_from<BASE, T> && (!std::is_same_v<BASE, T>)
    UnlockedBase(UnlockedBase<T, POLICY_MUTEX>& unlocked_base) : ConstUnlockedBase<BASE, POLICY_MUTEX>(unlocked_base)
    {
    }

  public:
    template<typename U>
    requires std::is_reference_v<U> && (!std::is_const_v<std::remove_reference_t<U>>) &&
             utils::is_specialization_of_v<std::remove_cvref_t<U>, Unlocked> &&
             std::derived_from<typename std::remove_cvref<U>::type::data_type, BASE>
    friend U unlocked_cast(UnlockedBase& orig)
    {
      return static_cast<U>(*orig.m_base);
    }

    template<typename U>
    requires std::is_reference_v<U> && (!std::is_const_v<std::remove_reference_t<U>>) &&
             utils::is_specialization_of_v<std::remove_cvref_t<U>, UnlockedBase> &&
             std::derived_from<typename std::remove_cvref<U>::type::data_type, BASE>
    friend U unlocked_cast(UnlockedBase& orig)
    {
      return {orig};
    }

  protected:
    friend rat;
    friend wat;
    friend w2rCarry;
    friend ratBase;

    // Accessors.
    using ConstUnlockedBase<BASE, POLICY_MUTEX>::ptr;
    BASE* ptr() { return ConstUnlockedBase<BASE, POLICY_MUTEX>::m_base; }
};

/**
 * @brief Read lock object and provide read access.
 */
template<class UNLOCKED>
struct ConstReadAccess
{
  public:
    using unlocked_type = UNLOCKED;

    /// Internal enum for the lock-type of the Access object.
    enum state_type
    {
      readlocked,	///< A ConstReadAccess or ReadAccess.
      read2writelocked,	///< A WriteAccess constructed from a ReadAccess.
      writelocked,	///< A WriteAccess constructed from a ThreadSafe.
      write2writelocked,///< A WriteAccess constructed from (the ReadAccess base class of) a WriteAccess.
      carrylocked	///< A ReadAccess constructed from a Write2ReadCarry.
    };

    /// Construct a ConstReadAccess from a constant Unlocked.
    template<typename ...Args>
    ConstReadAccess(UNLOCKED const& unlocked, Args&&... args) : m_unlocked(const_cast<UNLOCKED*>(&unlocked)), m_state(readlocked)
    {
#if THREADSAFE_DEBUG
      m_unlocked->increment_ref();
#endif // THREADSAFE_DEBUG
      m_unlocked->mutex().rdlock(std::forward<Args>(args)...);
    }

    /// Destruct the Access object.
    // These should never be dynamically allocated, so there is no need to make this virtual.
    ~ConstReadAccess()
    {
      if (AI_UNLIKELY(!m_unlocked))
        return;
      if (m_state == readlocked)
	m_unlocked->mutex().rdunlock();
      else if (m_state == writelocked)
	m_unlocked->mutex().wrunlock();
      else if (m_state == read2writelocked)
	m_unlocked->mutex().wr2rdlock();
#if THREADSAFE_DEBUG
      m_unlocked->decrement_ref();
#endif // THREADSAFE_DEBUG
    }

    /// Access the underlaying object for read access.
    typename UNLOCKED::data_type const* operator->() const { return m_unlocked->ptr(); }

    /// Access the underlaying object for read access.
    typename UNLOCKED::data_type const& operator*() const { return *m_unlocked->ptr(); }

  protected:
    /// Constructor used by ReadAccess.
    ConstReadAccess(UNLOCKED& unlocked, state_type state) : m_unlocked(&unlocked), m_state(state)
    {
#if THREADSAFE_DEBUG
      m_unlocked->increment_ref();
#endif // THREADSAFE_DEBUG
    }

    UNLOCKED* m_unlocked;       ///< Pointer to the object that we provide access to.
    state_type const m_state;   ///< The lock state that m_unlocked is in.

    // Disallow copy constructing directly.
    ConstReadAccess(ConstReadAccess const&) = delete;

    // Move constructor.
    ConstReadAccess(ConstReadAccess&& rvalue) : m_unlocked(rvalue.m_unlocked), m_state(rvalue.m_state) { rvalue.m_unlocked = nullptr; }
};

template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct ReadAccess;

template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct WriteAccess;

/**
 * @brief Allow to carry the read access from a wat to a rat.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
class Write2ReadCarry
{
  private:
    UNLOCKED& m_unlocked;
    bool m_used;

  public:
    explicit Write2ReadCarry(UNLOCKED& unlocked) : m_unlocked(unlocked), m_used(false)
    {
#if THREADSAFE_DEBUG
      m_unlocked.increment_ref();
#endif // THREADSAFE_DEBUG
    }
    ~Write2ReadCarry()
    {
#if THREADSAFE_DEBUG
      m_unlocked.decrement_ref();
#endif // THREADSAFE_DEBUG
      if (m_used)
	m_unlocked.mutex().rdunlock();
    }

    friend struct WriteAccess<UNLOCKED>;
    friend struct ReadAccess<UNLOCKED>;
};

/**
 * @brief Read lock object and provide read access, with possible promotion to write access.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct ReadAccess : public ConstReadAccess<UNLOCKED>
{
  public:
    using state_type = typename ConstReadAccess<UNLOCKED>::state_type;
    using ConstReadAccess<UNLOCKED>::readlocked;
    using ConstReadAccess<UNLOCKED>::carrylocked;

    /// Construct a ReadAccess from a non-constant Unlocked.
    template<typename ...Args>
    explicit ReadAccess(UNLOCKED& unlocked, Args&&... args) : ConstReadAccess<UNLOCKED>(unlocked, readlocked)
    {
      this->m_unlocked->mutex().rdlock(std::forward<Args>(args)...);
    }

    /// Construct a ReadAccess from a Write2ReadCarry object containing an read locked Unlocked. Upon destruction leave the Unlocked read locked.
    explicit ReadAccess(Write2ReadCarry<UNLOCKED> const& w2rc) : ConstReadAccess<UNLOCKED>(w2rc.m_unlocked, carrylocked)
    {
      assert(w2rc.m_used); // Always pass a w2rCarry to a wat first.
    }

  protected:
    /// Constructor used by WriteAccess.
    ReadAccess(UNLOCKED& unlocked, state_type state) : ConstReadAccess<UNLOCKED>(unlocked, state) { }

    friend struct WriteAccess<UNLOCKED>;

  public:
    operator ConstReadAccess<typename UNLOCKED::const_unlocked_type> const&() const
    {
      static_assert(sizeof(ConstReadAccess<UNLOCKED>) == sizeof(ConstReadAccess<typename UNLOCKED::const_unlocked_type>),
          "Unexpected size when doing reinterpret_cast");
      return reinterpret_cast<ConstReadAccess<typename UNLOCKED::const_unlocked_type> const&>(
          static_cast<ConstReadAccess<UNLOCKED> const&>(*this));
    }
};

/**
 * @brief Write lock object and provide read/write access.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct WriteAccess : public ReadAccess<UNLOCKED>
{
  public:
    using ConstReadAccess<UNLOCKED>::readlocked;
    using ConstReadAccess<UNLOCKED>::read2writelocked;
    using ConstReadAccess<UNLOCKED>::writelocked;
    using ConstReadAccess<UNLOCKED>::write2writelocked;
    using state_type = typename ReadAccess<UNLOCKED>::state_type;

    /// Construct a WriteAccess from a non-constant Unlocked.
    template<typename ...Args>
    explicit WriteAccess(UNLOCKED& unlocked, Args&&... args) : ReadAccess<UNLOCKED>(unlocked, writelocked)
    {
      this->m_unlocked->mutex().wrlock(std::forward<Args>(args)...);
    }

    /// Promote read access to write access.
    explicit WriteAccess(ReadAccess<UNLOCKED>& access) :
        ReadAccess<UNLOCKED>(*access.m_unlocked, write2writelocked)
    {
      if (access.m_state == readlocked)
      {
	this->m_unlocked->mutex().rd2wrlock();
        // We should have initialized the base class with read2writelocked, but if rd2wrlock() throws
        // then the base class destructor ~ConstReadAccess would call wr2rdlock() as if obtaining the
        // write-lock had succeeded. In order to stop it from doing that, we did set m_state to
        // write2writelocked which causes it to do nothing, and only after rd2wrlock() succeeded
        // we correct this value.
        const_cast<state_type&>(this->m_state) = read2writelocked;
      }
    }

    /// Construct a WriteAccess from a Write2ReadCarry object containing an unlocked Unlocked. Upon destruction leave the Unlocked read locked.
    explicit WriteAccess(Write2ReadCarry<UNLOCKED>& w2rc) : ReadAccess<UNLOCKED>(w2rc.m_unlocked, read2writelocked)
    {
      assert(!w2rc.m_used); // Always pass a w2rCarry to the wat first. There can only be one wat.
      w2rc.m_used = true;
      this->m_unlocked->mutex().wrlock();
    }

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type* operator->() const { return this->m_unlocked->ptr(); }

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type& operator*() const { return *this->m_unlocked->ptr(); }
};

/**
 * @brief The Const-Read-Access-Type (crat) of a Primitive locked object.
 *
 * This object can not be converted to a wat.
 */
template<class UNLOCKED>
struct AccessConst
{
    using unlocked_type = UNLOCKED;

    /// Construct a AccessConst from a constant Unlocked.
    template<typename ...Args>
    AccessConst(UNLOCKED const& unlocked, Args&&... args) : m_unlocked(const_cast<UNLOCKED*>(&unlocked))
    {
#if THREADSAFE_DEBUG
      m_unlocked->increment_ref();
#endif // THREADSAFE_DEBUG
      this->m_unlocked->mutex().lock(std::forward<Args>(args)...);
    }

    /// Access the underlaying object for read access.
    typename UNLOCKED::data_type const* operator->() const { return this->m_unlocked->ptr(); }

    /// Access the underlaying object for read access.
    typename UNLOCKED::data_type const& operator*() const { return *this->m_unlocked->ptr(); }

    ~AccessConst()
    {
      if (AI_LIKELY(this->m_unlocked))
      {
#if THREADSAFE_DEBUG
        this->m_unlocked->decrement_ref();
#endif // THREADSAFE_DEBUG
        this->m_unlocked->mutex().unlock();
      }
    }

    // If m_primitive_mutex is a ConditionVariable, then this can be used to wait for a signal.
    template<typename Predicate>
    void wait(Predicate pred) { this->m_unlocked->mutex().wait(pred); }
    // If m_primitive_mutex is a ConditionVariable then this can be used to wake up the waiting thread.
    void notify_one() { this->m_unlocked->mutex().notify_one(); }

    // Experimental unlock/relock. Const because we must be able to call it on a rat type (which is const).
    void unlock() const
    {
#if THREADSAFE_DEBUG
      this->m_unlocked->decrement_ref();
#endif // THREADSAFE_DEBUG
      this->m_unlocked->mutex().unlock();
      this->m_unlocked = nullptr;
    }

    // Relock a previously unlocked access object.
    void relock(UNLOCKED const& unlocked) const
    {
      m_unlocked = const_cast<UNLOCKED*>(&unlocked);
#if THREADSAFE_DEBUG
      m_unlocked->increment_ref();
#endif // THREADSAFE_DEBUG
      this->m_unlocked->mutex().lock();
    }

  protected:
    mutable UNLOCKED* m_unlocked;		///< Pointer to the object that we provide access to.

    // Disallow copy constructing directly.
    AccessConst(AccessConst const&) = delete;

    // Move constructor.
    AccessConst(AccessConst&& rvalue) : m_unlocked(rvalue.m_unlocked) { rvalue.m_unlocked = nullptr; }
};

/**
 * @brief The Read-Access-Type (rat) of a Primitive locked object.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct ConstAccess : public AccessConst<UNLOCKED>
{
  public:
    /// Construct a ConstAccess from a non-constant Unlocked.
    template<typename ...Args>
    explicit ConstAccess(UNLOCKED& unlocked, Args&&... args) : AccessConst<UNLOCKED>(unlocked, std::forward<Args>(args)...) { }

    operator AccessConst<typename UNLOCKED::const_unlocked_type> const&() const
    {
      static_assert(sizeof(AccessConst<UNLOCKED>) == sizeof(void*), "Unexpected size when doing reinterpret_cast");
      return reinterpret_cast<AccessConst<typename UNLOCKED::const_unlocked_type> const&>(static_cast<AccessConst<UNLOCKED> const&>(*this));
    }
};

/**
 * @brief The Write-Access-Type (wat) of a Primitive locked object.
 *
 * This access type provides non-const (write) access.
 *
 * It is not possible to create an Access type from a ConstAccess
 * because that would require recursive locking of the mutex, which
 * might not be supported. Instead, use `wat_cast` to convert a
 * rat into a wat when needed.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct Access : public ConstAccess<UNLOCKED>
{
  public:
    /// Construct a Access from a non-constant Unlocked.
    using ConstAccess<UNLOCKED>::ConstAccess;

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type* operator->() const { return this->m_unlocked->ptr(); }

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type& operator*() const { return *this->m_unlocked->ptr(); }
};

// Explicitly convert a ConstAccess to an Access type.
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
Access<UNLOCKED> const& wat_cast(ConstAccess<UNLOCKED> const& access)
{
  return static_cast<Access<UNLOCKED> const&>(access);
}

/**
 * @brief Access single threaded object for read access.
 */
template<class UNLOCKED>
struct OTAccessConst
{
  public:
    using unlocked_type = UNLOCKED;

    /// Construct a OTAccessConst from a constant Unlocked.
    template<typename ...Args>
    OTAccessConst(UNLOCKED const& unlocked, Args&&... args) : m_unlocked(const_cast<UNLOCKED*>(&unlocked), std::forward<Args>(args)...)
    {
#if THREADSAFE_DEBUG
      m_unlocked->increment_ref();
      assert(aithreadid::is_single_threaded(unlocked.mutex()));
#endif // THREADSAFE_DEBUG
    }

#if THREADSAFE_DEBUG
    ~OTAccessConst()
    {
      if (this->m_unlocked)
        m_unlocked->decrement_ref();
    }
#endif // THREADSAFE_DEBUG

    /// Access the underlaying object for read access.
    typename UNLOCKED::data_type const* operator->() const { return this->m_unlocked->ptr(); }

    /// Access the underlaying object for read write access.
    typename UNLOCKED::data_type const& operator*() const { return *this->m_unlocked->ptr(); }

  protected:
    UNLOCKED* m_unlocked;		///< Pointer to the object that we provide access to.

    // Disallow copy constructing directly, because one might want to switch to policy::Primitive
    // and then making a copy isn't possible (assuming a mutex is used that isn't recursively lockable).
    OTAccessConst(OTAccessConst const&) = delete;

    // Move constructor.
    OTAccessConst(OTAccessConst&& rvalue) : m_unlocked(rvalue.m_unlocked) { rvalue.m_unlocked = nullptr; }
};

/**
 * @brief The Read-Access-Type (rat) of a OneThread locked object.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct OTConstAccess : public OTAccessConst<UNLOCKED>
{
  public:
    /// Construct an OTConstAccess from a non-constant Unlocked.
    template<typename ...Args>
    explicit OTConstAccess(UNLOCKED& unlocked, Args&&... args) : OTAccessConst<UNLOCKED>(unlocked, std::forward<Args>(args)...) { }

    operator OTAccessConst<typename UNLOCKED::const_unlocked_type> const&() const
    {
      static_assert(sizeof(OTAccessConst<UNLOCKED>) == sizeof(void*), "Unexpected size when doing reinterpret_cast");
      return reinterpret_cast<OTAccessConst<typename UNLOCKED::const_unlocked_type> const&>(static_cast<OTAccessConst<UNLOCKED> const&>(*this));
    }
};

/**
 * @brief The Write-Access-Type (wat) of a OneThread locked object.
 *
 * Access single threaded object for read/write access.
 */
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
struct OTAccess : public OTConstAccess<UNLOCKED>
{
  public:
    /// Construct a OTAccess from a non-constant Unlocked.
    using OTConstAccess<UNLOCKED>::OTConstAccess;

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type* operator->() const { return this->m_unlocked->ptr(); }

    /// Access the underlaying object for (read and) write access.
    typename UNLOCKED::data_type& operator*() const { return *this->m_unlocked->ptr(); }
};

// Explicitly convert a OTConstAccess to an Access type.
template<class UNLOCKED>
requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
OTAccess<UNLOCKED> const& wat_cast(OTConstAccess<UNLOCKED> const& access)
{
  return static_cast<OTAccess<UNLOCKED> const&>(access);
}

namespace policy
{

template<typename T> struct helper { static constexpr bool value = false; };
template<typename UNLOCKED> struct unsupported_w2rCarry
{
  static_assert(helper<UNLOCKED>::value, "\n"
      "* The Primitive/OneThread policy does not support w2rCarry,\n"
      "* it makes no sense and would require extra space and cpu cycles to make it work.\n"
      "* Instead, of '{ foo_t::w2rCarry carry(foo); { foo_t::wat foo_rw(carry); ... } foo_t::rat foo_r(carry); ... }',\n"
      "* just use    '{ foo_t::wat foo_rw(foo); ... }'\n");
};

template<class RWMUTEX>
class ReadWriteAccess
{
  protected:
    template<class UNLOCKED>
    struct access_types_unlocked_base
    {
      using read_access_type = ReadAccess<UNLOCKED>;
      using write_access_type = WriteAccess<UNLOCKED>;
      using write_to_read_carry = Write2ReadCarry<UNLOCKED>;
    };

    template<class UNLOCKED>
    struct access_types_const_unlocked_base
    {
      using const_read_access_type = ConstReadAccess<UNLOCKED>;
      using read_access_base_type = ConstReadAccess<typename UNLOCKED::non_const_unlocked_type>;
    };

    template<class UNLOCKED>
    struct access_types_unlocked
    {
      using const_read_access_type = ConstReadAccess<UNLOCKED>;
      using read_access_type = ReadAccess<UNLOCKED>;
      using write_access_type = WriteAccess<UNLOCKED>;
      using write_to_read_carry = Write2ReadCarry<UNLOCKED>;
      using read_access_base_type = ConstReadAccess<UNLOCKED>;
    };

    template<class UNLOCKED>
    using access_types = std::conditional_t<
        utils::is_specialization_of_v<UNLOCKED, UnlockedBase>,
            access_types_unlocked_base<UNLOCKED>,
            std::conditional_t<
                utils::is_specialization_of_v<UNLOCKED, ConstUnlockedBase>,
                access_types_const_unlocked_base<UNLOCKED>,
                access_types_unlocked<UNLOCKED>
            >>;
};

template<class RWMUTEX>
class ReadWriteRef : public ReadWriteAccess<RWMUTEX>
{
  protected:
    template<class UNLOCKED>
    friend struct ::threadsafe::ConstReadAccess;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct ::threadsafe::ReadAccess;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct ::threadsafe::WriteAccess;

    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

    // Use a pointer in order to keep our assignment operator, which in turn
    // that allows assigning to UnlockedBase still.
    RWMUTEX* m_read_write_mutex_ptr;

    RWMUTEX& mutex() const { return *m_read_write_mutex_ptr; }

    ReadWriteRef(RWMUTEX& read_write_mutex) : m_read_write_mutex_ptr(&read_write_mutex) { }

    ReadWriteRef(ReadWriteRef const&) = default;

  public:
    void rd2wryield() { m_read_write_mutex_ptr->rd2wryield(); }
};

template<class RWMUTEX>
class ReadWrite : public ReadWriteAccess<RWMUTEX>
{
  public:
    using reference_type = ReadWriteRef<RWMUTEX>;

  protected:
    template<class UNLOCKED>
    friend struct ConstReadAccess;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct ReadAccess;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct WriteAccess;

    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

    RWMUTEX m_read_write_mutex;

    RWMUTEX& mutex() { return m_read_write_mutex; }

  public:
    void rd2wryield() { m_read_write_mutex.rd2wryield(); }
};

template<class MUTEX>
class PrimitiveAccess
{
  private:
    template<class UNLOCKED>
    struct access_types_unlocked_base
    {
      using read_access_type = ConstAccess<UNLOCKED>;
      using write_access_type = Access<UNLOCKED>;
      using write_to_read_carry = unsupported_w2rCarry<UNLOCKED>;
    };

    template<class UNLOCKED>
    struct access_types_const_unlocked_base
    {
      using const_read_access_type = AccessConst<UNLOCKED>;
      using read_access_base_type = AccessConst<typename UNLOCKED::non_const_unlocked_type>;
    };

    template<class UNLOCKED>
    struct access_types_unlocked
    {
      using const_read_access_type = AccessConst<UNLOCKED>;
      using read_access_type = ConstAccess<UNLOCKED>;
      using write_access_type = Access<UNLOCKED>;
      using write_to_read_carry = unsupported_w2rCarry<UNLOCKED>;
      using read_access_base_type = AccessConst<UNLOCKED>;
    };

  protected:
    template<class UNLOCKED>
    using access_types = std::conditional_t<
        utils::is_specialization_of_v<UNLOCKED, UnlockedBase>,
            access_types_unlocked_base<UNLOCKED>,
            std::conditional_t<
                utils::is_specialization_of_v<UNLOCKED, ConstUnlockedBase>,
                access_types_const_unlocked_base<UNLOCKED>,
                access_types_unlocked<UNLOCKED>
            >>;
};

template<class MUTEX>
class PrimitiveRef : public PrimitiveAccess<MUTEX>
{
  protected:
    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, ConstUnlockedBase>
    friend struct AccessConst;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct Access;

    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

    MUTEX* m_primitive_mutex_ptr;

    PrimitiveRef(MUTEX& primitive_mutex) : m_primitive_mutex_ptr(&primitive_mutex) { }

    MUTEX& mutex() const { return *m_primitive_mutex_ptr; }
};

template<class MUTEX>
class Primitive : public PrimitiveAccess<MUTEX>
{
  public:
    using reference_type = PrimitiveRef<MUTEX>;

  protected:
    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, ConstUnlockedBase>
    friend struct AccessConst;

    template<class UNLOCKED>
    requires utils::is_specialization_of_v<UNLOCKED, Unlocked> || utils::is_specialization_of_v<UNLOCKED, UnlockedBase>
    friend struct Access;

    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

    MUTEX m_primitive_mutex;

    MUTEX& mutex() { return m_primitive_mutex; }
};

class OneThreadAccess
{
  private:
    template<class UNLOCKED>
    struct access_types_unlocked_base
    {
      using read_access_type = OTAccess<UNLOCKED>;
      using write_access_type = OTAccess<UNLOCKED>;
      using write_to_read_carry = unsupported_w2rCarry<UNLOCKED>;
    };

    template<class UNLOCKED>
    struct access_types_const_unlocked_base
    {
      using const_read_access_type = OTAccessConst<UNLOCKED>;
      using read_access_base_type = OTAccessConst<typename UNLOCKED::non_const_unlocked_type>;
    };

    template<class UNLOCKED>
    struct access_types_unlocked
    {
      using const_read_access_type = OTAccessConst<UNLOCKED>;
      using read_access_type = OTAccess<UNLOCKED>;
      using write_access_type = OTAccess<UNLOCKED>;
      using write_to_read_carry = unsupported_w2rCarry<UNLOCKED>;
      using read_access_base_type = OTAccessConst<UNLOCKED>;
    };

  protected:
    template<class UNLOCKED>
    using access_types = std::conditional_t<
        utils::is_specialization_of_v<UNLOCKED, UnlockedBase>,
            access_types_unlocked_base<UNLOCKED>,
            std::conditional_t<
                utils::is_specialization_of_v<UNLOCKED, ConstUnlockedBase>,
                access_types_const_unlocked_base<UNLOCKED>,
                access_types_unlocked<UNLOCKED>
            >>;
};

class OneThreadRef : public OneThreadAccess
{
  protected:
    OneThreadRef(
#if THREADSAFE_DEBUG
        std::thread::id& thread_id
#else
        int
#endif
        )
#if THREADSAFE_DEBUG
      : m_thread_id(&thread_id)
#endif
    { }

    OneThreadRef(OneThreadRef const&) = default;

    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

#if THREADSAFE_DEBUG
    std::thread::id* m_thread_id;

    // Hijack mutex() to pass the reference to m_thread_id %-).
    std::thread::id& mutex() const { return *m_thread_id; }
#else
    int mutex() const { return {}; }
#endif // THREADSAFE_DEBUG
};

class OneThread : public OneThreadAccess
{
  public:
    using reference_type = OneThreadRef;

  protected:
    template<typename BASE, typename POLICY_MUTEX> friend class ::threadsafe::UnlockedBase;

#if THREADSAFE_DEBUG
    mutable std::thread::id m_thread_id;

    // Hijack mutex() to pass the reference to m_thread_id %-).
    std::thread::id& mutex() const { return m_thread_id; }
#else
    int mutex() const { return {}; }
#endif // THREADSAFE_DEBUG
};

} // namespace policy

template<typename T, typename POLICY_MUTEX>
requires std::derived_from<T, AIRefCount>
void intrusive_ptr_add_ref(Unlocked<T, POLICY_MUTEX> const* ptr)
{
  intrusive_ptr_add_ref(static_cast<T const*>(ptr));
}

template<typename T, typename POLICY_MUTEX>
requires std::derived_from<T, AIRefCount>
void intrusive_ptr_release(Unlocked<T, POLICY_MUTEX> const* ptr)
{
  intrusive_ptr_release(static_cast<T const*>(ptr));
}

} // namespace threadsafe
