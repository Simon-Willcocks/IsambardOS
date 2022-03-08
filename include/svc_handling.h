#include "isambard_syscalls.h"

#ifndef WITHOUT_GATE
static inline thread_switch handle_svc_gate( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

if (thread->partner == 0 && ((thread->spsr & 0x1e) == 8 || 0 != (thread->spsr & 0x10))) {
  asm ( "smc 5" );
}

  static const int32_t THREAD_WAITING = -1;

  // Thread parameter x0: 0 = this thread should wait, <>0 thread to wake
  // Timeout parameter x1: 0 => wait forever, > 0 => wait this many ticks
  //
  // Exception to above, thread = interrupt thread => timer tick, but only
  // from system driver.
  //
  // Timeout queue is implemented using x16 and x17; threads are only
  // put in the queue if they've called wake_until_woken, which means
  // they're not expecting those registers to be preserved.
  //
  // The gate value can be modified by other threads, so a regular
  // thread register cannot be used. (The other thread has to be in
  // the same map as the blocked one.)
  //
  // Ticks to wait after the previous entry times out will be preserved in x1.
  //
  // wait_until_woken( timeout ) returns
  // A > 0 if it happened without needing to be blocked (the number of
  //       times it was released)
  // B = 0 if it blocked, and was then released (the normal situation)
  // C < 0 if the wait timed out
  //
  // wake_thread( thread ) returns the previous value of gate
  if (thread->regs[0] == 0) { // Wait for gate, or timer tick
    if (thread == core->interrupt_thread) {
      if (thread->current_map != system_map_index) {
        BSOD( __COUNTER__ ); // FIXME: Throw an exception, an interrupt handler tried to wait!
      }
      // Timer tick
      if (core->blocked_with_timeout != 0) {
        thread_context *blocked_list_head = core->blocked_with_timeout;
#ifdef QEMU
  core->blocked_with_timeout->regs[1] = 1;
#endif
        if (--core->blocked_with_timeout->regs[1] == 0) {
          do {
            blocked_list_head->regs[0] = -1; // C
            blocked_list_head->gate = 0; // No longer blocked
            insert_new_thread_after_old( blocked_list_head, thread );
            blocked_list_head = (void*) blocked_list_head->regs[17];
          } while (blocked_list_head != 0 && blocked_list_head->regs[1] == 0);
          core->blocked_with_timeout = blocked_list_head;
          if (blocked_list_head != 0) {
            // Pointer back to the pointer pointing to the head :)
            blocked_list_head->regs[16] = (integer_register) &core->blocked_with_timeout;
          }
        }
      }
    }
    else if (thread->gate > 0) {
      thread->regs[0] = thread->gate; // A
      thread->gate = 0;
    }
    else {
      result.now = thread->next;
      core->runnable = thread->next;
      remove_thread( thread );
      thread->gate = THREAD_WAITING;
      thread->regs[0] = 0; // B (when it finally returns)

      if (thread->regs[1] > 0) {
        thread_context **prev = &core->blocked_with_timeout;
        thread_context *blocked_thread = *prev;
        integer_register remaining = thread->regs[1];
        while (blocked_thread != 0 && remaining > blocked_thread->regs[1]) {
          remaining -= blocked_thread->regs[1];
          prev = (void*) &blocked_thread->regs[17];
          blocked_thread = *prev;
        }
        thread->regs[1] = remaining;
        thread->regs[17] = (integer_register) blocked_thread; // Next in list, if any
        thread->regs[16] = (integer_register) prev; // Pointer to pointer to this thread
        *prev = thread;

        if (blocked_thread != 0) {
          // The next thread has remaining fewer ticks to wait, once this thread times out
          blocked_thread->regs[1] -= remaining;
          blocked_thread->regs[16] = (integer_register) &thread->regs[17];
        }
      }
      else {
        thread->regs[16] = 0; // Not in blocked_with_timeout queue
        thread->regs[17] = 0; // Not in blocked_with_timeout queue
      }
    }
  }
  else if (is_real_thread( thread->regs[0] )) {
    // Resume thread

    thread_context *release_thread = thread_from_code( thread->regs[0] );

    if (release_thread->gate == THREAD_WAITING) {
      if (thread->current_map == release_thread->current_map) { // More checks?
        // Indicates the thread is blocked 
        insert_new_thread_after_old( release_thread, thread );
        release_thread->gate = 0;
        if (release_thread->regs[16] != 0) {
          // The blocked thread is in a timeout list
          thread_context **prevp = (thread_context **) release_thread->regs[16];
          thread_context *next = (thread_context *) release_thread->regs[17];
          *prevp = next;
          if (next != 0) {
            next->regs[1] += release_thread->regs[1];
            next->regs[16] = release_thread->regs[16];
          }
        }
      }
      else {
        invalidate_all_caches();
	// This could, possibly, be legal. Probably not useful.
        BSOD( __COUNTER__ ); // Threads not blocked in same map
      }
    }
    else if (release_thread->gate < 0x7fffffff) { // No more than that, which is certainly an error, or an attack
      release_thread->gate++;
    }
  }
  else {
    BSOD( __COUNTER__ ); // Not real thread
  }

  return result;
}
#endif

