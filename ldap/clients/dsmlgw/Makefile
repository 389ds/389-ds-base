# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK

NOSTDCLEAN=true
NO_BUILD_NUM=true
COMPONENT_DEPS=true


BUILD_ROOT=../../..

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ldap/javarules.mk

all: $(ANT_DEP) $(LDAPJDK_DEP)
	cp $(CLASS_DEST)/$(AXIS_REL_DIR)/lib/axis.jar $(CLASS_DEST)
	$(ANT) 

clean: 	
	$(ANT) clean

