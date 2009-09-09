POLICY_MAKEFILE = /usr/share/selinux/devel/Makefile
POLICY_DIR = $(DESTDIR)/usr/share/selinux/targeted

all:
	if [ ! -e $(POLICY_MAKEFILE) ]; then echo "You need to install the SELinux policy development tools (selinux-policy)" && exit 1; fi

	$(MAKE) -f $(POLICY_MAKEFILE) $@ || exit 1;

clean:
	$(MAKE) -f $(POLICY_MAKEFILE) $@ || exit 1;

install: all
	install -d $(POLICY_DIR)
	install -m 644 dirsrv.pp $(POLICY_DIR)

load:
	/usr/sbin/semodule -i dirsrv.pp
