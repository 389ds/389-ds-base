srpm:
	# Generate spec file
	make -f rpm.mk rpmroot
	# Install build dependencies
	dnf install -y dnf-plugins-core
	dnf builddep -y --skip-broken --spec rpmbuild/SPECS/389-ds-base.spec --best --allowerasing --setopt=install_weak_deps=False
	# Generate srpm
	make -f rpm.mk srpms
	
	if [[ "${outdir}" != "" ]]; then \
	        mv dist/srpms/* ${outdir}; \
	fi

