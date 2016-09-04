//===- lib/Trace/TraceMemory.cpp ------------------------------------------===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "seec/Trace/TraceMemory.hpp"
#include "seec/Util/Maybe.hpp"

namespace seec {

namespace trace {

void TraceMemoryState::add(uintptr_t Address,
                           std::size_t Length)
{
  // Make room for the fragment.
  clear(Address, Length);

  // Add the new fragment.
  // TODO: get an iterator from clear() to hint to insert.
  Fragments.insert(std::make_pair(Address,
                                  TraceMemoryFragment(Address,
                                                      Length)));
}

void TraceMemoryState::memmove(uintptr_t const Source,
                               uintptr_t const Destination,
                               std::size_t const Size)
{
  auto const SourceEnd = Source + Size;
  
  // Create the new fragments.
  decltype(Fragments) Moved;
  auto MovedInsert = Moved.end();
  
  // Get the first source fragment starting >= Source.
  auto It = Fragments.lower_bound(Source);
  
  // Check if the previous fragment overlaps.
  if (It != Fragments.begin()
      && (It == Fragments.end() || It->first > Source)) {
    --It;
    
    if (It->second.area().lastAddress() >= Source) {
      // Previous fragment overlaps with our start.
      if (It->second.area().end() >= SourceEnd) {
        // Fragment completely covers the move area.
        MovedInsert =
            Moved.insert(MovedInsert,
                         std::make_pair(Destination,
                                        TraceMemoryFragment(Destination,
                                                            Size)));
      }
      else {
        // Copy the right-hand side of the fragment.
        auto const NewSize = It->second.area().withStart(Source).length();
        
        MovedInsert =
            Moved.insert(MovedInsert,
                         std::make_pair(Destination,
                                        TraceMemoryFragment(Destination,
                                                            NewSize)));
      }
    }
    
    ++It;
  }
  
  // Find remaining overlapping fragments.  
  while (It != Fragments.end() && It->first < SourceEnd) {
    auto const NewAddress = Destination + (It->first - Source);
    
    // If this fragment exceeds the move's source range, trim the size.
    auto const NewSize = It->second.area().end() <= SourceEnd
                       ? It->second.area().length()
                       : It->second.area().withEnd(Source + Size).length();
    
    MovedInsert =
          Moved.insert(MovedInsert,
                       std::make_pair(NewAddress,
                                      TraceMemoryFragment(NewAddress,
                                                          NewSize)));
    
    ++It;
  }
  
  // Make room for the fragments.
  clear(Destination, Size);
  
  // Add the fragments (it would be better if we could move them).
  Fragments.insert(Moved.begin(), Moved.end());
}

void TraceMemoryState::clear(uintptr_t Address,  std::size_t Length)
{
  auto LastAddress = Address + (Length - 1);
  
  // Get the first fragment starting >= Address.
  auto It = Fragments.lower_bound(Address);

  // Best-case scenario: perfect removal of a previous state.
  if (It != Fragments.end()
      && It->first == Address
      && It->second.area().lastAddress() == LastAddress)
  {
    Fragments.erase(It);
    return;
  }

  // Check if the previous fragment overlaps.
  if (It != Fragments.begin()
      && (It == Fragments.end() || It->first > Address))
  {
    if ((--It)->second.area().lastAddress() >= Address) {
      // Previous fragment overlaps with our start. Check if we are splitting
      // the fragment or performing a right-trim.
      if (It->second.area().lastAddress() > LastAddress) { // Split
        // Create a new fragment to use on the right-hand side.
        auto RightFragment = It->second;
        RightFragment.area().setStart(LastAddress + 1);
        
        // Resize the fragment to remove the right-hand side and overlap.
        It->second.area().setEnd(Address);
        
        // Add the right-hand side fragment.
        // TODO: Hint this insertion.
        Fragments.insert(std::make_pair(LastAddress + 1,
                                        std::move(RightFragment)));
      }
      else { // Right-Trim
        // Resize the fragment to remove the overlapping area.
        It->second.area().setEnd(Address);
      }
    }
    
    ++It;
  }

  // Find remaining overlapping fragments.
  while (It != Fragments.end() && It->first <= LastAddress) {
    if (It->second.area().lastAddress() <= LastAddress) {
      // Remove internally overlapping fragment.
      Fragments.erase(It++);
    }
    else {
      // Reposition right-overlapping fragment.
      auto Fragment = std::move(It->second);
      Fragments.erase(It++);
      Fragment.area().setStart(LastAddress + 1);
      Fragments.insert(std::make_pair(LastAddress + 1, std::move(Fragment)));
      
      break;
    }
  }
}

bool TraceMemoryState::hasKnownState(uintptr_t Address,
                                     std::size_t Length) const
{
  auto AreaEnd = Address + Length;
  
  // Get the first fragment starting >= Address.
  auto It = Fragments.lower_bound(Address);
  
  // If there was no fragment, we only have to check the last fragment in the
  // map (if there isn't one, then the state can't possibly be known).
  if (It == Fragments.end()) {
    if (It == Fragments.begin())
      return false;
    
    --It;
    
    return It->second.area().end() >= AreaEnd;
  }
  
  // If necessary, rewind to the previous fragment.
  if (It->first > Address) {
    // The left-hand side of the area (at least) is uninitialized.
    if (It == Fragments.begin())
      return false;
    
    --It;
    
    // The left-hand side of the area (at least) is uninitialized.
    if (It->second.area().lastAddress() < Address)
      return false;
  }
  
  // This single fragment covers the entire area.
  if (It->second.area().end() >= AreaEnd)
    return true;
  
  // Next address after the end of the previous fragment. If the next fragment
  // doesn't start here, there's an uninitialized gap in the area.
  auto NextAddress = It->second.area().end();
  
  while (++It != Fragments.end()) {
    if (It->first != NextAddress)
      return false;
    
    // This fragment covers the remainder of the area!
    if (It->second.area().end() >= AreaEnd)
      return true;
    
    NextAddress = It->second.area().end();
  }
  
  // The right-hand side of the area is uninitialized.
  return false;
}

size_t TraceMemoryState::getLengthOfKnownState(uintptr_t Address,
                                               std::size_t MaxLength)
const
{
  auto const AreaEnd = Address + MaxLength;
  
  // Get the first fragment starting >= Address.
  auto It = Fragments.lower_bound(Address);
  
  // If there was no fragment, we only have to check the last fragment in the
  // map (if there isn't one, then the state can't possibly be known).
  if (It == Fragments.end()) {
    if (It == Fragments.begin())
      return 0;
    
    --It;
    
    return It->second.area().end() - Address;
  }
  
  // If necessary, rewind to the previous fragment.
  if (It->first > Address) {
    // The left-hand side of the area (at least) is uninitialized.
    if (It == Fragments.begin())
      return 0;
    
    --It;
    
    // The left-hand side of the area (at least) is uninitialized.
    if (It->second.area().lastAddress() < Address)
      return 0;
  }
  
  // This single fragment covers the entire area.
  if (It->second.area().end() >= AreaEnd)
    return It->second.area().end() - Address;
  
  // Next address after the end of the previous fragment. If the next fragment
  // doesn't start here, there's an uninitialized gap in the area.
  auto NextAddress = It->second.area().end();
  
  while (++It != Fragments.end()) {
    if (It->first != NextAddress)
      return NextAddress - Address;
    
    // This fragment covers the remainder of the area!
    if (It->second.area().end() >= AreaEnd)
      return It->second.area().end() - Address;
    
    NextAddress = It->second.area().end();
  }
  
  // The right-hand side of the area is uninitialized.
  return NextAddress - Address;
}

} // namespace trace (in seec)

} // namespace seec
