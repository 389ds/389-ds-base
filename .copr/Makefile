srpm:
	# Install git in the buildroot to correctly generate commit hash
	dnf install -y git
	# Generate spec file
	make -f rpm.mk rpmroot
	# Install build dependencies
	dnf install -y dnf-plugins-core
	dnf builddep -y --skip-broken --spec rpmbuild/SPECS/389-ds-base.spec --best --allowerasing --setopt=install_weak_deps=False
	cargo install cargo-license --root /usr
	# chown files in current working directory to root:root
	# because when npm is run as root, scripts are always run
	# with the effective uid and gid of the working directory owner.
	# copr-rpmbuild runs mock with CAP_SETUID and CAP_SETGID
	# capabilities dropped, and build commands are executed as root.
	# So npm fails if current working directory is not owned by root.
	chown -R root:root .
	# Generate srpm
	SKIP_AUDIT_CI=1 make BUNDLE_LIBDB=1 -f rpm.mk srpm

	if [[ "${outdir}" != "" ]]; then \
	        mv dist/srpms/* ${outdir}; \
	fi

