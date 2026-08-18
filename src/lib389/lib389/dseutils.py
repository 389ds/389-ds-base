# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Helpers that read instance configuration from ``dse.ldif`` (or related paths)."""

from lib389.dseldif import DSEldif


def get_ldapurl_from_serverid(instance):
    """Take an instance name, and get the host/port/protocol from dse.ldif
    and return a LDAP URL to use in the CLI tools (dsconf)

    :param instance: The server ID of a server instance
    :return tuple of LDAPURL and certificate directory (for LDAPS)
    """
    try:
        dse_ldif = DSEldif(None, instance)
    except Exception:
        return (None, None)

    port = dse_ldif.get("cn=config", "nsslapd-port", single=True)
    secureport = dse_ldif.get("cn=config", "nsslapd-secureport", single=True)
    host = dse_ldif.get("cn=config", "nsslapd-localhost", single=True)
    sec = dse_ldif.get("cn=config", "nsslapd-security", single=True)
    ldapi_listen = dse_ldif.get("cn=config", "nsslapd-ldapilisten", single=True)
    ldapi_autobind = dse_ldif.get("cn=config", "nsslapd-ldapiautobind", single=True)
    ldapi_socket = dse_ldif.get("cn=config", "nsslapd-ldapifilepath", single=True)
    certdir = dse_ldif.get("cn=config", "nsslapd-certdir", single=True)

    if ldapi_listen is not None and ldapi_listen.lower() == "on" and \
       ldapi_autobind is not None and ldapi_autobind.lower() == "on" and \
       ldapi_socket is not None:
        # Use LDAPI
        socket = ldapi_socket.replace("/", "%2f")  # Escape the path
        return ("ldapi://" + socket, None)
    elif sec is not None and sec.lower() == "on" and secureport is not None:
        # Use LDAPS
        return ("ldaps://{}:{}".format(host, secureport), certdir)
    else:
        # Use LDAP
        return ("ldap://{}:{}".format(host, port), None)
