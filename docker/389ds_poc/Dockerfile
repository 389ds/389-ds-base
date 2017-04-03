# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

FROM centos:7
MAINTAINER 389-devel@lists.fedoraproject.org
EXPOSE 389 636
ENV container docker
ARG lib389repository=https://pagure.io/lib389.git
ARG dsrepository=https://pagure.io/389-ds-base.git
ARG lib389branch=master
ARG dsbranch=master

RUN mkdir -p /usr/local/src
WORKDIR /usr/local/src

RUN yum upgrade -y && \
    yum install -y @buildsys-build rpm-build make epel-release bzip2 git && \
    yum clean all

RUN git clone --depth=1 --branch=$lib389branch $lib389repository lib389 && \
    git clone --depth=1 --branch=$dsbranch $dsrepository 389-ds-base

RUN yum install -y --skip-broken `grep -E "^(Build)?Requires" 389-ds-base/rpm/389-ds-base.spec.in lib389/python-lib389.spec | grep -v -E '(name|MODULE)' | awk '{ print $2 }' | grep -v "^/" | grep -v pkgversion | sort | uniq|  tr '\n' ' '` && \
    yum clean all

RUN make -C lib389/ rpm

RUN cd 389-ds-base && make -f rpm.mk rpms

RUN yum install -y 389-ds-base/dist/rpms/389*.rpm lib389/rpmbuild/RPMS/noarch/*lib389* && \
    yum clean all

# Create the example setup inf. It's valid for containers!
# Build the instance from the new installer tools.
RUN /usr/sbin/dscreate example > /root/ds-setup.inf && /usr/sbin/dscreate -v fromfile /root/ds-setup.inf --IsolemnlyswearthatIamuptonogood --containerised

# Finally add the volumes, they will inherit the contents of these directories.
VOLUME /etc/dirsrv
VOLUME /var/log/dirsrv
VOLUME /var/lib/dirsrv

# Or, run them as dirsrv
USER dirsrv
CMD ["/usr/sbin/ns-slapd", "-d", "0", "-D", "/etc/dirsrv/slapd-localhost", "-i", "/var/run/dirsrv/slapd-localhost.pid"]

