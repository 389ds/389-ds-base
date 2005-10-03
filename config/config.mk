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
#! gmake

include $(DEPTH)/config/common.mn

#
# Important internal static macros
#
OS_ARCH		:= $(subst /,_,$(shell uname -s))
OS_TEST		:= $(shell uname -m)

# Force the IRIX64 machines to use IRIX.
ifeq ($(OS_ARCH),IRIX64)
OS_ARCH		:= IRIX
endif

# Attempt to differentiate between SunOS 5.4 and x86 5.4
ifeq ($(OS_TEST),i86pc)
OS_RELEASE	:= $(shell uname -r)_$(OS_TEST)
else
OS_RELEASE	:= $(shell uname -r)
endif

ifeq ($(OS_ARCH),AIX)
OS_RELEASE	:= $(shell uname -v).$(shell uname -r)
endif

# SINIX changes name to ReliantUNIX with 5.43
ifeq ($(OS_ARCH),ReliantUNIX-N)
OS_ARCH		:= ReliantUNIX
OS_RELEASE	:= 5.4
endif
ifeq ($(OS_ARCH),SINIX-N)
OS_ARCH		:= ReliantUNIX
OS_RELEASE	:= 5.4
endif

# SVR5 (UnixWare[7])
ifeq ($(OS_ARCH),UnixWare)
# For now get the OS release for backward compatibility (UnixWare5)
OS_RELEASE    := $(shell uname -r)
endif

# Catch NCR butchering of SVR4
ifeq ($(OS_ARCH),UNIX_SV)
ifneq ($(findstring NCR, $(shell grep NCR /etc/bcheckrc | head -1 )),)
OS_ARCH		:= NCR
OS_RELEASE	:= $(shell uname -v)
else # !NCR
# Make UnixWare something human readable
OS_ARCH               := UNIXWARE
# Check for UW2 using UDK, which looks like a Gemini (UnixWare[3,5,7]) build
OS_RELEASE    := $(shell $(DEPTH)/netsite/nsarch -f | sed 's/UnixWare //')
ifeq ($(OS_RELEASE),5)
OS_ARCH               := UnixWare
else # OS_RELEASE = 5
# Get the OS release number, not 4.2
OS_RELEASE    := $(shell uname -v)
ifeq ($(OS_RELEASE),2.1.2)
OS_RELEASE    := 2.1
endif # OS_RELEASE = 2.1.2
endif # OS_RELEASE = 5
endif # !NCR
endif # UNIX_SV


ifeq ($(OS_ARCH),Linux)
#
# Handle FreeBSD 2.2-STABLE and Linux 2.0.30-osfmach3 and 2.2.14-5.0smp
#
ifeq (,$(filter-out Linux FreeBSD,$(NSOS_ARCH)))
 OS_RELEASE  := $(shell echo $(OS_RELEASE) | sed 's/-.*//')
endif
# If the release returned by uname has _4_ components, the original
# logic here broke. The following lines detect this and add a second
# 'basename' to fixup the version such that everything still works.
OS_RELEASE_TEMP := $(subst ., ,$(OS_RELEASE))
OS_RELEASE_COUNT := $(words $(OS_RELEASE_TEMP))
ifeq ($(OS_RELEASE_COUNT), 4)
    OS_RELEASE := $(basename $(OS_RELEASE))
endif
OS_RELEASE := $(basename $(OS_RELEASE))
  ifeq (86,$(findstring 86,$(OS_TEST)))
    CPU_TAG = _x86
  else
    CPU_TAG = _$(OS_TEST)
  endif
  ifeq ($(USE_LIBC),1)
    LIBC_TAG = _libc
  else
    LIBC_TAG = _glibc
  endif
# always use pthreads
  USE_PTHREADS = 1
  ifeq ($(USE_PTHREADS),1)
    IMPL_STRATEGY = _PTH
  endif
  ifeq ($(USE_EGCS),1)
    COMPILER_TAG = _egcs
  endif
endif

# Clean up SCO
ifeq ($(OS_ARCH),SCO_SV)
OS_ARCH         := SCOOS
ifeq (5.0,$(findstring 5.0,$(shell ls /var/opt/K/SCO/Unix)))
OS_RELEASE      := 5.0
else
OS_RELEASE      := UNKNOWN
endif
endif

# Furnish the extra libraries for using ld on OSF1 
ifeq ($(OS_ARCH),OSF1)
LDEXTRA		:= -lcxx -lexc -lc
#
# Distinguish between OSF1 V4.0B and V4.0D
#
ifeq ($(OS_RELEASE),V4.0)
	OS_VERSION := $(shell uname -v)
	ifeq ($(OS_VERSION),564)
		OS_RELEASE := V4.0B
	endif
	ifeq ($(OS_VERSION),878)
		OS_RELEASE := V4.0D
	endif
