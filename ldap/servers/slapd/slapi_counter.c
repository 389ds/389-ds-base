/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "slap.h"

#ifdef SOLARIS
PRUint64 _sparcv9_AtomicSet(PRUint64 *address, PRUint64 newval);
PRUint64 _sparcv9_AtomicAdd(PRUint64 *address, PRUint64 val);
PRUint64 _sparcv9_AtomicSub(PRUint64 *address, PRUint64 val);
#endif

#ifdef HPUX
#ifdef ATOMIC_64BIT_OPERATIONS
#include <machine/sys/inline.h>
#endif
#endif

#ifdef ATOMIC_64BIT_OPERATIONS
#if defined LINUX && (defined CPU_x86 || !HAVE_DECL___SYNC_ADD_AND_FETCH)
/* On systems that don't have the 64-bit GCC atomic builtins, we need to
 * implement our own atomic functions using inline assembly code. */
PRUint64 __sync_add_and_fetch_8(PRUint64 *ptr, PRUint64 addval);
PRUint64 __sync_sub_and_fetch_8(PRUint64 *ptr, PRUint64 subval);
#endif

#if defined LINUX && !HAVE_DECL___SYNC_ADD_AND_FETCH
/* Systems that have the atomic builtins defined, but don't have
 * implementations for 64-bit values will automatically try to
 * call the __sync_*_8 versions we provide.  If the atomic builtins
 * are not defined at all, we define them here to use our local
 * functions. */
#define __sync_add_and_fetch __sync_add_and_fetch_8
#define __sync_sub_and_fetch __sync_sub_and_fetch_8
#endif
#endif /* ATOMIC_64BIT_OPERATIONS */


/*
 * Counter Structure
 */
typedef struct slapi_counter {
    PRUint64 value;
#ifndef ATOMIC_64BIT_OPERATIONS
    Slapi_Mutex *lock;
#endif
} slapi_counter;

/*
 * slapi_counter_new()
 *
 * Allocates and initializes a new Slapi_Counter.
 */
Slapi_Counter *slapi_counter_new()
{
    Slapi_Counter *counter = NULL;

    counter = (Slapi_Counter *)slapi_ch_calloc(1, sizeof(Slapi_Counter));

    if (counter != NULL) {
        slapi_counter_init(counter);
    }

    return counter;
}

/*
 * slapi_counter_init()
 *
 * Initializes a Slapi_Counter.
 */
void slapi_counter_init(Slapi_Counter *counter)
{
    if (counter != NULL) {
#ifndef ATOMIC_64BIT_OPERATIONS
        /* Create the lock if necessary. */
        if (counter->lock == NULL) {
            counter->lock = slapi_new_mutex();
        }
#endif
        
        /* Set the value to 0. */
        slapi_counter_set_value(counter, 0);
    }
}

/*
 * slapi_counter_destroy()
 *
 * Destroy's a Slapi_Counter and sets the
 * pointer to NULL to prevent reuse.
 */
void slapi_counter_destroy(Slapi_Counter **counter)
{
    if ((counter != NULL) && (*counter != NULL)) {
#ifndef ATOMIC_64BIT_OPERATIONS
        slapi_destroy_mutex((*counter)->lock);
#endif
        slapi_ch_free((void **)counter);
    }
}

/*
 * slapi_counter_increment()
 *
 * Atomically increments a Slapi_Counter.
 */
PRUint64 slapi_counter_increment(Slapi_Counter *counter)
{
    return slapi_counter_add(counter, 1);
}

/*
 * slapi_counter_decrement()
 *
 * Atomically decrements a Slapi_Counter. Note
 * that this will not prevent you from wrapping
 * around 0.
 */
PRUint64 slapi_counter_decrement(Slapi_Counter *counter)
{
    return slapi_counter_subtract(counter, 1);
}

/*
 * slapi_counter_add()
 *
 * Atomically add a value to a Slapi_Counter.
 */
