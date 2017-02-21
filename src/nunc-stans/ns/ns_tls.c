/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2015  Red Hat
 * see files 'COPYING' and 'COPYING.openssl' for use and warranty
 * information
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Additional permission under GPLv3 section 7:
 * 
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL, or a modified version of OpenSSL licensed
 * under the OpenSSL license
 * (https://www.openssl.org/source/license.html), the licensors of this
 * Program grant you additional permission to convey the resulting
 * work. Corresponding Source for a non-source form of such a
 * combination shall include the source code for the parts that are
 * licensed under the OpenSSL license as well as that of the covered
 * work.
 * --- END COPYRIGHT BLOCK ---
 */
#include "nspr.h"
#include "prio.h"
#include "secitem.h"
#include "nss.h"
#include "ssl.h"
#include "cert.h"
#include "pk11pub.h"
#include "keyhi.h"

#include "ns_tls.h"
#include <syslog.h>
#include "ns_private.h"

struct ns_sec_ctx_t {
    NSSInitContext *initctx;
    NSSInitParameters init_params;
    PRFileDesc *model_fd;
    PRBool is_client;
};

typedef struct ns_sec_ctx_t ns_sec_ctx_t;

static void
errExit(const char *msg)
{
    ns_log(LOG_ERR, msg);
}

static PRFileDesc *
create_tls_model_sock(const char *certname, PRBool is_client)
{
    PRFileDesc *model_sock = NULL;
    int rv;
    SECStatus secStatus;
    PRBool disableSSL2     = PR_TRUE;
    PRBool disableSSL3     = PR_FALSE;
    PRBool disableTLS      = PR_FALSE;
    PRBool disableRollBack = PR_FALSE;
    PRBool NoReuse         = PR_FALSE;
    PRBool disableStepDown = PR_FALSE;
    PRBool bypassPKCS11    = PR_FALSE;
    PRBool disableLocking  = PR_FALSE;
    PRBool enableFDX       = PR_FALSE;
    PRBool enableSessionTickets = PR_FALSE;
    PRBool enableCompression    = PR_FALSE;
    SSLKEAType certKEA;

    model_sock = PR_NewTCPSocket();
    if (model_sock == NULL) {
        errExit("PR_NewTCPSocket on model socket");
    }
    model_sock = SSL_ImportFD(NULL, model_sock);
    if (model_sock == NULL) {
        errExit("SSL_ImportFD");
    }

    /* do SSL configuration. */
    rv = SSL_OptionSet(model_sock, SSL_SECURITY,
        !(disableSSL2 && disableSSL3 && disableTLS));
    if (rv != SECSuccess) {
	errExit("SSL_OptionSet SSL_SECURITY");
    }

    rv = SSL_OptionSet(model_sock, SSL_ENABLE_SSL3, !disableSSL3);
    if (rv != SECSuccess) {
	errExit("error enabling SSLv3 ");
    }

    rv = SSL_OptionSet(model_sock, SSL_ENABLE_TLS, !disableTLS);
    if (rv != SECSuccess) {
	errExit("error enabling TLS ");
    }

    rv = SSL_OptionSet(model_sock, SSL_ENABLE_SSL2, !disableSSL2);
    if (rv != SECSuccess) {
	errExit("error enabling SSLv2 ");
    }
    
    rv = SSL_OptionSet(model_sock, SSL_ROLLBACK_DETECTION, !disableRollBack);
    if (rv != SECSuccess) {
	errExit("error enabling RollBack detection ");
    }

    rv = SSL_OptionSet(model_sock, SSL_HANDSHAKE_AS_CLIENT, is_client);
    if (rv != SECSuccess) {
	errExit("error handshake as client ");
    }

    rv = SSL_OptionSet(model_sock, SSL_HANDSHAKE_AS_SERVER, !is_client);
    if (rv != SECSuccess) {
	errExit("error handshake as server ");
    }

    if (disableStepDown) {
	rv = SSL_OptionSet(model_sock, SSL_NO_STEP_DOWN, PR_TRUE);
	if (rv != SECSuccess) {
	    errExit("error disabling SSL StepDown ");
	}
    }
    if (bypassPKCS11) {
        rv = SSL_OptionSet(model_sock, SSL_BYPASS_PKCS11, PR_TRUE);
	if (rv != SECSuccess) {
	    errExit("error enabling PKCS11 bypass ");
	}
    }
    if (disableLocking) {
        rv = SSL_OptionSet(model_sock, SSL_NO_LOCKS, PR_TRUE);
	if (rv != SECSuccess) {
	    errExit("error disabling SSL socket locking ");
	}
    } 
    if (enableSessionTickets) {
	rv = SSL_OptionSet(model_sock, SSL_ENABLE_SESSION_TICKETS, PR_TRUE);
	if (rv != SECSuccess) {
	    errExit("error enabling Session Ticket extension ");
	}
    }

    if (enableCompression) {
	rv = SSL_OptionSet(model_sock, SSL_ENABLE_DEFLATE, PR_TRUE);
	if (rv != SECSuccess) {
	    errExit("error enabling compression ");
	}
    }

    /* handle ciphers here - SSL_CipherPrefSetDefault etc. */

    /*
    rv = SSL_SNISocketConfigHook(model_sock, mySSLSNISocketConfig,
                                 (void*)&virtServerNameArray);
    if (rv != SECSuccess) {
        errExit("error enabling SNI extension ");
    }
    */

    if (!is_client) {
        CERTCertificate *cert;
        SECKEYPrivateKey *privKey;

	cert = PK11_FindCertFromNickname(certname, NULL);
	if (cert == NULL) {
	    ns_log(LOG_ERR, "selfserv: Can't find certificate %s\n", certname);
	    exit(10);
	}
	privKey = PK11_FindKeyByAnyCert(cert, NULL);
	if (privKey == NULL) {
	    ns_log(LOG_ERR, "selfserv: Can't find Private Key for cert %s\n", certname);
	    exit(11);
	}
        certKEA = NSS_FindCertKEAType(cert);
        secStatus = SSL_ConfigSecureServer(model_sock, cert, privKey, certKEA);
        if (secStatus != SECSuccess) {
            errExit("SSL_ConfigSecureServer");
        }
        CERT_DestroyCertificate(cert);
        SECKEY_DestroyPrivateKey(privKey);
        SSL_ConfigServerSessionIDCache(0, 0, 0, NULL);
    }

    if (enableFDX) { /* doing FDX */
	rv = SSL_OptionSet(model_sock, SSL_ENABLE_FDX, 1);
	if (rv != SECSuccess) {
	    errExit("SSL_OptionSet SSL_ENABLE_FDX");
	}
    }

    if (NoReuse) {
        rv = SSL_OptionSet(model_sock, SSL_NO_CACHE, 1);
        if (rv != SECSuccess) {
            errExit("SSL_OptionSet SSL_NO_CACHE");
        }
    }

    /*
    if (expectedHostNameVal) {
        SSL_HandshakeCallback(model_sock, handshakeCallback,
                              (void*)expectedHostNameVal);
    }
    */

    /*
    SSL_AuthCertificateHook(model_sock, mySSLAuthCertificate, 
	                        (void *)CERT_GetDefaultCertDB());
    */

    rv = SSL_OptionSet(model_sock, SSL_REQUEST_CERTIFICATE, PR_TRUE);
    if (rv != SECSuccess) {
        errExit("first SSL_OptionSet SSL_REQUEST_CERTIFICATE");
    }
    rv = SSL_OptionSet(model_sock, SSL_REQUIRE_CERTIFICATE, PR_FALSE);
    if (rv != SECSuccess) {
        errExit("first SSL_OptionSet SSL_REQUIRE_CERTIFICATE");
    }

    /* end of ssl configuration. */
    return model_sock;
}

void
ns_tls_done(ns_sec_ctx_t *ctx)
{
    if (ctx->model_fd) {
        PR_Close(ctx->model_fd);
    }
    if (ctx->initctx) {
        NSS_ShutdownContext(ctx->initctx);
    }
    ns_free(ctx);
}    

ns_sec_ctx_t *
ns_tls_init(const char *dir, const char *prefix, const char *certname, PRBool is_client)
{
    SECStatus rc = SECSuccess;
    ns_sec_ctx_t *ctx = ns_calloc(1, sizeof(ns_sec_ctx_t));

    ctx->init_params.length = sizeof(ctx->init_params);
    ctx->initctx = NSS_InitContext(dir, prefix, prefix, SECMOD_DB,
                                   &ctx->init_params, NSS_INIT_READONLY);
    if (ctx->initctx == NULL) {
        rc = SECFailure;
        goto fail;
    }

    NSS_SetDomesticPolicy();

    ctx->model_fd = create_tls_model_sock(certname, is_client);
    if (ctx->model_fd == NULL) {
        rc = SECFailure;
        goto fail;
    }

fail:
    if (rc != SECSuccess) {
        ns_tls_done(ctx);
        ctx = NULL;
    }

    return ctx;
}

PRErrorCode
ns_add_sec_layer(PRFileDesc *fd, ns_sec_ctx_t *ctx)
{
    PRErrorCode err = PR_SUCCESS;

    fd = SSL_ImportFD(ctx->model_fd, fd);
    if (fd == NULL) {
        err = PR_GetError();
        goto fail;
    }

    if (SECSuccess != SSL_ResetHandshake(fd, !ctx->is_client)) {
        err = PR_GetError();
        goto fail;
    }

fail:
    return err;
}
