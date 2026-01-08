# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import logging

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.serialization import pkcs12

from lib389.nss_ssl import NssSsl
from lib389.dyncerts import DynamicCerts
from lib389.config import RSA

log = logging.getLogger(__name__)

def _pem_to_der(blob: bytes):
    """
    Convert PEM certificate bytes to DER format.

    Returns:
        DER encoded certificate bytes
    """
    cert = x509.load_pem_x509_certificate(blob)
    return cert.public_bytes(serialization.Encoding.DER)

def _is_pem_cert(blob: bytes):
    """
    Check if the given blob is a PEM certificate.

    Returns:
        True if blob contains PEM certificate markers, else False
    """
    return b"-----BEGIN CERTIFICATE-----" in blob


class CertManager:
    """
    Certificate manager for 389 Directory Server.

    Automatically selects DynamicCert backend if available via LDAPI,
    otherwise falls back to NSS DB.
    """

    def __init__(self, dirsrv, backend=None):
        """
        Init the certificate manager.
        """
        self.dirsrv = dirsrv

        if backend is not None:
            if isinstance(backend, type):
                self.backend = backend(dirsrv=self.dirsrv)
            else:
                self.backend = backend
            log.info(f"Using forced backend: {type(self.backend).__name__}")
        else:
            dyncert = DynamicCerts(dirsrv=self.dirsrv)
            if dyncert.is_online():
                self.backend = dyncert
                log.info("DynamicCert backend is online")
            else:
                if dirsrv is None:
                    raise ValueError("dirsrv instance is required for NSS fallback")
                self.backend = NssSsl(dirsrv=self.dirsrv)
                log.info("DynamicCert offline, using NSS backend")

        self.backend_name = type(self.backend).__name__

    def list_certs(self):
        """
        Return a list of all certificates in the backend.

        Returns:
            List of tuples (nickname, subject, issuer, expires, trust flags)
        """
        return self.backend.list_certs()

    def get_cert_details(self, nickname: str):
        """
        Get certificate details by nickname/CN.

        Returns:
            dict containing certificate details (CN, subject, issuer, notBefore, notAfter, trustFlags)
            None if certificate does not exist
        """
        try:
            return self.backend.get_cert_details(nickname=nickname)
        except ValueError as e:
            if "not found" in str(e).lower():
                return None
            raise

    def del_cert(self, name: str):
        """
        Delete a certificate from the backend.

        Returns:
            None
        """
        return self.backend.del_cert(name)

    def add_cert(self, file_path: str, nickname: str,
                 pkcs12_file: bool = False, pkcs12_password: str = None,
                 primary: bool = False, ca: bool = False, force: bool = False):
        """
        Add or replace a certificate in the backend (DynamicCert or NSS).

        Args:
            file_path: Path to certificate file (PEM, DER, or PKCS#12)
            nickname: Certificate nickname/CN.
            pkcs12_file: True if file_path points to a PKCS#12.
            pkcs12_password: Password for PKCS#12, if any.
            primary: Set as primary SSL personality.
            ca: Whether this certificate is a CA certificate.

        Returns:
            None
        """
        if not os.path.isfile(file_path):
            raise ValueError(f"Certificate file not found: {file_path}")

        if not nickname:
            raise ValueError("Certificate nickname must not be empty")

        if isinstance(self.backend, DynamicCerts):
            der_cert: bytes = None
            der_privkey: bytes = None

            # p12
            if pkcs12_file:
                with open(file_path, "rb") as f:
                    p12_data = f.read()

                try:
                    privkey, cert, _ = pkcs12.load_key_and_certificates(
                        p12_data,
                        pkcs12_password.encode() if pkcs12_password else None
                    )
                except Exception as e:
                    raise ValueError(f"Failed to load PKCS#12 file: {file_path} {e}")

                if cert is None:
                    raise ValueError("PKCS#12 contains no certificate")

                der_cert = cert.public_bytes(serialization.Encoding.DER)

                if privkey is not None:
                    der_privkey = privkey.private_bytes(
                        encoding=serialization.Encoding.DER,
                        format=serialization.PrivateFormat.PKCS8,
                        encryption_algorithm=serialization.NoEncryption()
                    )
            else:
                # PEM/DER
                with open(file_path, "rb") as f:
                    blob = f.read()

                if _is_pem_cert(blob):
                    der_cert = _pem_to_der(blob)
                else:
                    der_cert = blob

            if der_cert is None:
                raise ValueError("Failed to load certificate data")

            # DynamicCerts
            self.backend.add_cert(
                cn=nickname,
                cert_file=der_cert,
                privkey_file=der_privkey,
                is_ca=ca,
                force=force
            )
        else:
            # NSS
            self.backend.add_cert(
                nickname=nickname,
                input_file=file_path,
                ca=ca,
                pkcs12_file=pkcs12_file,
                pkcs12_password=pkcs12_password
            )

        if primary:
            try:
                RSA(self.dirsrv).set("nsSSLPersonalitySSL", nickname)
                log.info(f"Set certificate: {nickname} as primary SSL cert.")
            except Exception as e:
                log.error(f"Failed to set primary SSL cert: {nickname} {e}")
                raise

    def edit_cert_trust(self, nickname: str, trust_flags: str):
        """
        Edit trust flags on an existing certificate.

        Args:
            nickname: Certificate nickname/CN
            trust_flags: NSS style trust flag triplet, e.g. 'CT,,'
        """
        return self.backend.edit_cert_trust(nickname, trust_flags)
