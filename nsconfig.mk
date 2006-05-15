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
# nsconfig.h: determines which product is being built, how, and for what 
# arch
#
# Rob McCool

# This is the default target for all Makefiles which include this file.
# Those Makefiles should define this target with the appropriate dependencies
# and build rules.
#
# More comment
all:


ABS_ROOT := $(shell cd $(BUILD_ROOT); pwd)
ABS_ROOT_PARENT := $(shell cd $(ABS_ROOT)/..; pwd)
MAKE=gmake $(BUILDOPT)

# 7/12/96 Adrian - allow MAKEFLAGS to propagate
# override MAKEFLAGS := 

# all of these things are on by default for internal builds
ifdef INTERNAL_BUILD
	USE_ADMINSERVER:=1
	USE_CONSOLE:=1
	USE_DSMLGW:=1
	USE_ORGCHART:=1
	USE_DSGW:=1
	USE_JAVATOOLS:=1
	USE_SETUPUTIL:=1
	USE_PERLDAP:=1
else
	USE_ADMINSERVER:=1
	USE_CONSOLE:=1
	USE_DSMLGW:=1
	USE_ORGCHART:=1
	USE_DSGW:=1
	USE_JAVATOOLS:=1
	USE_SETUPUTIL:=1
	USE_PERLDAP:=1
	GET_JAVA_FROM_PATH := 1
	GET_ANT_FROM_PATH := 1
	USE_PERL_FROM_PATH := 1
	BUILD_JAVA_CODE := 1
endif

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/component_versions.mk

# It looks like most of the latest versions of Unix that we ship on
# have a good enough heap implementations that they don't need 
# SmartHeap.  We still need it on NT and HPUX.
# Solaris 8 and later has mtmalloc
# By contract HPUX must be aligned with Solaris.
ifneq ($(ARCH), SOLARIS)
ifneq ($(ARCH), WINNT)
ifneq ($(ARCH), HPUX)
LDAP_DONT_USE_SMARTHEAP=1
endif
endif
endif

ifeq ($(ARCH), HPUX)
  ifeq ($(NSOS_TEST1),ia64)
    LDAP_DONT_USE_SMARTHEAP=1
    ifeq ($(DEBUG), optimize)
      CFLAGS+=+O3
    endif
  endif
endif

# Don't use smartheap for debug builds
ifeq ($(DEBUG), full)
LDAP_DONT_USE_SMARTHEAP=1
endif

ifeq ($(SECURITY), domestic)
  SEC_SUFFIX = D
else
  SEC_SUFFIX = E
endif

PRETTY_ARCH := $(shell uname -s)

NSOS_ARCH         := $(subst /,_,$(shell uname -s))

ifneq ($(NO_BUILD_NUM), true)
  GET_BUILD_NUM := $(shell cat $(BUILD_ROOT)/$(BUILD_ARCH)/buildnum.dat)
endif

ifeq ($(NSOS_ARCH), IRIX64)
  NSOS_ARCH := IRIX
endif

# Default
NSOS_RELEASE      := $(shell uname -r)
NSOS_RELEASE_NOTAG = $(NSOS_RELEASE)

# Check if we're on RHEL
ifeq ($(NSOS_ARCH), Linux)
  NSOS_TEST := $(shell cat /etc/redhat-release)
  ifeq ($(findstring Taroon,$(NSOS_TEST)),Taroon)
    NSOS_ARCH := RHEL
    NSOS_RELEASE := 3
    # Always use gcc on RHEL
    GCC_VERSION := gcc$(word 1, $(shell gcc --version | sed 's/gcc.*GCC.\s//' | sed 's/\..*//'))
  else
  ifeq ($(findstring Nahant,$(NSOS_TEST)),Nahant)
      NSOS_ARCH := RHEL
      NSOS_RELEASE := 4
      # Always use gcc on RHEL
      GCC_VERSION := gcc$(word 1, $(shell gcc --version | sed 's/gcc.*GCC.\s//' | sed 's/\..*//'))
  endif
  endif
endif

ifeq ($(NSOS_ARCH), AIX)
 NSOS_TEST        := $(shell uname -v)
 ifeq ($(NSOS_TEST),3)
   NSOS_RELEASE   := $(shell uname -r)
 else
   NSOS_RELEASE   := $(shell uname -v).$(shell uname -r)
 endif
endif

# Get OSF's OS revision number to figure out the OS release 
ifeq ($(NSOS_ARCH),OSF1)
  NSOS_OSF1REV    := $(shell uname -v)
  ifeq ($(NSOS_OSF1REV),878)
    NSOS_TAG := D
  else
    ifeq ($(NSOS_OSF1REV),564)
      NSOS_TAG := B
    else
      ifeq ($(NSOS_OSF1REV),464)
        NSOS_TAG := A
      else
# Grenoble: Defaulting to release D since these are the only version of components we have so far.
        NSOS_TAG := D
      endif
    endif
  endif
  NSOS_RELEASE_NOTAG := $(NSOS_RELEASE)
  NSOS_RELEASE := $(NSOS_RELEASE_NOTAG)$(NSOS_TAG)
else
  NSOS_RELEASE_NOTAG := $(NSOS_RELEASE)
endif

# IRIX: Recommond: USE_PTHREADS=1 and USE_N32=1
ifeq ($(NSOS_ARCH), IRIX)
 ifeq ($(USE_PTHREADS), 1)
  ifeq ($(USE_N32), 1)
   NSOS_RELEASE := $(shell uname -r)_n32_PTH
  else
   NSOS_RELEASE := $(shell uname -r)_PTH
  endif
 endif
 NSOS_RELEASE_NOTAG := $(shell uname -r)
endif

# SVR5 (UnixWare7)
ifeq ($(NSOS_ARCH),UnixWare)
# For now get the OS release for backward compatibility (UnixWare5)
NSOS_RELEASE    := $(shell uname -r)
endif

# Catch NCR butchering of SVR4
ifeq ($(NSOS_ARCH),UNIX_SV)
ifneq ($(findstring NCR, $(shell grep NCR /etc/bcheckrc | head -1 )),)
NSOS_ARCH       := NCR
PRETTY_ARCH     := NCR
else # !NCR
# Make UnixWare something human readable
NSOS_ARCH       := UNIXWARE
PRETTY_ARCH     := UNIXWARE
endif # !NCR
# Check for UW2 using UDK, which looks like a Gemini (UnixWare7) build
NSOS_RELEASE    := $(shell $(BUILD_ROOT)/nsarch -f | sed 's/UnixWare //')
ifeq ($(NSOS_RELEASE),5)
NSOS_ARCH       := UnixWare
else # NSOS_RELEASE = 5
# Get the OS release number, not 4.2
NSOS_RELEASE    := $(shell uname -v)
ifeq ($(NSOS_RELEASE),2.1.2)
# so we don't need yet another set of duplicate UNIXWARE$VER.mk gmake files
NSOS_RELEASE    := 2.1
endif # NSOS_RELEASE = 2.1.2
endif # NSOS_RELEASE = 5
endif # UNIX_SV

# Clean up SCO
ifeq ($(NSOS_ARCH),SCO_SV)
NSOS_ARCH	:= SCOOS
ifeq (5.0,$(findstring 5.0,$(shell ls /var/opt/K/SCO/Unix)))
NSOS_RELEASE	:= 5.0
else
NSOS_RELEASE	:= UNKNOWN
endif
endif

# both values (SINIX-N and ReliantUNIX-N) are possible
ifeq ($(NSOS_ARCH), SINIX-N)
NSOS_ARCH     := ReliantUNIX
PRETTY_ARCH   := ReliantUNIX
NSOS_RELEASE  := 5.4
endif
ifeq ($(NSOS_ARCH), ReliantUNIX-N)
NSOS_ARCH     := ReliantUNIX
PRETTY_ARCH   := ReliantUNIX
NSOS_RELEASE  := 5.4
endif

