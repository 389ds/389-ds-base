srpm:
	# Install git in the buildroot to correctly generate commit hash
	dnf install -y git
	# Generate spec file
	make -f rpm.mk rpmroot
	# Install build dependencies
	dnf install -y dnf-plugins-core
	dnf builddep -y --skip-broken --spec rpmbuild/SPECS/389-ds-base.spec --best --allowerasing --setopt=install_weak_deps=False
	# Generate srpm
	SKIP_AUDIT_CI=1 make -f rpm.mk srpms
	
	if [[ "${outdir}" != "" ]]; then \
	        mv dist/srpms/* ${outdir}; \
	fi

