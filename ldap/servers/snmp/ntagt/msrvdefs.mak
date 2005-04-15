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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#############################################################################
#                       Mail Server Common Rulesets                         #
#############################################################################

MSRV_RELEASE    =   4.0

ifeq ($(DEBUG), optimize)
MSRV_DEBUG  =   no
else
MSRV_DEBUG  =   yes
endif

ifeq ($(MSRV_DEBUG), yes)
DEBUG_DEST  =   DBG-
else
DEBUG_DEST  =
endif

# this allows mixing DBG objects with non DBG libs, avoiding
# a complete autobuild or other linking hacks. Any DBG libs 
# (under mailserv2) will get picked up first, followed by non-DBG
# libs. In any event, no DBG libs will be linked if MSRV_DEBUG=no.
NDBGDEST    =   $(MSRV_ROOT)/built/$(ARCH)

# This is where we are generally supposed to put built stuff 
BLDDEST     =   $(MSRV_ROOT)/built/$(DEBUG_DEST)$(ARCH)

# module specific locations for build stuff
LIBDEST     =   $(BLDDEST)/lib
NETDEST     =   $(BLDDEST)/network
LOCDEST     =   $(BLDDEST)/local
EXTDEST     =   $(BLDDEST)/extras
BINDEST     =   $(BLDDEST)/bin
OBJDEST     =   $(BLDDEST)/obj

IMPORTS_DIR =   $(MSRV_ROOT)/code/import


# Extensions for generated files.

SHLIB_SUFFIX    =       so
ARCHIVE_SUFFIX  =       a
OBJ_SUFFIX      =       o
EXE_SUFFIX      =

export SHLIB_EXT ARCHIVE_EXT OBJ_EXT EXE_EXT

RM      =   rm -f
AR      =   ar cr
MD      =   mkdir -p
MAKE    =   gmake
STRIP   =   strip
CP      =   cp
ECHO    =   echo

# For reasons you really don't want to know, we put all the C++ core 
# modules into a static lib on Unix platforms. If the system supports
# shared libs, we use it only for C-derived object modules.

ifneq ($(ARCH), WINNT)
STAT_LIB    =   libNSmObj.$(ARCHIVE_SUFFIX)
endif

ifeq ($(ARCH), WINNT)

ifeq ($(DEBUG), full)
ML_DEBUG    =   /DEBUG
ifeq ($(NTDEBUGENV),ON)
MC_DEBUGENV= /D__NTDebugEnv__
else
MC_DEBUGENV= 
endif
ifeq ($(NTDEBUGSLEEP),ON)
MC_DEBUG=/D__NTDebug__ /D__NTDebugSleep__ $(MC_DEBUGENV)
else
MC_DEBUG    =   /D__NTDebug__ $(MC_DEBUGENV)
endif
else
ML_DEBUG    =
MC_DEBUG    =
endif

endif
### Solaris #############################################################

ifeq ($(ARCH), SOLARIS)
	BUILD_SHARED    =   TRUE
	BUILDAPI_SHARED =   FALSE
	PLUGIN_SHARED   =   TRUE
	CC		=   gcc
	CXX		=   g++

ifeq ($(PURIFY), yes)
	CC      =   /stuff/iasrc3/xtern/purify/purify gcc
	CXX     =   /stuff/iasrc3/xtern/purify/purify g++
endif

ifeq ($(QUANTIFY), yes)
	CC      =   /stuff/iasrc3/xtern/quantify/quantify gcc
	CXX     =   /stuff/iasrc3/xtern/quantify/quantify g++
endif

	LD		=   ld
	DLL_CFLAGS	=   -fpic
	DLL_CXXFLAGS    =   -fpic
	DLL_LDFLAGS	=   -G
	OPTIMIZE_FLAGS  =   -O2
	CFLAGS		=    $(DLL_CFLAGS) -DSVR4 -D__svr4__ -DSOLARIS \
				-DHAVE_NIS -DHAS_GETSPNAM -DHAS_FGETPWENT \
				-DXP_UNIX -DSTATOBJS \
				-DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				-DOSVERSION=$(OSVERSION) -D_REENTRANT
	LD_EXTRAS	=   -L/tools/ns/lib -lthread -lsocket -lnsl -lgen -ldl
	MADM_LDLIBS	=   -ldl
endif

