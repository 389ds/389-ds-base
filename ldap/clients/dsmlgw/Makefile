NOSTDCLEAN=true
NO_BUILD_NUM=true
COMPONENT_DEPS=true


BUILD_ROOT=../../..

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ldap/javarules.mk

all: $(ANT_DEP) $(LDAPJDK_DEP)
	$(ANT) 

clean: 	
	$(ANT) clean