PRUint64 slapi_counter_add(Slapi_Counter *counter, PRUint64 addvalue)
{
    PRUint64 newvalue = 0;
#ifdef HPUX
    PRUint64 prev = 0;
#endif

    if (counter == NULL) {
        return newvalue;
    }

#ifndef ATOMIC_64BIT_OPERATIONS
    slapi_lock_mutex(counter->lock);
    counter->value += addvalue;
    newvalue = counter->value;
    slapi_unlock_mutex(counter->lock);
#else
#ifdef LINUX
    newvalue = __sync_add_and_fetch(&(counter->value), addvalue);
#elif defined(SOLARIS)
    newvalue = _sparcv9_AtomicAdd(&(counter->value), addvalue);
#elif defined(HPUX)
    /* fetchadd only works with values of 1, 4, 8, and 16.  In addition, it requires
     * it's argument to be an integer constant. */
    if (addvalue == 1) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), 1, _LDHINT_NONE);
        newvalue += 1;
    } else if  (addvalue == 4) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), 4, _LDHINT_NONE);
        newvalue += 4;
    } else if (addvalue == 8) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), 8, _LDHINT_NONE);
        newvalue += 8;
    } else if (addvalue == 16) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), 16, _LDHINT_NONE);
        newvalue += 16;
    } else {
        /* For other values, we have to use cmpxchg. */
        do {
            prev = slapi_counter_get_value(counter);
            newvalue = prev + addvalue;
            /* Put prev in a register for cmpxchg to compare against */
           _Asm_mov_to_ar(_AREG_CCV, prev);
        } while (prev != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), newvalue, _LDHINT_NONE));
    }
#endif
#endif /* ATOMIC_64BIT_OPERATIONS */

    return newvalue;
}

/*
 * slapi_counter_subtract()
 *
 * Atomically subtract a value from a Slapi_Counter.  Note
 * that this will not prevent you from wrapping around 0.
 */
PRUint64 slapi_counter_subtract(Slapi_Counter *counter, PRUint64 subvalue)
{
    PRUint64 newvalue = 0;
#ifdef HPUX
    PRUint64 prev = 0;
#endif

    if (counter == NULL) {
        return newvalue;
    }

#ifndef ATOMIC_64BIT_OPERATIONS
    slapi_lock_mutex(counter->lock);
    counter->value -= subvalue;
    newvalue = counter->value;
    slapi_unlock_mutex(counter->lock);
#else
#ifdef LINUX
    newvalue = __sync_sub_and_fetch(&(counter->value), subvalue);
#elif defined(SOLARIS)
    newvalue = _sparcv9_AtomicSub(&(counter->value), subvalue);
#elif defined(HPUX)
    /* fetchadd only works with values of -1, -4, -8, and -16.  In addition, it requires
     * it's argument to be an integer constant. */
    if (subvalue == 1) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), -1, _LDHINT_NONE);
        newvalue -= 1;
    } else if  (subvalue == 4) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), -4, _LDHINT_NONE);
        newvalue -= 4;
    } else if (subvalue == 8) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), -8, _LDHINT_NONE);
        newvalue -= 8;
    } else if (subvalue == 16) {
        newvalue = _Asm_fetchadd(_FASZ_D, _SEM_ACQ, &(counter->value), -16, _LDHINT_NONE);
        newvalue -= 16;
    } else {
        /* For other values, we have to use cmpxchg. */
        do {
            prev = slapi_counter_get_value(counter);
            newvalue = prev - subvalue;
            /* Put prev in a register for cmpxchg to compare against */
           _Asm_mov_to_ar(_AREG_CCV, prev);
        } while (prev != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), newvalue, _LDHINT_NONE));
    }
#endif
#endif /* ATOMIC_64BIT_OPERATIONS */

    return newvalue;
}

/*
 * slapi_counter_set_value()
 *
 * Atomically sets the value of a Slapi_Counter.
 */
PRUint64 slapi_counter_set_value(Slapi_Counter *counter, PRUint64 newvalue)
{
    PRUint64 value = 0;

    if (counter == NULL) {
        return value;
    }

#ifndef ATOMIC_64BIT_OPERATIONS
    slapi_lock_mutex(counter->lock);
    counter->value = newvalue;
    slapi_unlock_mutex(counter->lock);
    return newvalue;
#else
#ifdef LINUX
    while (1) {
        value = counter->value;
        if (__sync_bool_compare_and_swap(&(counter->value), value, newvalue)) {
            return newvalue;
        }
    }
#elif defined(SOLARIS)
    _sparcv9_AtomicSet(&(counter->value), newvalue);
    return newvalue;
#elif defined(HPUX)
    do {
        value = counter->value;
        /* Put value in a register for cmpxchg to compare against */
        _Asm_mov_to_ar(_AREG_CCV, value);
    } while (value != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), newvalue, _LDHINT_NONE));
    return newvalue;
#endif
#endif /* ATOMIC_64BIT_OPERATIONS */
}

/*
 * slapi_counter_get_value()
 *
 * Returns the value of a Slapi_Counter.
 */