### IRIX  notes ##########################################################
## Suppress warnings about statement unreachable (3203) and 
## unused parameters (3262). Increase Olimit
## Suppress REALLY ANNOYING link warnings unless a debug build
## 84 is unsed libs. 85 is duplicate symbol preemption. 
## This hides duplicate symbols in the link - so watch it
##########################################################################
ifeq ($(ARCH), IRIX)
	BUILD_SHARED    =   TRUE
	BUILDAPI_SHARED =       FALSE
	PLUGIN_SHARED   =   TRUE
	CC      =   cc
ifneq ($(MSRV_DEBUG),yes)
	CXX     =   CC -woff 3203,3262 -Wl,-woff,84 -Wl,-woff,85
else
	CXX	=   CC
endif
	LD      =   CC
	DLL_CFLAGS  =
	DLL_CXXFLAGS    =
	DLL_LDFLAGS =   -shared
	OPTIMIZE_FLAGS  =   -O -Olimit 4096
	CFLAGS      =    $(DLL_CFLAGS) -DIRIX -DSTATOBJS \
				-DHAVE_NIS -DHAS_GETSPNAM -DHAS_FGETPWENT \
				-DXP_UNIX \
				-DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				-DOSVERSION=$(OSVERSION)

	LD_EXTRAS   =   
endif

### HPUX notes ###########################################################
## g++ does not support pic on a300 architecture
## -Wl,+b,/usr/lib,+s is needed so that runtime can find shared lib 
## correctly
## Also, gcc is used to compile and link modules with c-based main
## to overcome a global constructor problem. Change in
## admin/src/Makefile and code/network/IMAP4-Server/unix/Makefile
## and code/tools/Makefile
#########################################################################

ifeq ($(ARCH), HPUX)
	SHLIB_SUFFIX    =       sl
	BUILD_SHARED    =   TRUE
	BUILDAPI_SHARED =       FALSE
	PLUGIN_SHARED   =   TRUE
	CC      =   cc
	CXX     =   g++
	LD      =   ld
	NATIVE_CFLAGS   =   -Aa -D_HPUX_SOURCE
	DLL_CFLAGS  =   +z
	DLL_CXXFLAGS    =
	DLL_LDFLAGS =   -b
	OPTIMIZE_FLAGS  =   -O
	CFLAGS      =   -DHPUX -DSTATOBJS -DHAVE_NIS \
				-DXP_UNIX -DHAS_FGETPWENT \
				-DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				-DOSVERSION=$(OSVERSION)
	LD_EXTRAS   =   -Wl,+b,/usr/lib,+s \
				-L/tools/ns/lib -liostream -ldld -lm 
				#-L/tools/ns/lib -liostream -ldld -lm -lpthread
	MSRV_INCLUDES   =   -I/tools/ns/lib/g++-include 
endif


### OSF1 notes ############################################################
## -taso options and -DIS_64 are needed to get 32 bit behavior on the alpha
## ...otherwise libdbm will not work
###########################################################################

ifeq ($(ARCH), OSF1)
	BUILD_SHARED    =   TRUE
	BUILDAPI_SHARED =       FALSE
	PLUGIN_SHARED   =   TRUE
	CC      =   cc -taso -DIS_64 -Olimit 4000
	CXX     =   g++
	LD      =   ld
	DLL_CFLAGS  =
	DLL_CXXFLAGS    =
	DLL_LDFLAGS =
	OPTIMIZE_FLAGS  =   -O
	CFLAGS      =   -DOSF1 -DSTATOBJS -DHAVE_NIS -DXP_UNIX \
				-DHAS_FGETPWENT \
				-DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				-DOSVERSION=$(OSVERSION)
	LD_EXTRAS   =   -lc_r -L/tools/ns/lib -liostream -Wl,-taso
	MSRV_INCLUDES   =   -I/tools/ns/lib/g++-include 
endif

### AIX notes #############################################################
## Yah, right. In your dreams...
## What a horrid c++ platform. We need to supply our own g++ config file
## because the systems supplied one is broken. Lots of surprises in store.
## Needs a separate build tree on 3.2.5 because automount is broken.
## Also, like HP - gcc is used to compile and link modules with c-based main
## to overcome a global constructor problem. Change in
## admin/src/Makefile and code/network/IMAP4-Server/unix/Makefile
## and code/tools/Makefile
##
## DLL_CFLAGS type of stuff is set specifically in plugins/Makefile
## and also local/SMTP-Router/Makefile to use a specific export list
## (for the NSmatch plugin in this case). Also see further below for the 
## linker override option to use the import list instead of a named lib
##########################################################################

