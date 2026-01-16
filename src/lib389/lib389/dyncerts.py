# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import datetime
import os
import logging
from typing import Optional
import ldap
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389.utils import cert_is_ca, pem_to_der, is_pem_cert
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.serialization import pkcs12

log = logging.getLogger(__name__)

DYCATTR_CN    = "cn"
DYNCERT_SUFFIX = "cn=dynamiccertificates"

DYCATTR_PREFIX      = "dsdynamiccertificate"
DYCATTR_CERTDER     = DYCATTR_PREFIX + "der"
DYCATTR_PKEYDER     = DYCATTR_PREFIX + "privatekeyder"
DYCATTR_SUBJECT     = DYCATTR_PREFIX + "subject"
DYCATTR_ISSUER      = DYCATTR_PREFIX + "issuer"
DYCATTR_TRUST       = DYCATTR_PREFIX + "trustflags"
DYCATTR_NOTAFTER    = DYCATTR_PREFIX + "notafter"
DYCATTR_FORCE	    = DYCATTR_PREFIX + "Force"
DYCATTR_ISCA        = DYCATTR_PREFIX + "IsCA"

class DynamicCert(DSLdapObject):
    """
    Represents a single DynamicCert LDAP entry.
    """

    _must_attributes = [DYCATTR_CN]

    def __init__(self, instance, dn: Optional[str] = None):
        """
        Initialise a DynamicCert object.

        :param instance: DirSrv instance
        :param dn: Entry distinguished name (DN)
        """
        super(DynamicCert, self).__init__(instance, dn)
        self._rdn_attribute = DYCATTR_CN
        self._create_objectclasses = ["top", "extensibleObject"]
        self._protected = False
        self._basedn = DYNCERT_SUFFIX

    def _normalise_timestamp(self, raw: str):
        """
        Convert DynamicCert timestamp to NSS like format.

        :param raw: Raw DynamicCert timestamp (e.g. 20260109181934Z)
        :return: Formatted timestamp as "YYYY-MM-DD HH:MM:SS", or original string
        """
        try:
            year   = int(raw[0:4])
            month  = int(raw[4:6]) + 1  # PRExplodedTime.tm_month is 0–11
            day    = int(raw[6:8])
            hour   = int(raw[8:10])
            minute = int(raw[10:12])
            second = int(raw[12:14])
            return datetime.datetime(year, month, day, hour, minute, second).strftime("%Y-%m-%d %H:%M:%S")
        except Exception:
            return raw

    def is_ca(self):
        """
        Determine if this certificate is a CA.

        :return: True if certificate is a CA, else False
        """
        # Check trust flags first
        trust_flags = self.get_attr_val_utf8(DYCATTR_TRUST)
        if trust_flags:
            try:
                ssl_field, email_field, object_field = trust_flags.split(",")
                if "C" in ssl_field:
                    return True
            except Exception:
                pass

        return False

    def delete_cert(self):
        """
        Delete this DynamicCert entry from LDAP.

        :raises ValueError: If the DynamicCert object does not have a DN
        :raises ldap.LDAPError: If an LDAP operation fails (other than NO_SUCH_OBJECT)
        """
        if not self._dn:
            raise ValueError("Cannot delete DynamicCert without a DN")

        try:
            self.delete()
        except ldap.NO_SUCH_OBJECT:
            log.warning(f"DynamicCert already deleted: {self._dn}")
        except ldap.LDAPError as e:
            log.error(f"Failed to delete DynamicCert: {self._dn}: {e}")
            raise

    def edit_trust(self, trust_flags: str):
        """
        Edit certificate trust flags.

        :param trust_flags: Comma separated trust flags string (SSL,Email,ObjectSigning)
        :raises ValueError: If trust flags are invalid or empty
        """
        if not trust_flags:
            raise ValueError("Trust flags cannot be empty")

        trust_fields = trust_flags.strip().split(",")

        if len(trust_fields) != 3:
            raise ValueError("Trust flags must have 3 comma separated fields")

        # Allowed field values (NSS)
        valid_flags = set("pPcCTu")
        for field in trust_fields:
            if field and any(flag not in valid_flags for flag in field):
                raise ValueError(f"Invalid characters in trust flags: '{trust_flags}'")

        try:
            self.replace(DYCATTR_TRUST, trust_flags)
        except ldap.LDAPError as e:
            log.error(f"Failed to update trust flags for {self._dn}: {e}")
            raise

        # log.info(f"Updated trust flags for {self._dn} to '{trust_flags}'")


