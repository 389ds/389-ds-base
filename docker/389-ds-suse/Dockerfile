#!BuildTag: 389-ds-container
FROM opensuse/leap:15.1
MAINTAINER wbrown@suse.de

EXPOSE 3389 3636

# RUN zypper ar -G obs://network:ldap network:ldap && \
RUN zypper ar http://download.opensuse.org/update/leap/15.1/oss/ u && \
    zypper ar http://download.opensuse.org/distribution/leap/15.1/repo/oss/ m && \
    zypper ar http://download.opensuse.org/repositories/network:ldap/openSUSE_Leap_15.1/ "network:ldap" && \
    zypper mr -p 97 "network:ldap" && \
    zypper --gpg-auto-import-keys ref

RUN zypper --non-interactive si --build-deps-only 389-ds && \
    zypper in -y acl cargo cyrus-sasl cyrus-sasl-plain db48-utils krb5-client libLLVM7 libedit0 libgit2-26 libhttp_parser2_7_1 libssh2-1 mozilla-nss-tools rust

# Install build dependencies
# RUN zypper in -C -y autoconf automake cracklib-devel cyrus-sasl-devel db-devel doxygen gcc-c++ \
#   gdb krb5-devel libcmocka-devel libtalloc-devel libtevent-devel libtool \
#   net-snmp-devel openldap2-devel pam-devel pkgconfig python-rpm-macros "pkgconfig(icu-i18n)" \
#   "pkgconfig(icu-uc)" "pkgconfig(libcap)" "pkgconfig(libpcre)" "pkgconfig(libsystemd)" \
#   "pkgconfig(nspr)" "pkgconfig(nss)" rsync cargo rust rust-std acl cyrus-sasl-plain db-utils \
#   bind-utils krb5 fillup shadow openldap2-devel pkgconfig "pkgconfig(nspr)" "pkgconfig(nss)" \
#   "pkgconfig(systemd)" python3-argcomplete python3-argparse-manpage python3-ldap \
#   python3-pyasn1 python3-pyasn1-modules python3-python-dateutil python3-six krb5-client \
#   mozilla-nss-tools

# Push source code to the container
ADD ./ /usr/local/src/389-ds-base
WORKDIR /usr/local/src/389-ds-base


# Build and install
# Derived from rpm --eval '%configure' on opensuse.
RUN autoreconf -fiv && \
    ./configure --host=x86_64-suse-linux-gnu --build=x86_64-suse-linux-gnu \
    --program-prefix= \
    --disable-dependency-tracking \
    --prefix=/usr \
    --exec-prefix=/usr \
    --bindir=/usr/bin \
    --sbindir=/usr/sbin \
    --sysconfdir=/etc \
    --datadir=/usr/share \
    --includedir=/usr/include \
    --libdir=/usr/lib64 \
    --libexecdir=/usr/lib \
    --localstatedir=/var \
    --sharedstatedir=/var/lib \
    --mandir=/usr/share/man \
    --infodir=/usr/share/info \
    --disable-dependency-tracking \
    --enable-debug \
    --enable-gcc-security --enable-autobind --enable-auto-dn-suffix --with-openldap \
    --enable-cmocka --enable-rust --disable-perl --with-pythonexec="python3" --without-systemd \
    --libexecdir=/usr/lib/dirsrv/ --prefix=/ && \
    make -j 12 && \
    make install && \
    make lib389 && \
    make lib389-install

# Link some known static locations to point to /data
RUN mkdir -p /data/config && \
    mkdir -p /data/ssca && \
    mkdir -p /data/run && \
    mkdir -p /var/run/dirsrv && \
    ln -s /data/config /etc/dirsrv/slapd-localhost && \
    ln -s /data/ssca /etc/dirsrv/ssca && \
    ln -s /data/run /var/run/dirsrv

# Temporal volumes for each instance

VOLUME /data

# Set the userup correctly.
# USER dirsrv

HEALTHCHECK --start-period=5m --timeout=5s --interval=5s --retries=2 \
    CMD /usr/libexec/dirsrv/dscontainer -H

CMD [ "/usr/libexec/dirsrv/dscontainer", "-r" ]