endif
else
LDEXTRA		:=
endif

# Relative pathname from top-of-tree to current source directory
ifneq ($(OS_ARCH),WINNT)
REVDEPTH	:= $(DEPTH)/config/revdepth
SRCDIR		:= $(shell perl $(REVDEPTH).pl $(DEPTH))
endif

# define an include-at-most-once flag
NS_CONFIG_MK	= 1

#
# Default command macros; can be overridden in <arch>.mk.
#
AS		= $(CC)
ASFLAGS		= $(CFLAGS)
CCF		= $(CC) $(CFLAGS)
PURIFY		= purify $(PURIFYOPTIONS)
LINK_EXE	= $(LINK) $(OS_LFLAGS) $(LFLAGS)
LINK_DLL	= $(LINK) $(OS_DLLFLAGS) $(DLLFLAGS)
NFSPWD		= $(DEPTH)/config/nfspwd

ifeq ($(OS_ARCH),WINNT)
RC		= rc.exe
XP_DEFINE	= -DXP_PC
LIB_SUFFIX	= lib
DLL_SUFFIX	= dll
AR		= lib -NOLOGO -OUT:"$@"
DLLFLAGS	= $(XLFLAGS) -OUT:"$@"
LFLAGS		= $(OBJS) $(DEPLIBS) $(EXTRA_LIBS) -OUT:"$@"
NSINSTALL	= nsinstall
INSTALL		= $(NSINSTALL)
else
include $(DEPTH)/config/UNIX.mk
endif

ifdef BUILD_OPT
ifeq ($(OS_ARCH),WINNT)
OPTIMIZER	= -O2
XCFLAGS		= $(LCFLAGS)
XLFLAGS		= $(LLFLAGS)
else
OPTIMIZER	= -O
JAVA_OPTIMIZER	= -O
DEFINES		= -UDEBUG -DNDEBUG -DTRIMMED
endif
OBJDIR_TAG	= _OPT
else
ifeq ($(OS_ARCH),WINNT)
OPTIMIZER	= -Od -Z7
LDFLAGS		= -DEBUG

XCFLAGS		= $(LCFLAGS)
XLFLAGS		= -DEBUG $(LLFLAGS)
XBCFLAGS	= -FR$*
JAVA_OPTIMIZER	= -Od -Z7
else
ifeq ($(ARCH), ReliantUNIX)
OPTIMIZER	= -gdwarf
JAVA_OPTIMIZER	= -gdwarf
else
OPTIMIZER	= -g
JAVA_OPTIMIZER	= -g
endif
ifeq ($(OS_ARCH),OSF1)
DEFINES		= -DDEBUG_$(shell whoami) -DTRACING
else
DEFINES		= -DDEBUG -UNDEBUG -DDEBUG_$(shell whoami) -DTRACING
endif
endif
OBJDIR_TAG	= _DBG
endif

LIBNT		= $(DIST)/lib/libnt.$(LIB_SUFFIX)
LIBAWT		= $(DIST)/lib/libawt.$(LIB_SUFFIX)
LIBMMEDIA	= $(DIST)/lib/libmmedia.$(LIB_SUFFIX)
LIBNSPR		= $(DIST)/lib/libnspr.$(LIB_SUFFIX)
PURELIBNSPR	= $(DIST)/lib/libpurenspr.$(LIB_SUFFIX)

ifeq ($(OS_ARCH),WINNT)
LIBNSJAVA	= $(DIST)/lib/jrt3221.$(LIB_SUFFIX)
else
LIBNSJAVA	= $(DIST)/lib/nsjava32.$(LIB_SUFFIX)
endif


# XXX For now, we're including $(DEPTH)/include directly instead of 
# getting this stuff from dist. This stuff is old and will eventually 
# be put in the library directories where it belongs so that it can 
# get exported to dist properly.
INCLUDES	= $(LOCAL_PREINCLUDES) -I$(DEPTH)/include $(LOCAL_INCLUDES)

CFLAGS		= $(XP_DEFINE) $(OPTIMIZER) $(OS_CFLAGS) $(DEFINES) $(INCLUDES) $(XCFLAGS)
# For purify
NOMD_CFLAGS	= $(XP_DEFINE) $(OPTIMIZER) $(NOMD_OS_CFLAGS) $(DEFINES) $(INCLUDES) $(XCFLAGS)

