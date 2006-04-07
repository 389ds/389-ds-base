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
# this is ns_usesh.mk
# a make include file intended to make the use of SmartHeap
# within builds easier. 
# winges to: dboreham
ifdef bogus_variable_to_skip_comments
# to use this:
# include nsconfig.mk because this file uses stuff defined in there
# in your makefile, define SH_VERSION if you need to
# (defaults to the latest version otherwise)
# include this file.
# you can now use the following variables in your
# makefile:
#	SH_LIB_DEP	Declare a dependency on this if you want to rebuild when the DB lib changes
#	SH_INCLUDE	The pathname to the DB include directory use -I$(SH_INCLUDE) in CFLAGS to pickup the db header
#	SH_LIBPATH	The pathname to the DB libraries. Use this to find the .so or .dll file
#	SH_LIB		The pathname to the DB library file---put this on your linker command line to pickup the library
#	SH_STATIC_LIB	The pathname to a static link version of DB.
#
#	Here is a sample:
#
#BUILD_ROOT=..
#
#include $(BUILD_ROOT)/nsconfig.mk
#include $(BUILD_ROOT)/ns_usesh.mk
#
#
#ifeq ($(ARCH), WINNT)
#EXE_SUFFIX=.exe
#OBJ_SUFFIX=obj
#%.exe: %.$(OBJ_SUFFIX)
#	$(CC) $(LDFLAGS) $< -o $@
#%.$(OBJ_SUFFIX): %.c
#	$(CC) $(CFLAGS) -c $< -o $@
#else
## currently assume that if not NT then UNIX
#EXE_SUFFIX=
#OBJ_SUFFIX=o
#endif
#
#LINK_EXE	= $(CC) $(LDFLAGS) -o $@ $(OBJS) $< $(EXTRA_LIBS)
#
#
## these modules use SH, so we add to CFLAGS to ensure the headers get found
#CFLAGS += -I$(SH_INCLUDE)
## and, again because they us DB, we add the db library to the link flags
#EXTRA_LIBS += $(SH_LIB)
#
#target=prog
#
#target_bin=prog$(EXE_SUFFIX)
#
#print:
#	@echo =========== Building with the follow SH Variables ============
#	@echo SH_LIB_DEP=$(SH_LIB_DEP)
#	@echo SH_INCLUDE=$(SH_INCLUDE)
#	@echo SH_LIBPATH=$(SH_LIBPATH)
#	@echo SH_LIB=$(SH_LIB)
#	@echo SH_STATIC_LIB=$(SH_STATIC_LIB)
#	@echo SH_VERSION=$(SH_VERSION)
#	@echo =========== ===================================== ============
#	
#all: 	print $(SH_LIB_DEP) $(target_bin)
#
#$(target_bin): $(target).$(OBJ_SUFFIX)
#	$(LINK_EXE)
#
endif

#if no version specified, we'll use the latest one
ifndef SH_VERSION
SH_VERSION=latest
endif

# this is the _only_ place the component name gets defined
# if you're the next person adding a component to the build
# process, you need only edit this line, and change the 
# external variable names---go home early today !
# if we wanted to get really smart ass, we could use computed
# variable names. Hmm...
sh_component_name:=smartheap6
# define the paths to the component parts
sh_path_root:=$(NSCP_DISTDIR)/$(sh_component_name)
sh_components_share=/share/builds/components/$(sh_component_name)
sh_release_config:=$(sh_components_share)/$(SH_VERSION)/$(NSCONFIG)$(NSOBJDIR_TAG)
SH_INCLUDE:=$(sh_path_root)/include
SH_LIBPATH:=$(sh_path_root)/lib
# hack below because I couldn't find this defined anywhere in the nsxxx.mk headers
ifeq ($(ARCH), WINNT)
sh_import_lib_suffix:=$(LIB_SUFFIX)
SH_LIB:=$(SH_LIBPATH)/shdsmpmt.$(sh_import_lib_suffix)
SH_STATIC_LIB:=$(SH_LIBPATH)/shlsmpmt.$(LIB_SUFFIX)
SH_LIB_DEP:=$(SH_STATIC_LIB)
else
sh_import_lib_suffix:=$(DLL_SUFFIX)
#This needed to get the libraries initialized in the correct order for Solaris C++ code
ifeq ($(ARCH), SOLARIS)
SH_LIB:=-L$(SH_LIBPATH) -lsh -lc
else
#LA 05/16/01: add C++ smartheap library/test for HPUX
  ifeq ($(ARCH), HPUX)
    SH_LIB:=-L$(SH_LIBPATH) -lsh -lsmartheapC_smp
  else
    SH_LIB:=-L$(SH_LIBPATH) -lsh
  endif
endif
SH_STATIC_LIB:=-L$(SH_LIBPATH) -lshs
SH_LIB_DEP:=$(SH_LIBPATH)/libshs.a
endif

ifeq ($(ARCH), WINNT)
#Install smartheap dll in the server binary directory
ifeq ($(DEBUG), optimize)
PACKAGE_SRC_DEST += $(SH_LIBPATH)/libsh.$(DLL_SUFFIX) bin/slapd/server
endif
endif

# add ",bin" to DB_FILES if you want the programs
SH_FILES=include,lib

ifndef SH_PULL_METHOD
SH_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SH_LIB_DEP): $(NSCP_DISTDIR)
	$(FTP_PULL) -method $(SH_PULL_METHOD) \
		-objdir $(sh_path_root) -componentdir $(sh_release_config) \
		-files $(SH_FILES)
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component $(sh_component_name) file $@" ; \
	fi