# Make NT versions 5.1 and 5.2 look like 5.0 for build purposes
ifndef NSOS_RELEASE_OVERRIDE
ifeq ($(NSOS_ARCH),WINNT)
ifeq ($(NSOS_RELEASE),5.1)
NSOS_RELEASE_OVERRIDE=5.0
endif
ifeq ($(NSOS_RELEASE),5.2)
NSOS_RELEASE_OVERRIDE=5.0
endif
endif
endif

ifdef NSOS_RELEASE_OVERRIDE
NSOS_RELEASE_NOTAG  := $(NSOS_RELEASE_OVERRIDE)
NSOS_RELEASE := $(NSOS_RELEASE_NOTAG)$(NSOS_TAG)
endif

ifeq ($(NSOS_ARCH),Linux)
#
# Handle FreeBSD 2.2-STABLE and Linux 2.0.30-osfmach3 and 2.2.14-5.0smp
#
ifeq (,$(filter-out Linux FreeBSD,$(NSOS_ARCH)))
 NSOS_RELEASE  := $(shell echo $(NSOS_RELEASE) | sed 's/-.*//')
endif 
NSOS_RELEASE_TEMP := $(subst ., ,$(NSOS_RELEASE))
NSOS_RELEASE_COUNT := $(words $(NSOS_RELEASE_TEMP))
ifeq ($(NSOS_RELEASE_COUNT), 4)
	NSOS_RELEASE := $(basename $(NSOS_RELEASE))
endif
NSOS_RELEASE := $(basename $(NSOS_RELEASE))
NSOS_ARCH       := Linux
PRETTY_ARCH     := Linux
endif

NSOS_TEST1       := $(shell uname -m)
ifeq ($(NSOS_ARCH),Linux)
  ifneq (x86_64, $(NSOS_TEST1))
    ifeq (86,$(findstring 86,$(NSOS_TEST1)))
      NSOS_TEST1    = x86
    endif
  endif

  ifeq ($(USE_LIBC),1)
    LIBC_VERSION  = _libc
  else
    LIBC_VERSION  = _glibc
  endif
  USE_PTHREADS = 1
  ifeq ($(USE_PTHREADS),1)
    PTHREAD_TAG   = _PTH
  endif
  NSCONFIG        = $(NSOS_ARCH)$(NSOS_RELEASE)_$(NSOS_TEST1)$(LIBC_VERSION)$(PTHREAD_TAG)
  NSCONFIG_NOTAG  = $(NSCONFIG)
else
  ifeq ($(NSOS_ARCH),RHEL)
    ifneq (x86_64, $(NSOS_TEST1))
      ifeq (86,$(findstring 86,$(NSOS_TEST1)))
        NSOS_TEST1    = x86
      endif
    endif
    NSCONFIG        = $(NSOS_ARCH)$(NSOS_RELEASE)_$(NSOS_TEST1)_$(GCC_VERSION)
    NSCONFIG_NOTAG  = $(NSCONFIG)
  else
    ifeq ($(NSOS_ARCH),HP-UX)
      NSOS_TEST1       := $(shell uname -m)
      ifeq ($(NSOS_TEST1), ia64)
        NSCONFIG         = $(NSOS_ARCH)$(NSOS_RELEASE)_$(NSOS_TEST1)$(NS64TAG)
        NSCONFIG_NOTAG   = $(NSOS_ARCH)$(NSOS_RELEASE_NOTAG)_$(NSOS_TEST1)$(NS64TAG)
      else
        NSCONFIG         = $(NSOS_ARCH)$(NSOS_RELEASE)$(NS64TAG)
        NSCONFIG_NOTAG   = $(NSOS_ARCH)$(NSOS_RELEASE_NOTAG)$(NS64TAG)
      endif
    else
    ifeq ($(NSOS_TEST1),i86pc)
      NSCONFIG         = $(NSOS_ARCH)$(NSOS_RELEASE)_$(NSOS_TEST1)$(NS64TAG)
      NSCONFIG_NOTAG   = $(NSOS_ARCH)$(NSOS_RELEASE_NOTAG)_$(NSOS_TEST1)$(NS64TAG)
    else
      NSCONFIG         = $(NSOS_ARCH)$(NSOS_RELEASE)$(NS64TAG)
      NSCONFIG_NOTAG   = $(NSOS_ARCH)$(NSOS_RELEASE_NOTAG)$(NS64TAG)
    endif
    endif
  endif
endif

ifeq ($(DEBUG), full)
  MCC_DEBUG=-DMCC_DEBUG $(ARCH_DEBUG)
  NSOBJDIR_TAG=_DBG
  ML_DEBUG=$(ARCH_LINK_DEBUG)
else
ifeq ($(DEBUG), purify)
  MCC_DEBUG=$(ARCH_DEBUG)
  NSOBJDIR_TAG=_DBG
  ML_DEBUG=$(ARCH_LINK_DEBUG)
  PUREOPTS=-follow-child-processes=true -check-debug-timestamps=no -best-effort
  PURIFY=purify $(PUREOPTS)
else
ifeq ($(DEBUG), quantify)
  MCC_DEBUG=$(ARCH_DEBUG)
  NSOBJDIR_TAG=_DBG
  ML_DEBUG=$(ARCH_LINK_DEBUG)
  QUANOPTS=-follow-child-processes=true -check-debug-timestamps=no -best-effort
  QUANTIFY=quantify $(QUANOPTS)
  USE_QUANTIFY=1
else
  MCC_DEBUG=$(ARCH_OPT)
  NSOBJDIR_TAG=_OPT
  ML_DEBUG=$(ARCH_LINK_OPT)
  BUILDOPT = BUILD_OPT=1
endif
endif
endif

ifeq ($(NSOS_ARCH),WINNT)
  ifneq ($(PROCESSOR_ARCHITECTURE),x86)
    NSOBJDIR_NAME     = $(NSCONFIG)$(PROCESSOR_ARCHITECTURE)$(NSOBJDIR_TAG).OBJ
    NSOBJDIR_NAME_32     = $(NSCONFIG)$(PROCESSOR_ARCHITECTURE)$(NSOBJDIR_TAG).OBJ
  else
    NSOBJDIR_NAME     = $(NSCONFIG)$(NSOBJDIR_TAG).OBJ
    NSOBJDIR_NAME_32     = $(NSCONFIG)$(NSOBJDIR_TAG).OBJ
  endif
else
  NSOBJDIR_NAME     = $(NSCONFIG)$(NSOBJDIR_TAG).OBJ
  NSOBJDIR_NAME_32  = $(subst $(NS64TAG),,$(NSOBJDIR_NAME))
  NSOBJDIR_NAME1    = $(NSOBJDIR_NAME)
endif


# Where to put component packages (libs, includes) to build against and package
NSCP_DIST	=  $(BUILD_ROOT)/../dist
NSCP_DISTDIR          = $(BUILD_ROOT)/../dist/$(NSOBJDIR_NAME)

MAKE=gmake $(BUILDOPT) NO_JAVA=1

export NSPR20=1

LWDEFS = LIVEWIRE=1
JSDEFS = JSFILE=1 JS_THREADSAFE=1

-include ../lw/config/netsite-config.mk

NSDEFS=SERVER_BUILD=1 XCFLAGS=$(MCL_CFLAGS) USE_PTHREADS=$(USE_PTHREADS) \
       NS_PRODUCT=$(NS_PRODUCT) VERSION=$(SERVER_VERSION) \
       NS_USE_NATIVE=$(NS_USE_NATIVE) NSPR_BASENAME=$(NSPR_BASENAME)

NSPR_DEFINES = -DSERVER_BUILD


# Destination for class files and packages
CLASS_DEST        = $(NSCP_DIST)/classes

# ----------- Where to get shared components --------------------
# COMPONENTS_DIR_DEV can be used to pull stuff from the integration area
# at rtm time we switch it over to use the official components

ifndef COMPONENTS_DIR_DEV
COMPONENTS_DIR_DEV = /share/builds/sbsintegration
endif

# internal repository for all pre-built RTM components, including Red Hat branded ones
ifndef COMPONENTS_DIR
COMPONENTS_DIR = /share/builds/components
endif

# internal repository for pre-built RTM Fedora branded components
ifndef FED_COMPONENTS_DIR
FED_COMPONENTS_DIR = /fedora/components
endif

###########################################################


