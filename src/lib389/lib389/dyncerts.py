# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import datetime
import ldap
import ldap.sasl
import logging
from typing import Optional, Union
from lib389.utils import get_ldapurl_from_serverid

log = logging.getLogger(__name__)

DYCATTR_CN    = "cn"
DYNCERT_SUFFIX      = "cn=dynamiccertificates"

DYCATTR_PREFIX      = "dsdynamiccertificate"
DYCATTR_CERTDER     = DYCATTR_PREFIX + "der"
DYCATTR_PKEYDER     = DYCATTR_PREFIX + "privatekeyder"
DYCATTR_SUBJECT     = DYCATTR_PREFIX + "subject"
DYCATTR_ISSUER      = DYCATTR_PREFIX + "issuer"
DYCATTR_IS_CA       = DYCATTR_PREFIX + "isca"
DYCATTR_IS_ROOT_CA  = DYCATTR_PREFIX + "isrootca"
DYCATTR_IS_SERVER   = DYCATTR_PREFIX + "isservercert"
DYCATTR_HAS_PKEY    = DYCATTR_PREFIX + "hasprivatekey"
DYCATTR_TRUST       = DYCATTR_PREFIX + "trustflags"
DYCATTR_TYPE        = DYCATTR_PREFIX + "type"
DYCATTR_TOKEN       = DYCATTR_PREFIX + "tokenname"
DYCATTR_KALGO       = DYCATTR_PREFIX + "keyalgorithm"
DYCATTR_NOTBEFORE   = DYCATTR_PREFIX + "notbefore"
DYCATTR_NOTAFTER    = DYCATTR_PREFIX + "notafter"
DYCATTR_FORCE	    = DYCATTR_PREFIX + "Force"