## AIX 4
ifeq ($(ARCH), AIX)
	BUILD_SHARED    =   FALSE
	BUILDAPI_SHARED =       FALSE
	PLUGIN_SHARED   =   TRUE
	SHLIB_SUFFIX	=   _shr.a
	CC      =   cc
	CXX     =   g++
#	CXX     =   g++ -I$(MSRV_ROOT)/code/include/aix
	LD      =   ld
	DLL_CLAGS   =   
	DLL_CXXFLAGS    =
	OPTIMIZE_FLAGS  =   -O
	CFLAGS      =   -DAIX -DAIXV3 -DAIXV4 -DSTATOBJS \
				-DHAVE_NIS -DXP_UNIX -mcpu=common\
				-DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				-DOSVERSION=$(OSVERSION)
	LD_EXTRAS   =  -L/gnu/install/lib -L/gnu/install/lib/gcc-lib/powerpc-ibm-aix4.1.4.0/2.7.2.1/common -lstdc++ -ls -lsvld -lgcc -lc_r
#	LD_EXTRAS   =   -L/usr/gnu/lib  -liostream -ls -ldl
	MSRV_INCLUDES   =   -I/gnu/install/lib/g++-include  
#	MSRV_INCLUDES   =   -I/usr/gnu/lib/g++-include  
endif

## AIX 3
#ifeq ($(ARCH), AIX)
#   BUILD_SHARED    =   FALSE
#       BUILDAPI_SHARED =       FALSE
#   PLUGIN_SHARED   =   TRUE
#   CC      =   svcc
#   CXX     =   g++ -I$(MSRV_ROOT)/code/include/aix
#   LD      =   ld
#   DLL_CLAGS   =   
#   DLL_CXXFLAGS    =
#   OPTIMIZE_FLAGS  =   -O
#   CFLAGS      =   -DAIX -DAIXV3 -DSTATOBJS \
#               -DHAVE_NIS -DXP_UNIX \
#		-DMSRV_RELEASE=\"$(MSRV_RELEASE)\"
#   LD_EXTRAS   =   -L/tools/ns/lib -liostream -ls
#   MSRV_INCLUDES   =   -I/tools/ns/lib/g++-include 
#endif


ifeq ($(ARCH), WINNT)

	SHLIB_SUFFIX    =       dll
	ARCHIVE_SUFFIX  =       lib
	RM              =       del /q
	EXE_SUFFIX      =       .exe
	OBJ_SUFFIX      =       obj

	SHARED_LIB	=	NetscapeMTA30.$(SHLIB_SUFFIX)
	SHARED_IMPLIB   =	NetscapeMTA30.$(ARCHIVE_SUFFIX)
	SHARED_BASE_LIB	=	NetscapeMTAX30.$(SHLIB_SUFFIX)
	SHARED_BASE_IMPLIB   =	NetscapeMTAX30.$(ARCHIVE_SUFFIX)
	MATCH_LIB	=	NSMatch30.$(SHLIB_SUFFIX)
	MATCH_IMPLIB    =	NSMatch30.$(ARCHIVE_SUFFIX)
	POSEC_LIB       =       nsSupport30.$(SHLIB_SUFFIX)
	POSEC_IMPLIB    =       nsSupport30.$(ARCHIVE_SUFFIX)
	MDBAPI_LIB      =       NetscapeMDB30.$(SHLIB_SUFFIX)
	MDBAPI_IMPLIB   =       NetscapeMDB30.$(ARCHIVE_SUFFIX)
	BUILD_SHARED    =	TRUE
	BUILDAPI_SHARED =       TRUE
	PLUGIN_SHARED   =	TRUE
	POSEC_SHARED    =       FALSE
	CFLAGS          =	/D__NT__ $(MC_DEBUG) \
				/DMSRV_RELEASE=\"$(MSRV_RELEASE)\" \
				/D$(NS_PRODUCT) $(XP_FLAG) \
				/D__USE_THREAD_HEAPS__ -Gy
	OPTIMIZE_FLAGS	=	-Ob1 -O2
	DLL_CFLAGS	=	/D__Lib__
	DLL_CXXFLAGS    =	/D__Lib__
	LD_EXTRAS	=	/INCREMENTAL:NO $(ML_DEBUG)
	LINK_CONSOLE    =	link /NOLOGO /SUBSYSTEM:CONSOLE $(ML_DEBUG) \
				/OUT:$@ /OPT:REF /MAP