# ------------------------ Product-specific flags ------------------------


ifdef PRODUCT_IS_DIRECTORY_SERVER
  ifeq ($(SECURITY), export)
    MCC_SERVER=-DMCC_HTTPD -DNS_EXPORT -DNET_SSL -DCLIENT_AUTH $(NSPR_DEFINES)
  endif
  ifeq ($(SECURITY), domestic)
    MCC_SERVER=-DMCC_HTTPD -DNS_DOMESTIC -DNET_SSL -DCLIENT_AUTH $(NSPR_DEFINES)
  endif
  ifeq ($(SECURITY), none)
    MCC_SERVER=-DMCC_HTTPD -DNS_UNSECURE $(NSPR_DEFINES)
  endif
  DS_SERVER_DEFS = -DNS_DS
  ifeq ($(BUILD_DEBUG),full)
    MODULE_CFLAGS += -DDEBUG
  endif
  MCC_SERVER += $(DS_SERVER_DEFS)
  NS_PRODUCT = DIRECTORY_SERVER
  ifeq ($(LDAP_NO_LIBLCACHE),1)
    MODULE_CFLAGS+=-DNO_LIBLCACHE
  endif
endif

MCC_SERVER += -DSPAPI20 -DBUILD_NUM=$(GET_BUILD_NUM)

# ----------- Default Flags, may be overridden below ------------

PEER_DATE=19980121
CPPCMD=$(CC) -E
HTTPDSO_NAME=$(BUILD_HTTPDLL_NAME)
MATHLIB=-lm
LIB_SUFFIX=a
AR=ar rcv $@
DLL_PRESUF=
DLL_SUFFIX=so
LDAP_DLL_PRESUF=
LDAP_DLL_SUFFIX=$(DLL_SUFFIX)
LINK_DLL=ld $(DLL_LDFLAGS) -o $@
LINK_PLUGIN=$(LINK_DLL)

# ---------------------- OS-specific compile flags -----------------------


# Used by peer snmp defines below
DEVROOT = $(BUILD_ROOT)/peer

ifeq ($(ARCH), AIX)
# convert the output of oslevel to a 4 digit number
# e.g. 4.2.0.0 -> 4200  4.2.1.0 -> 4210
OSVERSION := $(shell oslevel | sed "s/[.]//g")
OSMAJORVERSION := $(shell oslevel | cut -f1 -d.)
OSMINORVERSION := $(shell oslevel | cut -f2 -d.)
# we had to do a lot of special stuff to make shared libraries work 
ifeq ("yes", $(shell /bin/sh -c "[ $(OSVERSION) -lt 4200 ] && echo yes" ) )
	OLD_AIX_LINKING=1
endif

ifdef OLD_AIX_LINKING
	DLL_PRESUF=_shr
	DLL_SUFFIX=a
else
# there is apparently a lot of stuff to do before we can use .so as the
# shared library suffix, so leave it for now . . .
	DLL_PRESUF=
	DLL_SUFFIX=so
	LD=xlC_r
endif

#CC=xlC_r -qinfo -qarch=com -qgenpcomp=$(OBJDIR)/.pcomp -qusepcomp -DAIX -DAIXV3 -DAIXV4 -DSYSV -DOSVERSION=$(OSVERSION)
CC=xlC_r -qinfo -qarch=com -DAIX -DAIXV3 -DAIXV4 -DSYSV -DOSVERSION=$(OSVERSION) -DAIX$(OSMAJORVERSION)_$(OSMINORVERSION)
CCC=$(CC)
CXX=$(CC)
CPPCMD=/usr/ccs/lib/cpp -P
ARCH_DEBUG=-g -qfullpath
ARCH_OPT=-O
RANLIB=ranlib
SHARED_FLAG=-brtl
NONSHARED_FLAG=-bnso -bI:/lib/syscalls.exp
EXPORT_FILE=$*.exp
DEF_LIBPATH=/usr/lib/threads:/usr/lpp/xlC/lib:/usr/lib:/lib
# JCM - LINK_PLUGIN+=-berok
ifdef OLD_AIX_LINKING
	MKSHLIB_FLAGS=-p 0 -blibpath:$(DEF_LIBPATH)
	DLL_LDFLAGS=-bM:SRE -bnoentry -blibpath:$(DEF_LIBPATH)
	LINK_DLL=$(BUILD_ROOT)/build/aixmkshlib -o $@ $(MKSHLIB_FLAGS)
	MKSHLIB=$(LINK_DLL)
	EXTRA_LIBS=-lsvld
else
	MKSHLIB_FLAGS=-brtl -p 0 -blibpath:$(DEF_LIBPATH)
	DLL_LDFLAGS=-bM:SRE -bnoentry -blibpath:$(DEF_LIBPATH)
	LINK_DLL=/usr/lpp/xlC/bin/makeC++SharedLib_r -o $@ $(MKSHLIB_FLAGS)
	ifeq ($(OSMAJORVERSION), 4)
	  ifeq ($(OSMINORVERSION), 3)
	    LINK_DLL=/usr/ibmcxx/bin/makeC++SharedLib_r -o $@ $(MKSHLIB_FLAGS)
	  endif
	endif
	MKSHLIB=$(LINK_DLL)
	EXTRA_LIBS=-ldl
endif
#LINK_DLL=$(LD) $(DLL_LDFLAGS)  -o $@
# AIX plugins can have unresolved references
ifdef PRODUCT_IS_DIRECTORY_SERVER
EXTRA_LIBS += -lpthreads -lc_r -lm
endif

VERITY_ARCH=_rs6k41
NSAPI_CAPABLE=true
USE_PTHREADS=1
ifdef OLD_AIX_LINKING
	DL_LIB =-lsvld
else
	DL_LIB =-ldl
endif
NSPR_DEFINES += -DNSPR20 
RWTOOLS_VERSION = latest
#HTTPDSO_NAME=libdsnshttpd
PEER_ARCH=aix

else
ifeq ($(ARCH), BSDI)

CC=cc
ARCH_DEBUG=-g
ARCH_OPT=-O2
ARCH_CFLAGS=-Wall -DNO_GETDOMAINNAME
RANLIB=ranlib
NONSHARED_FLAG=-static
PEER_ARCH=bsdi

else 
ifeq ($(ARCH), HPUX)
ifeq ($(NSOS_TEST1), ia64)
DLL_SUFFIX=so
else
DLL_SUFFIX=sl
endif
#-D_POSIX_C_SOURCE=199506L turns kernel threads on for HPUX11
CC=cc -Ae -D_POSIX_C_SOURCE=199506L
ifeq ($(BUILD_MODULE), HTTP_ADMIN)
ifeq ($(NSOS_RELEASE),B.11.23)
# -AA -- new-lib libstd 2
CXX=aCC -AA -DHPUX_ACC -D__STDC_EXT__ -D_POSIX_C_SOURCE=199506L -ext
else
CXX=aCC -DHPUX_ACC -D__STDC_EXT__ -D_POSIX_C_SOURCE=199506L -ext
endif
else
ifeq ($(NSOS_RELEASE),B.11.23)
# -AA -- new-lib libstd 2
CXX=aCC -AA -DHPUX_ACC -D__STDC_EXT__ -D_POSIX_C_SOURCE=199506L -ext
else
CXX=aCC -DHPUX_ACC -D__STDC_EXT__ -D_POSIX_C_SOURCE=199506L -ext
endif
endif
CCC=$(CXX)
ARCH_DEBUG=-g
ifeq ($(NSOS_RELEASE),B.11.23)
# optimization level changes actually is due to the aCC changes,
# it is applicable to 11i v1 also, but conditional compile here
# anyway.
ARCH_OPT=+O3
else
ARCH_OPT=-O
endif
# Compile everything pos-independent in case we need to put it in shared lib
ifeq ($(NSOS_RELEASE),B.11.23)
ifdef USE_64
  ARCH_CFLAGS=-D_HPUX_SOURCE +DD64 +DSblended +Z
else
  ARCH_CFLAGS=-D_HPUX_SOURCE +DD32 +DSblended +Z
