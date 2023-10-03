#pragma once

#include "AIReadWriteSpinLock.h"
#include "threadsafe.h"
#include "utils/Badge.h"
#include <memory>
#include <atomic>
#include "debug.h"

// threadsafe::ObjectTracker
//
// Allows objects to have a non-moving tracker object, allocated on the heap,
// that manages a pointer that points back to them, all while being thread-safe.
//
// This allows other objects to point to the tracker object without having
// to worry if the tracked object is moved in memory.
//
// Usage:
#if -0 // EXAMPLE_CODE

// Forward declare the object that should be tracked and protected against concurrent access.
class locked_Node;

// Define the Unlocked version (using the desired locking policy).
using Node = threadsafe::UnlockedTrackedObject<locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;

// Define a corresponding tracker class.
using NodeTracker = threadsafe::ObjectTracker<Node>;

// Optionally derive from ObjectTracker.
class NodeTracker : public threadsafe::ObjectTracker<Node>
{
 public:
  // The argument must be a Node const& which is passed to ObjectTracker base class.
  NodeTracker(Node const& tracked) : threadsafe::ObjectTracker<Node>(tracked) { }
};

// The to-be-tracked object must be derived from threadsafe::TrackedObject,
// passing both, the Unlocked type as well as the tracker class.
class locked_Node : public threadsafe::TrackedObject<Node, NodeTracker>
{
 private:
  std::string s_;

 public:
  Node(std::string const& s) : s_(s) { }

  std::string const& s() const { return s_; }
};

int main()
{
  // Now one can construct a Node object:
  Node node("hello");
  // And obtain the tracker at any moment (also after it was moved):
  std::weak_ptr<NodeTracker> node_tracker = node;

  // Even if node is moved,
  Node node2(std::move(node));
  // node_tracker will point to node2:
  auto node_r{node_tracker.lock()->tracked_rat()};
  std::cout << "s = " << node_r->s() << std::endl;  // Prints "s = hello".
}
#endif // EXAMPLE_CODE

namespace threadsafe {

// UnlockedTrackedObject
//
// The type of an unlocked tracked object: this type wraps TrackedLockedType
// protecting it against concurrent access using POLICY_MUTEX, just like Unlocked
// and UnlockedBase, but simultaneously supports tracking (TrackedLockedType
// must be derived from TrackedObject<...> which creates the tracker and
// makes a reference to the that tracker available through the `tracker()`
// member function).
//
// Note that you can not derive from UnlockedTrackedObject (that is why it is
// marked 'final'): allowing that would mean that the mutex that is locked
// by the call to orig.do_wrlock() is not locked during the move constructor
// of the derived class.
//
template<typename TrackedLockedType, typename POLICY_MUTEX>
class UnlockedTrackedObject final : public Unlocked<TrackedLockedType, POLICY_MUTEX>
{
 public:
  // Provide all the normal constructors of Unlocked, except the move constructor which is overridden below.
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::Unlocked;

  // Move constructor with tracking support: if this (final) object is moved then first
  // the mutex of orig is write locked, then that object is "moved" (moving the underlaying
  // data object, but creating a new policy mutex). This also updates the pointer of the
  // tracker that points to that data object. Finally, in the body of this constructor,
  // the tracker is updated to point to the newly created mutex after which orig is unlocked.
  UnlockedTrackedObject(UnlockedTrackedObject&& orig) :
    Unlocked<TrackedLockedType, POLICY_MUTEX>(std::move(orig.do_wrlock()), this->NoLock)
  {
    auto& mutex = this->mutex();
    this->tracker_->update_mutex_pointer(&mutex);
    orig.do_wrunlock();
  }

  // Give access to tracker.
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::tracker;
  using Unlocked<TrackedLockedType, POLICY_MUTEX>::operator std::weak_ptr<typename Unlocked<TrackedLockedType, POLICY_MUTEX>::tracker_type>;
};

// ObjectTracker
//
// The type of the tracker returned by the above class (UnlockedTrackedObject).
// It is based around an UnlockedBase that provides both, the data pointer
// and the data mutex pointer and is itself protected by Unlocked.
//
// The template parameter TrackedType must be the above UnlockedTrackedObject.
// This class then must be passed as second template parameter to TrackedObject,
// see below.
//
// Note that it is allowed to derive from ObjectTracker, but a typical usage
// will be to use it as-is for your tracker type.
//
template<typename TrackedType>
requires utils::is_specialization_of_v<TrackedType, UnlockedTrackedObject>  // The tracked type must be an UnlockedTrackedObject.
class ObjectTracker
{
 public:
  using tracked_type = TrackedType;
  using tracked_locked_type = typename tracked_type::data_type;
  using policy_type = typename tracked_type::policy_type;

