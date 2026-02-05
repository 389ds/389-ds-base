# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import datetime
import os
import ldap
import logging
import re
import tempfile
from typing import Optional
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389.utils import cert_is_ca, pem_to_der, is_pem_cert, ensure_str
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

CA_NAME = 'Self-Signed-CA'
CERT_NAME = 'Server-Cert'

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

    def del_cert(self):
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
        :param ca: Whether this certificate is a CA certificate
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
                    cert_bytes, pkcs12_password.encode() if pkcs12_password else None,
                    backend=default_backend()
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

        if ca and not cert_is_ca(cert_file):
            raise ValueError(f"Certificate ({nickname}) is not a CA certificate")

        attrs = {
            "cn": [nickname.encode()],
            "objectClass": [b"top", b"extensibleObject"],
            DYCATTR_CERTDER: [ der_cert, ]
        }
        if der_privkey:
            attrs[DYCATTR_PKEYDER] = [der_privkey]

        if ca:
            attrs[DYCATTR_TRUST] = [b"CT,,"]

        if force:
            attrs[DYCATTR_FORCE] = [b"TRUE"]

        if not der_cert:
            raise ValueError(f"Failed to extract DER bytes from {cert_file}")

        # Escape the CN to handle special chars in the nickname
        escaped_cn = ldap.dn.escape_dn_chars(nickname)
        dn = ensure_str(f"cn={escaped_cn},{self._basedn}")
        # Raw CN used for lookup
        cert_obj = self.get_cert_obj(nickname)
        if not cert_obj:
            cert_obj = DynamicCert(self._instance, dn)
            cert_obj.create(properties=attrs)
        else:
            log.info(f"Updating existing certificate; {nickname}")
            attrs_list = [(attr, vals) for attr, vals in attrs.items()]
            cert_obj.replace_many(*attrs_list)

    def add_ca_cert(self,
                    cert_file: str,
                    nickname: str,
                    pkcs12_password: Optional[str] = None,
                    force: bool = False
        ):
        """
        Add a CA certificate from a PEM bundle or single PEM/DER file.

        :param cert_file: Path to the certificate file (PEM or DER)
        :param nickname: Certificate nickname
        :param pkcs12_password: Password for PKCS#12, if any
        :param force: Force the addition of a certificate that cannot be verified
        :raises ValueError: If file is invalid or file does not exist
        """
        if not os.path.exists(cert_file):
            raise ValueError(f"Certificate file does not exist: {cert_file}")

        # Normalise nickname(s)
        if isinstance(nickname, str):
            nicknames = [nickname]
        elif isinstance(nickname, list):
            nicknames = nickname
        else:
            raise TypeError(f"nickname must be str or list[str], got {type(nickname)}")

        # PEM (may be bundle)
        if cert_file.lower().endswith(".pem"):
            with open(cert_file, "r") as f:
                pem_data = f.read()

            pem_certs = re.findall(
                r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
                pem_data,
                re.DOTALL
            )

            if not pem_certs:
                raise ValueError("No certificates found in PEM file")

            temp_files = []
            try:
                for idx, cert in enumerate(pem_certs):
                    with tempfile.NamedTemporaryFile(delete=False, suffix=".pem", mode="w") as tmp:
                        tmp.write(cert.strip() + "\n")
                        tmp_cert_path = tmp.name
                    temp_files.append(tmp_cert_path)

                    # Determine nickname, could be a list, or not enough
                    if idx < len(nicknames):
                        ca_nick = nicknames[idx]
                    else:
                        ca_nick = f"{nicknames[-1]}{idx}"

                    # Check we dont trample over installer certs
                    if ca_nick.lower() in (CERT_NAME.lower(), CA_NAME.lower()):
                        raise ValueError(f"You may not import a CA with the nickname {CERT_NAME} or {CA_NAME}")

                    if not cert_is_ca(tmp_cert_path):
                        raise ValueError(f"Certificate ({ca_nick}) is not a CA certificate")

                    try:
                        # Handle existing LDAP object
                        if self.get_cert_details(ca_nick):
                            if not force:
                                raise ValueError(
                                    f"Certificate already exists with the same name ({ca_nick})"
                                )
                            else:
                                log.info(f"Overwriting existing CA cert {ca_nick}")
                                self.del_cert(ca_nick)
                    except ValueError:
                        pass

                    self.add_cert(
                        tmp_cert_path,
                        ca_nick,
                        pkcs12_password=pkcs12_password,
                        ca=True,
                        force=force
                    )

            finally:
                for tmp_file in temp_files:
                    try:
                        os.remove(tmp_file)
                    except OSError as e:
                        log.debug(f"Failed to remove tmp cert file: {tmp_file}: {e}")
        else:
            # Single binary cert
            if len(nicknames) != 1:
                raise ValueError("Single cert requires exactly one nickname")

            ca_nick = nicknames[0]
            try:
                if self.get_cert_details(ca_nick):
                    if force:
                        self.del_cert(ca_nick)
                    else:
                        raise ValueError(f"Certificate already exists: {ca_nick}")
            except ValueError:
                pass

            self.add_cert(cert_file, ca_nick, pkcs12_password=pkcs12_password, ca=True, force=force)

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

        cert_obj.del_cert()

    def list_certs(self):
        """
        List all server certificates.

        :return: A list of certificate dictionaries for each certificate
        """
        cert_objects = self.list()
        certs = []
        for cert in cert_objects:
            if cert._dn == self._basedn:
                continue
            der_cert = cert.get_attr_vals_bytes(DYCATTR_CERTDER)[0]
            if der_cert:
                if not cert_is_ca(der_cert):
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
            if cert._dn == self._basedn:
                continue
            der_cert = cert.get_attr_vals_bytes(DYCATTR_CERTDER)[0]
            if der_cert:
                if cert_is_ca(der_cert):
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
