#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
