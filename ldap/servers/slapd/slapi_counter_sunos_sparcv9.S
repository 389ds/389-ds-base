! BEGIN COPYRIGHT BLOCK
! This Program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; version 2 of the License.
! 
! This Program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
! 
! You should have received a copy of the GNU General Public License along with
! this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
! Place, Suite 330, Boston, MA 02111-1307 USA.
! 
! In addition, as a special exception, Red Hat, Inc. gives You the additional
! right to link the code of this Program with code not covered under the GNU
! General Public License ("Non-GPL Code") and to distribute linked combinations
! including the two, subject to the limitations in this paragraph. Non-GPL Code
! permitted under this exception must only link to the code of this Program
! through those well defined interfaces identified in the file named EXCEPTION
! found in the source code files (the "Approved Interfaces"). The files of
! Non-GPL Code may instantiate templates or use macros or inline functions from
! the Approved Interfaces without causing the resulting work to be covered by
! the GNU General Public License. Only Red Hat, Inc. may make changes or
! additions to the list of Approved Interfaces. You must obey the GNU General
! Public License in all respects for all of the Program code and other code used
! in conjunction with the Program except the Non-GPL Code covered by this
! exception. If you modify this file, you may extend this exception to your
! version of the file, but you are not obligated to do so. If you do not wish to
! provide this exception without modification, you must delete this exception
! statement from your version and license this file solely under the GPL without
! exception. 
! 
! 
! The Original Code is the Netscape Portable Runtime (NSPR).
!
! The Initial Developer of the Original Code is
! Netscape Communications Corporation.
! Portions created by the Initial Developer are Copyright (C) 1998-2000
! the Initial Developer. All Rights Reserved.
!
! The original code has been modified to support 64-bit atomic increments by
! Red Hat, Inc.  These portions are Copyright (C) 2008 Red Hat, Inc. All Rights
! reserved.
!
! END COPYRIGHT BLOCK
!

#define _ASM 1 /* force to set an assembler code macro _ASM */
#include <sys/asm_linkage.h>

!  ======================================================================
!
!  Perform the sequence a = b atomically with respect to other
!  fetch-and-stores to location a in a wait-free fashion.
!
!  usage : old_val = PR_AtomicSet(address, newval)
!
!  -----------------------
!  Note on REGISTER USAGE:
!  as this is a LEAF procedure, a new stack frame is not created;
!  we use the caller's stack frame so what would normally be %i (input)
!  registers are actually %o (output registers).  Also, we must not
!  overwrite the contents of %l (local) registers as they are not
!  assumed to be volatile during calls.
!  -----------------------

        ENTRY(_sparcv9_AtomicSet)            ! standard assembler/ELF prologue

retryAS:
        ldx     [%o0], %o2              ! set o2 to the current value
        mov     %o1, %o3                ! set up the new value
        casx    [%o0], %o2, %o3         ! atomically set if o0 hasn't changed
        cmp     %o2, %o3                ! see if we set the value
        bne     retryAS                 ! if not, try again
        nop                             ! empty out the branch pipeline
        retl                            ! return back to the caller
        mov     %o3, %o0                ! set the return code to the prev value

        SET_SIZE(_sparcv9_AtomicSet)    ! standard assembler/ELF epilogue

!
!  end
!
!  ======================================================================
!
!  Perform the sequence a = a + b atomically with respect to other
!  fetch-and-adds to location a in a wait-free fashion.
!
!  usage : newval = PR_AtomicAdd(address, val)
!  return: the value after addition
!
        ENTRY(_sparcv9_AtomicAdd)      ! standard assembler/ELF prologue

retryAA:
        ldx     [%o0], %o2              ! set o2 to the current value
        addx    %o2, %o1, %o3           ! calc the new value
        mov     %o3, %o4                ! save the return value
        casx    [%o0], %o2, %o3         ! atomically set if o0 hasn't changed
        cmp     %o2, %o3                ! see if we set the value
        bne     retryAA                 ! if not, try again
        nop                             ! empty out the branch pipeline
        retl                            ! return back to the caller
        mov     %o4, %o0                ! set the return code to the new value

        SET_SIZE(_sparcv9_AtomicAdd)    ! standard assembler/ELF epilogue

!
!  end
!
!  ======================================================================
!
!  Perform the sequence a = a - b atomically with respect to other
!  fetch-and-subs to location a in a wait-free fashion.
!
!  usage : newval = PR_AtomicSub(address, val)
!  return: the value after addition
!
        ENTRY(_sparcv9_AtomicSub)      ! standard assembler/ELF prologue

retryAU:
        ldx     [%o0], %o2              ! set o2 to the current value
        subx    %o2, %o1, %o3           ! calc the new value
        mov     %o3, %o4                ! save the return value
        casx    [%o0], %o2, %o3         ! atomically set if o0 hasn't changed
        cmp     %o2, %o3                ! see if we set the value
        bne     retryAU                 ! if not, try again
        nop                             ! empty out the branch pipeline
        retl                            ! return back to the caller
        mov     %o4, %o0                ! set the return code to the new value

        SET_SIZE(_sparcv9_AtomicSub)    ! standard assembler/ELF epilogue

!
!  end
!