class DynamicCerts(DSLdapObjects):
    """
    Collection of DynamicCert entries under cn=dynamiccertificates.
    """

    def __init__(self, instance):
        """
        Initialise the DynamicCerts collection.

        :param instance: DirSrv instance
        """
        super(DynamicCerts, self).__init__(instance=instance)
        self._objectclasses = ["extensibleObject"]
        self._filterattrs = [DYCATTR_CN, DYCATTR_SUBJECT, DYCATTR_ISSUER]
        self._childobject = DynamicCert
        self._basedn = DYNCERT_SUFFIX

    def is_online(self):
        """
        Check if the DynamicCert LDAP backend is online.

        returns: True if accessible, else False
        """
        try:
            return self.exists(dn=self._basedn)
        except ldap.LDAPError as e:
            log.warning(f"DynamicCert backend not reachable: {e}")
            return False
        except Exception as e:
            log.warning(f"DynamicCert backend check failed: {e}")
            return False

    def add_cert(self,
                 cert_file: str,
                 nickname: str,
                 pkcs12_password: Optional[str] = None,
                 ca: bool = False,
                 force: bool = False,
    ):
        """
        Add or update a certificate (PEM, DER, or PKCS#12).

        :param cert_file: Path to certificate file (PEM, DER, or PKCS#12)
        :param nickname: Certificate nickname
        :param pkcs12_password: Password for PKCS#12, if any
        :param ca: Set CA trust flag
        :param force: Force the addition of a certificate that cannot be verified
        """
        if not nickname:
            raise ValueError("Certificate CN cannot be empty")

        if not os.path.isfile(cert_file):
            raise ValueError(f"Certificate file does not exist: {cert_file}")

        if pkcs12_password and not isinstance(pkcs12_password, str):
            raise TypeError("PKCS#12 password must be a string")

        with open(cert_file, "rb") as f:
            cert_bytes = f.read()

        der_cert = None
        der_privkey = None
        if cert_file.lower().endswith((".p12", ".pfx")):
            try:
                privkey, cert, _ = pkcs12.load_key_and_certificates(
                    cert_bytes, pkcs12_password.encode() if pkcs12_password else None
                )
            except Exception as e:
                raise ValueError(f"Failed to load PKCS#12 file: {cert_file}: {e}")

            if cert is None:
                raise ValueError("PKCS#12 file contains no certificate")

            der_cert = cert.public_bytes(serialization.Encoding.DER)
            if privkey is not None:
                der_privkey = privkey.private_bytes(
                    encoding=serialization.Encoding.DER,
                    format=serialization.PrivateFormat.PKCS8,
                    encryption_algorithm=serialization.NoEncryption()
                )
        else:
            try:
                der_cert = pem_to_der(cert_bytes) if is_pem_cert(cert_bytes) else cert_bytes
            except Exception as e:
                raise ValueError(f"Failed to parse certificate '{cert_file}': {e}")

        dn = f"cn={nickname},{self._basedn}"
        attrs = {
            "cn": [nickname.encode()],
            "objectClass": [b"top", b"extensibleObject"],
            DYCATTR_CERTDER: [der_cert]
        }
        if der_privkey:
            attrs[DYCATTR_PKEYDER] = [der_privkey]

        if ca:
            if not cert_is_ca(cert_file):
                raise ValueError(f"Certificate ({nickname}) is not a CA certificate")
            attrs[DYCATTR_TRUST] = [b"CT,,"]

        if force:
            attrs[DYCATTR_FORCE] = [b"TRUE"]

        cert_obj = self.get_cert_obj(nickname)
        if not cert_obj:
            cert_obj = DynamicCert(self._instance, dn)
            cert_obj.create(properties=attrs)
        else:
            attrs_list = [(attr, vals) for attr, vals in attrs.items()]
            cert_obj.replace_many(*attrs_list)


    def add_ca_cert(self,
                    cert_file: str,
                    nickname: str,
                    pkcs12_password: Optional[str] = None,
                    force: bool = False
        ):
        """
        Add a CA certificate from a PEM or DER file.

        :param cert_file: Path to the certificate file (PEM or DER)
        :param nickname: Certificate nickname
        :param force: Force the addition of a certificate that cannot be verified
        :raises ValueError: If file is invalid or file does not exist
        """
        if not os.path.exists(cert_file):
            raise ValueError(f"Certificate file does not exist: {cert_file}")

        if not cert_is_ca(cert_file):
            raise ValueError(f"Certificate ({nickname}) is not a CA certificate")

        self.add_cert(cert_file=cert_file, nickname=nickname, pkcs12_password=pkcs12_password, ca=True, force=force)

    def del_cert(self, nickname: str):
        """
        Delete a certificate.

        :param nickname: The certificate nickname to delete.
        :raises ValueError: If the nickname is empty or the entry cannot be found.
        """
        if not nickname:
            raise ValueError("Certificate nickname cannot be empty")

        cert_obj = self.get_cert_obj(nickname)
        if not cert_obj:
            raise ValueError(f"Certificate entry not found: {nickname}")

        cert_obj.delete_cert()

    def list_certs(self):
        """
        List all server certificates.

        :return: A list of certificate dictionaries for each certificate
        """
        cert_objects = self.list()
        certs = []
        for cert in cert_objects:
            if cert._dn != self._basedn and not cert.is_ca():
                certs.append({
                    "cn": cert.get_attr_vals_utf8(DYCATTR_CN)[0],
                    "subject": cert.get_attr_vals_utf8(DYCATTR_SUBJECT)[0],
                    "issuer": cert.get_attr_vals_utf8(DYCATTR_ISSUER)[0],
                    "expires": cert._normalise_timestamp(cert.get_attr_vals_utf8(DYCATTR_NOTAFTER)[0]),
                    "trust_flags": cert.get_attr_vals_utf8(DYCATTR_TRUST)[0],
                })
        return certs

    def list_ca_certs(self):
        """
        List all ca certificates.

        :return: A list of certificate dictionaries for each certificate
        """
        cert_objects = self.list()
        certs = []
        for cert in cert_objects:
            if cert._dn != self._basedn and cert.is_ca():
                certs.append({
                    "cn": cert.get_attr_vals_utf8(DYCATTR_CN)[0],
                    "subject": cert.get_attr_vals_utf8(DYCATTR_SUBJECT)[0],
                    "issuer": cert.get_attr_vals_utf8(DYCATTR_ISSUER)[0],
                    "expires": cert._normalise_timestamp(cert.get_attr_vals_utf8(DYCATTR_NOTAFTER)[0]),
                    "trust_flags": cert.get_attr_vals_utf8(DYCATTR_TRUST)[0],
                })
        return certs

    def get_cert_obj(self, nickname: str):
        """
        Retrieve a certificate object.

        :param cn: Certificate nickname
        :raises ValueError: If the cn is empty
        :return: DynamicCert object if found, else None
        """
        if not nickname:
            raise ValueError("Certificate CN cannot be empty")
        try:
            cert = self.get(nickname)
        except ldap.NO_SUCH_OBJECT:
            return None

        return cert

    def get_cert_details(self, nickname: str):
        """
        Get a certificates details.

        :param nickname: Certificate nickname
        :raises ValueError: If the nickname is empty
        :return: DynamicCert object if found, else None
        """
        if not nickname:
            raise ValueError("Certificate CN cannot be empty")
        try:
            cert = self.get(nickname)
        except ldap.NO_SUCH_OBJECT:
            return None

        return {
            "cn": cert.get_attr_vals_utf8(DYCATTR_CN)[0],
            "subject": cert.get_attr_vals_utf8(DYCATTR_SUBJECT)[0],
            "issuer": cert.get_attr_vals_utf8(DYCATTR_ISSUER)[0],
            "expires": cert._normalise_timestamp(cert.get_attr_vals_utf8(DYCATTR_NOTAFTER)[0]),
            "trust_flags": cert.get_attr_vals_utf8(DYCATTR_TRUST)[0],
        }

    def edit_cert_trust(self, nickname: str, trust_flags: str):
        """
        Edit trust flags on an existing certificate.

        :param nickname: Certificate nickname
        :param trust_flags: 3 field NSS trust string
        """
        cert_obj = self.get_cert_obj(nickname)
        if not cert_obj:
            raise ValueError(f"Certificate {nickname} does not exist")

        try:
            cert_obj.edit_trust(trust_flags=trust_flags)
        except ValueError as ve:
            log.error(f"Invalid input for certificate '{nickname}': {ve}")
            raise
        except ldap.LDAPError as le:
            log.error(f"LDAP error while updating certificate '{nickname}': {le}")
            raise
        except Exception as e:
            log.error(f"Unexpected error while updating certificate '{nickname}': {e}")
            raise
