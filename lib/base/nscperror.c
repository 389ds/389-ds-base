/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* nscperrors.c
 * Very crude error handling for nspr and libsec.
 */

#include "netsite.h"

#define NSCP_NSPR_ERROR_BASE		(-6000)
#define NSCP_NSPR_MAX_ERROR             (NSCP_NSPR_ERROR_BASE + 29)
#define NSCP_LIBSEC_ERROR_BASE 		(-8192)
#define NSCP_LIBSEC_MAX_ERROR           (NSCP_LIBSEC_ERROR_BASE + 63)
#define NSCP_LIBSSL_ERROR_BASE 		(-12288)
#define NSCP_LIBSSL_MAX_ERROR           (NSCP_LIBSSL_ERROR_BASE + 19)

typedef struct nscp_error_t {
    int errorNumber;
    const char *errorString;
} nscp_error_t;

nscp_error_t nscp_nspr_errors[]  =  {
    {  0, "NSPR error" },
    {  1, "Out of memory" },
    {  2, "Bad file descriptor" },
    {  3, "Data temporarily not available" },
    {  4, "Access fault" },
    {  5, "Invalid method" },
    {  6, "Illegal access" },
    {  7, "Unknown error" },
    {  8, "Pending interrupt" },
    {  9, "Not implemented" },
    { 10, "IO error" },
    { 11, "IO timeout error" },
    { 12, "IO already pending error" },
    { 13, "Directory open error" },
    { 14, "Invalid Argument" },
    { 15, "Address not available" },
    { 16, "Address not supported" },
    { 17, "Already connected" },
    { 18, "Bad address" },
    { 19, "Address already in use" },
    { 20, "Connection refused" },
    { 21, "Network unreachable" },
    { 22, "Connection timed out" },
    { 23, "Not connected" },
    { 24, "Load library error" },
    { 25, "Unload library error" },
    { 26, "Find symbol error" },
    { 27, "Connection reset by peer" },
    { 28, "Range Error" },
    { 29, "File Not Found Error" }
};

nscp_error_t nscp_libsec_errors[] = {
    {  0, "SEC_ERROR_IO" },
    {  1, "SEC_ERROR_LIBRARY_FAILURE" },
    {  2, "SEC_ERROR_BAD_DATA" },
    {  3, "SEC_ERROR_OUTPUT_LEN" },
    {  4, "SEC_ERROR_INPUT_LEN" },
    {  5, "SEC_ERROR_INVALID_ARGS" },
    {  6, "SEC_ERROR_INVALID_ALGORITHM" },
    {  7, "SEC_ERROR_INVALID_AVA" },
    {  8, "SEC_ERROR_INVALID_TIME" },
    {  9, "SEC_ERROR_BAD_DER" },
    { 10, "SEC_ERROR_BAD_SIGNATURE" },
    { 11, "SEC_ERROR_EXPIRED_CERTIFICATE" },
    { 12, "SEC_ERROR_REVOKED_CERTIFICATE" },
    { 13, "SEC_ERROR_UNKNOWN_ISSUER" },
    { 14, "SEC_ERROR_BAD_KEY" },
    { 15, "SEC_ERROR_BAD_PASSWORD" },
    { 16, "SEC_ERROR_UNUSED" },
    { 17, "SEC_ERROR_NO_NODELOCK" },
    { 18, "SEC_ERROR_BAD_DATABASE" },
    { 19, "SEC_ERROR_NO_MEMORY" },
    { 20, "SEC_ERROR_UNTRUSTED_ISSUER" },
    { 21, "SEC_ERROR_UNTRUSTED_CERT" },
    { 22, "SEC_ERROR_DUPLICATE_CERT" },
    { 23, "SEC_ERROR_DUPLICATE_CERT_TIME" },
    { 24, "SEC_ERROR_ADDING_CERT" },
    { 25, "SEC_ERROR_FILING_KEY" },
    { 26, "SEC_ERROR_NO_KEY" },
    { 27, "SEC_ERROR_CERT_VALID" },
    { 28, "SEC_ERROR_CERT_NOT_VALID" },
    { 29, "SEC_ERROR_CERT_NO_RESPONSE" },
    { 30, "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE" },
    { 31, "SEC_ERROR_CRL_EXPIRED" },
    { 32, "SEC_ERROR_CRL_BAD_SIGNATURE" },
    { 33, "SEC_ERROR_CRL_INVALID" },
    { 34, "SEC_ERROR_" },
    { 35, "SEC_ERROR_" },
    { 36, "SEC_ERROR_" },
    { 37, "SEC_ERROR_" },
    { 38, "SEC_ERROR_" },
    { 39, "SEC_ERROR_" },
    { 40, "SEC_ERROR_" },
    { 41, "SEC_ERROR_" },
    { 42, "SEC_ERROR_" },
    { 43, "SEC_ERROR_" },
    { 44, "SEC_ERROR_" },
    { 45, "SEC_ERROR_" },
    { 46, "SEC_ERROR_" },
    { 47, "SEC_ERROR_" },
    { 48, "SEC_ERROR_" },
    { 49, "SEC_ERROR_" },
    { 50, "SEC_ERROR_" },
    { 51, "SEC_ERROR_" },
    { 52, "SEC_ERROR_" },
    { 53, "SEC_ERROR_" },
    { 54, "SEC_ERROR_" },
    { 55, "SEC_ERROR_" },
    { 56, "SEC_ERROR_" },
    { 57, "SEC_ERROR_" },
    { 58, "SEC_ERROR_" },
    { 59, "SEC_ERROR_" },
    { 60, "SEC_ERROR_" },
    { 61, "SEC_ERROR_" },
    { 62, "SEC_ERROR_" },
    { 63, "SEC_ERROR_NEED_RANDOM" }
};