#
# To build on SunOS5.8 when some SunOS5.8 components or config
# files are missing, just pretend you're on SunOS5.6 by setting
#   NSOS_RELEASE_OVERRIDE=5.6
#
ifdef NSOS_RELEASE_OVERRIDE
OS_RELEASE := $(NSOS_RELEASE_OVERRIDE)
endif


include $(DEPTH)/config/$(OS_ARCH)$(OS_RELEASE).mk

OS_CONFIG	:= $(OS_ARCH)$(OS_RELEASE)

include $(DEPTH)/config/$(OS_CONFIG).mk

# now take care of default GCC (rus@5/5/97)

ifdef NS_USE_GCC
# if gcc-settings are redefined already - don't touch it
# 
ifeq (,$(findstring gcc, $(CC)))
CC	= gcc
CCC	= g++
CXX	= g++
# always use -fpic - some makefiles are still broken and don't distinguish
# situation when they build shared and static libraries
CFLAGS	+= -fpic -Wall -DNS_USE_GCC $(GCC_FLAGS_EXTRA)
OS_LIBS += -L/usr/local/lib -lstdc++ -lg++ -lgcc
endif
endif
###

# Name of the binary code directories
ifeq ($(OS_ARCH),WINNT)
ifneq ($(PROCESSOR_ARCHITECTURE),x86)
OBJDIR_NAME	= $(OS_CONFIG)$(PROCESSOR_ARCHITECTURE)$(OBJDIR_TAG).OBJ
else
OBJDIR_NAME	= $(OS_CONFIG)$(OBJDIR_TAG).OBJ
endif
else   # WINNT
ifeq ($(OS_ARCH),Linux)
OBJDIR_NAME     = $(OS_CONFIG)$(CPU_TAG)$(COMPILER_TAG)$(LIBC_TAG)$(IMPL_STRATEGY)$(OBJDIR_TAG).OBJ
else
ifeq ($(OS_ARCH), IRIX)
 ifeq ($(USE_PTHREADS), 1)
  ifeq ($(USE_N32), 1)
   OBJDIR_NAME   = $(OS_CONFIG)_n32_PTH$(OBJDIR_TAG).OBJ
  else
   OBJDIR_NAME   = $(OS_CONFIG)_PTH$(OBJDIR_TAG).OBJ
  endif
 else
  OBJDIR_NAME   = $(OS_CONFIG)$(OBJDIR_TAG).OBJ
 endif
else
 OBJDIR_NAME   = $(OS_CONFIG)$(OBJDIR_TAG).OBJ
endif	# IRIX
endif   # Linux
endif   # WINNT

# Figure out where the binary code lives. It either lives in the src
# tree (NSBUILDROOT is undefined) or somewhere else.
ifdef NSBUILDROOT
BUILD		= $(NSBUILDROOT)/$(OBJDIR_NAME)/build
OBJDIR		= $(BUILD)/$(SRCDIR)
XPDIST		= $(NSBUILDROOT)
DIST		= $(NSBUILDROOT)/$(OBJDIR_NAME)/dist
else
BUILD		= $(OBJDIR_NAME)
OBJDIR		= $(OBJDIR_NAME)
XPDIST		= $(DEPTH)/dist
DIST		= $(DEPTH)/dist/$(OBJDIR_NAME)
endif

# all public include files go in subdirectories of PUBLIC:
PUBLIC		= $(XPDIST)/public

VPATH		= $(OBJDIR)
DEPENDENCIES	= $(OBJDIR)/.md

# Personal makefile customizations go in these optional make include files.
MY_CONFIG	= $(DEPTH)/config/myconfig.mk
MY_RULES	= $(DEPTH)/config/myrules.mk

-include $(MY_CONFIG)

######################################################################

# Specify that we are building a client.
# This will instruct the cross platform libraries to
# include all the client specific cruft.
ifndef SERVER_BUILD
ifndef LIVEWIRE
DEFINES += -DMOZILLA_CLIENT -DNETSCAPE
endif
else
DEFINES += -DSERVER_BUILD
endif
DEFINES += -DNETSCAPE

# Now test variables that might have been set or overridden by $(MY_CONFIG).

# if ((BUILD_EDITOR || BUILD_EDT) && !NO_EDITOR) -> -DEDITOR is defined
ifndef NO_EDITOR
ifdef BUILD_EDITOR
OBJDIR_TAG	:= $(OBJDIR_TAG)_EDT
DEFINES		+= -DEDITOR -DGOLD
# This is the product classification not the feature classification.
# It effects things like where are the release notes, etc..
BUILD_GOLD = yea
else
# We ought to get rid of this now that BUILD_EDITOR has replaced it.
ifdef BUILD_EDT
OBJDIR_TAG	:= $(OBJDIR_TAG)_EDT
DEFINES		+= -DEDITOR -DGOLD
BUILD_EDITOR	= yea
endif 
endif 
endif