endif
else
ifdef USE_64
  ARCH_CFLAGS=-D_HPUX_SOURCE +DA2.0W +DS2.0 +Z
else
  ARCH_CFLAGS=-D_HPUX_SOURCE +DAportable +DS1.1 +Z
endif
endif
# NSPR uses fpsetmask which I'm told is in the math lib
EXTRA_LIBS= -ldld -lm
ifeq ($(NSOS_RELEASE), B.10.10)
ARCH_CFLAGS+=-DHPUX10 -DHPUX10_10
# Debug with HPUX "dde" - makes the server single process - avoids fork()ing.
# Can also be used for non HPUX if desired.
#ARCH_CFLAGS+=-DUSE_DDE_DEBUG
EXTRA_LIBS += -ldce
else

ifeq ($(NSOS_RELEASE), B.11.00)
	MODERNHP=1
endif

ifeq ($(NSOS_RELEASE), B.11.11)
	MODERNHP=1
endif

ifeq ($(NSOS_RELEASE), B.11.23)
	MODERNHP=1
endif

ifeq ($(MODERNHP), 1)
ifeq ($(NSOS_RELEASE), B.11.00)
	ARCH_CFLAGS+=-DHPUX11 -DHPUX11_00
endif
ifeq ($(NSOS_RELEASE), B.11.11)
	ARCH_CFLAGS+=-DHPUX11 -DHPUX11_11
endif
ifeq ($(NSOS_RELEASE), B.11.23)
	ARCH_CFLAGS+=-DHPUX11 -DHPUX11_11
endif
# Debug with HPUX "dde" - makes the server single process - avoids fork()ing.
# Can also be used for non HPUX if desired.
#ARCH_CFLAGS+=-DUSE_DDE_DEBUG
EXTRA_LIBS+= -lpthread
endif
endif
RANLIB=true
NONSHARED_FLAG=-Wl,-a,archive

# Flags passed to CC to pass onto the linker; separate it from EXTRA_LIBS 
ifeq ($(USE_64),1)
LD_CFLAGS=-W1,-E
else
LD_CFLAGS=-Wl,-E,-N
endif
DLL_CFLAGS=+Z
DLL_LDFLAGS=-b
NSAPI_CAPABLE=true
VERITY_ARCH=_hp800
USE_PTHREADS=1
NSPR_DEFINES += -DNSPR20 -D_PR_NTHREAD -D_PR_USECPU -D_REENTRANT
LINK_DLL=$(CCC) $(DLL_LDFLAGS) $(ARCH_CFLAGS) -o $@
PEER_ARCH=hp
RWTOOLS_VERSION = latest

else 
ifeq ($(ARCH), IRIX)
OSVERSION := $(shell uname -r)
# the ns/ side of the fence assumes SVR4 is defined. --Rob
CC=cc -DSVR4
ifndef PRODUCT_IS_DIRECTORY_SERVER
CXX=CC -DSVR4 -exceptions
else
# XXXmcs: 17-Sep-1997 - the -exceptions flag causes the IRIX CC compiler
# to dump core when compiling ldapserver/lib/safs/aclsafs.c
# We don't seem to need this for Directory Server....
CXX=CC -DSVR4
endif
CCC=$(CXX)
ARCH_DEBUG=-g
ARCH_OPT=-O
ifeq ($(USE_N32), 1)
 ARCH_CFLAGS=-fullwarn -use_readonly_const -MDupdate .depends -n32
else
 ARCH_CFLAGS=-fullwarn -use_readonly_const -MDupdate .depends -o32
endif
RANLIB=true
DLL_LDFLAGS=-shared
NONSHARED_FLAG=-non_shared
NLIST=-lmld
NSAPI_CAPABLE=true
# Use -DIRIX6_2 for 6.x
ifeq (6.,$(findstring 6.,$(OSVERSION)))
ARCH_CFLAGS+=-DIRIX6_2
endif
ifeq (6.5,$(OSVERSION))
ARCH_CFLAGS+=-DIRIX6_5 -mips3
endif
NSPR_DEFINES += -DNSPR20 -D_PR_NTHREAD -D_SGI_MP_SOURCE

LINK_DLL=$(CCC) $(DLL_LDFLAGS) -o $@
PEER_ARCH=irix

export NO_DB2=1

else 
ifeq ($(ARCH), Linux)
OSVERSION	:= $(basename $(shell uname -r))
CC=/usr/bin/gcc
CXX=/usr/bin/g++
CCC=$(CXX)
LD=$(CXX)
ARCH_DEBUG=-g
ARCH_OPT=-O2
ARCH_CFLAGS=-Wall -DNO_DBM -DLINUX -DLINUX2_2 -DLINUX2_4 -fPIC -D_REENTRANT
RANLIB=ranlib
DLL_LDFLAGS=-shared
LINK_DLL=$(CC) $(DLL_LDFLAGS) -o $@
NSPR_DEFINES += -DNSPR20 
NLIST=-lelf
NSAPI_CAPABLE=true
EXTRA_LIBS=-ldl -lcrypt -lpthread
BUILD_IIOPLIB=no
ifeq (2.4,$(OSVERSION))
PEER_ARCH=linux2.4
else
PEER_ARCH=linux
endif
# linux always uses pthreads
USE_PTHREADS = 1
# remove this line when smartheap is built for linux
LDAP_DONT_USE_SMARTHEAP = 1

else
ifeq ($(ARCH), UNIXWARE)

CC=$(BUILD_ROOT)/build/hcc
CXX=$(BUILD_ROOT)/build/hcpp
CCC=$(CXX)
CPPCMD=/lib/cpp -P
ARCH_DEBUG=-g
ARCH_OPT=
ARCH_CFLAGS=
RANLIB=true
NLIST=-lelf
NSAPI_CAPABLE=true

# to use native threads - uncomment this
# export USE_SVR4_THREADS=1

# The -lgen is for syslog.

ifdef USE_SVR4_THREADS
OS_THREADLIB = -lthread
endif

EXTRA_LIBS=-lsocket -lnsl -lresolv -ldl -lgen -lC -lc /usr/ucblib/libucb.a $(OS_THREADLIB)
# extra libs because svr4 doesn't support sockets the way we want...?
DLL_LDFLAGS=-d y -G
USE_LD_RUN_PATH=true
LDAP_VERSION = oem
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem
NEED_VTASKSTUB = 1
NO_MSOFT_OBJ=1
BUILD_WEBPUB=no
BUILD_IIOPLIB=no
NSPR_DEFINES += -DNSPR20 
PEER_ARCH=unixware

export NO_INFORMIX=1
export NO_ORACLE=1
export NO_SYBASE=1
export NO_ODBC=1
export NO_DB2=1

else
ifeq ($(ARCH), UnixWare)
# We don't want to use absolute paths here, because we might want to pickup
# UDK tools from /udk/usr/ccs, so we depend on the PATH being correctly set
CC=cc
CCC=CC
CXX=$(CCC)
CPPCMD=/lib/cpp -P
ARCH_DEBUG=-g
ARCH_OPT=-O
SV_REL := $(shell uname -r)
# SVR5 if Gemini UnixWare
# SVR4 if UnixWare 2.1.x with the UDK tools
ifeq ($(SV_REL),5)
  ARCH_CFLAGS=-DSVR5 -D_SIMPLE_R
else
  ARCH_CFLAGS=-DSVR4 -D_SIMPLE_R
endif
RANLIB=true
NLIST=-lelf
NSAPI_CAPABLE=true

# to use native threads - uncomment this
# export USE_SVR4_THREADS=1

ifdef USE_SVR4_THREADS
OS_THREADLIB = -lthread
endif

# extra libs because svr4 doesn't support sockets the way we want...?
EXTRA_LIBS=-lsocket -lnsl -lresolv -ldl -lgen -lC -lc /usr/ucblib/libucb.a
$(OS_THREADLIB)
LICENSE_LIBS=
#LICENSE_LIBS=-lscolicense -lannot -li4clnt -li4shl -li4nsi -li4rpci\
#                -lnck_task -lcps -lsocket /usr/ccs/lib/libC.a
# extra libs because svr4 doesn't support sockets the way we want...?
VERITY_LIB=_386svr4

