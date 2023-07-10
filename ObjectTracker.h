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

//-----------------------------------------------------------------------------

#if UNLOCKED_TYPE_IS_TYPEDEF
// 1) If Node is a typedef, then it has to be defined next:
// Define the Unlocked version (using the desired locking policy):
using Node = threadsafe::UnlockedTrackedObject<locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;
#else
// 2) otherwise, forward declare Node.
class Node;
#endif

// Then define a corresponding tracker class.
#if TRACKER_IS_TYPEDEF
//   Either also as a typedef:
using NodeTracker = threadsafe::ObjectTracker<Node, locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;
#else
//   Or derived from ObjectTracker:
//
//   Note: if you run into the compile error 'incomplete locked_Node' or UnlockedTrackedObject<locked_Node, ...> during the
//   instantiation of the destructor of this NodeTracker class, then move its constructors and destructor out of the header
//   (to a .cxx file).
class NodeTracker : public threadsafe::ObjectTracker<Node, locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>
{
 public:
  // The arguments must be a Badge and a Node const& which is passed to ObjectTracker base class.
  NodeTracker(utils::Badge<threadsafe::TrackedObject<Node, NodeTracker>>, Node const& tracked) :
    threadsafe::ObjectTracker<Node, locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>(tracked) { }
};
#endif

// Finally define the locked type (the type that can only be accessed through an
// access object, like NodeTracker::rat or NodeTracker::wat).
//
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

#if !UNLOCKED_TYPE_IS_TYPEDEF
// If Node is not a typedef (2) then define Node after locked_Node:
class Node : public threadsafe::UnlockedTrackedObject<locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>
{
 public:
  using threadsafe::ObjectTracker<Node, locked_Node, threadsafe::policy::Primitive<std::mutex>>::ObjectTracker;
};
#endif

int main()
{
  // Now one can construct a Node object:
  Node node("hello");
  // And obtain the tracker at any moment (also after it was moved):
  std::weak_ptr<NodeTracker> node_tracker = node;

  // And then even if node is moved,
  Node node2(std::move(node));
  // node_tracker will point to the correct instance (node2 in this case):
  auto node_r{node_tracker.lock()->tracked_rat()};
  std::cout << "s = " << node_r->s() << std::endl;  // Prints "s = hello".
}
#endif // EXAMPLE_CODE

namespace threadsafe {

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
template<typename TrackedType, typename TrackedLockedType, typename POLICY_MUTEX>
class ObjectTracker
{
 public:
  using tracked_type = TrackedType;
  using tracked_locked_type = TrackedLockedType;
  using policy_type = POLICY_MUTEX;
  using unlocked_type = UnlockedBase<tracked_locked_type, policy_type>;

  using crat = typename unlocked_type::crat;
  using rat = typename unlocked_type::rat;
  using wat = typename unlocked_type::wat;
  using w2rCarry = typename unlocked_type::w2rCarry;

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
      if constexpr (std::is_same_v<wat, WriteAccess<Unlocked<tracked_type, policy_type>>>)
        this->m_read_write_mutex_ptr = mutex_ptr;
      else if constexpr (std::is_same_v<wat, Access<Unlocked<tracked_type, policy_type>>>)
        this->m_primitive_mutex_ptr = mutex_ptr;
    }
  };

 protected:
  using tracked_unlocked_ptr_type = Unlocked<UnlockedBaseTrackedObject, policy::ReadWrite<AIReadWriteSpinLock>>;
  tracked_unlocked_ptr_type tracked_unlocked_ptr_;

  // Used by trackers that are derived from ObjectTracker.
  ObjectTracker(tracked_type& tracked_unlocked) : tracked_unlocked_ptr_(tracked_unlocked) { }

 public:
  // Construct a new ObjectTracker that tracks tracked_unlocked.
  template<typename TrackerType>
  ObjectTracker(utils::Badge<TrackedObject<tracked_type, TrackerType>>, tracked_type const& tracked_unlocked) :
    tracked_unlocked_ptr_(tracked_unlocked) { }

  // This is called when the object is moved in memory, see below.
  template<typename TrackerType>
  void set_tracked_unlocked(utils::Badge<TrackedObject<tracked_type, TrackerType>>, tracked_type* tracked_unlocked_ptr)
  {
    // This function should not be called while the tracker is being constructed!
    ASSERT(debug_initialized_);
    typename tracked_unlocked_ptr_type::wat tracked_unlocked_ptr_w(tracked_unlocked_ptr_);
    // This is called while the mutex of the tracked_type is locked.
    tracked_unlocked_ptr_w->set_tracked_unlocked(tracked_unlocked_ptr);
  }

  void update_mutex_pointer(auto* mutex_ptr)
  {
    // This function should not be called while the tracker is being constructed!
    ASSERT(debug_initialized_);
    typename tracked_unlocked_ptr_type::wat tracked_unlocked_ptr_w(tracked_unlocked_ptr_);
    tracked_unlocked_ptr_w->update_mutex_pointer(mutex_ptr);
  }

  // Accessors.
  rat tracked_rat()
  {
    // This function should not be called while the tracker is being constructed!
    ASSERT(debug_initialized_);
    typename tracked_unlocked_ptr_type::rat tracked_unlocked_ptr_r(tracked_unlocked_ptr_);
    // rat wants to be explicitly constructed from a non-const reference.
    return rat{const_cast<UnlockedBaseTrackedObject&>(*tracked_unlocked_ptr_r)};
  }
  wat tracked_wat()
  {
    // This function should not be called while the tracker is being constructed!
    ASSERT(debug_initialized_);
    typename tracked_unlocked_ptr_type::rat tracked_unlocked_ptr_r(tracked_unlocked_ptr_);
    return wat{const_cast<UnlockedBaseTrackedObject&>(*tracked_unlocked_ptr_r)};
  }

#if CW_DEBUG
 private:
  bool debug_initialized_ = false;

 public:
  void set_is_initialized() { debug_initialized_ = true; }
#endif
};

} // namespace threadsafe
