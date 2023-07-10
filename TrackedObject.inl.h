#ifndef THREADSAFE_TRACKED_OBJECT_H
#error "TrackedObject.inl.h only needs to be included in TU's that include TrackedObject.h (directly or indirectly). Include it below other header files."
#endif

// The second template parameter, Tracker, is incomplete while
// processing the header.
//
// The functions in this file access the Tracker pointed to by tracker_
// and therefore requires Tracker to be complete.
//
// Include this header in every .cxx file that include ObjectTracker.h
// directly or indirectly.

namespace threadsafe {

template<typename TrackedType, typename Tracker>
TrackedObject<TrackedType, Tracker>::TrackedObject() :
  tracker_(std::make_shared<Tracker>(utils::Badge<TrackedObject>{}, *static_cast<TrackedType*>(this)))
{
  static_assert(utils::is_complete_v<Tracker>,
      "Include the header of Tracker before including threadsafe/TrackedObject.inl.h.");
  static_assert(utils::is_derived_from_specialization_of_v<Tracker, ObjectTracker>,
      "Tracker must be derived from threadsafe::ObjectTracker<TrackedType>.");

  tracker_->set_is_initialized();
}

template<typename TrackedType, typename Tracker>
TrackedObject<TrackedType, Tracker>::TrackedObject(TrackedObject&& orig) : tracker_(std::move(orig.tracker_))
{
  // The orig object must be write-locked (blocking all concurrent access)!
  tracker_->set_tracked_unlocked(utils::Badge<TrackedObject>{}, static_cast<typename Tracker::tracked_type*>(this));
}

template<typename TrackedType, typename Tracker>
TrackedObject<TrackedType, Tracker>::~TrackedObject()
{
  if (tracker_) // This is null if the tracked object was moved.
    tracker_->set_tracked_unlocked(utils::Badge<TrackedObject>{}, nullptr);
}

} // namespace threadsafe