endif


.SUFFIXES: .cxx

# In Debug mode, always build archive libraries

ifeq ($(MSRV_DEBUG), yes)

ifneq ($(ARCH), WINNT)
MSRV_DBG_DEFINES    =       -g -DMSRV_DEBUG $(DBG_FLAGS)
BUILD_SHARED        =   FALSE
PLUGIN_SHARED       =   FALSE   
else
MSRV_DBG_DEFINES    =       /Od /Zi $(DBG_FLAGS)
endif

else
MSRV_DBG_DEFINES =  $(OPTIMIZE_FLAGS)
endif


ifneq ($(ARCH), WINNT)

ifeq ($(BUILD_SHARED), TRUE)
	SHARED_LIB  =   libNSmail.$(SHLIB_SUFFIX)
else
	SHARED_LIB  =   libNSmail.$(ARCHIVE_SUFFIX)
endif

ifeq ($(BUILDAPI_SHARED), TRUE)
	MDBAPI_LIB  =   libNSmdb.$(SHLIB_SUFFIX)
else
	MDBAPI_LIB  =   libNSmdb.$(ARCHIVE_SUFFIX)
endif


ifeq ($(PLUGIN_SHARED), TRUE)
ifeq ($(ARCH), AIX)
	MATCH_LIB   =   libNSmatch$(SHLIB_SUFFIX)
	HDR_LIB     =   libNShdr$(SHLIB_SUFFIX)
else
	MATCH_LIB   =   libNSmatch.$(SHLIB_SUFFIX)
	HDR_LIB     =   libNShdr.$(SHLIB_SUFFIX)
endif
else
	MATCH_LIB   =   libNSmatch.$(ARCHIVE_SUFFIX)
	HDR_LIB     =   libNShdr.$(ARCHIVE_SUFFIX)
endif

	
ifeq ($(ARCH), HPUX)
ifeq ($(MSRV_HPUX_CURSES_STATIC), TRUE)
CURSES=/usr/lib/libHcurses.a
else
CURSES=/usr/lib/libcurses.sl
endif
else
CURSES=-lcurses -ltermcap
endif

ifeq ($(ARCH), AIX)
CURSES=-lcurses
endif

# Before merge stuff - nirmal 4/24
#LD_LINKLIB  =		-L$(LIBDEST) -L/usr/lib -L$(NDBGDEST)/lib \
#			-lNSmdb -lNSmail -lNSmObj \
#			-lNSmail -lNSmdb -L$(NSCP_DISTDIR)/lib \
#			-llcache10 -lldap10 $(CURSES)
#

ifeq ($(ARCH), HPUX)
LD_LINKLIB  =		-L$(LIBDEST) -L/usr/lib -L$(NDBGDEST)/lib \
			-lNSmdb -lNSmail -lNSmObj -lNSmail -lNSmdb \
			-L$(NSCP_DISTDIR)/lib  -llcache10 \
			-lldap10 \
			$(CURSES)
else
LD_LINKLIB  =		-L$(LIBDEST) -L/usr/lib -L$(NDBGDEST)/lib \
			-lNSmdb -lNSmail -lNSmObj -lNSmail -lNSmdb \
			$(NSCP_DISTDIR)/lib/liblcache10.$(SHLIB_SUFFIX) \
			$(NSCP_DISTDIR)/lib/libldap10.$(SHLIB_SUFFIX) \
			$(CURSES)
endif

ifeq ($(ARCH), AIX)
CURSES=-lcurses
LD_LINKLIB  =		-L$(LIBDEST) -L/usr/lib -L$(NDBGDEST)/lib \
			-lNSmdb -lNSmail -lNSmObj \
			$(NSCP_DISTDIR)/lib/liblcache10$(SHLIB_SUFFIX) \
			$(NSCP_DISTDIR)/lib/libldap10$(SHLIB_SUFFIX) \
			$(NSCP_DISTDIR)/lib/libssldap10.a \
			$(CURSES)

endif