#If you comment the line below, you will turn off the SCO licensing.
#This will allow you to use the server internally for test purposes.
#MCC_SERVER += -DSCO_PM
DLL_LDFLAGS=-d y -G
#DLL_CFLAGS=-KPIC
USE_LD_RUN_PATH=true
LDAP10_VERSION = oem
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem
NEED_VTASKSTUB = 1
NO_MSOFT_OBJ=1
BUILD_WEBPUB=no
BUILD_IIOPLIB=no
NSPR_DEFINES += -DNSPR20
PEER_ARCH=unixware5
DB_VERSION=oem

export NO_INFORMIX=1
export NO_ORACLE=1
export NO_SYBASE=1
export NO_ODBC=1
export NO_DB2=1

else
ifeq ($(ARCH), SCOOS)
CC=cc -b elf -KPIC -DSCOOS
CXX=g++ -b elf -DPRFSTREAMS_BROKEN -I/usr/local/lib/g++-include
CCC=$(CXX)
CPPCMD=/lib/cpp
ARCH_DEBUG=-g
ARCH_OPT=
ARCH_CFLAGS=
RANLIB=true
NLIST=-lelf
NSAPI_CAPABLE=true
EXTRA_LIBS=-lsocket -lnsl -ldl -lpmapi -lc -lPW
DLL_LDFLAGS=-d y -G

VERITY_ARCH = _scoodt
MCC_SERVER += -DSCO_PM
DLL_LDFLAGS=-d y -G
USE_LD_RUN_PATH=true
LDAP_VERSION = oem
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem
BUILD_WEBPUB = no
NO_MSOFT_OBJ = 1
NSPR_DEFINES += -DNSPR20 
PEER_ARCH=sco

else
ifeq ($(ARCH), NCR)

ABS_ROOT_PARENT := $(shell cd $(BUILD_ROOT)/..; pwd)

NS_USE_GCC	= 1

CPPCMD		= /lib/cpp
ARCH_DEBUG	= -g
ARCH_OPT	=
ARCH_CFLAGS	=
RANLIB		= true
NLIST		= -lelf
NSAPI_CAPABLE   = true

ifdef NS_USE_GCC
# if gcc-settings are redefined already - don't touch it
#
ifeq (,$(findstring gcc, $(CC)))
CC      = gcc
CCC     = g++
CXX     = g++
# always use -fpic - some makefiles are still broken and don't distinguish
# situation when they build shared and static libraries
ARCH_CFLAGS += -fpic -Wall -DPRFSTREAMS_BROKEN -DNS_USE_GCC $(GCC_FLAGS_EXTRA)
CCC_EXTRA_LIBS  = -L/usr/local/lib -lstdc++ -lg++ -lgcc
endif
endif

ifdef NS_USE_NATIVE
CC              = cc
CCC             = ncc
CXX             = ncc
ARCH_CFLAGS    += -DNS_USE_NATIVE
CCC_EXTRA_LIBS  = -L/opt/ncc/lib
endif

###

# order is important
EXTRA_LIBS      = -lsocket -lnsl -lresolv -lgen -ldl $(CCC_EXTRA_LIBS) -lc /usr/ucblib/libucb.a
GCC_FLAGS_EXTRA = -pipe

DLL_LDFLAGS     = -d y -G
VERITY_ARCH     = _isvr4
USE_LD_RUN_PATH = true
LDAP_VERSION    = oem
RWTOOLS_VERSION = latest
ADM_RELDATE     = oem
ADM_VERSDIR 	= admserv35
NEED_VTASKSTUB  = 1
NO_MSOFT_OBJ	= 1
AUTOCATALOG_VERSION = oem
BUILD_WEBPUB    = no
BUILD_IIOPLIB   = no
DL_LIB          =-ldl
NSPR_DEFINES    += -DNSPR20 
# svr4-x86 compatible
PEER_ARCH=unixware

export NO_ODBC=1
export NO_DB2=1

else
ifeq ($(ARCH), SONY)

CC=cc
ARCH_DEBUG=-g
ARCH_OPT=
ARCH_CFLAGS=
RANLIB=true
NLIST=-lelf
NSAPI_CAPABLE=true
EXTRA_LIBS=-lgen -lsocket -lnsl -ldl
DLL_LDFLAGS=-G
# extra libs because svr4 doesn't support sockets the way we want...

LDAP_VERSION = oem
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem

else 
ifeq ($(ARCH), NEC)

CC=$(BUILD_ROOT)/build/hcc
ARCH_DEBUG=-g
ARCH_OPT=-KOlimit=4000
ARCH_CFLAGS=-Xa
RANLIB=true
NLIST=
NSAPI_CAPABLE=true
EXTRA_LIBS=-lsocket -lnsl -ldl -lsdbm
DLL_LDFLAGS=-G
PEER_ARCH=nec
LDAP_VERSION = oem
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem

else
ifeq ($(ARCH), ReliantUNIX)

# do not use DCE PTHREADS now
# USE_PTHREADS = 1
# do not use gcc anymore
# NS_USE_GCC = 1

DLL_LDFLAGS = -G

ifdef NS_USE_GCC

# gcc section
CC=gcc
CXX=gcc
CCC=gcc
LD=gld
ARCH_DEBUG=-gdwarf
ARCH_OPT=-O2
ARCH_LDFLAGS=-Xlinker -Blargedynsym
ARCH_CFLAGS=-pipe -DSVR4 -DSNI
LINK_DLL=gld $(DLL_LDFLAGS) -o $@

else

# CDS++ section
CC=cc
CXX=CC -K old_for_init
CCC=CC -K old_for_init
CPPCMD=/usr/ccs/lib/cpp -P
ARCH_DEBUG=-g
ARCH_OPT=-O2
ARCH_LDFLAGS=-Wl,-Blargedynsym
ARCH_CFLAGS=-DSVR4 -DSNI
# we need to use $(CCC) if the .so contains C++ code
# so in Makefiles where we want the .so to be linked with CC
# we add a DLL_CC = $(CCC)
LINK_DLL=$(DLL_CC) $(DLL_LDFLAGS) -o $@
# otherwise, we just use $(CC)
DLL_CC=$(CC)
# There is another quirk: You cannot load .so's containing C++
# code in an executable linked with $(CC)

ifeq ($(USE_PTHREADS), 1)
ARCH_CFLAGS += -K thread -DUSE_PTHREADS
ARCH_LDFLAGS += -K thread
DLL_LDFLAGS += -K thread
endif

endif

RANLIB=true
DLL_LDFLAGS=-G
NONSHARED_FLAG=
NLIST=-lelf
NSAPI_CAPABLE=true
VERITY_ARCH=_mipsabi
NONSHARED_FLAG=
EXTRA_LIBS=-lsocket -lnsl -lresolv -ldl -lgen -L/usr/local/lib -lsni
USE_LD_RUN_PATH=true

#LDAP_VERSION = latest
#LDAP_RELDATE = latest
LDAP_RELDATE = 20001119
RWTOOLS_VERSION = latest
# NEED_VTASKSTUB = 1

#NO_MSOFT_OBJ=1
BUILD_WEBPUB=yes
BUILD_IIOPLIB=yes
DO_AUTOCATALOG=yes
DO_PKGAUTOCATALOG=yes

#this is no longer needed
#NEED_NSPR_MALLOC=yes
NSPR_DEFINES += -DNSPR20 

PEER_ARCH=reliantunix
RWTOOLS_VERSION = latest
ADM_RELDATE = oem
ADM_VERSDIR = admserv35
AUTOCATALOG_VERSION = oem
DB_VERSION=oem

#export NO_INFORMIX=1
#export NO_ORACLE=1
export NO_SYBASE=1
export NO_ODBC=1
export NO_DB2=1

else 
ifeq ($(ARCH), OSF1)