class DynamicCerts:
    """
    Handles certificate operations via the DynamicCert LDAP backend.
    """

    def __init__(self, dirsrv) -> None:
        self.dirsrv = dirsrv
        self.conn = None

    def _ensure_ldapi_connection(self):
        """
        Ensure LDAP connection is initialized and bound using SASL EXTERNAL.

        Returns:
            None
        """
        if self.conn is not None:
            return

        try:
            ldapiurl, _ = get_ldapurl_from_serverid(self.dirsrv.serverid)
            self.conn = ldap.initialize(ldapiurl)
            self.conn.protocol_version = 3
            self.conn.sasl_interactive_bind_s("", ldap.sasl.external())
            log.debug(f"DynamicCert connected via LDAPI: {ldapiurl}")
        except ldap.LDAPError as e:
            self.conn = None
            raise RuntimeError(f"Failed to connect via LDAPI: ({ldapiurl}: {e})"
        )

    def _read_file_or_bytes(self, data_or_path: Union[bytes, str]):
        """
        Read content from file path or return bytes directly.

        Returns:
            bytes: Binary data read from the file or provided directly.
        """
        if data_or_path is None:
            return None

        if isinstance(data_or_path, (bytes, bytearray)):
            return bytes(data_or_path)

        try:
            with open(data_or_path, "rb") as f:
                return f.read()
        except OSError as e:
            raise ValueError(f"Failed to read file: {data_or_path}: {e}")

    def _normalise_dyncert_timestamp(self, raw):
        """
        Convert a DynamicCert timestamp (NSPR PRExplodedTime format) to
        an ISO datetime string. Example: 20270006181934Z

        Returns:
            str: Timestamp in YYYY-MM-DD HH:MM:SS format, else orig string.
        """
        try:
            year   = int(raw[0:4])
            month  = int(raw[4:6]) + 1  # PRExplodedTime.tm_month from NSPR is 0–11
            day    = int(raw[6:8])
            hour   = int(raw[8:10])
            minute = int(raw[10:12])
            second = int(raw[12:14])

            # log.info(f"year:{year} month:{month} day:{day} hour:{hour} minute:{minute} second:{second}")
            not_after = datetime.datetime(year, month, day, hour, minute, second).strftime("%Y-%m-%d %H:%M:%S")
        except Exception:
            not_after = raw

        return not_after

    def _normalise_dyncert_trustflags(self, raw):
        """
        Convert DynamicCert trust flags to NSS style.

        Returns:
            str: Trust flags in NSS format, 'u' replaced by empty fields.
        """
        if not raw:
            return ",,"

        parts = raw.split(",")
        result = []
        for p in parts:
            if p == "u":
                result.append("")
            else:
                result.append(p)

        return ",".join(result)

    def is_online(self):
        """
        Check if the DynamicCert backend is reachable over LDAPI.

        Returns:
            True is backend is online, else False
        """
        try:
            self._ensure_ldapi_connection()
            self.conn.search_s(DYNCERT_SUFFIX, ldap.SCOPE_ONELEVEL,
                               "(objectClass=*)", attrlist=[DYCATTR_CN])
            return True
        except ldap.NO_SUCH_OBJECT:
            log.warning(f"DynamicCert base DN {DYNCERT_SUFFIX} does not exist")
            return False
        except ldap.LDAPError as e:
            log.error(f"DynamicCert backend not reachable: {e}")
            return False

    def list_certs(self):
        """
        List all certificates in the dynamiccertificates subtree.

        Returns:
            List of tuples: (cn, subject, issuer, notAfter, trust flags)
        """
        self._ensure_ldapi_connection()

        try:
            results = self.conn.search_s(DYNCERT_SUFFIX, ldap.SCOPE_ONELEVEL,
                                         "(objectClass=extensibleObject)")
        except ldap.LDAPError as e:
            log.error(f"Failed to list certificates: {e}")
            return []

        certs = []
        for _, entry in results:
            cn = entry.get(DYCATTR_CN, [b""])[0].decode()
            subject = entry.get(DYCATTR_SUBJECT, [b""])[0].decode()
            issuer = entry.get(DYCATTR_ISSUER, [b""])[0].decode()
            # Convert LDAP timestamp to NSS format
            not_after_str = entry.get(DYCATTR_NOTAFTER, [b""])[0].decode()
            not_after = self._normalise_dyncert_timestamp(not_after_str)
            # Convert LDAP trust flags to NSS format
            trust_flags_str = entry.get(DYCATTR_TRUST, [b""])[0].decode()
            trust_flags = self._normalise_dyncert_trustflags(trust_flags_str)
            certs.append((cn, subject, issuer, not_after, trust_flags))

        return certs

    def get_cert_details(self, nickname: str):
        """
        Get certificate details for a given CN.

        Returns:
            dict if certificate info on success
            None if certificate does not exist
        """
        self._ensure_ldapi_connection()
        dn = f"cn={nickname},{DYNCERT_SUFFIX}"

        try:
            result = self.conn.search_s(dn, ldap.SCOPE_BASE)
            if not result:
                return None

            _, entry = result[0]

            cn = entry.get(DYCATTR_CN, [b""])[0].decode()
            subject = entry.get(DYCATTR_SUBJECT, [b""])[0].decode()
            issuer = entry.get(DYCATTR_ISSUER, [b""])[0].decode()

            # Convert LDAP timestamp to NSS format
            not_after_str = entry.get(DYCATTR_NOTAFTER, [b""])[0].decode()
            not_after = self._normalise_dyncert_timestamp(not_after_str)

            # Convert LDAP trust flags to NSS format
            trust_flags_str = entry.get(DYCATTR_TRUST, [b""])[0].decode()
            trust_flags = self._normalise_dyncert_trustflags(trust_flags_str)

            return [cn, subject, issuer, not_after, trust_flags]

        except ldap.NO_SUCH_OBJECT:
            log.debug(f"Certificate {nickname} does not exist in DynamicCert")
            return None
        except ldap.LDAPError as e:
            log.error(f"Error fetching certificate details: {e}")
            raise

    def add_cert(self, cn: str,
                 cert_file: Union[bytes, str],
                 privkey_file: Union[bytes, str] = None,
                 is_ca: bool = False,
                 force: bool = False):
        """
        Add or replace a certificate in DynamicCerts backend.

        Args:
            cn: Certificate CN
            cert_file: DER cert bytes or file path
            privkey_file: DER private key bytes or file path
            is_ca: Whether this is a CA cert

        Returns:
            None
        """
        if not cn:
            raise ValueError("Certificate CN cannot be empty")

        self._ensure_ldapi_connection()
        dn = f"cn={cn},{DYNCERT_SUFFIX}"

        der_cert = self._read_file_or_bytes(cert_file)
        if not der_cert:
            raise ValueError("No certificate data found")

        der_privkey = self._read_file_or_bytes(privkey_file) if privkey_file else None

        attrs = [
            ("objectClass", [b"top", b"extensibleObject"]),
            (DYCATTR_CN, [cn.encode(encoding="utf-8")]),
            (DYCATTR_CERTDER, [der_cert]),
        ]

        if der_privkey:
            attrs.append((DYCATTR_PKEYDER, [der_privkey]))

        if is_ca:
            attrs.append((DYCATTR_TRUST, [b"CT,,"]))

        if force:
            attrs.append((DYCATTR_FORCE, [b"TRUE"]))

        try:
            self.conn.add_s(dn, attrs)
            log.info(f"Added {dn} to DynamicCert backend")
            return
        except ldap.ALREADY_EXISTS:
            pass

        mods = [
            (ldap.MOD_REPLACE, DYCATTR_CERTDER, [der_cert]),
            (ldap.MOD_REPLACE, DYCATTR_CN, [cn.encode("utf-8")]),
        ]

        if der_privkey is not None:
            mods.append((ldap.MOD_REPLACE, DYCATTR_PKEYDER, [der_privkey]))
        else:
            mods.append((ldap.MOD_DELETE, DYCATTR_PKEYDER, None))

        if is_ca:
            mods.append((ldap.MOD_REPLACE, DYCATTR_TRUST, [b"CT,,"]))
        else:
            mods.append((ldap.MOD_DELETE, DYCATTR_TRUST, [b",,"]))

        try:
            self.conn.modify_s(dn, mods)
            log.info(f"Replaced certificate {cn} in DynamicCert")
        except ldap.LDAPError as e:
            log.error(f"Failed to update certificate: {e}")
            raise

    def del_cert(self, cn: str):
        """
        Delete a certificate by CN.

        Returns:
            None
        """
        self._ensure_ldapi_connection()
        dn = f"cn={cn},{DYNCERT_SUFFIX}"

        try:
            self.conn.delete_s(dn)
            log.info(f"Deleted certificate: {cn} from DynamicCerts backend: {dn}")
        except ldap.NO_SUCH_OBJECT:
            log.warning(f"Certificate: {cn} does not exist in DynamicCerts backend: {dn}")
        except ldap.LDAPError as e:
            log.error(f"Failed to delete certificate: {cn}: {e}")
            raise
