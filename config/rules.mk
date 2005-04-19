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
################################################################################
# Wow this is complicated! The story is that we now have a 4 pass build process:
#
# Pass 1. export - Create generated headers and stubs. Publish public headers to
#		dist/<arch>/include.
#
# Pass 2. libs - Create libraries. Publish libraries to dist/<arch>/lib.
#
# Pass 3. all - Create programs. 
#
# Pass 4. install - Publish programs to dist/<arch>/bin.
#
# Parameters to this makefile (set these before including):
#
# a)
#	TARGETS	-- the target to create 
#			(defaults to $LIBRARY $PROGRAM)
# b)
#	DIRS	-- subdirectories for make to recurse on
#			(the 'all' rule builds $TARGETS $DIRS)
# c)
#	CSRCS, CPPSRCS -- .c and .cpp files to compile
#			(used to define $OBJS)
# d)
#	PROGRAM	-- the target program name to create from $OBJS
#			($OBJDIR automatically prepended to it)
# e)
#	LIBRARY	-- the target library name to create from $OBJS
#			($OBJDIR automatically prepended to it)
# f)
#	JSRCS	-- java source files to compile into class files
#			(if you don't specify this it will default to *.java)
#	PACKAGE	-- the package to put the .class files into
#			(e.g. netscape/applet)
#	JMC_EXPORT -- java files to be exported for use by JMC_GEN
#			(this is a list of Class names)
# g)
#	JRI_GEN	-- files to run through javah to generate headers and stubs
#			(output goes into the _jri sub-dir)
# h)
#	JMC_GEN	-- files to run through jmc to generate headers and stubs
#			(output goes into the _jmc sub-dir)
#
################################################################################

#
# Common rules used by lots of makefiles...
#
ifndef NS_CONFIG_MK
include $(DEPTH)/config/config.mk
endif

ifdef PROGRAM
PROGRAM		:= $(addprefix $(OBJDIR)/, $(PROGRAM))
endif

ifndef LIBRARY
ifdef LIBRARY_NAME
LIBRARY		:= lib$(LIBRARY_NAME).$(LIB_SUFFIX)
endif
endif

ifdef LIBRARY
LIBRARY		:= $(addprefix $(OBJDIR)/, $(LIBRARY))
ifdef MKSHLIB
SHARED_LIBRARY	:= $(LIBRARY:.$(LIB_SUFFIX)=$(DLL_PRESUF).$(DLL_SUFFIX))
endif
endif

ifndef TARGETS
TARGETS		= $(LIBRARY) $(SHARED_LIBRARY) $(PROGRAM)
endif

ifndef OBJS
OBJS		= $(JRI_STUB_CFILES) $(addsuffix .o, $(JMC_GEN)) $(CSRCS:.c=.o) $(CPPSRCS:.cpp=.o) $(ASFILES:.s=.o)
endif

ifdef OBJS
OBJS		:= $(addprefix $(OBJDIR)/, $(OBJS))
endif

ifdef REQUIRES
MODULE_PREINCLUDES	= $(addprefix -I$(XPDIST)/public/, $(REQUIRES))
endif

ifeq ($(OS_ARCH),WINNT)
ifdef DLL
DLL		:= $(addprefix $(OBJDIR)/, $(DLL))
LIB		:= $(addprefix $(OBJDIR)/, $(LIB))
endif
endif
define MAKE_OBJDIR
if test ! -d $(@D); then rm -rf $(@D); $(NSINSTALL) -D $(@D); fi
endef

