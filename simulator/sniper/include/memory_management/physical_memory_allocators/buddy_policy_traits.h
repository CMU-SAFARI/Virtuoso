// Default: no mapping (compile error if used without specialization)
// PROBLEM: a classic compile‑time policy mapping problem:
//      >> You want to map ReservationTHPAllocator’s Policy to a different Buddy policy at compile time.
// SOLUTION: We create a type trait that takes the ReservationTHPAllocator’s policy type and gives back the correct Buddy policy type.
// USE-CASE: In phys. memory allocator, before instantiating the Buddy, define 'using BuddyPolicy = typename BuddyPolicyFor<Policy>::type;'
//           buddy_allocator = new Buddy<BuddyPolicy>(memory_size, max_order, kernel_size, frag_type);

// This is a neutral traits template — it just declares the mapping.
// Actual specializations are provided in side-specific buddy_policy.h files.
template <typename AllocPolicy>
struct BuddyPolicyFor;