CC=cc
CCC=cxx
CXX=cxx
ARCH_DEBUG=-g
ARCH_OPT=-O2
ARCH_CFLAGS=-DIS_64 -DOSF1V4 -DOSF1V4$(NSOS_TAG) -ieee_with_inexact -pthread -std1
# ranlib no longer needed on OSF1 V4.0 
RANLIB=true
DLL_LDFLAGS=-shared -all -expect_unresolved "*"
NONSHARED_FLAG=-non_shared
NSAPI_CAPABLE=true
VERITY_ARCH=_aosf40
ifdef PRODUCT_IS_DIRECTORY_SERVER
#EXTRA_LIBS+=-lcxxstd -lcxx
else
#EXTRA_LIBS=-lcxxstd -lcxx
endif
USE_PTHREADS=1
NSPR_DEFINES += -DNSPR20 
RWTOOLS_VERSION = latest
PEER_ARCH=osf
PEERDIR=$(DEVROOT)/osf/dev

export NO_DB2=1
export NO_ODBC=1

else 
ifeq ($(ARCH), SUNOS4)

# Compile everything position-independent so we can put it into shared lib
CC=gcc -fPIC
ARCH_DEBUG=-g
ARCH_OPT=-O2
ARCH_CFLAGS=-Wall -pipe
RANLIB=ranlib
DLL_CFLAGS=-fPIC
DLL_LDFLAGS=-assert pure-text
NONSHARED_FLAG=-static
NSAPI_CAPABLE=true
RESOLV_LINK=/usr/local/lib/libresolvPIC.a
PEER_ARCH=sunos

ifeq ($(RESOLV_FLAG), noresolv)
EXTRA_LIBS=-ldl
DNS_STUB_C=dns-stub.c
DNS_STUB_O=dns-stub.o

else
EXTRA_LIBS=$(RESOLV_LINK) -ldl
DNS_STUB_C=nis-stub.c
DNS_STUB_O=nis-stub.o
endif

else
ifeq ($(ARCH), SOLARIS)

#
# 7/12/96 Adrian - Switch to use SparcWorks for 3.0 Development
#		   Therefore no need to separate from catalog server
#
OSVERSION := $(shell uname -r | sed "y/./0/")
ifndef NS_USE_GCC
NS_USE_NATIVE=1
else
CC=gcc
CXX=g++
CCC=$(CXX)
endif
ifdef NS_USE_NATIVE
CC=cc -v
CXX=CC +w
CCC=$(CXX)
LD=$(CC)
endif
ARCH_OPT=-xO2
ARCH_CFLAGS=-DSVR4 -D__svr4 -D__svr4__ -D_SVID_GETTOD -DOSVERSION=$(OSVERSION)
ifdef USE_64
ifdef NS_USE_NATIVE
  ARCH_CFLAGS += -xarch=v9
else
  ARCH_CFLAGS += -m64
endif
endif
ifdef NS_USE_NATIVE
  ARCH_CFLAGS += -KPIC
else
  ARCH_CFLAGS += -fPIC
endif
ARCH_DEBUG=-g
RANLIB=true

EXTRA_LIBS = -lthread -lposix4 -lsocket -lnsl -ldl -lresolv -lw
ifdef PRODUCT_IS_DIRECTORY_SERVER
EXTRA_LIBS += -lgen
endif

DLL_LDFLAGS=-G
NONSHARED_FLAG=-static
NSAPI_CAPABLE=true
NLIST=-lelf
VERITY_ARCH=_solaris
NSPR_DEFINES += -DNSPR20 -D_PR_NTHREAD -D_REENTRANT
PEER_ARCH=solaris
ifndef NS_USE_NATIVE
NSPR_DEFINES += -DSOLARIS_GCC
endif

# XXXrobm BARF
# ifeq ($(NSOS_RELEASE), 5.3)
# GCCLIB=/usr/local/lib/gcc-lib/sparc-sun-solaris2.3/2.6.3/libgcc.a
GCCLIB=
# We are using a CC compiler but we were linking to libgcc.a - should use sparcworks libC
#GCCLIB=/tools/ns-arch/sparc_sun_solaris2.5/soft/sparcworks-3.0.1/run/default/share/lib/sparcworks/SUNWspro/SC3.0.1/lib/libC.a
# else
#	GCCLIB=/usr/local/lib/gcc-lib/sparc-sun-solaris2.4/2.6.3/libgcc.a
# endif

else
ifeq ($(ARCH), SOLARISx86)
OSVERSION := $(shell uname -r | sed "y/./0/")
NS_USE_NATIVE=1
# Add SOLARIS define as well as SOLARISx86 which happens automagically
CC=cc -DSOLARIS
CXX=CC -DSOLARIS
CCC=$(CXX)
ARCH_OPT=-xO2
ARCH_CFLAGS=-DSVR4 -D__svr4 -D__svr4__ -D_SVID_GETTOD -DOSVERSION=$(OSVERSION)
ARCH_DEBUG=-g
RANLIB=true

EXTRA_LIBS = -lsocket -lthread -lposix4 -lnsl -ldl -lresolv -lm -lw

DLL_LDFLAGS=-G
#LDAP_VERSION    = oem
RWTOOLS_VERSION = latest
#ADM_RELDATE     = oem
#ADM_VERSDIR = admserv30
AUTOCATALOG_VERSION = oem
#DB_VERSION=oem
NONSHARED_FLAG=-static
NSAPI_CAPABLE=true
NLIST=-lelf
NO_MSOFT_OBJ=1
BUILD_WEBPUB=no
BUILD_IIOPLIB=no
NSPR_DEFINES += -DNSPR20 -D_PR_NTHREAD -D_REENTRANT
export NO_INFORMIX=1
export NO_ORACLE=1
export NO_SYBASE=1
export NO_ODBC=1
export NO_DB2=1
PEER_ARCH=solarisx86

else
ifeq ($(ARCH), WINNT)
ifdef DEBUG_RUNTIME
RTFLAG=-MDd
else
RTFLAG=-MD
endif
LIB_SUFFIX=lib
DLL_SUFFIX=dll
PROCESSOR := $(shell uname -p)
PEER_ARCH=nti31
ifeq ($(PROCESSOR), I386)
CPU_ARCH = x386
VERITY_ARCH=_nti31
ARCH_OPT=-DNDEBUG -O2
CC=cl -nologo $(RTFLAG) -W3 -GT -GX -D_X86_ -Dx386 -DWIN32 -D_WINDOWS -D_RWTOOLSDLL
CCP=cl -nologo $(RTFLAG) -W3 -GT -GX -D_X86_ -Dx386 -DWIN32 -D_WINDOWS -D_MBCS -D_AFXDLL -D_RWTOOLSDLL
else 
ifeq ($(PROCESSOR), MIPS)
CPU_ARCH = MIPS
VERITY_ARCH=verity_is_undefined
ARCH_OPT=-DNDEBUG -O2
CC=cl -nologo $(RTFLAG) -W3 -GT -GX -D_MIPS_ -DWIN32 -D_WINDOWS -D_RWTOOLSDLL
CCP=cl -nologo $(RTFLAG) -W3 -GT -GX -D_X86_ -Dx386 -DWIN32 -D_WINDOWS -D_MBCS -D_AFXDLL -D_RWTOOLSDLL
else 
ifeq ($(PROCESSOR), ALPHA)
CPU_ARCH = ALPHA
VERITY_ARCH=_ant35
ARCH_OPT=-DNDEBUG -Od
CC=cl -nologo $(RTFLAG) -W3 -GT -GX -D_ALPHA=1 -DWIN32 -D_WINDOWS -D_RWTOOLSDLL
CCP=cl -nologo $(RTFLAG) -W3 -GT -GX -D_ALPHA_ -DWIN32 -D_WINDOWS -D_MBCS -D_AFXDLL -D_RWTOOLSDLL
BUILD_IIOPLIB=no
export NO_SYBASE=1
export NO_ODBC=1
export NO_DB2=1
else 
CPU_ARCH = processor_is_undefined
endif
endif
endif
RC=rc $(MCC_SERVER)
MC=mc
ifdef HEAPAGENT
OBJSWITCH=-Zi
else
OBJSWITCH=-Z7
endif
ARCH_DEBUG=-D_DEBUG -Od	$(OBJSWITCH)
ARCH_CFLAGS=
ARCH_LINK_DEBUG=-DEBUG
ARCH_LINK_OPT=
RANLIB=echo