PRUint64 slapi_counter_get_value(Slapi_Counter *counter)
{
    PRUint64 value = 0;

    if (counter == NULL) {
        return value;
    }

#ifndef ATOMIC_64BIT_OPERATIONS
    slapi_lock_mutex(counter->lock);
    value = counter->value;
    slapi_unlock_mutex(counter->lock);
#else
#ifdef LINUX
    while (1) {
        value = counter->value;
        if (__sync_bool_compare_and_swap(&(counter->value), value, value)) {
            break;
        }
    }
#elif defined(SOLARIS)
    while (1) {
        value = counter->value;
        if (value == _sparcv9_AtomicSet(&(counter->value), value)) {
            break;
        }
    }
#elif defined(HPUX)
    do {
        value = counter->value;
        /* Put value in a register for cmpxchg to compare against */
        _Asm_mov_to_ar(_AREG_CCV, value);
    } while (value != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), value, _LDHINT_NONE));
#endif
#endif /* ATOMIC_64BIT_OPERATIONS */

    return value;
}

#ifdef ATOMIC_64BIT_OPERATIONS
#if defined LINUX && (defined CPU_x86 || !HAVE_DECL___SYNC_ADD_AND_FETCH)
/* On systems that don't have the 64-bit GCC atomic builtins, we need to
 * implement our own atomic add and subtract functions using inline
 * assembly code. */
PRUint64 __sync_add_and_fetch_8(PRUint64 *ptr, PRUint64 addval)
{
    PRUint64 retval = 0;

    /*
     * %0 = *ptr
     * %1 = retval
     * %2 = addval
     */
    __asm__ __volatile__(
#ifdef CPU_x86
        /* Save the PIC register */
        " pushl %%ebx;"
#endif /* CPU_x86 */
        /* Put value of *ptr in EDX:EAX */
        "retryadd: movl %0, %%eax;"
        " movl 4%0, %%edx;"
        /* Put addval in ECX:EBX */
        " movl %2, %%ebx;"
        " movl 4+%2, %%ecx;"
        /* Add value from EDX:EAX to value in ECX:EBX */
        " addl %%eax, %%ebx;"
        " adcl %%edx, %%ecx;"
        /* If EDX:EAX and *ptr are the same, replace ptr with ECX:EBX */
        " lock; cmpxchg8b %0;"
        " jnz retryadd;"
        /* Put new value into retval */
        " movl %%ebx, %1;"
        " movl %%ecx, 4%1;"
#ifdef CPU_x86
        /* Restore the PIC register */
        " popl %%ebx"
#endif /* CPU_x86 */
        : "+o" (*ptr), "=m" (retval)
        : "m" (addval)
#ifdef CPU_x86
        : "memory", "eax", "ecx", "edx", "cc");
#else
        : "memory", "eax", "ebx", "ecx", "edx", "cc");
#endif

    return retval;
}

PRUint64 __sync_sub_and_fetch_8(PRUint64 *ptr, PRUint64 subval)
{
    PRUint64 retval = 0;

    /*
     * %0 = *ptr
     * %1 = retval
     * %2 = subval
     */
    __asm__ __volatile__(
#ifdef CPU_x86
        /* Save the PIC register */
        " pushl %%ebx;"
#endif /* CPU_x86 */
        /* Put value of *ptr in EDX:EAX */
        "retrysub: movl %0, %%eax;"
        " movl 4%0, %%edx;"
        /* Copy EDX:EAX to ECX:EBX */
        " movl %%eax, %%ebx;"
        " movl %%edx, %%ecx;"
        /* Subtract subval from value in ECX:EBX */
        " subl %2, %%ebx;"
        " sbbl 4+%2, %%ecx;"
        /* If EDX:EAX and ptr are the same, replace *ptr with ECX:EBX */
        " lock; cmpxchg8b %0;"
        " jnz retrysub;"
        /* Put new value into retval */
        " movl %%ebx, %1;"
        " movl %%ecx, 4%1;"
#ifdef CPU_x86
        /* Restore the PIC register */
        " popl %%ebx"
#endif /* CPU_x86 */
        : "+o" (*ptr), "=m" (retval)
        : "m" (subval)
#ifdef CPU_x86 
        : "memory", "eax", "ecx", "edx", "cc");
#else
        : "memory", "eax", "ebx", "ecx", "edx", "cc");
#endif

    return retval;
}
#endif /* LINUX && (defined CPU_x86 || !HAVE_DECL___SYNC_ADD_AND_FETCH) */
#endif /* ATOMIC_64BIT_OPERATIONS */
