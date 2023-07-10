#ifndef THREADSAFE_UNLOCKED_TRACKED_OBJECT_H
#define THREADSAFE_UNLOCKED_TRACKED_OBJECT_H

#include "threadsafe.h"
#include "utils/is_complete.h"
#include <type_traits>
#include <utility>
#include <memory>

namespace threadsafe {

#ifdef CWDEBUG
template<typename TrackedLockedType, typename POLICY_MUTEX>
class SanityCheckArgsOfUnlockedTrackedObject
{
  static_assert(utils::is_complete_v<TrackedLockedType>,
      "The first template parameter of UnlockedTrackedObject must already be complete, but isn't.");
  static_assert(utils::is_complete_v<POLICY_MUTEX>,
      "The second template parameter of UnlockedTrackedObject must already be complete, but isn't.");
  static_assert(utils::is_derived_from_specialization_of_v<TrackedLockedType, TrackedObject>,
      "The first template parameter of UnlockedTrackedObject must be derived from threadsafe::TrackedObject<TrackedType, Tracker>, but isn't.");
};
#endif

// UnlockedTrackedObject
//
// The type of an unlocked tracked object: this type wraps TrackedLockedType
// protecting it against concurrent access using POLICY_MUTEX, just like Unlocked
// and UnlockedBase, but simultaneously supports tracking (TrackedLockedType
// must be derived from TrackedObject<...> which creates the tracker and
// makes a reference to the that tracker available through the `tracker()`
// member function).
//
// Note: both TrackedLockedType and POLICY_MUTEX must be complete at this point
// because the base class Unlocked<TrackedLockedType, POLICY_MUTEX> derives from
// both.
template<typename TrackedLockedType, typename POLICY_MUTEX>
class UnlockedTrackedObject :
#ifdef CWDEBUG
  SanityCheckArgsOfUnlockedTrackedObject<TrackedLockedType, POLICY_MUTEX>,
#endif
  public Unlocked<TrackedLockedType, POLICY_MUTEX>
{
 public:
  // Provide all the normal constructors of Unlocked, except the move constructor which is overridden below.
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::Unlocked;

  // Move constructor with tracking support: if this (final) object is moved then first
  // the mutex of orig is write locked, then that object is "moved" (moving the underlaying
  // data object, but creating a new policy mutex). This also updates the pointer of the
  // tracker that points to that data object. Finally, in the body of this constructor,
  // the tracker is updated to point to the newly created mutex after which orig is unlocked.
  //
  // If this is not the most derived class then do not use this constructor!
  template<typename... ARGS>
  requires ((!std::is_base_of_v<Unlocked<TrackedLockedType, POLICY_MUTEX>, std::decay_t<ARGS>> &&
             !std::is_same_v<typename Unlocked<TrackedLockedType, POLICY_MUTEX>::no_locking_t, std::decay_t<ARGS>>) && ... )
  explicit UnlockedTrackedObject(UnlockedTrackedObject&& orig, ARGS&&... args) :
    Unlocked<TrackedLockedType, POLICY_MUTEX>(std::move(orig.do_wrlock()), this->noLock, std::forward<ARGS>(args)...)
  {
    auto& mutex = this->mutex();
    this->tracker_->update_mutex_pointer(&mutex);
    orig.do_wrunlock();
  }

 protected:
  // This move-constructor can be used from a derived class, which then has to do the locking!
  template<typename... ARGS>
  requires (!std::is_base_of_v<Unlocked<TrackedLockedType, POLICY_MUTEX>, std::decay_t<ARGS>> && ...)
  explicit UnlockedTrackedObject(UnlockedTrackedObject&& orig, typename Unlocked<TrackedLockedType, POLICY_MUTEX>::no_locking_t nolock, ARGS&&... args) :
    Unlocked<TrackedLockedType, POLICY_MUTEX>(std::move(orig), nolock, std::forward<ARGS>(args)...)
  {
    auto& mutex = this->mutex();
    this->tracker_->update_mutex_pointer(&mutex);
  }

 public:
  // Give access to tracker.
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::tracker;
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::operator std::weak_ptr<typename Unlocked<TrackedLockedType, POLICY_MUTEX>::tracker_type>;
};

} // namespace threadsafe

#endif // THREADSAFE_UNLOCKED_TRACKED_OBJECT_H
