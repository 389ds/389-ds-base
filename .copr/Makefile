srpm:
	# Install dependencies to generate srpm
	dnf install -y git rsync bzip2 rpm-build cargo rust npm make python3-argcomplete
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

