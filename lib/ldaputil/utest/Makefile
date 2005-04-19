#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Makefile for ldaputil unit test.
#
BUILD_ROOT=../../..
MODULE=LibLdapUtil

OBJDEST=.
UTESTDEST=utest

include $(BUILD_ROOT)/nsconfig.mk

MODULE_CFLAGS=-I$(NSROOT)/include

include $(INCLUDE_DEPENDS)

TESTFLAGS = -DUTEST -DDBG_PRINT -DDONT_USE_LDAP_SSL

CC=CC
PURIFY=

CSRC = stubs.c
CPPSRC = auth.cpp
TSRC = authtest
SRC  = $(CSRC) $(CPPSRC) $(TSRC)
XSRC = ../ldapauth.c ../ldapdb.c ../errors.c ../dbconf.c ../certmap.c ../ldapauth.c ../init.c ../encode.c

COBJ = $(CPPSRC:%.cpp=%.o) $(CSRC:%.c=%.o)
XOBJ = $(XSRC:../%.c=../utest/%.o) 

ifeq ($(ARCH), WINNT)
  BINS=./auth.exe
  LDAP_LIBLINK = $(addprefix $(LDAP_LIBPATH)/, $(addsuffix .lib, $(LDAP_LIBNAMES)))
  XLIBS = ${LDAP_LIBLINK}
  LOCAL_LINK_EXE = link -OUT:"$@" /MAP  $(ARCH_LINK_DEBUG) $(LCFLAGS) /NOLOGO \
		 /PDB:NONE /INCREMENTAL:NO /SUBSYSTEM:windows $(XLIBS)
else
  BINS = auth
  LDAP_LIBLINK = -L$(LDAP_LIBPATH) $(addprefix -l, ${LDAP_SOLIB_NAMES})
endif

ifeq ($(ARCH), SOLARIS)
  XLIBS = -R$(LDAP_LIBPATH) ${LDAP_LIBLINK} $(LIBNSPR) $(LIBSEC) -lthread -lposix4 -lsocket -lnsl -ldl
else
  ifeq ($(ARCH), IRIX)
    XLIBS = ${LDAP_LIBLINK} $(LIBNSPR) $(LIBSEC)
  else
    ifeq ($(ARCH), WINNT)
      echo "XLIBS = ${XLIBS}"
    else
      #Other UNIX platforms
      XLIBS = -R$(LDAP_LIBPATH) ${LDAP_LIBLINK} $(LIBNSPR) $(LIBSEC) -lthread -lposix4 -lsocket -lnsl -ldl
    endif
  endif
endif

PLUGIN = plugin.so

all:	$(LIBLDAP) $(COBJ) $(TSRC) ${BINS} $(PLUGIN)
	./authtest 2> test.out
	diff test.out test.ref
	@echo
	@echo "The unit test is passed if there is no diff output, and the"
	@echo "Purify window shows no errors and 0 bytes leaked."
	@echo
	@echo "Run - gmake coverage - manually to get code coverage analysis."
	@echo

auth:	$(XOBJ) $(COBJ)
	$(PURIFY) $(CC) $(XLIBS) $^ -o $@

auth.exe:	$(XOBJ) $(COBJ)
	$(PURIFY) $(LOCAL_LINK_EXE) $(XOBJ) $(COBJ) ${XLIBS}

testcert:	testcert.o $(XOBJ) ../utest/cert.o
	$(PURIFY) $(CC) $(XLIBS) $^ -o $@

%.o:%.c
	$(PURIFY) $(CC) -c $(CFLAGS) $(TESTFLAGS) $(MCC_INCLUDE) $< -o $@

../utest/%.o:../%.c
	$(PURIFY) $(CC) -c $(CFLAGS) $(TESTFLAGS) $(MCC_INCLUDE) -I.. $< -o $(OBJDEST)/$*.o

../utest/%.o:../%.cpp
	$(PURIFY) $(CC) -c $(CFLAGS) $(TESTFLAGS) $(MCC_INCLUDE) -I.. $< -o $(OBJDEST)/$*.o

PLUGIN_INC = ./include

$(PLUGIN_INC):
	mkdir -p include

certmap.h: $(BUILD_ROOT)/include/ldaputil/extcmap.h
	\rm -rf $(PLUGIN_INC)/$@
	cp $^ $(PLUGIN_INC)/$@

ldap.h: $(LDAP_INCLUDE)/ldap.h
	\rm -rf $(PLUGIN_INC)/$@
	cp $^ $(PLUGIN_INC)/$@

lber.h: $(LDAP_INCLUDE)/lber.h
	\rm -rf $(PLUGIN_INC)/$@
	cp $^ $(PLUGIN_INC)/$@

example.o: example.c $(PLUGIN_INC) certmap.h ldap.h lber.h
	$(CC) -c -I$(PLUGIN_INC) $*.c -o $(OBJDEST)/$*.o

plugin.o: plugin.c $(PLUGIN_INC) certmap.h ldap.h lber.h
	$(PURIFY) $(CC) -c -I. -I$(PLUGIN_INC) $*.c -o $(OBJDEST)/$*.o

$(PLUGIN): plugin.o
	$(LINK_DLL) $^