EXTRA_LIBS=wsock32.lib kernel32.lib user32.lib gdi32.lib winspool.lib \
           comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib \
           rpcrt4.lib uuid.lib odbc32.lib odbccp32.lib winmm.lib

DLL_LDFLAGS=
NONSHARED_FLAG=
NSAPI_CAPABLE=true
NO_MSOFT_OBJ=1
NSPR_DEFINES += -DNSPR20 -D_PR_NTHREAD -D_PR_USECPU

else

CC=UNKNOWN_SYSTEM_TYPE

endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif

# Peer SNMP defines
ifeq ($(BUILD_SNMP), yes)
MCC_SERVER += -DPEER_SNMP
SNMP=libsnmp
SNMP_INCLUDE=$(DEVROOT)/include
PEERLIB=$(DEVROOT)/$(PEER_ARCH)/dev
SNMPNOLIB=snmp
ifeq ($(ARCH), WINNT)
PEERLIBOBJ=$(PEERLIB)/mgmt.lib
else
PEERLIBOBJ=$(PEERLIB)/mgmt.o
endif
else
SNMP=
SNMP_INCLUDE=.
PEERLIB=
PEERLIBOBJ=
SNMPNOLIB=
endif

# ------------------------ The actual build rules ------------------------
include $(BUILD_ROOT)/nsperl.mk

RELTOOLSPATH = $(ABS_ROOT_PARENT)/reltools
FTP_PULL = $(PERL) $(RELTOOLSPATH)/ftp_puller_new.pl -logdir $(ABS_ROOT_PARENT) -trimlog

# make sure ftp puller exists
$(RELTOOLSPATH)/ftp_puller_new.pl:
	cd $(ABS_ROOT_PARENT) ; cvs co RelToolsLite

# Define preferred pull method for the platform.
# Can be overridden for the entire build, and also for each component
# each component has an XXX_PULL_METHOD macro that defaults to the pull
# method listed below; see components.mk
ifndef COMPONENT_PULL_METHOD
ifeq ($(ARCH), WINNT)
COMPONENT_PULL_METHOD = FTP
else
COMPONENT_PULL_METHOD = SYMLINK
endif
endif

# platforms without full rtl debugging versions of libraries i.e. not NT
FULL_RTL_OBJDIR = $(NSOBJDIR_NAME)
NSCP_DISTDIR_FULL_RTL = $(NSCP_DISTDIR)
NSCP_ABS_DISTDIR_FULL_RTL = $(ABS_ROOT_PARENT)/dist/$(FULL_RTL_OBJDIR)

FULL_RTL_OBJDIR_32 = $(NSOBJDIR_NAME_32)
NSCP_DISTDIR_FULL_RTL_32 = $(BUILD_ROOT)/../dist/$(NSOBJDIR_NAME_32)
NSCP_ABS_DISTDIR_FULL_RTL_32 = $(ABS_ROOT_PARENT)/dist/$(FULL_RTL_OBJDIR_32)

# these components may have additional RTL debugging support built in on NT
# adminutil, dbm, ldapsdk, NLS, NSPR, NSS (security)
# we cannot simply redefine NSOBJDIR_NAME and NSCP_DISTDIR because other
# components do not have this RTL support stuff and the .OBJD directory
# does not exist
ifeq ($(ARCH), WINNT)
  ifeq ($(DEBUG), fulld)
    FULL_RTL_OBJDIR=$(NSOBJDIR_NAME)D
	NSCP_DISTDIR_FULL_RTL = $(BUILD_ROOT)/../dist/$(FULL_RTL_OBJDIR)
	NSCP_ABS_DISTDIR_FULL_RTL = $(BUILD_ROOT)/../dist/$(FULL_RTL_OBJDIR)
  endif
endif

$(NSCP_DISTDIR_FULL_RTL) $(CLASS_DEST): $(RELTOOLSPATH)/ftp_puller_new.pl
	mkdir -p $@

include $(BUILD_ROOT)/components.mk

# these two macros are to fool the crazy NSPR nsinstall and fasttime
# stuff into putting the objects and binaries in this location
ABS_OBJDIR=$(ABS_ROOT)/built/$(NS_BUILD_FLAVOR)
NSDEFS += DIST=$(NSPR_ABS_BUILD_DIR) OBJDIR=$(ABS_OBJDIR) \
	FASTTIME_HEADER_DEST=$(ABS_OBJDIR)/include \
	FASTTIME_TARGET_DEST=$(ABS_OBJDIR)
# NT uses nsinstall.exe from the path
ifneq ($(ARCH), WINNT)
	NSDEFS += NSINSTALL=$(ABS_OBJDIR)/nsinstall
endif

##### FTP over AIX and HPUX since they are flakey over NFS ######
#################################################################
ifeq ($(ARCH), AIX)
  FTP_UNIX=YES
endif
ifeq ($(ARCH), HPUX)
  FTP_UNIX=YES
endif
################################################################

#############################################################################
# Pull the DS4.1 MCC package to get nsslapd.dll/.lib/.so for the DS ResKit
# ONLY. Do not USE these macros for anything else 
#############################################################################
MCCBINARYDATE=19990621$(SEC_SUFFIX)
FULLDSBINARY=$(COMPONENTS_DIR)/mcc41/$(MCCBINARYDATE)/$(NSOBJDIR_NAME)

RM=rm
DEPEND=makedepend

ifeq ($(ARCH), IRIX)
STRIP=strip -x
else
STRIP=strip
endif # IRIX

# XXXrobm The Sun MD stuff #includes stuff in the nspr dir without a prefix
# Otherwise the second NSCP_DISTDIR/include/nspr would not be necessary
MCC_INCLUDE=-I$(BUILD_ROOT)/include \
            -I$(BUILD_ROOT)/include \
            $(NSPR_INCLUDE) $(DBM_INCLUDE) $(SECURITY_INCLUDE) \
            $(SVRCORE_INCLUDE) -I$(LDAP_INCLUDE) -I$(SASL_INCLUDE)

ifeq ($(ARCH), WINNT)
XP_FLAG=-DXP_WIN32  -DXP_WIN -D_WINDOWS -DXP_PC -DXP_WINNT
else
XP_FLAG=-DXP_UNIX
endif
# CFLAGS_NO_ARCH is needed temporarily by NS_MAIL
CFLAGS_NO_ARCH=$(NODE_FLAG) $(BOMB) $(MODULE_CFLAGS) $(XP_FLAG) \
       -D$(ARCH) $(MCC_DEBUG) $(MCC_SERVER)
CFLAGS=$(ARCH_CFLAGS) $(CFLAGS_NO_ARCH)
ifeq ($(BUILD_DLL), yes)
CFLAGS += -DBUILD_DLL
endif
LFLAGS=$(ML_DEBUG)

ifeq ($(BSCINFO), yes)
CBSCFLAGS=-FR$(OBJDEST)/$*.sbr
endif

include $(BUILD_ROOT)/nscore.mk

# These are the defaults, individual Makefiles can override them as necessary
FVERSION=$(MAJOR_VERSION).$(MINOR_VERSION)
FVERSION_FLAGS=-v$(FVERSION)

ifeq ($(ARCH), WINNT)
$(OBJDEST)/%.res: %.rc 
	$(RC) -I$(DIRVERDIR) -Fo$@ $*.rc
endif

ifdef USE_LINT
LINT = lint
LINTCFLAGS = -errchk=%all -u -F -errtags -errhdr -errfmt=simple -Nlevel=4 -Xarch=v9 -erroff=E_ASGN_NEVER_USED
#no lint for c++ code on Solaris
#LINTCCFLAGS = $(LINTCFLAGS)
endif

#ifndef NOSTDCOMPILE
$(OBJDEST)/%.o: %.cc
ifeq ($(ARCH), WINNT)
	$(CCP) -c $(CCFLAGS) $(CFLAGS) $(MCC_INCLUDE) $< -Fo$(OBJDEST)/$*.o
else
	$(CCC) -c $(CCFLAGS) $(CFLAGS) $(MCC_INCLUDE) $< -o $(OBJDEST)/$*.o
endif
#endif