LD_MATCHLIB =   -lNSmatch
else  # ARCH = WINNT section
LD_POSECLIB =   $(addprefix $(LIBDEST)/, $(POSEC_IMPLIB))
LD_LINKLIB  =   $(addprefix $(LIBDEST)/, $(SHARED_IMPLIB))
LD_MATCHLIB =   $(addprefix $(LIBDEST)/, $(MATCH_IMPLIB))
endif

ifeq ($(ARCH), WINNT)
LD_LINKLIB += $(NSCP_DISTDIR)/lib/nsldap32v10.lib $(NSCP_DISTDIR)/lib/nslch32v10.lib
LDX_LINKLIB = $(LD_LINKLIB) $(LIBDEST)/$(SHARED_BASE_IMPLIB)

I18NLIBS=$(addsuffix .$(LIB_SUFFIX),\
		$(addprefix $(OBJDIR)/lib/lib, \
		ldapu  $(LIBADMIN) $(FRAME) $(CRYPT) $(LIBACCESS))) \
		$(LIBDBM) $(LIBXP) $(LIBNSPR)  $(LIBARES) $(LIBSEC)
else

# The way I18LIBS was defined in server3_mail_branch,  is quite misleading. 
# (see modules.mk for details). After this, LD_LINKLIB gets redefined
# and that adds unwanted lines to the link of bunch of things. Please
# add I18LIBS to the individual makefiles, where necessary. See 
# mailserv2/admin/src/Makefile -> DEPLIBS for an example.
# I am changing this definition for the merge. Retain these changes in
# future merges. I haved added posix4 to keep what was before merge.
# - Nirmal 4/24/97.
#I18NLIBS=$(addsuffix .$(LIB_SUFFIX),\
#		$(addprefix $(OBJDIR)/lib/lib, \
#		ldapu  $(LIBADMIN) $(FRAME) $(CRYPT) $(LIBACCESS))) \
#		$(LIBDBM) $(LIBXP) $(LIBNSPR)  $(LIBARES) \
#		-L$(MCOM_ROOT)/components/ldapsdk/$(NSOBJDIR_NAME)/lib \
#		-lssldap10 $(LIBSEC)
I18NLIBS=$(addsuffix .$(LIB_SUFFIX),\
		$(addprefix $(OBJDIR)/lib/lib, \
		ldapu  $(LIBADMIN) $(FRAME) $(CRYPT) $(LIBACCESS))) \
		$(LIBDBM) $(LIBXP) $(LIBNSPR)  $(LIBARES) \
		-L$(NSCP_DISTDIR)/lib \
		-lssldap10 $(LIBSEC)

endif

ifeq ($(ARCH), SOLARIS)  # IRIX and HPUX have no posix4 lib 
	I18NLIBS += -lposix4
endif

LD_LINKLIB += $(I18NLIBS)

ifeq ($(ARCH), IRIX)  
LD_LINKLIB += $(NSCP_DISTDIR)/lib/libldap10.so
endif

##########################################################################
## AIX override to make dynamic linking work correctly
## instead of linking directly with the shared object, we use an
## import list. Note this syntax only works with g++ or gcc as the compiler
##########################################################################

ifeq ($(ARCH), AIX)
LD_MATCHLIB     =  -Wl,-bI:$(MSRV_ROOT)/code/plugins/NSMatch.exp
endif



DB_DEFINES  =   -DMEMMOVE -D__DBINTERFACE_PRIVATE -DPOSIX_MISTAKE

INCLUDES    =   -I$(MSRV_ROOT)/code/include \
			-I$(MCOM_ROOT)/lib/libdbm \
			-I$(MSRV_ROOT)/contrib/regex \
			$(MSRV_INCLUDES)

CCOPTS      =   $(MSRV_DBG_DEFINES) $(CFLAGS) $(INCLUDES) \
			$(MCC_INCLUDE) $(NSPR_DEFINES)
CXXOPTS     =   $(MSRV_DBG_DEFINES) $(CFLAGS) $(INCLUDES) \
			$(MCC_INCLUDE) $(NSPR_DEFINES)


MSRVDESTS = $(BLDDEST) $(LIBDEST) $(NETDEST) $(LOCDEST) $(EXTDEST) \
			$(BINDEST) $(OBJDEST)

$(MSRVDESTS):
	$(MD) $@

default: all

all: $(MSRVDESTS)

depend: localdepend

clean: localclean

spotless: clean
	$(RM) -r $(BLDDEST) 

