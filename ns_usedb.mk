#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# this is ns_usedb.mk
# a make include file intended to make the use of berkeley db
# within builds easier. 
# winges to: dboreham
ifdef bogus_variable_to_skip_comments
# to use this:
# include nsconfig.mk because this file uses stuff defined in there
# in your makefile, define DB_VERSION if you need to
# (defaults to the latest version otherwise)
# include this file.
# you can now use the following variables in your
# makefile:
#	DB_LIB_DEP	Declare a dependency on this if you want to rebuild when the DB lib changes
#	DB_INCLUDE	The pathname to the DB include directory use -I$(DB_INCLUDE) in CFLAGS to pickup the db header
#	DB_LIBPATH	The pathname to the DB libraries. Use this to find the .so or .dll file
#	DB_LIB		The pathname to the DB library file---put this on your linker command line to pickup the library
#	DB_STATIC_LIB	The pathname to a static link version of DB.
#
#	Here is a sample:
#
#BUILD_ROOT=..
#
#include $(BUILD_ROOT)/nsconfig.mk
#include $(BUILD_ROOT)/ns_usedb.mk
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
## these modules use DB, so we add to CFLAGS to ensure the headers get found
#CFLAGS += -I$(DB_INCLUDE)
## and, again because they us DB, we add the db library to the link flags
#EXTRA_LIBS += $(DB_LIB)
#
#target=prog
#
#target_bin=prog$(EXE_SUFFIX)
#
#print:
#	@echo =========== Building with the follow DB Variables ============
#	@echo DB_LIB_DEP=$(DB_LIB_DEP)
#	@echo DB_INCLUDE=$(DB_INCLUDE)
#	@echo DB_LIBPATH=$(DB_LIBPATH)
#	@echo DB_LIB=$(DB_LIB)
#	@echo DB_STATIC_LIB=$(DB_STATIC_LIB)
#	@echo DB_VERSION=$(DB_VERSION)
#	@echo =========== ===================================== ============
#	
#all: 	print $(DB_LIB_DEP) $(target_bin)
#
#$(target_bin): $(target).$(OBJ_SUFFIX)
#	$(LINK_EXE)
#
endif

#if no version specified, we'll use the latest one
ifndef DB_VERSION
DB_VERSION=20040130
endif

# Sleepycat version #major#minor
ifndef DB_MAJOR_MINOR
DB_MAJOR_MINOR:=db42
endif

DB_LIBNAME=lib$(DB_MAJOR_MINOR)

# this is the _only_ place the component name gets defined
# if you're the next person adding a component to the build
# process, you need only edit this line, and change the 
# external variable names---go home early today !
# if we wanted to get really smart ass, we could use computed
# variable names. Hmm...
component_name:=$(DB_MAJOR_MINOR)

# define the paths to the component parts
db_components_share=$(COMPONENTS_DIR)/$(component_name)
MY_NSOBJDIR_TAG=$(NSOBJDIR_TAG).OBJ

db_path_config:=$(NSCP_DISTDIR)/$(component_name)
ifeq ($(ARCH), IRIX)
db_release_config:=$(db_components_share)/$(DB_VERSION)/$(NSCONFIG)$(NSOBJDIR_TAG)
else
ifeq ($(ARCH), OSF1)
db_release_config:=$(db_components_share)/$(DB_VERSION)/$(NSCONFIG)$(MY_NSOBJDIR_TAG)
else
db_release_config:=$(db_components_share)/$(DB_VERSION)/$(NSCONFIG_NOTAG)$(NS64TAG)$(MY_NSOBJDIR_TAG)
endif	# OSF1
endif	# IRIX

ifeq ($(ARCH), AIX) 
ifeq ($(AIXOS), 4.3) 
CFLAGS += -D__BIT_TYPES_DEFINED__ 
endif 
endif # AIX

DB_INCLUDE:=$(db_path_config)/include
DB_LIBPATH:=$(db_path_config)/lib
DB_BINPATH:=$(db_path_config)/bin
# hack below because I couldn't find this defined anywhere in the nsxxx.mk headers
ifeq ($(ARCH), WINNT)
db_import_lib_suffix:=$(LIB_SUFFIX)
DB_LIB:=$(DB_LIBPATH)/$(DB_LIBNAME).$(db_import_lib_suffix)
DB_STATIC_LIB:=$(DB_LIBPATH)/$(DB_LIBNAME).$(LIB_SUFFIX)
DB_LIB_DEP:=$(DB_STATIC_LIB)
else	# not WINNT
db_import_lib_suffix:=$(DLL_SUFFIX)
DB_LIB:=-L$(DB_LIBPATH) -l$(DB_MAJOR_MINOR)
# XXXsspitzer: we need the spinlock symbols staticly linked in to libdb
DB_STATIC_LIB:=-L$(DB_LIBPATH) -ldbs
DB_LIB_DEP:=$(DB_LIBPATH)/$(DB_LIBNAME).$(DLL_SUFFIX)
endif	# not WINNT

# libdb only needs to be in the server directory since only the server uses it
PACKAGE_SRC_DEST += $(wildcard $(DB_LIBPATH)/*.$(DLL_SUFFIX)) bin/slapd/server

# add ",bin" to DB_FILES if you want the programs
DB_FILES=include,lib,bin

ifndef DB_PULL_METHOD
DB_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(DB_LIB_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(DB_PULL_METHOD) \
		-objdir $(db_path_config) -componentdir $(db_release_config) \
		-files $(DB_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component $(component_name) file $@" ; \
	fi
