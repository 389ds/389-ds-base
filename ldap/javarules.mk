#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# javarules.mk
#
# Identify tools, directories, classpath for building the Directory
# console

# Where the source root is
JAVA_SRC_DIR=$(ABS_ROOT)/ldap/admin/src/java

# Where the class files go
JAVA_BUILD_DIR=$(ABS_ROOT)/built/java/$(BUILD_DEBUG)/admin
JAVA_DEST_DIR=$(BUILD_ROOT)/built/java/$(BUILD_DEBUG)
CLASS_DIR=$(JAVA_DEST_DIR)/admin
DSADMIN_DIR=$(CLASS_DIR)/com/netscape/admin

# Where docs go
DSADMIN_DOC_DIR=$(JAVA_DEST_DIR)/doc


# Java setup ##############################################

# disable optimized builds for now until we can figure out why
# optimized doesn't build . . .
ifeq ($(BUILD_DEBUG),optimize)
#  JAVAFLAGS=-O
  JAVAFLAGS=
else
  JAVAFLAGS=-g
endif

PATH_SEP := :
ifeq ($(OS), Windows_NT)
	GET_JAVA_FROM_PATH := 1
	PATH_SEP := ;
	EXE_SUFFIX := .exe
endif

# For NT, assume a locally installed JDK
ifdef GET_JAVA_FROM_PATH
  # Figure out where the java lib .jar files are, from where javac is
  JDKCOMP := $(shell which javac)
  JDKPRELIB := $(subst bin/javac$(EXE_SUFFIX),lib,$(JDKCOMP))
  JDKLIB := $(addprefix $(JDKPRELIB)/,tools.jar)
else

# For UNIX, use JDK and JAR files over NFS
# Use NT classes.zip; doesn't matter that it was compiled on NT
#
# Version 1.4.0_01 of the JDK does not seem to run well on RHEL 3.0
  ifeq ($(ARCH), Linux)
  	JDK_VERSION=1.4.2
  else
	ifeq ($(ARCH), HPUX)
		JDK_VERSION=1.4.1_05
	else
  		JDK_VERSION=1.4.0_01
	endif
  endif	
	
  JDK_VERSDIR=jdk$(JDK_VERSION)
  JDKLIB=/share/builds/components/jdk/$(JDK_VERSION)/$(PRETTY_ARCH)/lib/tools.jar
  ifeq ($(NSOS_ARCH), IRIX)
# Get IRIX compiler from tools directory, currently 1.1.3
    JAVABINDIR=/tools/ns/bin
  else
	ifeq ($(ARCH), AIX)
# Get AIX compiler from tools directory, currently 1.1.2
      JAVABINDIR=/tools/ns/bin
	else
	  ifeq ($(ARCH), OSF1)
		JAVABINDIR=/share/builds/components/jdk/1.1.6beta/OSF1/bin
	  else
# Solaris, Linux, HP/UX and any others:
        JDK_DIR=$(COMPONENTS_DIR)/jdk
        JAVABINDIR=$(JDK_DIR)/$(JDK_VERSION)/$(PRETTY_ARCH)/bin
	  endif
	endif
  endif
endif

CLASSPATH := $(JAVA_SRC_DIR)$(PATH_SEP)$(NMCLFJARFILE)$(PATH_SEP)$(LDAPJARFILE)$(PATH_SEP)$(MCCJARFILE)$(PATH_SEP)$(JAVASSLJARFILE)$(PATH_SEP)$(BASEJARFILE)$(PATH_SEP)$(JSSJARFILE)
#CLASSPATH := $(JAVA_SRC_DIR)$(PATH_SEP)$(SWINGJARFILE)$(PATH_SEP)$(NMCLFJARFILE)$(PATH_SEP)$(LDAPJARFILE)$(PATH_SEP)$(MCCJARFILE)$(PATH_SEP)$(JAVASSLJARFILE)$(PATH_SEP)$(BASEJARFILE)

RUNCLASSPATH:=$(JAVA_BUILD_DIR) $(PACKAGE_UNDER_JAVA)

ifndef JAVA
  ifdef JAVABINDIR
    JAVA= $(JAVABINDIR)/java
  else
    JAVA=java
  endif
endif

# Some java compilers run out of memory, so must be run as follows
JAVAC_PROG=-mx32m sun.tools.javac.Main
HEAVY_JAVAC=$(JAVA) $(JAVAC_PROG) $(JAVAFLAGS)

ifndef JAVAC
  ifdef JAVABINDIR
    JAVAC= $(JAVABINDIR)/javac $(JAVAFLAGS)
  else
    JAVAC= javac $(JAVAFLAGS)
  endif
endif
ifndef JAVADOC
  JAVADOC=$(JAVA) -mx64m sun.tools.javadoc.Main -classpath "$(CLASSPATH)"
endif

# How to run ant (the Java "make" system)
ANT = $(JAVA) -Dant.home=$(ANT_HOME) -classpath "$(ANT_CP)$(PATH_SEP)$(JDKLIB)" org.apache.tools.ant.Main

##########################################################
