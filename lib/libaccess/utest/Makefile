#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
#
BUILD_ROOT=../../..
MODULE=LibAcl
include $(BUILD_ROOT)/nsdefs.mk

OBJDEST=.
UTESTDEST=utest

include $(BUILD_ROOT)/nsconfig.mk

MODULE_CFLAGS=-I$(BUILD_ROOT)/include/libaccess -I$(BUILD_ROOT)/include -I$(BUILD_ROOT)/include/public -I. -DACL_LIB_INTERNAL

include $(INCLUDE_DEPENDS)

#TESTFLAGS = -DUTEST -DDEBUG_LEVEL_2
TESTFLAGS = -DUTEST 
CC = $(CCC)

CSRC = acltest.cpp onetest.cpp ustubs.cpp twotest.cpp 
TSRC = aclfile0 aclfile1 aclfile2 aclfile3 aclfile4 aclfile5 aclfile6 aclfile7 aclfile8 aclfile9 aclfile10 aclfile11 aclfile12 aclfile13 aclfile14 aclfile15 aclfile16 aclfile17 aclfile18 aclfile19 test.ref
SRC  = $(CSRC) $(TSRC)
XSRC = \
	../oneeval.cpp \
	../lastod.cpp \
	../lasip.cpp \
	../aclutil.cpp \
	../lasdns.cpp \
	../acl.tab.cpp \
	../acl.yy.cpp \
	../acltools.cpp \
	../aclspace.cpp \
	../lasgroup.cpp \
	../lasuser.cpp \
	../lasprogram.cpp \
	../nseframe.cpp \
	../aclcache.cpp \
	../register.cpp \
	../symbols.cpp \
	../method.cpp \
	../authdb.cpp 

COBJ = $(CSRC:%.cpp=%.o)
XOBJ = $(XSRC:../%.cpp=%.o) 

# This may be needed for other platforms too
ifeq ($(ARCH), IRIX)
XLIBS = -rpath $(LDAP_LIBPATH)
endif

ifeq ($(ARCH), HPUX)
XLIBS = -lpthread
endif
ifeq ($(ARCH), SOLARIS)
XLIBS = -lsocket -lnsl -ldl -lposix4 
endif

XLIBS+=	$(OBJDIR)/lib/base/plist.o \
	$(OBJDIR)/lib/base/pool.o \
	$(OBJDIR)/lib/base/util.o \
	$(OBJDIR)/lib/base/ereport.o \
	$(OBJDIR)/lib/base/system.o \
	$(OBJDIR)/lib/base/shexp.o \
	$(OBJDIR)/lib/base/pblock.o \
	$(OBJDIR)/lib/base/file.o \
	$(OBJDIR)/lib/base/systhr.o \
	$(OBJDIR)/lib/base/nscperror.o \
	$(OBJDIR)/lib/libldapu.a \
	$(LIBNSPR) 

all: $(COBJ) $(TSRC) acltest 
	./acltest > test.out
	diff test.ref test.out
	@echo
	@echo "The unit test is passed if there is no diff output, and the"
	@echo "Purify window shows no errors and 0 bytes leaked."
	@echo
	@echo "Run - gmake coverage - manually to get code coverage analysis."
	@echo

aclparse: ustubs.o testmain.o $(XOBJ)
	purify $(CC) -o aclparse testmain.o $(XOBJ)  ustubs.o $(XLIBS)

aclparse.pure: acl.tab.o acl.yy.o testmain.o acltools.o ustubs.o
	purify -user-path=.. $(CC) -o aclparse.pure $(XOBJ) ustubs.o $(XLIBS)

onetest: onetest.o ustubs.o $(XOBJ)
	$(CC) -o onetest onetest.o $(XOBJ) ustubs.o $(XLIBS)

twotest: twotest.o ustubs.o $(XOBJ)
	$(CC) -o twotest twotest.o $(XOBJ) ustubs.o $(XLIBS)

acltest: acltest.o ustubs.o $(XOBJ)
#	purify $(CC) -o acltest acltest.o $(XOBJ) ustubs.o $(XLIBS)
	$(CC) -o acltest acltest.o $(XOBJ) ustubs.o $(XLIBS) 

coverage: acltest.o ustubs.o $(XOBJ)
	purecov $(CC) -o acltestcov acltest.o $(XOBJ) ustubs.o $(XLIBS)
	rm -f *.pcv
	acltestcov

lasemail: lasemail.o
	$(LD) -G -h lasemail.so -o lasemail.so lasemail.o

#$(XOBJ): $(XSRC)
#	cd ..; gmake OBJDEST=$(UTESTDEST) CC=$(OCC) TESTFLAGS=$(TESTFLAGS)

%.o:../%.c
	$(CC) -c $(CFLAGS) $(TESTFLAGS) $(MCC_INCLUDE) -I.. $< -o $(OBJDEST)/$*.o

%.o:../%.cpp
	$(CC) -c $(CFLAGS) $(TESTFLAGS) $(MCC_INCLUDE) -I.. $< -o $(OBJDEST)/$*.o
