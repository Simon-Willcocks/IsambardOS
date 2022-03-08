# IsambardOS

Aarch64 operating system with an unusual approach to multi-processing.

Key features:

* Memory protection

* Minimal OS overhead

* Permits direct hardware manipulation at EL0 (memory protected)

* Runs arm32 code in virtual machines (Non-Secure)

* Maps instead of processes

* Threads that navigate through Maps

* Simple locking mechanism for synchronisation

* Virtualisation for arm32 code

* Simple abstract interrupt model

## Memory protection

Almost all code runs at Aarch64 EL0. The kernel runs in Secure EL1,
with a very small amount of code at EL3, used at boot time and to 
switch between Secure and Non-Secure modes, only.

Even device drivers are implemented Secure EL0.

Rather than traditional processes, IsambardOS uses Maps which can be 
viewed as a cluster of object interfaces with well-defined semantics.

Threads make calls to the objects which take them into the object's
Map, keeping the code and data of the called object safely out of
sight of the caller.

## Minimal OS overhead

The kernel provides system calls to allow code in a Map to:

* Create an object to return or pass to another object
* Duplicate an interface to return or pass to another object
* Call routines of an object
* Return from a routine, either successfully or as an exception
* Maintain Locks: Wait or Release, only needed when EL0 detects a lock is held by another thread
* Yield the processor to another thread

It does /not/:

* Manage hardware (other than the processor and MMU)
* Manage threads (they are created by the System Map)
* Manage memory (maintained by physical\_memory\_allocator & System)

### System Map

The System Map is a special Map that is allowed to make certain 
special requests of the kernel.

These include:

* Create a map
* Create a thread
* Register the Interrupt Thread
* Adding device pages to Maps that request them
* Virtualisation helper routines

## Virtualisation

Threads that are going to run virtualised code work by creating a
"partner" thread to hold the context of the virtualised core. Interrupts
switch from Non-Secure to Secure mode, and from the running thread to
its partner thread. The Secure partner thread has access to modify the
Non-Secure thread's context.

## Locks

Locks are an easy-to-use mechanism to ensure resources are not accessed
by more than one thread at a time. They are extremely light-weight,
making use of the LDREX/STREX features of the processor. Usually, gaining
a lock is as simple as reading a zero and successfully writing the thread's
ID to the 64-bit word containing the lock. If another thread already owns
the lock, the kernel is asked to block the thread until it becomes free.

## Interrupt model

The System Map provides an interface to register an interrupt handler for
a numbered interrupt. The handler (a normal object interface) will be
called with interrupts disabled and should manipulate the hardware to
acknowledge the interrupt, perhaps store some information about it, and
perhaps wake up an associated interrupt thread before returning.

The interrupt handler will:
```
  Check reason for interrupt
  If trivial to handle
    Handle interrupt
  Else
    Store the reason for the interrupt
    Stop the hardware from generating the interrupt
    Wake the interrupt thread (using the shared variable)
  Endif
```

An interrupt thread will often have the following form:

```
  Set a shared variable to this thread's ID
  Register interrupt handler
  Initialise hardware
  Loop
    Wait until woken
    Handle reason for interrupt (can take our time, within reason)
  Endloop
```

In other cases, a thread making a request of some hardware might:

```
  Lock the hardware
  Set a shared variable to this thread's ID
  Setup the hardware to perform an action and interrupt when it's done
  Wait until woken
  Read the results of the action from the hardware
  Clear the shared variable
  Release the hardware lock
```

For examples of the latter, see drivers/pi3gpu/sd.c or mailbox_properties.c.