# Build layers by default
ifndef NO_LAYERS
DEFINES		+= -DLAYERS
endif

# if (BUILD_EDITOR_UI && !NO_EDITOR_UI) -> -DEDITOR_UI is defined
ifdef BUILD_EDITOR_UI
ifndef NO_EDITOR_UI
DEFINES		+= -DEDITOR_UI
endif
endif

ifdef BUILD_DEBUG_GC
DEFINES		+= -DDEBUG_GC
endif

ifdef BUILD_UNIX_PLUGINS
# UNIX_EMBED Should not be needed. For now these two defines go
# together until I talk with jg.  --dp
DEFINES         += -DUNIX_EMBED -DX_PLUGINS
endif

ifndef NO_UNIX_LDAP
DEFINES 	+= -DUNIX_LDAP
endif

#
# Platform dependent switching off of NSPR and JAVA
#
ifndef NO_NSPR
DEFINES		+= -DNSPR -DNSPR20
endif

ifndef NO_JAVA
DEFINES		+= -DJAVA
endif

ifeq ($(LW_JAVA), 1)
DEFINES += -DJAVA
endif

######################################################################

GARBAGE		= $(DEPENDENCIES) core

ifneq ($(OS_ARCH),WINNT)
NSINSTALL	= $(DEPTH)/config/$(OBJDIR_NAME)/nsinstall

ifeq ($(NSDISTMODE),copy)
# copy files, but preserve source mtime
INSTALL		= $(NSINSTALL) -t
else
ifeq ($(NSDISTMODE),absolute_symlink)
# install using absolute symbolic links
INSTALL		= $(NSINSTALL) -L `$(NFSPWD)`
else
# install using relative symbolic links
INSTALL		= $(NSINSTALL) -R
endif
endif
endif

ifndef PLATFORM_HOSTS
PLATFORM_HOSTS	=		\
		  atm		\
		  bsdi		\
		  diva		\
		  gunwale	\
		  openwound	\
		  server2	\
		  server3	\
		  server9	\
		  zot		\
		  $(NULL)
endif

######################################################################

# always copy files for the sdk
SDKINSTALL	= $(NSINSTALL) -t

ifndef SDK
SDK		= $(DEPTH)/dist/sdk
endif

######################################################################
### Java Stuff
######################################################################
## java interpreter

JAVA_PROG = java	# from the ether

# Let user over-ride CLASSPATH from environment
#ifdef xCLASSPATH	# bad idea
#JAVA_CLASSPATH = $(CLASSPATH)
#else
# keep sun-java/classsrc until bootstrapped
#JAVA_CLASSPATH = $(XPDIST)/classes:$(DEPTH)/sun-java/classsrc
#endif

#JAVA_FLAGS = -classpath $(JAVA_CLASSPATH) -ms8m
#JAVA = $(JAVA_PROG) $(JAVA_FLAGS) 

######################################################################
## java compiler
# XXX - ram included from common.mn
#JAVAC_PROG = javac	# from the ether
#JAVAC_FLAGS = -classpath $(JAVAC_CLASSPATH) $(JAVA_OPTIMIZER)
#JAVAC = $(JAVAC_PROG) $(JAVAC_FLAGS)

PATH_SEPARATOR	= :

#
# The canonical classpath for building java libraries
# includes these two entries first, then any additional zips
# or directories
#
# see "JAVAC_CLASSPATH" in common.mn
#

# where the bytecode will go
JAVA_DESTPATH	= $(XPDIST)/classes
# where the sources for the module you are compiling are
# default is sun-java/classsrc, override for other modules
JAVA_SOURCEPATH	= $(DEPTH)/sun-java/classsrc

######################################################################
## javadoc

# Rules to build java .html files from java source files

JAVADOC_PROG = $(JAVA) sun.tools.javadoc.Main
JAVADOC_FLAGS = -classpath $(JAVAC_CLASSPATH)
JAVADOC = $(JAVADOC_PROG) $(JAVADOC_FLAGS)

######################################################################
## zip

ZIP_PROG = zip
ZIP_FLAGS = -0rq
ZIP = $(ZIP_PROG) $(ZIP_FLAGS)

######################################################################
JRTDLL		= libjrt.$(DLL_SUFFIX)
MMDLL		= libmm32$(VERSION_NUMBER).$(DLL_SUFFIX)
AWTDLL		= libawt.$(DLL_SUFFIX)
JITDLL		= libjit