ALL_TRASH		= $(TARGETS) $(OBJS) $(OBJDIR) LOGS TAGS $(GARBAGE) \
			  $(NOSUCHFILE) $(JDK_HEADER_CFILES) $(JDK_STUB_CFILES) \
			  $(JRI_HEADER_CFILES) $(JRI_STUB_CFILES) $(JMC_STUBS) \
			  $(JMC_HEADERS) $(JMC_EXPORT_FILES) so_locations \
			  _gen _jmc _jri _stubs \
			  $(wildcard $(JAVA_DESTPATH)/$(PACKAGE)/*.class)

ifdef JDIRS
ALL_TRASH		+= $(addprefix $(JAVA_DESTPATH)/,$(JDIRS))
endif

ifdef NSBUILDROOT
JDK_GEN_DIR		= $(XPDIST)/_gen
JMC_GEN_DIR		= $(XPDIST)/_jmc
JRI_GEN_DIR		= $(XPDIST)/_jri
JDK_STUB_DIR		= $(XPDIST)/_stubs
else
JDK_GEN_DIR		= _gen
JMC_GEN_DIR		= _jmc
JRI_GEN_DIR		= _jri
JDK_STUB_DIR		= _stubs
endif

#
# If this is an "official" build, try to build everything.
# I.e., don't exit on errors.
#
ifdef BUILD_OFFICIAL
EXIT_ON_ERROR		= +e
CLICK_STOPWATCH		= date
else
EXIT_ON_ERROR		= -e
CLICK_STOPWATCH		= true
endif


################################################################################

ifdef ALL_PLATFORMS
all_platforms:: $(NFSPWD)
	@d=`$(NFSPWD)`;                                                       \
	if test ! -d LOGS; then rm -rf LOGS; mkdir LOGS; fi;                  \
	for h in $(PLATFORM_HOSTS); do                                        \
		echo "On $$h: $(MAKE) $(ALL_PLATFORMS) >& LOGS/$$h.log";      \
		rsh $$h -n "(chdir $$d;                                       \
			     $(MAKE) $(ALL_PLATFORMS) >& LOGS/$$h.log;        \
			     echo DONE) &" 2>&1 > LOGS/$$h.pid &              \
		sleep 1;                                                      \
	done

$(NFSPWD):
	cd $(@D); $(MAKE) $(@F)
endif

ifdef REQUIRES
ifndef NO_NSPR
INCLUDES += -I$(XPDIST)/$(OBJDIR)/include/nspr20/pr $(addprefix -I$(XPDIST)/public/, $(REQUIRES))
else
INCLUDES += $(addprefix -I$(XPDIST)/public/, $(REQUIRES))
endif
endif

all:: $(TARGETS)
	+$(LOOP_OVER_DIRS)

libs:: $(LIBRARY) $(SHARED_LIBRARY)

$(PROGRAM): $(OBJS)
	@$(MAKE_OBJDIR)
ifeq ($(OS_ARCH),WINNT)
	$(CC) $(OBJS) -Fe$@ -link $(LDFLAGS) $(OS_LIBS) $(EXTRA_LIBS)
else
	$(CC) -o $@ $(CFLAGS) $(OBJS) $(LDFLAGS)
endif

$(LIBRARY): $(OBJS)
	@$(MAKE_OBJDIR)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

$(SHARED_LIBRARY): $(OBJS)
	@$(MAKE_OBJDIR)
	rm -f $@
	$(MKSHLIB) -o $@ $(OBJS) $(EXTRA_SHLIBS)
	chmod +x $@

ifeq ($(OS_ARCH),WINNT)
$(DLL): $(OBJS) $(EXTRA_LIBS)
	@$(MAKE_OBJDIR)
	rm -f $@
	$(LINK_DLL) $(OBJS) $(OS_LIBS) $(EXTRA_LIBS)
endif

.SUFFIXES: .i .pl .class .java .html

.PRECIOUS: .java

$(OBJDIR)/%: %.c
	@$(MAKE_OBJDIR)
ifeq ($(OS_ARCH),WINNT)
	$(CC) -Fo$@ -c $(CFLAGS) $*.c
else
	$(CC) -o $@ $(CFLAGS) $*.c $(LDFLAGS)
endif

$(OBJDIR)/%.o: %.c
	@$(MAKE_OBJDIR)
ifeq ($(OS_ARCH),WINNT)
	$(CC) -Fo$@ -c $(CFLAGS) $*.c
else
	$(CC) -o $@ -c $(CFLAGS) $*.c
endif

$(OBJDIR)/%.o: %.s
	@$(MAKE_OBJDIR)
	$(AS) -o $@ $(ASFLAGS) -c $*.s

$(OBJDIR)/%.o: %.S
	@$(MAKE_OBJDIR)
	$(AS) -o $@ $(ASFLAGS) -c $*.S

$(OBJDIR)/%: %.cpp
	@$(MAKE_OBJDIR)
	$(CCC) -o $@ $(CFLAGS) $*.c $(LDFLAGS)

#
# Please keep the next two rules in sync.
#
$(OBJDIR)/%.o: %.cc
	@$(MAKE_OBJDIR)
	$(CCC) -o $@ -c $(CFLAGS) $*.cc

$(OBJDIR)/%.o: %.cpp
	@$(MAKE_OBJDIR)
ifdef STRICT_CPLUSPLUS_SUFFIX
	echo "#line 1 \"$*.cpp\"" | cat - $*.cpp > $(OBJDIR)/t_$*.cc
	$(CCC) -o $@ -c $(CFLAGS) $(OBJDIR)/t_$*.cc
	rm -f $(OBJDIR)/t_$*.cc
else
ifeq ($(OS_ARCH),WINNT)
	$(CCC) -Fo$@ -c $(CFLAGS) $*.cpp
else
	$(CCC) -o $@ -c $(CFLAGS) $*.cpp
endif
endif #STRICT_CPLUSPLUS_SUFFIX

%.i: %.cpp
	$(CCC) -C -E $(CFLAGS) $< > $*.i

%.i: %.c
	$(CC) -C -E $(CFLAGS) $< > $*.i

%: %.pl
	rm -f $@; cp $*.pl $@; chmod +x $@

%: %.sh
	rm -f $@; cp $*.sh $@; chmod +x $@

#
# If this is an "official" build, try to build everything.
# I.e., don't exit on errors.
#
ifdef BUILD_OFFICIAL
EXIT_ON_ERROR	= +e
else
EXIT_ON_ERROR	= -e
endif

ifdef DIRS
ifneq ($(OS_ARCH),WINNT)
override MAKEFLAGS :=
endif
LOOP_OVER_DIRS =						\
	@for d in $(DIRS); do					\
		if test -d $$d; then				\
			set $(EXIT_ON_ERROR);			\
			echo "cd $$d; $(MAKE) $(MAKEFLAGS) $@";		\
			cd $$d; $(MAKE) $(MAKEFLAGS) $@; cd ..;		\
			set +e;					\
		else						\
			echo "Skipping non-directory $$d...";	\
		fi;						\
		$(CLICK_STOPWATCH);				\
done

$(DIRS)::
	@if test -d $@; then				\
		set $(EXIT_ON_ERROR);			\
		echo "cd $@; $(MAKE) $(MAKEFLAGS)";			\
		cd $@; $(MAKE) $(MAKEFLAGS);				\
		set +e;					\
	else						\
		echo "Skipping non-directory $@...";	\
	fi						\
        $(CLICK_STOPWATCH);
endif # DIRS

clean::
	rm -f $(OBJS) $(NOSUCHFILE)
	+$(LOOP_OVER_DIRS)

clobber::
	rm -f $(OBJS) $(TARGETS) $(GARBAGE) $(NOSUCHFILE)
	+$(LOOP_OVER_DIRS)

realclean clobber_all::
	rm -rf LOGS TAGS $(wildcard *.OBJ) $(OBJS) $(TARGETS) $(GARBAGE) $(NOSUCHFILE)
	+$(LOOP_OVER_DIRS)

alltags:
	rm -f TAGS
	find . -name dist -prune -o \( -name '*.[hc]' -o -name '*.cp' -o -name '*.cpp' \) -print | xargs etags -a

export::
	+$(LOOP_OVER_DIRS)

libs::
	+$(LOOP_OVER_DIRS)

install::
	+$(LOOP_OVER_DIRS)

mac::
	+$(LOOP_OVER_DIRS)

################################################################################
### Bunch of things that extend the 'export' rule (in order):
################################################################################
### JSRCS -- for compiling java files

ifndef PACKAGE
PACKAGE = .
endif
$(JAVA_DESTPATH) $(JAVA_DESTPATH)/$(PACKAGE) $(JMCSRCDIR)::
	@if test ! -d $@; then	    \
		echo Creating $@;   \
		rm -rf $@;	    \
		$(NSINSTALL) -D $@; \
	fi

ifneq ($(JSRCS),)
export:: $(JAVA_DESTPATH) $(JAVA_DESTPATH)/$(PACKAGE)
	@list=`perl $(DEPTH)/config/outofdate.pl $(PERLARG)	\
		    -d $(JAVA_DESTPATH)/$(PACKAGE) $(JSRCS)`;	\
	if test "$$list"x != "x"; then				\
	    echo $(JAVAC) $$list;				\
	    $(JAVAC) $$list;					\
	fi

all:: export

clobber::
	rm -f $(XPDIST)/classes/$(PACKAGE)/*.class

endif

################################################################################
## JDIRS -- like JSRCS, except you can give a list of directories and it will
## compile all the out-of-date java files recursively below those directories.

ifdef JDIRS

export:: $(JAVA_DESTPATH) $(JAVA_DESTPATH)/$(PACKAGE)
	@for d in $(JDIRS); do							      \
		if test -d $$d; then						      \
			set $(EXIT_ON_ERROR);					      \
			files=`echo $$d/*.java`;			      \
			list=`perl $(DEPTH)/config/outofdate.pl $(PERLARG)	      \
				    -d $(JAVA_DESTPATH)/$(PACKAGE) $$files`;	      \
			if test "$${list}x" != "x"; then			      \
			    echo Building all java files in $$d;		      \
			    echo $(JAVAC) $$list;		      \
			    $(JAVAC) $$list;		      \
			fi;							      \
			set +e;							      \
		else								      \
			echo "Skipping non-directory $$d...";			      \
		fi;								      \
		$(CLICK_STOPWATCH);						\
	done

all:: export

clobber::
	@for d in $(JDIRS); do			   \
		echo rm -rf $(XPDIST)/classes/$$d; \
		rm -rf $(XPDIST)/classes/$$d;	   \
	done

endif

################################################################################
### JDK_GEN -- for generating "old style" native methods 

# Generate JDK Headers and Stubs into the '_gen' and '_stubs' directory

ifneq ($(JDK_GEN),)

ifdef NSBUILDROOT
JDK_GEN_DIR	= $(XPDIST)/_gen
JDK_STUB_DIR	= $(XPDIST)/_stubs
else
JDK_GEN_DIR	= _gen
JDK_STUB_DIR	= _stubs
endif

INCLUDES += -I$(JDK_GEN_DIR)

JDK_PACKAGE_CLASSES = $(JDK_GEN)
JDK_PATH_CLASSES = $(subst .,/,$(JDK_PACKAGE_CLASSES))
JDK_PATH_CLASSES = $(subst .,/,$(JDK_PACKAGE_CLASSES))
JDK_HEADER_CLASSFILES	= $(patsubst %,$(JAVA_DESTPATH)/%.class,$(JDK_PATH_CLASSES))
JDK_STUB_CLASSFILES	= $(patsubst %,$(JAVA_DESTPATH)/%.class,$(JDK_PATH_CLASSES))
JDK_HEADER_CFILES	= $(patsubst %,$(JDK_GEN_DIR)/%.h,$(JDK_GEN))
JDK_STUB_CFILES		= $(patsubst %,$(JDK_STUB_DIR)/%.c,$(JDK_GEN))

$(JDK_HEADER_CFILES): $(JDK_HEADER_CLASSFILES)
$(JDK_STUB_CFILES): $(JDK_STUB_CLASSFILES)

export::
	@echo Generating/Updating JDK headers 
	$(JAVAH) -d $(JDK_GEN_DIR) $(JDK_PACKAGE_CLASSES)
	@echo Generating/Updating JDK stubs
	$(JAVAH) -stubs -d $(JDK_STUB_DIR) $(JDK_PACKAGE_CLASSES)

mac::
	@echo Generating/Updating JDK headers for the Mac
	$(JAVAH) -mac -d $(DEPTH)/lib/mac/Java/_gen $(JDK_PACKAGE_CLASSES)
	@echo Generating/Updating JDK stubs for the Mac
	$(JAVAH) -mac -stubs -d $(DEPTH)/lib/mac/Java/_stubs $(JDK_PACKAGE_CLASSES)

# Don't delete them if the don't compile (makes it hard to debug):
.PRECIOUS: $(JDK_HEADERS) $(JDK_STUBS)

clobber::
	rm -rf $(JDK_HEADER_CFILES) $(JDK_STUB_CFILES)

endif

################################################################################
### JRI_GEN -- for generating JRI native methods

# Generate JRI Headers and Stubs into the 'jri' directory

ifneq ($(JRI_GEN),)

ifdef NSBUILDROOT
JRI_GEN_DIR	= $(XPDIST)/_jri
else
JRI_GEN_DIR	= _jri
endif

INCLUDES += -I$(JRI_GEN_DIR)

JRI_PACKAGE_CLASSES = $(JRI_GEN)
JRI_PATH_CLASSES = $(subst .,/,$(JRI_PACKAGE_CLASSES))

## dependency fu
JRI_HEADER_CLASSFILES = $(patsubst %,$(XPDIST)/classes/%.class,$(JRI_PATH_CLASSES))
JRI_HEADER_CFILES = $(patsubst %,$(JRI_GEN_DIR)/%.h,$(JRI_GEN))
$(JRI_HEADER_CFILES): $(JRI_HEADER_CLASSFILES)

## dependency fu
JRI_STUB_CLASSFILES = $(patsubst %,$(XPDIST)/classes/%.class,$(JRI_PATH_CLASSES))
JRI_STUB_CFILES = $(patsubst %,$(JRI_GEN_DIR)/%.c,$(JRI_GEN))
$(JRI_STUB_CFILES): $(JRI_STUB_CLASSFILES)

export::
	@echo Generating/Updating JRI headers 
	$(JAVAH) -jri -d $(JRI_GEN_DIR) $(JRI_PACKAGE_CLASSES)
	@echo Generating/Updating JRI stubs
	$(JAVAH) -jri -stubs -d $(JRI_GEN_DIR) $(JRI_PACKAGE_CLASSES)
	@if test ! -d $(DEPTH)/lib/mac/Java/; then						\
		echo "!!! You need to have a ns/lib/mac/Java directory checked out.";		\
		echo "!!! This allows us to automatically update generated files for the mac.";	\
		echo "!!! If you see any modified files there, please check them in.";		\
	fi
	@echo Generating/Updating JRI headers for the Mac
	$(JAVAH) -jri -mac -d $(DEPTH)/lib/mac/Java/_jri $(JRI_PACKAGE_CLASSES)
	@echo Generating/Updating JRI stubs for the Mac
	$(JAVAH) -jri -mac -stubs -d $(DEPTH)/lib/mac/Java/_jri $(JRI_PACKAGE_CLASSES)

# Don't delete them if the don't compile (makes it hard to debug):
.PRECIOUS: $(JRI_HEADERS) $(JRI_STUBS)

clobber::
	rm -rf $(JRI_HEADER_CFILES) $(JRI_STUB_CFILES)

endif

################################################################################
## JMC_EXPORT -- for declaring which java classes are to be exported for jmc

ifneq ($(JMC_EXPORT),)

JMC_EXPORT_PATHS = $(subst .,/,$(JMC_EXPORT))
JMC_EXPORT_FILES = $(patsubst %,$(XPDIST)/classes/$(PACKAGE)/%.class,$(JMC_EXPORT_PATHS))

# We're doing NSINSTALL -t here (copy mode) because calling INSTALL will pick up 
# your NSDISTMODE and make links relative to the current directory. This is a
# problem because the source isn't in the current directory:

export:: $(JMC_EXPORT_FILES) $(JMCSRCDIR)
	$(NSINSTALL) -t -m 444 $(JMC_EXPORT_FILES) $(JMCSRCDIR)

clobber::
	rm -rf $(JMC_EXPORT_FILES)

endif


################################################################################
## EXPORTS
#
# Copy each element of EXPORTS to $(XPDIST)/public/$(MODULE)/
#

ifneq ($(EXPORTS),)

$(XPDIST)/public/$(MODULE)::
	@if test ! -d $@; then	    \
		echo Creating $@;   \
		rm -rf $@;	    \
		mkdir -p $@;	    \
	fi

export:: $(EXPORTS) $(XPDIST)/public/$(MODULE)
	$(NSINSTALL) -t -m 444 $(EXPORTS) $(XPDIST)/public/$(MODULE)

endif

################################################################################
## JMC_GEN -- for generating java modules

# Provide default export & install rules when using JMC_GEN
ifneq ($(JMC_GEN),)

ifdef NSBUILDROOT
JMC_GEN_DIR	= $(XPDIST)/_jmc
else
JMC_GEN_DIR	= _jmc
endif

INCLUDES += -I$(JMC_GEN_DIR)

JMC_HEADERS = $(patsubst %,$(JMC_GEN_DIR)/%.h,$(JMC_GEN))
JMC_STUBS = $(patsubst %,$(JMC_GEN_DIR)/%.c,$(JMC_GEN))
JMC_OBJS = $(patsubst %,$(OBJDIR)/%.o,$(JMC_GEN))

$(JMC_GEN_DIR)/M%.h: $(JMCSRCDIR)/%.class
	$(JMC) -d $(JMC_GEN_DIR) -interface $(JMC_GEN_FLAGS) $(?F:.class=)

$(JMC_GEN_DIR)/M%.c: $(JMCSRCDIR)/%.class
	$(JMC) -d $(JMC_GEN_DIR) -module $(JMC_GEN_FLAGS) $(?F:.class=)

$(OBJDIR)/M%.o: $(JMC_GEN_DIR)/M%.h $(JMC_GEN_DIR)/M%.c
	@$(MAKE_OBJDIR)
	$(CC) -o $@ -c $(CFLAGS) $(JMC_GEN_DIR)/M$*.c

export:: $(JMC_HEADERS) $(JMC_STUBS)

# Don't delete them if the don't compile (makes it hard to debug):
.PRECIOUS: $(JMC_HEADERS) $(JMC_STUBS)

clobber::
	rm -rf $(JMC_HEADERS) $(JMC_STUBS)

endif

################################################################################
## LIBRARY -- default rules for for building libraries

ifdef LIBRARY
libs:: $(LIBRARY)
	$(INSTALL) -m 444 $(LIBRARY) $(DIST)/lib

install:: $(LIBRARY)
	$(INSTALL) -m 444 $(LIBRARY) $(DIST)/lib
endif

ifdef SHARED_LIBRARY
libs:: $(SHARED_LIBRARY)
	$(INSTALL) -m 555 $(SHARED_LIBRARY) $(DIST)/bin

install:: $(SHARED_LIBRARY)
	$(INSTALL) -m 555 $(SHARED_LIBRARY) $(DIST)/bin
endif

-include $(DEPENDENCIES)

ifneq ($(OS_ARCH),WINNT)
# Can't use sed because of its 4000-char line length limit, so resort to perl
.DEFAULT:
	@perl -e '                                                            \
	    open(MD, "< $(DEPENDENCIES)");                                    \
	    while (<MD>) {                                                    \
		if (m@ \.*/*$< @) {                                           \
		    $$found = 1;                                              \
		    last;                                                     \
		}                                                             \
	    }                                                                 \
	    if ($$found) {                                                    \
		print "Removing stale dependency $< from $(DEPENDENCIES)\n";  \
		seek(MD, 0, 0);                                               \
		$$tmpname = "$(OBJDIR)/fix.md" . $$$$;                        \
		open(TMD, "> " . $$tmpname);                                  \
		while (<MD>) {                                                \
		    s@ \.*/*$< @ @;                                           \
		    if (!print TMD "$$_") {                                   \
			unlink(($$tmpname));                                  \
			exit(1);                                              \
		    }                                                         \
		}                                                             \
		close(TMD);                                                   \
		if (!rename($$tmpname, "$(DEPENDENCIES)")) {                  \
		    unlink(($$tmpname));                                      \
		}                                                             \
	    } elsif ("$<" ne "$(DEPENDENCIES)") {                             \
		print "$(MAKE): *** No rule to make target $<.  Stop.\n";     \
		exit(1);                                                      \
	    }'
endif

#############################################################################
# X dependency system
#############################################################################

ifneq ($(OS_ARCH),WINNT)

$(MKDEPENDENCIES)::
	@$(MAKE_OBJDIR)
	touch $(MKDEPENDENCIES)
	$(MKDEPEND) -p$(OBJDIR_NAME)/ -o'.o' -f$(MKDEPENDENCIES) $(INCLUDES) $(CSRCS) $(CPPSRCS)

$(MKDEPEND)::
	cd $(MKDEPEND_DIR); $(MAKE)

ifdef OBJS
depend:: $(MKDEPEND) $(MKDEPENDENCIES)
else
depend::
endif
	+$(LOOP_OVER_DIRS)

dependclean::
	rm -f $(MKDEPENDENCIES)
	+$(LOOP_OVER_DIRS)

#-include $(OBJDIR)/depend.mk

endif

#############################################################################

-include $(MY_RULES)

$(MY_CONFIG):
$(MY_RULES):

# Generate Emacs tags in a file named TAGS if ETAGS was set in $(MY_CONFIG)
# or in $(MY_RULES)
ifdef ETAGS
ifneq ($(CSRCS)$(HEADERS),)
all:: TAGS
TAGS:: $(CSRCS) $(HEADERS)
	$(ETAGS) $(CSRCS) $(HEADERS)
endif
endif

################################################################################
# Special gmake rules.
################################################################################

#
# Re-define the list of default suffixes, so gmake won't have to churn through
# hundreds of built-in suffix rules for stuff we don't need.
#
.SUFFIXES:
.SUFFIXES: .out .a .ln .o .c .cc .C .cpp .y .l .s .S .h .sh .i .pl .class .java .html

#
# Don't delete these files if we get killed.
#
.PRECIOUS: .java $(JDK_HEADERS) $(JDK_STUBS) $(JRI_HEADERS) $(JRI_STUBS) $(JMC_HEADERS) $(JMC_STUBS)

#
# Fake targets.  Always run these rules, even if a file/directory with that
# name already exists.
#
.PHONY: all all_platforms alltags boot clean clobber clobber_all export install libs realclean $(OBJDIR) $(DIRS)