#ifndef WITHOUT_INTERFACE_CREATION
static inline thread_switch new_interface( Core *core, thread_context *thread, interface_index user, interface_index provider, integer_register handler, integer_register value )
{
  core = core;
  Interface *e = obtain_interface();

  e->provider = provider;
  e->user = user;
  e->handler = handler;
  e->object.as_number = value;

  thread->regs[0] = index_from_interface( e );

  // Stays with the same thread, for now. When an interface cannot be obtained, this thread will
  // be paused while another allocates more space. Probably. Anyway, that will be done here.
  thread_switch result = { .then = thread, .now = thread };
  return result;
}

static inline thread_switch handle_svc_duplicate_to_return( Core *core, thread_context *thread )
{
  Interface *interface = interface_from_index( thread->regs[0] );

  if (0 == interface) {
    BSOD( __COUNTER__ );
  }

  if (interface->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  return new_interface( core, thread, thread->stack_pointer[0].caller_map, interface->provider, interface->handler, interface->object.as_number );
}

static inline thread_switch handle_svc_duplicate_to_pass_to( Core *core, thread_context *thread )
{
  // FIXME lots of testing!
  Interface *interface = interface_from_index( thread->regs[1] );

  if (0 == interface) {
    BSOD( __COUNTER__ );
  }
 
  if (interface->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  Interface *target = interface_from_index( thread->regs[0] );
  if (0 == target) {
    BSOD( __COUNTER__ );
  }
 
  if (target->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  return new_interface( core, thread, target->provider, interface->provider, interface->handler, interface->object.as_number );
}

static inline thread_switch handle_svc_interface_to_pass_to( Core *core, thread_context *thread )
{
  if (thread->regs[1] & 0x3)
    asm ( "smc 13" );

  // FIXME lots of testing!
  Interface *interface = interface_from_index( thread->regs[0] );
  if (0 == interface) {
    BSOD( __COUNTER__ );
  }
  if (interface->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  return new_interface( core, thread, interface->provider, thread->current_map, thread->regs[1], thread->regs[2] );
}

static inline thread_switch handle_svc_interface_to_return( Core *core, thread_context *thread )
{
  if (thread->regs[0] & 0x3)
    asm ( "smc 13" );

  return new_interface( core, thread, thread->stack_pointer[0].caller_map, thread->current_map, thread->regs[0], thread->regs[1] );
}
#endif

#ifndef WITHOUT_LOCKS
static thread_context *blocked = 0; // A fake list head, for creating blocked lists

static inline thread_switch handle_svc_wait_for_lock( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

  uint64_t x17 = thread->regs[17];
  uint64_t x18 = thread->regs[18];
  if (!address_is_user_writable( core, thread, x17 )) {
    BSOD( __COUNTER__ ); // Lock address not user writable (releasing)
  }
  else if (thread_from_code( x18 ) != thread) {
    BSOD( __COUNTER__ ); // thread code invalid (releasing).
  }
  else {
    // FIXME: This is only safe with single core (IRQs disabled, so nothing can write to the lock)
    // FIXME: This is only safe while maps are not shared across cores
    // FIXME: This is only safe while the lock address is not shared across maps
    // Question: should locks be sharable between maps?
    uint64_t lock_value;
    uint32_t write_failed;
    do {
      write_failed = false; // until proven otherwise

#ifdef DEBUG_ASM
      lock_value = LDXR( x17 );
#else
      asm volatile ( "ldxr %[lv], [%[l]]" : [lv] "=r" (lock_value) : [l] "r" (x17) );
#endif

      if (lock_value == 0) {
#ifdef DEBUG_ASM
        write_failed = STXR( x17, x18 );
#else
        asm volatile ( "stxr %w[w], %[lv], [%[l]]" : [w] "=&r" (write_failed) : [lv] "r" (x18), [l] "r" (x17) );
#endif

        if (!write_failed && result.now != thread) {
          // We've blocked ourselves, but the lock owner has released the lock, thinking there was no-one blocked
          // So, we've now got the lock: unblock the thread.
 
          result.now = thread;

          if (blocked != thread) {
            BSOD( __COUNTER__ );
          }

          remove_thread( thread );
          insert_thread_as_head( &core->runnable, result.now );
        }
      }
      else if (lock_value == x18) {
        // Will this ever happen?
#ifdef DEBUG_ASM
        CLREX();
#else
        asm volatile ( "clrex" ); // We won't be writing to the lock
#endif
      }
      else {
              // What happens on second go through?
              if (result.now != thread) BSOD( 4 );
        uint32_t blocked_thread_code = lock_value >> 32;
        uint32_t locking_thread_code = 0xffffffff & lock_value;

        if (!is_real_thread( locking_thread_code )) {
          BSOD( __COUNTER__ ); // Invalid lock value - throw exception, 
        }
        if (blocked_thread_code != 0
         && !is_real_thread( blocked_thread_code )) {
          BSOD( __COUNTER__ ); // Invalid lock value - throw exception, blocked not zero, not real thread
        }

        result.now = thread->next;
        core->runnable = result.now;
        if (result.now == thread) {
          BSOD( __COUNTER__ ); // This is the only runnable thread on this core, what happened to the idle thread?
        }
        remove_thread( thread ); // No longer runnable

        if (blocked_thread_code == 0) {
          blocked = 0;
          insert_thread_as_head( &blocked, thread );
          lock_value |= x18 << 32;

#ifdef DEBUG_ASM
          write_failed = STXR( x17, lock_value );
#else
          asm volatile ( "stxr %w[w], %[lv], [%[l]]" : [w] "=&r" (write_failed) : [lv] "r" (lock_value), [l] "r" (x17) );
#endif
        }
        else {
          // If there is already a (list of) blocked thread(s), the lock value doesn't change.
#ifdef DEBUG_ASM
          CLREX();
#else
          asm volatile ( "clrex" ); // We won't be writing to the lock
#endif

          thread_context *first_blocked_thread = thread_from_code( blocked_thread_code );
          blocked = first_blocked_thread;

          if (first_blocked_thread != 0 && first_blocked_thread->regs[17] != x17) {
            BSOD( __COUNTER__ ); // Invalid lock value - throw exception, they should all be blocked on the same VA
          }
          // TODO Should the blocking thread be re-scheduled, if runnable?
          // The current thread is doing what I've asked it to, so it should bump up the urgency...
          insert_thread_at_tail( &blocked, thread );
          // TODO deadlock checks? (Return with V set?)
        }
      }
    } while (write_failed);
    dsb();
  }

  return result;
}

static inline thread_switch handle_svc_release_lock( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

  uint64_t x17 = thread->regs[17];
  uint64_t x18 = thread->regs[18];
  if (!address_is_user_writable( core, thread, x17 )) {
    BSOD( __COUNTER__ ); // Lock address not user writable (releasing)
  }
  else if (thread_from_code( x18 ) != thread) {
    BSOD( __COUNTER__ ); // thread code invalid (releasing).
  }
  else {
    // FIXME: This is only safe with single core (IRQs disabled, so nothing can write to the lock)
    // FIXME: This is only safe while maps are not shared across cores
    // FIXME: This is only safe while the lock address is not shared across maps
    // Question: should locks be sharable between maps?
    // TODO Spin lock for claim and release
    uint64_t lock_value = *(uint64_t*) x17; // Protected by kernel spin lock

    if (lock_value == 0) {
      BSOD( __COUNTER__ ); // Throw exception: trying to unlock unlocked lock
    }
    uint32_t blocked_thread_code = lock_value >> 32;
    uint32_t locking_thread_code = 0xffffffff & lock_value;
    if (locking_thread_code != x18) {
      BSOD( __COUNTER__ ); // Throw exception: trying to unlock someone else's lock
    }

    uint64_t new_value = blocked_thread_code;

    if (blocked_thread_code != 0) {
      // It seems unlikely this will not be the case, but it could happen if there's an
      // interrupt between the ldxr and stxr at el0.
      if (!is_real_thread( blocked_thread_code )) {
        BSOD( __COUNTER__ ); // Invalid lock value - throw exception, blocked not zero, not real thread
      }
      thread_context *first_blocked_thread = thread_from_code( blocked_thread_code );

      if (first_blocked_thread == first_blocked_thread->next) {
        // Only one blocked thread
      }
      else {
        blocked = first_blocked_thread->next; // Fake list head for all blocked threads

        remove_thread( first_blocked_thread ); // Not at head of list
        new_value |= (((uint64_t) thread_code( blocked )) << 32);
      }

      result.now = first_blocked_thread;

      // The newly unblocked thread gets a go...?
      insert_thread_as_head( &core->runnable, result.now );
    }

    *(uint64_t*) x17 = new_value;
    dsb();
    // TODO release kernel lock
  }

  return result;
}
#endif

#ifndef WITHOUT_SVC
static inline thread_switch handle_svc( Core *core, thread_context *thread, int number )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

  switch (number) {
  case 0:
    invalidate_all_caches();
    return result;
  case 7:
{ // FIXME FIXME FIXME Remove: testing only! Only works with one VM. Massive security hole!
    thread_context *t = thread;
    while (t->next != thread && t->partner == 0) {
      t = t->next;
    }
    if ((t->spsr & 0x10) == 0) t = t->partner;
    if (t != 0) {
      thread->regs[0] = t->regs[thread->regs[0]];
    }
}
    return result;
  case 8:
{ // FIXME FIXME FIXME Remove: testing only! Only works with one VM. Massive security hole!
    thread_context *t = thread;
    while (t->next != thread && t->partner == 0) {
      t = t->next;
    }
    if ((t->spsr & 0x10) == 0) t = t->partner;
  vm[1].hcr_el2 |= (1 << 7);
}
    return result;
  case 9:
{ // FIXME FIXME FIXME Remove: testing only! Only works with one VM. Massive security hole!
    thread_context *t = thread;
    while (t->next != thread && t->partner == 0) {
      t = t->next;
    }
    if ((t->spsr & 0x10) == 0) t = t->partner;
  vm[1].hcr_el2 &= ~(1 << 7);
}
    return result;
  case 10: { asm( "mov x4, %[addr]\nsmc 800" : : [addr] "r" (thread->regs[0]) ); } ; break;
  case ISAMBARD_GATE: // gate (wait_until_woken or wake_thread)
    return handle_svc_gate( core, thread );
  case ISAMBARD_DUPLICATE_TO_RETURN:
    return handle_svc_duplicate_to_return( core, thread );
  case ISAMBARD_DUPLICATE_TO_PASS:
    return handle_svc_duplicate_to_pass_to( core, thread );
  case ISAMBARD_INTERFACE_TO_PASS: // Interface for provider
    return handle_svc_interface_to_pass_to( core, thread );
  case ISAMBARD_INTERFACE_TO_RETURN: // Interface for caller
    return handle_svc_interface_to_return( core, thread );

  case ISAMBARD_LOCK_WAIT: // Blocked. x17 -> lock variable, x18 -> thread code, do not change any thread registers
    return handle_svc_wait_for_lock( core, thread );
  case ISAMBARD_LOCK_RELEASE: // Release blocked x17 -> lock variable
    return handle_svc_release_lock( core, thread );
  case ISAMBARD_YIELD: // Well tested
  {
    // Yield
    if (thread->next != thread) {
      // Another thread is runnable, yield returns True, eventually
      thread->regs[0] = true;
      result.now = thread->next;
      core->runnable = result.now;
    }
    else {
      thread->regs[0] = false;
    }
    return result;
  }
  case ISAMBARD_EXCEPTION: // Like return, but one parameter and V flag set in thread
  {
    // Not going to change thread, just map (and stack)
    thread->pc = thread->stack_pointer->caller_return_address;
    // Not changing thread, so SP hasn't been stored for restoration
    asm volatile ( "\n\tmsr sp_el0, %[caller_sp]" : : [caller_sp] "r" (thread->stack_pointer->caller_sp) );
    if (thread->current_map != thread->stack_pointer->caller_map) {
      change_map( core, thread, thread->stack_pointer->caller_map );
    }
    thread->stack_pointer++;
    thread->spsr |= (1<<28); // oVerflow flag set

// Still at the stage where everything should be working properly, so exceptions are exceptional!
asm ( "mov x26, %[r0]\nmov x27, %[r1]\nmov x28, %[r2]\nmov x29, %[r30]\nsmc 4" :: [r0] "r" (thread->regs[0]), [r1] "r" (thread->regs[1]), [r2] "r" (thread->regs[2]), [r30] "r" (thread->regs[30])  );

    return result;
  }
  case ISAMBARD_RETURN: // Well tested
  {
    // Inter-map return

    // Not going to change thread, just map (and stack)
    thread->pc = thread->stack_pointer->caller_return_address;
    // Not changing thread, so SP hasn't been stored for restoration
    asm volatile ( "\n\tmsr sp_el0, %[caller_sp]" : : [caller_sp] "r" (thread->stack_pointer->caller_sp) );
    if (thread->current_map != thread->stack_pointer->caller_map) {
      change_map( core, thread, thread->stack_pointer->caller_map );
    }
    thread->stack_pointer++;
    thread->spsr &= ~(1<<28); // oVerflow flag clear

    return result;
  }
  case ISAMBARD_CALL: // Well tested
  {
    // Inter-map call

    if (thread->regs[18] != thread_code( thread )) BSOD( __COUNTER__ ); // FIXME Replace with exception
    if (thread->regs[0] != 2 && thread->regs[1] < 0x100) asm ("wfi");

    Interface *interface = interface_from_index( thread->regs[0] );
    if (0 == interface) {
      BSOD( __COUNTER__ );
    }

    if (interface->provider == system_map_index
     && interface->handler == System_Service_Map) {
      if (thread->regs[1] == DRIVER_SYSTEM_physical_address_of) {
        // This is the simplest and quickest method, and can't be done at el0
        // Note: It fails for non-writable addresses. Is that right?
        uint64_t pa;
        asm volatile ( "\tAT S1E0W, %[va]"
                     "\n\tmrs %[pa], PAR_EL1"
                       : [pa] "=r" (pa)
                       : [va] "r" (thread->regs[2]) );
        if (0 != (pa & 1)) {
          if (find_and_map_memory( core, thread, thread->regs[2] )) {
// FIXME This doesn't seem to walk the table
            asm volatile ( "\tAT S1E0W, %[va]"
                         "\n\tmrs %[pa], PAR_EL1"
                           : [pa] "=r" (pa)
                           : [va] "r" (thread->regs[2]) );
            if (0 != (pa & 1)) { BSOD( __COUNTER__ ); } // Physical address of memory not mapped
          } else {
            BSOD( __COUNTER__ ); // Physical address of memory not mapped
          }
        }
        thread->regs[0] = (pa & 0x000ffffffffff000ull) | (thread->regs[2] & 0xfff);

        return result;
      }
    }

    if (interface->user != thread->current_map) { asm ( "mov x24, %[u]\n\tmov x25, %[i]\n\tmov x26, %[v]\n\tmov x27, %[p]\n\tmov x28, %[l]" : : [u] "r" (interface->user), [i] "r" (thread->regs[0]), [p] "r" (thread->regs[2]), [v] "r" (thread->regs[1]), [l] "r" (thread->regs[30]) ); BSOD( __COUNTER__ ); }

    thread->regs[0] = interface->object.as_number;

    thread->stack_pointer--;

    asm volatile ( "\n\tmrs %[caller_sp], sp_el0" : [caller_sp] "=r" (thread->stack_pointer->caller_sp) );
    thread->stack_pointer->caller_return_address = thread->pc;
    thread->stack_pointer->caller_map = thread->current_map;

    if (thread->stack_pointer-1 <= thread->stack_limit) {
      result.now = thread_stack_is_full( thread );
      return result;
    }

    if (interface->provider != thread->current_map) {
      change_map( core, thread, interface->provider );
    }

    thread->pc = (integer_register) interface->handler;

    return result;
  }
  case ISAMBARD_SWITCH_TO_PARTNER:
    {
      if (thread->partner == 0) BSOD( __COUNTER__ );
      if (thread->current_map != thread->partner->current_map) BSOD( __COUNTER__ );

      // These members are only affected by secure el1.
      thread->partner->next = thread->next;
      thread->partner->prev = thread->prev;
      thread->next->prev = thread->partner;
      thread->prev->next = thread->partner;
      thread->next = thread;
      thread->prev = thread;
      result.now = thread->partner;
      core->runnable = result.now;

      integer_register spsr = result.now->spsr;
      if (0 != (spsr & 0x10) || ((spsr & 0x1e) == 8)) {
        asm ( "dc ivac, %[r]" : : [r] "r" (&result.now->pc) );
        result.now->pc = thread->regs[1];
        result.now->spsr |= (1 << 21); // Single-step
        asm ( "dc civac, %[r]" : : [r] "r" (&result.now->pc) );
        // Moving to Non-Secure, these registers will be filled in on return to secure mode
        thread->regs[0] = 0x7777777777777777ull; // Will be pc
        thread->regs[1] = 0x7777777777777777ull; // Will be syndrome
        thread->regs[2] = 0x7777777777777777ull; // Will be fault address
        thread->regs[3] = 0x7777777777777777ull; // Will be intermediate physical address
        asm ( "dc civac, %[r]" : : [r] "r" (thread->regs) );
      }
      else {
        // EL2 will have updated the first three registers
        asm ( "dc ivac, %[r]" : : [r] "r" (&core->runnable->regs[0]) );
        asm ( "dc ivac, %[r]" : : [r] "r" (&core->runnable->regs[2]) );
      }
    }
    return result;
  case ISAMBARD_GET_PARTNER_REG:
    {
      thread_context *partner = thread->partner;
      unsigned int register_index = thread->regs[0];

      if (partner == 0) BSOD( __COUNTER__ );
      if (thread->current_map != partner->current_map) BSOD( __COUNTER__ );
      if (register_index > 31) BSOD( __COUNTER__ ); // Register code, takes care of _svc registers, etc.

      integer_register spsr = partner->spsr;
      if (0 != (spsr & 0x10) || ((spsr & 0x1e) == 8)) {
        // partner thread WILL be waiting to be re-started

        asm ( "dc ivac, %[r]" : : [r] "r" (&partner->regs[register_index]) );
        thread->regs[0] = partner->regs[register_index];
      }
      else {
        // The non-secure code doesn't call this
        BSOD( __COUNTER__ );
      }
    }
    return result;
  case ISAMBARD_SET_PARTNER_REG:
    {
      thread_context *partner = thread->partner;
      unsigned int register_index = thread->regs[0];

      if (partner == 0) BSOD( __COUNTER__ );
      if (thread->current_map != partner->current_map) BSOD( __COUNTER__ );
      if (register_index > 31) BSOD( __COUNTER__ ); // Register code, takes care of _svc registers, etc.

      integer_register spsr = partner->spsr;
      if (0 != (spsr & 0x10) || ((spsr & 0x1e) == 8)) {
        // partner thread WILL be waiting to be re-started

        asm ( "dc ivac, %[r]" : : [r] "r" (&partner->regs[register_index]) );
        partner->regs[register_index] = thread->regs[1];
        asm ( "dc civac, %[r]" : : [r] "r" (&partner->regs[register_index]) );
      }
      else {
        // The non-secure code doesn't call this
        BSOD( __COUNTER__ );
      }
    }
    return result;
  case ISAMBARD_SET_VM_SYSTEM_REGISTER:
    {
      thread_context *partner = thread->partner;
      if (partner == 0) BSOD( __COUNTER__ );
      if (thread->current_map != partner->current_map) BSOD( __COUNTER__ );

      unsigned int register_index = thread->regs[0];
      if (register_index >= 20) BSOD( __COUNTER__ );

      uint64_t *sysregs = (void*) &vm[1]; // FIXME more than one vm!

      asm ( "dc civac, %[r]" : : [r] "r" (&sysregs[register_index]) );
      sysregs[register_index] = thread->regs[1];
      asm ( "dsb sy\ndc cvac, %[r]" : : [r] "r" (&sysregs[register_index]) );
    }
    return result;
  case ISAMBARD_GET_VM_SYSTEM_REGISTER:
    {
      thread_context *partner = thread->partner;
      if (partner == 0) BSOD( __COUNTER__ );
      if (thread->current_map != partner->current_map) BSOD( __COUNTER__ );

      unsigned int register_index = thread->regs[0];
      if (register_index >= 20) BSOD( __COUNTER__ ); // FIXME: 20 = number of system registers stored in vm_state

      uint64_t *sysregs = (void*) &vm[1]; // FIXME more than one vm!

      asm ( "dc civac, %[r]" : : [r] "r" (&sysregs[register_index]) );
      thread->regs[0] = sysregs[register_index];
      asm ( "dsb sy\ndc cvac, %[r]" : : [r] "r" (&sysregs[register_index]) );
    }
    return result;
  case ISAMBARD_SYSTEM_REQUEST:
    return system_driver_request( core, thread );
  default: BSOD( __COUNTER__ );
  }

  return result;
}
#endif

