/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "slap.h"

#ifdef HPUX
#ifdef ATOMIC_64BIT_OPERATIONS
#include <machine/sys/inline.h>
#endif
#endif

/*
 * Counter Structure
 */
typedef struct slapi_counter {
    uint64_t value;
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
        slapi_ch_free((void **)counter);
    }
}

/*
 * slapi_counter_increment()
 *
 * Atomically increments a Slapi_Counter.
 */
uint64_t slapi_counter_increment(Slapi_Counter *counter)
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
uint64_t slapi_counter_decrement(Slapi_Counter *counter)
{
    return slapi_counter_subtract(counter, 1);
}

/*
 * slapi_counter_add()
 *
 * Atomically add a value to a Slapi_Counter.
 */
uint64_t slapi_counter_add(Slapi_Counter *counter, uint64_t addvalue)
{
    uint64_t newvalue = 0;
#ifdef HPUX
    uint64_t prev = 0;
#endif

    if (counter == NULL) {
        return newvalue;
    }

#ifndef HPUX
    newvalue = __atomic_add_fetch_8(&(counter->value), addvalue, __ATOMIC_SEQ_CST);
#else
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

    return newvalue;
}

/*
 * slapi_counter_subtract()
 *
 * Atomically subtract a value from a Slapi_Counter.  Note
 * that this will not prevent you from wrapping around 0.
 */
uint64_t slapi_counter_subtract(Slapi_Counter *counter, uint64_t subvalue)
{
    uint64_t newvalue = 0;
#ifdef HPUX
    uint64_t prev = 0;
#endif

    if (counter == NULL) {
        return newvalue;
    }

#ifndef HPUX
    newvalue = __atomic_sub_fetch_8(&(counter->value), subvalue, __ATOMIC_SEQ_CST);
#else
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

    return newvalue;
}

/*
 * slapi_counter_set_value()
 *
 * Atomically sets the value of a Slapi_Counter.
 */
uint64_t slapi_counter_set_value(Slapi_Counter *counter, uint64_t newvalue)
{
    uint64_t value = 0;

    if (counter == NULL) {
        return value;
    }

#ifndef HPUX
/* Use our own inline assembly for an atomic set if
 * the builtins aren't available. */
#if !HAVE_64BIT_ATOMIC_CAS_FUNC
    /*
     * %0 = counter->value
     * %1 = newvalue
     */
    __asm__ __volatile__(
#ifdef CPU_x86
        /* Save the PIC register */
        " pushl %%ebx;"
#endif /* CPU_x86 */
        /* Put value of counter->value in EDX:EAX */
        "retryset: movl %0, %%eax;"
        " movl 4%0, %%edx;"
        /* Put newval in ECX:EBX */
        " movl %1, %%ebx;"
        " movl 4+%1, %%ecx;"
        /* If EDX:EAX and counter-> are the same,
         * replace *ptr with ECX:EBX */
        " lock; cmpxchg8b %0;"
        " jnz retryset;"
#ifdef CPU_x86
        /* Restore the PIC register */
        " popl %%ebx"
#endif /* CPU_x86 */
        : "+o" (counter->value)
        : "m" (newvalue)
#ifdef CPU_x86
        : "memory", "eax", "ecx", "edx", "cc");
#else
        : "memory", "eax", "ebx", "ecx", "edx", "cc");
#endif

    return newvalue;
#else /* HAVE_64BIT_ATOMIC_CAS_FUNC */
    while (1) {
        value = __atomic_load_8(&(counter->value), __ATOMIC_SEQ_CST);
        if (__atomic_compare_exchange_8(&(counter->value), &value, newvalue, PR_FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)){
            return newvalue;
        }
    }
#endif
#else /* HPUX */
    do {
        value = counter->value;
        /* Put value in a register for cmpxchg to compare against */
        _Asm_mov_to_ar(_AREG_CCV, value);
    } while (value != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), newvalue, _LDHINT_NONE));
    return newvalue;
#endif
}

/*
 * slapi_counter_get_value()
 *
 * Returns the value of a Slapi_Counter.
 */
uint64_t slapi_counter_get_value(Slapi_Counter *counter)
{
    uint64_t value = 0;

    if (counter == NULL) {
        return value;
    }

#ifndef HPUX
/* Use our own inline assembly for an atomic get if
 * the builtins aren't available. */
#if !HAVE_64BIT_ATOMIC_CAS_FUNC
    /*
     * %0 = counter->value
     * %1 = value
     */
    __asm__ __volatile__(
#ifdef CPU_x86
        /* Save the PIC register */
        " pushl %%ebx;"
#endif /* CPU_x86 */
        /* Put value of counter->value in EDX:EAX */
        "retryget: movl %0, %%eax;"
        " movl 4%0, %%edx;"
        /* Copy EDX:EAX to ECX:EBX */
        " movl %%eax, %%ebx;"
        " movl %%edx, %%ecx;"
        /* If EDX:EAX and counter->value are the same,
         * replace *ptr with ECX:EBX */
        " lock; cmpxchg8b %0;"
        " jnz retryget;"
        /* Put retrieved value into value */
        " movl %%ebx, %1;"
        " movl %%ecx, 4%1;"
#ifdef CPU_x86
        /* Restore the PIC register */
        " popl %%ebx"
#endif /* CPU_x86 */
        : "+o" (counter->value), "=m" (value)
        : 
#ifdef CPU_x86
        : "memory", "eax", "ecx", "edx", "cc");
#else
        : "memory", "eax", "ebx", "ecx", "edx", "cc");
#endif
#else  /* HAVE_64BIT_ATOMIC_CAS_FUNC */
    while (1) {
        value = __atomic_load_8(&(counter->value), __ATOMIC_SEQ_CST);
        if (__atomic_compare_exchange_8(&(counter->value), &value, value, PR_FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)){
            break;
        }
    }
#endif
#else  /* HPUX */
    do {
        value = counter->value;
        /* Put value in a register for cmpxchg to compare against */
        _Asm_mov_to_ar(_AREG_CCV, value);
    } while (value != _Asm_cmpxchg(_FASZ_D, _SEM_ACQ, &(counter->value), value, _LDHINT_NONE));
#endif

    return value;
}

