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
#   Name: 
#   Platform: gmake
# --------------------------------------------------------------------------
override BUILD_MODULE=HTTP_ADMIN

NOSTDCLEAN=true
NO_BUILD_NUM=true
COMPONENT_DEPS=true

BUILD_ROOT=../../../../../../..

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ldap/javarules.mk
CLASS_DIR=$(JAVA_DEST_DIR)/xmltools
PACKAGE_DIR=$(ABS_ROOT_PARENT)/dist/$(BUILD_DEBUG)
_PACKAGE_DIR=$(NSCP_DIST)/$(BUILD_DEBUG)
DEST_DIR=$(CLASS_DIR)/com/netscape/xmltools
JARS_DIR=lib
TOOLS_JARS=$(JARS_DIR)/crimson.jar$(PATH_SEP)$(JARS_DIR)/ldapjdk.jar
TOOLS_JAR_FILE=xmltools.jar
#
# programs list
#
source = \
		$(DEST_DIR)/DSML2LDIF.class \
		$(DEST_DIR)/LDIF2DSML.class \
		$(DEST_DIR)/DSMLReader.class \
		$(DEST_DIR)/DSMLWriter.class \
		$(DEST_DIR)/GetOpt.class \
		$(DEST_DIR)/DSMLSAXBuilder.class \
		$(DEST_DIR)/DSMLSAXHandler.class


package: all $(_PACKAGE_DIR) 
# when zip finds nothing to do, it exits with code 12 which is not an error
# so we turn that 12 into a 0 which means success so make will continue
	cd  $(CLASS_DIR); zip -r -u $(PACKAGE_DIR)/$(TOOLS_JAR_FILE) com || if [ $$? -eq 12 ]; then exit 0 ; else exit $$? ; fi

all: $(DEST_DIR) $(CRIMSONJAR_DEP) $(LDAPJDK_DEP) $(source)  

clean:
	rm -f  $(DEST_DIR)/*.class; rm -f  $(PACKAGE_DIR)/$(TOOLS_JAR_FILE)

fresh: $(clean) $(all)


jdoc: ./doc
	$(JAVADOC) -classpath "$(CLASSPATH)$(PATH_SEP)$(CLASS_DIR)$(PATH_SEP)$(CRIMSONJAR_FILE)" -d ./doc com.netscape.xmltools

jdoc.clean :
	rm -rf ./doc

$(_PACKAGE_DIR):
	mkdir -p $(_PACKAGE_DIR)

$(DEST_DIR)/%.class : %.java
	$(JAVAC) -deprecation -classpath "$(CLASSPATH)$(PATH_SEP)$(CLASS_DIR)$(PATH_SEP)$(CRIMSONJAR_FILE)" -d $(CLASS_DIR) $<

$(DEST_DIR) ./doc:	
	mkdir -p $@
