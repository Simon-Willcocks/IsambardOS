
#ifndef WITHOUT_GATE
static inline thread_switch handle_svc_gate( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

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
        if (--core->blocked_with_timeout->regs[1] == 0) {
          thread_context *blocked_thread = core->blocked_with_timeout;
          do {
            blocked_thread->regs[0] = -1; // C
            blocked_thread->gate = 0; // No longer blocked
            insert_new_thread_after_old( blocked_thread, thread );
            blocked_thread = (void*) blocked_thread->regs[17];
          } while (blocked_thread != 0 && blocked_thread->regs[1] == 0);
          core->blocked_with_timeout = blocked_thread;
          if (blocked_thread != 0) {
            blocked_thread->regs[16] = (integer_register) &core->blocked_with_timeout;
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
      thread->gate = thread_code( thread ); // Marks as blocked
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

    if (thread->current_map == release_thread->current_map) { // More checks?
      if (release_thread->gate == (int32_t) thread->regs[0]) {
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
        release_thread->gate++;
      }
    }
    else {
      invalidate_all_caches();
      BSOD( __COUNTER__ ); // Threads not blocked in same map
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

  if (interface->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  return new_interface( core, thread, thread->stack_pointer[0].caller_map, interface->provider, interface->handler, interface->object.as_number );
}

static inline thread_switch handle_svc_duplicate_to_pass_to( Core *core, thread_context *thread )
{
  // FIXME lots of testing!
  Interface *interface = interface_from_index( thread->regs[1] );

  if (interface->user != thread->current_map) {
    BSOD( __COUNTER__ );
  }

  Interface *target = interface_from_index( thread->regs[0] );

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
  if (address_is_user_writable( x17 )
   && thread_from_code( x18 ) == thread) {
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
  else {
    BSOD( __COUNTER__ ); // Not writable, or thread code invalid, blocking
  }

  return result;
}

static inline thread_switch handle_svc_release_lock( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread

  uint64_t x17 = thread->regs[17];
  uint64_t x18 = thread->regs[18];
  if (address_is_user_writable( x17 )
   && thread_from_code( x18 ) == thread) {

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
  else {
    BSOD( __COUNTER__ ); // Not writable, or thread code invalid, releasing.
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
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
  case 15:
    //led_blink( number & 15 );
    return result;
  case 0xfff5: // gate (wait_until_woken or wake_thread)
    return handle_svc_gate( core, thread );
  case 0xfff6:
    return handle_svc_duplicate_to_return( core, thread );
  case 0xfff7:
    return handle_svc_duplicate_to_pass_to( core, thread );
  case 0xfff8: // Interface for provider
    return handle_svc_interface_to_pass_to( core, thread );
  case 0xfff9: // Interface for caller
    return handle_svc_interface_to_return( core, thread );

  case 0xfffa: // Blocked. x17 -> lock variable, x18 -> thread code, do not change any thread registers
    return handle_svc_wait_for_lock( core, thread );
  case 0xfffb: // Release blocked x17 -> lock variable
    return handle_svc_release_lock( core, thread );
  case 0xfffc: // Well tested
  {
    // Yield
    if (thread->next != thread) {
      // Another thread is runnable, yield returns True, eventually
      thread->regs[0] = true;
      result.now = thread->next;
    }
    else {
      thread->regs[0] = false;
    }
    return result;
  }
  case 0xfffd: // Well tested
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

    return result;
  }
  case 0xfffe: // Well tested
  {
    // Inter-map call

    if (thread->regs[18] != thread_code( thread )) BSOD( __COUNTER__ ); // FIXME Replace with exception
    if (thread->regs[0] != 2 && thread->regs[1] < 0x100) asm ("wfi");

    Interface *interface = interface_from_index( thread->regs[0] );

    if (interface->provider == system_map_index
     && interface->handler == System_Service_Map) {
      if (thread->regs[1] == DRIVER_SYSTEM_physical_address_of) {
        // This is the simplest and quickest method, and can't be done at el0
        uint64_t pa;
        asm volatile ( "\tAT S1E0W, %[va]"
                     "\n\tmrs %[pa], PAR_EL1"
                       : [pa] "=r" (pa)
                       : [va] "r" (thread->regs[2]) );
        if (0 != (pa & 1)) { BSOD( __COUNTER__ ); }
        thread->regs[0] = (pa & 0x000ffffffffff000ull) | (thread->regs[2] & 0xfff);

        return result;
      }
    }

    if (interface->user != thread->current_map) { BSOD( __COUNTER__ ); }

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
  case 0xffff:
    return system_driver_request( core, thread );
  default: BSOD( __COUNTER__ );
  }

  return result;
}
#endif