ifndef NOSTDCOMPILE
$(OBJDEST)/%.o: %.cpp
ifeq ($(ARCH), WINNT)
	$(CCP) -c $(CCFLAGS) $(CFLAGS) $(MCC_INCLUDE) $< -Fo$(OBJDEST)/$*.o
else
	$(CCC) -c $(CCFLAGS) $(CFLAGS) $(MCC_INCLUDE) $< -o $(OBJDEST)/$*.o
endif
endif

ifndef NOSTDCOMPILE
$(OBJDEST)/%.o: %.c
ifeq ($(ARCH), WINNT)
ifeq ($(BOUNDS_CHECKER), yes)
	bcompile -c -Zop $(BUILD_ROOT)/bchecker.ini -nologo $(RTFLAG) -W3 -GT -GX -DWIN32 \
	    -D_WINDOWS $(CFLAGS) $(MCC_INCLUDE) $< -Fo$(OBJDEST)/$*.o
else
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< -Fo$(OBJDEST)/$*.o $(CBSCFLAGS)
endif
else
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< -o $(OBJDEST)/$*.o
endif
ifdef USE_LINT
	$(LINT) $(LINTCFLAGS) $(subst -KPIC,,$(CFLAGS)) $(MCC_INCLUDE) $< > $(OBJDEST)/$*.ln 2>&1
#	$(LINT) $(LINTCFLAGS) $(MODULE_CFLAGS) $(DEFS) $(MCC_SERVER) $(INCLUDES) $(MCC_INCLUDE) $< > $(OBJDEST)/$*.ln 2>&1
endif
endif

ifeq ($(ARCH), WINNT)
AR=lib /nologo /out:"$@"
ifdef HEAPAGENT
LDAP_DONT_USE_SMARTHEAP=true
else
PDBSWITCH=/PDB:NONE
endif
LINK_DLL	= link /nologo $(USE_MAP) /DLL $(PDBSWITCH) $(ML_DEBUG) /SUBSYSTEM:WINDOWS $(LLFLAGS) $(DLL_LDFLAGS) /out:"$@"
LINK_EXE	= link -OUT:"$@" $(USE_MAP)  $(ARCH_LINK_DEBUG) $(LCFLAGS) /NOLOGO $(PDBSWITCH) /INCREMENTAL:NO \
                  /SUBSYSTEM:windows $(OBJS) $(DEPLIBS) $(EXTRA_LIBS)
BSCMAKE = bscmake.exe /nologo /o $@
endif # WINNT

ifndef NOSTDCLEAN
clean:
	$(RM) -f .depends $(LIBS) $(OBJDEST)/*.o *_pure_* $(BINS) $(PUREFILES)
endif

ifndef NOSTDDEPEND
ifeq ($(ARCH), WINNT)
INCLUDE_DEPENDS = $(NULL)
depend:
	echo making depends
else
INCLUDE_DEPENDS = .depends
.depends:
	touch .depends

depend:
	$(DEPEND) -f .depends -- $(MCC_INCLUDE) $(CFLAGS) *.c *.cpp
endif
endif

ifndef NOSTDSTRIP
strip:
	$(STRIP) $(BINS)
endif


# Whoa. Hack around the fact that we're using CPP for something it
# wasn't designed to do.
#
# Note: I copied and pasted the HTML def to the section below.  Please
#       make changes in BOTH places.  MLM

ifeq ($(ARCH), WINNT)
WIN_HTMLDEFS= /D "XP_WIN32" /D"WIN32" $(MCC_SERVER)

$(HTMLDEST)/%.html: %.h
	cl /nologo /P /EP -I. $(WIN_HTMLDEFS) -Fo$@  $*.h
	cp $*.i $@
	rm $*.i
else
$(HTMLDEST)/%.html: %.h
	@echo "$< -> $(HTMLDEST)/$*.html..."
	@sed -e s/\'/::NETSCAPE_QUOTE_CHAR::/g \
            -e s/\"/::NETSCAPE_DBL_QUOTE::/g \
            -e 's^/\*^::NETSCAPE_SLASHSTAR::^g' \
            -e 's^\*/^::NETSCAPE_STARSLASH::^g' \
            -e 's^//^::NETSCAPE_DBLSLASH::^g' \
            -e 's^\.^::NETSCAPE_PERIOD::^g' \
            -e 's/^# /::NETSCAPE_HASH_BEGIN::/g' $< > /tmp/$*.HTEMP.c
	@$(CPPCMD) -I. $(MCC_SERVER) -DXP_UNIX $(HTMLDEFS) /tmp/$*.HTEMP.c | \
            sed -e s/::NETSCAPE_QUOTE_CHAR::/\'/g \
                -e s/::NETSCAPE_DBL_QUOTE::/\"/g \
                -e 's^::NETSCAPE_SLASHSTAR::^/\*^g' \
                -e 's^::NETSCAPE_DBLSLASH::^//^g' \
                -e 's^::NETSCAPE_PERIOD::^.^g' \
                -e 's^::NETSCAPE_STARSLASH::^\*/^g' | \
            egrep -v '^# .*' | grep -v '#ident ' | \
            sed -e 's/::NETSCAPE_HASH_BEGIN::/# /g' > $(HTMLDEST)/$*.html
	@rm /tmp/$*.HTEMP.c
endif

#
# Compile in TNF_PROBE_*_DEBUG() macros by doing eg: gmake BUILD_DEBUG=optimize TNF_DEBUG=1
# See aclplugin for TNF_PROBE_0_DEBUG() egs.
# After do:
# prex ns-slapd -z -D <instance-dir>
# enable $all
# trace $all
# continue
# <ctrl-c>
#
# and do a tnfdump /tmp/trace-<ns-slapd-PID> to see tnf logs.
# 

ifeq ($(ARCH), SOLARIS)
# richm 20050309 - only use mtmalloc on Solaris from now on
LDAP_DONT_USE_SMARTHEAP=1
ifdef TNF_DEBUG
CFLAGS += -DTNF_DEBUG
endif
endif

# Do the same for index.lst...
ifeq ($(ARCH), WINNT)
L_WIN_HTMLDEFS= /D "XP_WIN32" /D"WIN32" $(MCC_SERVER)

$(HTMLDEST)/%.lst: %.lst
	cl /nologo /P /EP $(L_WIN_HTMLDEFS) -Fo$@  $*.lst
	cp $*.i $@
	rm $*.i
else
$(HTMLDEST)/%.lst: %.lst
	@echo "$< -> $(HTMLDEST)/$*.lst..."
	@sed -e s/\'/::NETSCAPE_QUOTE_CHAR::/g \
            -e s/\"/::NETSCAPE_DBL_QUOTE::/g \
            -e 's^/\*^::NETSCAPE_SLASHSTAR::^g' \
            -e 's^\*/^::NETSCAPE_STARSLASH::^g' \
            -e 's^//^::NETSCAPE_DBLSLASH::^g' \
            -e 's/^# /::NETSCAPE_HASH_BEGIN::/g' $< > /tmp/$*.HTEMP.c
	@$(CPPCMD) -I. $(MCC_SERVER) -DXP_UNIX $(HTMLDEFS) /tmp/$*.HTEMP.c | \
            sed -e s/::NETSCAPE_QUOTE_CHAR::/\'/g \
                -e s/::NETSCAPE_DBL_QUOTE::/\"/g \
                -e 's^::NETSCAPE_SLASHSTAR::^/\*^g' \
                -e 's^::NETSCAPE_DBLSLASH::^//^g' \
                -e 's^::NETSCAPE_STARSLASH::^\*/^g' | egrep -v '^# .*' | \
            sed -e 's/::NETSCAPE_HASH_BEGIN::/# /g' > $(HTMLDEST)/$*.lst
	@rm /tmp/$*.HTEMP.c
endif

# ------------------------- Finally, the modules -------------------------
$(BUILD_ROOT)/modules.mk: $(BUILD_ROOT)/modules.awk
	@echo re-making $(BUILD_ROOT)/modules.mk ...
	@cd $(BUILD_ROOT); sh modules.sh

include $(BUILD_ROOT)/modules.mk
