// Compatibility stdatomic.h for GCC 4.8.5
// This file provides C11 atomic operations using GCC built-in functions
#ifndef COMPAT_STDATOMIC_H
#define COMPAT_STDATOMIC_H

// Atomic type definitions
#define atomic_int int
#define atomic_uint unsigned int
#define atomic_uint_fast unsigned int
#define atomic_uintptr_t unsigned long
#define atomic_uintptr_fast unsigned long
#define atomic_size_t size_t
#define atomic_ptrdiff_t ptrdiff_t

// Basic atomic operations
#define atomic_store(ptr, val) (*(ptr) = (val))
#define atomic_load(ptr) (*(ptr))

// Atomic arithmetic operations
#define atomic_fetch_add(ptr, val) __sync_fetch_and_add(ptr, val)
#define atomic_fetch_sub(ptr, val) __sync_fetch_and_sub(ptr, val)
#define atomic_fetch_or(ptr, val) __sync_fetch_and_or(ptr, val)
#define atomic_fetch_and(ptr, val) __sync_fetch_and_and(ptr, val)
#define atomic_fetch_xor(ptr, val) __sync_fetch_and_xor(ptr, val)

// Atomic compare and exchange operations
#define atomic_compare_exchange_strong(ptr, expected, desired) __sync_bool_compare_and_swap(ptr, *(expected), desired)

#define atomic_compare_exchange_weak(ptr, expected, desired) __sync_bool_compare_and_swap(ptr, *(expected), desired)

// Atomic exchange operation
#define atomic_exchange(ptr, desired) __sync_lock_test_and_set(ptr, desired)

// Memory order definitions (simplified)
#define memory_order_relaxed 0
#define memory_order_acquire 1
#define memory_order_release 2
#define memory_order_acq_rel 3
#define memory_order_seq_cst 4

// Atomic operations with explicit memory order (simplified implementation)
#define atomic_store_explicit(ptr, val, order) atomic_store(ptr, val)
#define atomic_load_explicit(ptr, order) atomic_load(ptr)
#define atomic_fetch_add_explicit(ptr, val, order) atomic_fetch_add(ptr, val)
#define atomic_fetch_sub_explicit(ptr, val, order) atomic_fetch_sub(ptr, val)
#define atomic_exchange_explicit(ptr, desired, order) atomic_exchange(ptr, desired)

// Atomic flag operations
#define atomic_flag_test_and_set(flag) __sync_lock_test_and_set(flag, 1)
#define atomic_flag_clear(flag) __sync_lock_release(flag)

// Atomic initialization macros
#define ATOMIC_VAR_INIT(value) (value)
#define atomic_init(ptr, value) (*(ptr) = (value))

// Atomic type initialization
#define atomic_int_init(ptr, value) atomic_init(ptr, value)
#define atomic_uint_init(ptr, value) atomic_init(ptr, value)

#endif  // COMPAT_STDATOMIC_H