 private:
  // This class is used to get access to the protected m_base.
  class UnlockedBaseTrackedObject : public UnlockedBase<tracked_locked_type, policy_type>
  {
   public:
    using UnlockedBase<tracked_locked_type, policy_type>::UnlockedBase;

    void set_tracked_unlocked(tracked_locked_type* base)
    {
      this->m_base = base;
    }

    void update_mutex_pointer(auto* mutex_ptr)
    {
      this->m_read_write_mutex_ptr = mutex_ptr;
    }
  };

 public:
  using crat = typename UnlockedBaseTrackedObject::crat;
  using rat = typename UnlockedBaseTrackedObject::rat;
  using wat = typename UnlockedBaseTrackedObject::wat;
  using w2rCarry = typename UnlockedBaseTrackedObject::w2rCarry;

 protected:
  using tracked_unlocked_ptr_type = Unlocked<UnlockedBaseTrackedObject, policy::ReadWrite<AIReadWriteSpinLock>>;
  tracked_unlocked_ptr_type tracked_unlocked_ptr_;

  // Used by trackers that are derived from ObjectTracker.
  ObjectTracker(tracked_type& tracked_unlocked) : tracked_unlocked_ptr_(tracked_unlocked) { }

 public:
  // Construct a new ObjectTracker that tracks tracked_unlocked.
  ObjectTracker(utils::Badge<TrackedObject<tracked_type, ObjectTracker>>, tracked_type const& tracked_unlocked) :
    tracked_unlocked_ptr_(tracked_unlocked) { }

  // This is called when the object is moved in memory, see below.
  void set_tracked_unlocked(utils::Badge<TrackedObject<tracked_type, ObjectTracker>>, tracked_type* tracked_unlocked_ptr)
  {
    typename tracked_unlocked_ptr_type::wat tracked_unlocked_ptr_w(tracked_unlocked_ptr_);
    // This is called while the mutex of the tracked_type is locked.
    tracked_unlocked_ptr_w->set_tracked_unlocked(tracked_unlocked_ptr);
  }

  void update_mutex_pointer(auto* mutex_ptr)
  {
    typename tracked_unlocked_ptr_type::wat tracked_unlocked_ptr_w(tracked_unlocked_ptr_);
    tracked_unlocked_ptr_w->update_mutex_pointer(mutex_ptr);
  }

  // Accessors.
  rat tracked_rat()
  {
    typename tracked_unlocked_ptr_type::rat tracked_unlocked_ptr_r(tracked_unlocked_ptr_);
    // rat wants to be explicitly constructed from a non-const reference.
    return rat{const_cast<UnlockedBaseTrackedObject&>(*tracked_unlocked_ptr_r)};
  }
  wat tracked_wat()
  {
    typename tracked_unlocked_ptr_type::rat tracked_unlocked_ptr_r(tracked_unlocked_ptr_);
    return wat{const_cast<UnlockedBaseTrackedObject&>(*tracked_unlocked_ptr_r)};
  }
};

// TrackedObject
//
// Base class of the unlocked data type.
// The template parameters must be the two types defined above (UnlockedTrackedObject and ObjectTracker respectively).
//
template<typename TrackedType, typename Tracker>
requires utils::is_specialization_of_v<Tracker, ObjectTracker>
class TrackedObject
{
 public:
  using tracked_type = TrackedType;
  using tracker_type = Tracker;

 protected:
  std::shared_ptr<Tracker> tracker_;

  TrackedObject() : tracker_(std::make_shared<Tracker>(utils::Badge<TrackedObject>{}, *static_cast<tracked_type*>(this)))
  {
  }

  TrackedObject(TrackedObject&& orig) : tracker_(std::move(orig.tracker_))
  {
    // The orig object must be write-locked (blocking all concurrent access)!
    tracker_->set_tracked_unlocked({}, static_cast<typename Tracker::tracked_type*>(this));
  }

  ~TrackedObject()
  {
    if (tracker_) // This is null if the tracked object was moved.
      tracker_->set_tracked_unlocked({}, nullptr);
  }

 public:
  // Accessor for the Tracker object. Make sure to keep the TrackedObject alive while using this.
  Tracker const& tracker() const
  {
    // Note that tracker_ can only be null when the Tracker was moved.
    // Do not call this function (or any other member function except the destructor) on a moved object!
    ASSERT(tracker_);
    return *tracker_;
  }

  Tracker& tracker()
  {
    // See above.
    ASSERT(tracker_);
    return *tracker_;
  }

  // Automatic conversion to a weak_ptr.
  operator std::weak_ptr<Tracker>() const { return tracker_; }
};

} // namespace threadsafe
