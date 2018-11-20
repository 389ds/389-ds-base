# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

FROM fedora:26
MAINTAINER 389-devel@lists.fedoraproject.org
EXPOSE 389 636
ENV container docker

RUN mkdir -p /usr/local/src
WORKDIR /usr/local/src

ADD ./ /usr/local/src/389-ds-base

RUN dnf upgrade -y && \
    dnf install --setopt=strict=False -y \
        @buildsys-build rpm-build make bzip2 git rsync \
        `grep -E "^(Build)?Requires" 389-ds-base/rpm/389-ds-base.spec.in | grep -v -E '(name|MODULE)' | awk '{ print $2 }' | sed 's/%{python3_pkgversion}/3/g' | grep -v "^/" | grep -v pkgversion | sort | uniq | tr '\n' ' '` && \
    dnf clean all


### CHANGE THIS TO A ./configure and build that way.

RUN cd 389-ds-base && \
    PERL_ON=0 RUST_ON=1 make -f rpm.mk rpms

RUN dnf install -y 389-ds-base/dist/rpms/*389*.rpm && \
    dnf clean all

# Create the example setup inf. It's valid for containers!
# Build the instance from the new installer tools.
RUN /usr/sbin/dscreate create-template > /root/ds-setup.inf && /usr/sbin/dscreate -v from-file /root/ds-setup.inf --containerised

# Finally add the volumes, they will inherit the contents of these directories.
VOLUME /etc/dirsrv
VOLUME /var/log/dirsrv
VOLUME /var/lib/dirsrv

# Or, run them as dirsrv
USER dirsrv
CMD ["/usr/sbin/ns-slapd", "-d", "0", "-D", "/etc/dirsrv/slapd-localhost", "-i", "/var/run/dirsrv/slapd-localhost.pid"]