nscp_error_t nscp_libssl_errors[] = {
    {  0, "SSL_ERROR_EXPORT_ONLY_SERVER" },
    {  1, "SSL_ERROR_US_ONLY_SERVER" },
    {  2, "SSL_ERROR_NO_CYPHER_OVERLAP" },
    {  3, "SSL_ERROR_NO_CERTIFICATE" },
    {  4, "SSL_ERROR_BAD_CERTIFICATE" },
    {  5, "unused SSL error #5" },
    {  6, "SSL_ERROR_BAD_CLIENT - the server has encountered bad data from the client." },
    {  7, "SSL_ERROR_BAD_SERVER" },
    {  8, "SSL_ERROR_UNSUPPORTED_CERTIFICATE_TYPE" },
    {  9, "SSL_ERROR_UNSUPPORTED_VERSION" },
    { 10, "unused SSL error #10" },
    { 11, "SSL_ERROR_WRONG_CERTIFICATE" },
    { 12, "SSL_ERROR_BAD_CERT_DOMAIN" },
    { 13, "SSL_ERROR_POST_WARNING" },
    { 14, "SSL_ERROR_SSL2_DISABLED" },
    { 15, "SSL_ERROR_BAD_MAC_READ - SSL has received a record with an incorrect Message Authentication Code." },
    { 16, "SSL_ERROR_BAD_MAC_ALERT - SSL has received an error indicating an incorrect Message Authentication Code." },
    { 17, "SSL_ERROR_BAD_CERT_ALERT - the server cannot verify your certificate." },
    { 18, "SSL_ERROR_REVOKED_CERT_ALERT - the server has rejected your certificate as revoked." },
    { 19, "SSL_ERROR_EXPIRED_CERT_ALERT - the server has rejected your certificate as expired." },
};

const char *
nscperror_lookup(int error)
{
    if ((error >= NSCP_NSPR_ERROR_BASE) && 
        (error <= NSCP_NSPR_MAX_ERROR)) {
        return nscp_nspr_errors[error-NSCP_NSPR_ERROR_BASE].errorString;
    } else if ((error >= NSCP_LIBSEC_ERROR_BASE) &&
        (error <= NSCP_LIBSEC_MAX_ERROR)) {
        return nscp_libsec_errors[error-NSCP_LIBSEC_ERROR_BASE].errorString;
    } else if ((error >= NSCP_LIBSSL_ERROR_BASE) &&
        (error <= NSCP_LIBSSL_MAX_ERROR)) {
        return nscp_libssl_errors[error-NSCP_LIBSSL_ERROR_BASE].errorString;
    } else {
        return (const char *)NULL;
    }
}
