/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

/**
 * Implementation of a Simple HTTP Client
 */
#include <stdio.h>
#include <string.h>

#include "nspr.h"
#include "nss.h"
#include "pk11func.h"
#include "ssl.h"
#include "prprf.h"
#include "plstr.h"
#include "slapi-plugin.h"
#include "http_client.h"
#include "secerr.h"
#include "sslerr.h"
#include "slapi-private.h"
#include "slapi-plugin-compat4.h"
/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/*** from proto-slap.h ***/

int slapd_log_error_proc( char *subsystem, char *fmt, ... );
char *config_get_instancedir();

/*** from ldaplog.h ***/

/* edited ldaplog.h for LDAPDebug()*/
#ifndef _LDAPLOG_H
#define _LDAPLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILD_STANDALONE
#define slapi_log_error(a,b,c,d) printf((c),(d))
#define stricmp strcasecmp
#endif

#define LDAP_DEBUG_TRACE	0x00001		/*     1 */
#define LDAP_DEBUG_ANY      0x04000		/* 16384 */
#define LDAP_DEBUG_PLUGIN	0x10000		/* 65536 */

/* debugging stuff */
#    ifdef _WIN32
       extern int	*module_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    else /* _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    endif /* Win32 */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

#define HTTP_PLUGIN_SUBSYSTEM   "http-client-plugin"   /* used for logging */

#define HTTP_IMPL_SUCCESS		0
#define HTTP_IMPL_FAILURE		-1

#define HTTP_REQ_TYPE_GET		1
#define HTTP_REQ_TYPE_REDIRECT		2
#define HTTP_REQ_TYPE_POST		3

#define HTTP_GET			"GET"
#define HTTP_POST			"POST"
#define HTTP_PROTOCOL			"HTTP/1.0"
#define HTTP_CONTENT_LENGTH		"Content-length:"
#define HTTP_CONTENT_TYPE_URL_ENCODED	"Content-type: application/x-www-form-urlencoded"
#define HTTP_GET_STD_LEN		18
#define HTTP_POST_STD_LEN		85
#define HTTP_DEFAULT_BUFFER_SIZE	4096
#define HTTP_RESPONSE_REDIRECT		(retcode == 302 || retcode == 301)

/**
 * Error strings used for logging error messages
 */
#define HTTP_ERROR_BAD_URL			" Badly formatted URL"
#define HTTP_ERROR_NET_ADDR			" NetAddr initialization failed"
#define HTTP_ERROR_SOCKET_CREATE	" Creation of socket failed"
#define HTTP_ERROR_SSLSOCKET_CREATE	" Creation of SSL socket failed"
#define HTTP_ERROR_CONNECT_FAILED	" Couldn't connect to remote host"
#define HTTP_ERROR_SEND_REQ			" Send request failed"
#define HTTP_ERROR_BAD_RESPONSE		" Invalid response from remote host"

#define HTTP_PLUGIN_DN			"cn=HTTP Client,cn=plugins,cn=config"
#define CONFIG_DN			"cn=config"
#define ATTR_CONNECTION_TIME_OUT	"nsHTTPConnectionTimeOut"
#define ATTR_READ_TIME_OUT		"nsHTTPReadTimeOut"
#define ATTR_RETRY_COUNT		"nsHTTPRetryCount"
#define ATTR_DS_SECURITY		"nsslapd-security"
#define ATTR_INSTANCE_PATH		"nsslapd-errorlog"

/*static Slapi_ComponentId *plugin_id = NULL;*/

typedef struct {
    int        retryCount;
    int        connectionTimeOut;
    int        readTimeOut;
    int        nssInitialized;
    char      *DS_sslOn;
} httpPluginConfig;

httpPluginConfig *httpConfig;

/**
 * Public functions
 */
int http_impl_init(Slapi_ComponentId *plugin_id);
int http_impl_get_text(char *url, char **data, int *bytesRead);
int http_impl_get_binary(char *url, char **data, int *bytesRead);
int http_impl_get_redirected_uri(char *url, char **data, int *bytesRead);
int http_impl_post(char *url, httpheader **httpheaderArray, char *body, char **data, int *bytesRead);
void http_impl_shutdown();

/**
 * Http handling functions
 */
static int doRequest(const char *url, httpheader **httpheaderArray, char *body, char **buf, int *bytesRead, int reqType);
static int doRequestRetry(const char *url, httpheader **httpheaderArray, char *body, char **buf, int *bytesRead, int reqType);
static void setTCPNoDelay(PRFileDesc* fd);
static PRStatus sendGetReq(PRFileDesc *fd, const char *path);
static PRStatus sendPostReq(PRFileDesc *fd, const char *path, httpheader **httpheaderArray, char *body);
static PRStatus processResponse(PRFileDesc *fd, char **resBUF, int *bytesRead, int reqType);
static PRStatus getChar(PRFileDesc *fd, char *buf);
static PRInt32 http_read(PRFileDesc *fd, char *buf, int size);
static PRStatus getBody(PRFileDesc *fd, char **buf, int *actualBytesRead);
static PRBool isWhiteSpace(char ch);
static PRStatus sendFullData( PRFileDesc *fd, char *buf, int timeOut);

/**
 * Helper functions to parse URL
 */
static PRStatus parseURI(const char *url, char **host, PRInt32 *port, char **path, int *sslOn);
static void toLowerCase(char* str);
static PRStatus parseAtPort(const char* url, PRInt32 *port, char **path);
static PRStatus parseAtPath(const char *url, char **path);
static PRInt32 getPort(const char* src);
static PRBool isAsciiSpace(char aChar);
static PRBool isAsciiDigit(char aChar);
static char * isHttpReq(const char *url, int *sslOn);

/*To get config from entry*/
static int readConfigLDAPurl(Slapi_ComponentId *plugin_id, char *plugindn);
static int parseHTTPConfigEntry(Slapi_Entry *e);
static int parseConfigEntry(Slapi_Entry *e);

static int nssReinitializationRequired();

/*SSL functions */
PRFileDesc* setupSSLSocket(PRFileDesc* fd);

/*SSL callback functions */
SECStatus   badCertHandler(void *arg, PRFileDesc *socket);
SECStatus   authCertificate(void *arg, PRFileDesc *socket, PRBool checksig, PRBool isServer);
SECStatus   getClientAuthData(void *arg, PRFileDesc *socket,struct CERTDistNamesStr *caNames, struct CERTCertificateStr **pRetCert, struct SECKEYPrivateKeyStr **pRetKey);
SECStatus   handshakeCallback(PRFileDesc *socket, void *arg);

static int doRequestRetry(const char *url, httpheader **httpheaderArray, char *body, char **buf, int *bytesRead, int reqType)
{
    int status = HTTP_IMPL_SUCCESS;
	int retrycnt = 0;
	int i = 1;

	retrycnt = httpConfig->retryCount;
	
	if (retrycnt == 0) {
		  LDAPDebug( LDAP_DEBUG_PLUGIN, "doRequestRetry: Retry Count cannot be read. Setting to default value of 3 \n", 0,0,0);
	   	  retrycnt = 3;
	}
        status = doRequest(url, httpheaderArray, body, buf, bytesRead, reqType);
	if (status != HTTP_IMPL_SUCCESS) {
	       LDAPDebug( LDAP_DEBUG_PLUGIN, "doRequestRetry: Failed to perform http request \n", 0,0,0);
       	 while (retrycnt > 0) {
       	         LDAPDebug( LDAP_DEBUG_PLUGIN, "doRequestRetry: Retrying http request %d.\n", i,0,0);
       	         status = doRequest(url, httpheaderArray, body, buf, bytesRead, reqType);
       	         if (status == HTTP_IMPL_SUCCESS) {
                        break;
       	         }
       	         retrycnt--;
       	         i++;
        }
        if (status != HTTP_IMPL_SUCCESS) {
                LDAPDebug( LDAP_DEBUG_ANY, "doRequestRetry: Failed to perform http request after %d attempts.\n", i,0,0);
	LDAPDebug( LDAP_DEBUG_ANY, "doRequestRetry:  Verify plugin URI configuration and contact Directory Administrator.\n",0,0,0);
        }

	}
    return status;
}

static int doRequest(const char *url, httpheader **httpheaderArray, char *body, char **buf, int *bytesRead, int reqType)
{
	PRStatus status = PR_SUCCESS;

	char *host = NULL;
	char *path = NULL;
	char *val = NULL;
	char *defaultprefix = NULL;
   	PRFileDesc *fd = NULL;
	PRNetAddr addr;
	PRInt32 port;
	PRInt32 errcode = 0;
	PRInt32 http_connection_time_out = 0;
	PRInt32 sslOn;
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> doRequest -- BEGIN\n",0,0,0);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> url=[%s] \n",url,0,0);

	/* Parse the URL and initialize the host, port, path */
	if (parseURI(url, &host, &port, &path, &sslOn) == PR_FAILURE) {
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "doRequest: %s \n", HTTP_ERROR_BAD_URL);
		status = PR_FAILURE;
		goto bail;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> host=[%s] port[%d] path[%s] \n",host,port,path);

	/* Initialize the Net Addr */
    if (PR_StringToNetAddr(host, &addr) == PR_FAILURE) {
        char buf[PR_NETDB_BUF_SIZE];
        PRHostEnt ent;

        status = PR_GetIPNodeByName(host, PR_AF_INET, PR_AI_DEFAULT, buf, sizeof(buf), &ent);
		if (status == PR_SUCCESS) {
            PR_EnumerateHostEnt(0, &ent, (PRUint16)port, &addr);
        } else {
			slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "doRequest: %s\n", HTTP_ERROR_NET_ADDR);
			status = HTTP_CLIENT_ERROR_NET_ADDR;
			goto bail;
        }
    } else {
		addr.inet.port = (PRUint16)port;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Successfully created NetAddr \n",0,0,0);

	/* open a TCP connection to the server */
    fd = PR_NewTCPSocket();
    if (!fd) {
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "doRequest: %s\n", HTTP_ERROR_SOCKET_CREATE);
        	status = HTTP_CLIENT_ERROR_SOCKET_CREATE;
		goto bail;
    }

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Successfully created New TCP Socket \n",0,0,0);

	/* immediately send the response */
    setTCPNoDelay(fd);

    if (sslOn) {
	
		/* Have to reinitialize NSS is the DS security is set to off.
		This is because the HTTPS required the cert dbs to be created.
		The default prefixes are used as per DS norm */

		if (PL_strcasecmp(httpConfig->DS_sslOn, "off") == 0) {	
			if (!httpConfig->nssInitialized) {
				if (nssReinitializationRequired())
				{
					PRInt32 nssStatus;
					PRUint32 nssFlags = 0;
					char certDir[1024];
					char certPref[1024];
					char keyPref[1024];

					NSS_Shutdown();
					nssFlags &= (~NSS_INIT_READONLY);
       				val = config_get_instancedir();
       				PL_strncpyz(certDir, val, sizeof(certDir));
					defaultprefix = strrchr(certDir, '/');
					if (!defaultprefix)
    						defaultprefix = strrchr(certDir, '\\');
					if (!defaultprefix) /* still could not find it . . . */
    						goto bail; /* . . . can't do anything */
					defaultprefix++;
					PR_snprintf(certPref, 1024, "%s-",defaultprefix);
					PL_strncpyz(keyPref, certPref, sizeof(keyPref));
					*defaultprefix= '\0';
					PR_snprintf(certDir, 1024, "%salias", certDir);
       				nssStatus = NSS_Initialize(certDir, certPref, keyPref, "secmod.db", nssFlags);
					slapi_ch_free((void **)&val);
		
   					if (nssStatus != 0) {
   	    	 	 			slapi_log_error(SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
   	    		   			"doRequest: Unable to initialize NSS Cert/Key Database\n");
							status = HTTP_CLIENT_ERROR_NSS_INITIALIZE;
   	     	 			goto bail;
   					}
   				}
				httpConfig->nssInitialized = 1;
			}
		}

		NSS_SetDomesticPolicy();

		fd = setupSSLSocket(fd);
		if (fd == NULL) {
			slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     	"doRequest: %s\n", HTTP_ERROR_SSLSOCKET_CREATE);
        		status = HTTP_CLIENT_ERROR_SSLSOCKET_CREATE;
			goto bail;
		}
	
		if (SSL_SetURL(fd, host) != 0) {
    			errcode = PR_GetError();
				slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
		     	"doRequest: SSL_SetURL -> NSPR Error code (%d) \n", errcode);
        		status = HTTP_CLIENT_ERROR_SSLSOCKET_CREATE;
			goto bail;
		}

    }
   
    http_connection_time_out = httpConfig->connectionTimeOut;
	/* connect to the host */
    if (PR_Connect(fd, &addr, PR_MillisecondsToInterval(http_connection_time_out)) == PR_FAILURE) {
    	errcode = PR_GetError();
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
			"doRequest: %s (%s:%d) -> NSPR Error code (%d)\n",
			HTTP_ERROR_CONNECT_FAILED, host, addr.inet.port, errcode);
    	status = HTTP_CLIENT_ERROR_CONNECT_FAILED;
		goto bail;
    }

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Successfully connected to host [%s] \n",host,0,0);

	/* send the request to the server */
	if (reqType == HTTP_REQ_TYPE_POST) {
		if (sendPostReq(fd, path, httpheaderArray, body) == PR_FAILURE) {
			slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
				"doRequest-sendPostReq: %s (%s)\n", HTTP_ERROR_SEND_REQ, path);
       		status = HTTP_CLIENT_ERROR_SEND_REQ;
			goto bail;
		}
	}
	else {
		if (sendGetReq(fd, path) == PR_FAILURE) {
			slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
				"doRequest-sendGetReq: %s (%s)\n", HTTP_ERROR_SEND_REQ, path);
       		status = HTTP_CLIENT_ERROR_SEND_REQ;
			goto bail;
		}
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Successfully sent the request [%s] \n",path,0,0);

	/* read the response */
	if (processResponse(fd, buf, bytesRead, reqType) == PR_FAILURE) {
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
			"doRequest: %s (%s)\n", HTTP_ERROR_BAD_RESPONSE, url);
        status = HTTP_CLIENT_ERROR_BAD_RESPONSE;
		goto bail;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Successfully read the response\n",0,0,0);
bail:
	if (host) {
		PR_Free(host);
	}
	if (path) {
		PR_Free(path);
	}
	if (fd) {
		PR_Close(fd);
		fd = NULL;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- doRequest -- END\n",0,0,0);
	return status;
}

static PRStatus processResponse(PRFileDesc *fd, char **resBUF, int *bytesRead, int reqType) 
{
	PRStatus status = PR_SUCCESS;
	char *location = NULL;
    char *protocol = NULL;
    char *statusNum = NULL;
	char *statusString = NULL;
	char *headers = NULL;

    char tmp[HTTP_DEFAULT_BUFFER_SIZE];
    int pos=0;
	char ch;
	int index;
	int retcode;

    PRBool doneParsing = PR_FALSE;
    PRBool isRedirect  = PR_FALSE;
    char name[HTTP_DEFAULT_BUFFER_SIZE];
	char value[HTTP_DEFAULT_BUFFER_SIZE];
    PRBool atEOL = PR_FALSE;
    PRBool inName = PR_TRUE;

	/* PKBxxx: If we are getting a redirect and the response is more the
	 * the HTTP_DEFAULT_BUFFER_SIZE, it will cause the server to crash. A 4k
	 * buffer should be good enough.
	 */
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> processResponse -- BEGIN\n",0,0,0);

	headers = (char *)PR_Calloc(1, 4 * HTTP_DEFAULT_BUFFER_SIZE);
    /* Get protocol string */
    index = 0;
    while (1) {
		status = getChar(fd, headers+pos);
		if (status == PR_FAILURE) {
			/* Error : */
			goto bail;
		}
		ch = (char)headers[pos];
		pos++;
		if (!isWhiteSpace(ch)) {
			tmp[index++] = ch;
		} else {
			break;
		}
	}
    tmp[index] = '\0';
    protocol = PL_strdup(tmp);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> protocol=[%s] \n",protocol,0,0);

    /* Get status num */
    index = 0;
    while (1) {
		status = getChar(fd, headers+pos);
		if (status == PR_FAILURE) {
			/* Error : */
			goto bail;
		}
		ch = (char)headers[pos];
		pos++;
		if (!isWhiteSpace(ch)) {
			tmp[index++] = ch;
		} else {
			break;
		}
	}
    tmp[index] = '\0';
    statusNum = PL_strdup(tmp);
	retcode=atoi(tmp);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> statusNum=[%s] \n",statusNum,0,0);

	if (HTTP_RESPONSE_REDIRECT && (reqType == HTTP_REQ_TYPE_REDIRECT)) {
		isRedirect = PR_TRUE;
	}
    /* Get status string */
    if (ch != '\r')
	{
        index = 0;
        while (ch != '\r') {
			status = getChar(fd, headers+pos);
			if (status == PR_FAILURE) {
				/* Error : */
				goto bail;
			}
			ch = (char)headers[pos];
			pos++;
            tmp[index++] = ch;
		}
		tmp[index] = '\0';
        statusString = PL_strdup(tmp);
		LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> statusString [%s] \n",statusString,0,0);
    }

    /**
	 * Skip CRLF
	 */
	status = getChar(fd, headers+pos);
	if (status == PR_FAILURE) {
		/* Error : */
		goto bail;
	}
	ch = (char)headers[pos];
	pos++;

    /**
	 * loop over response headers
	 */
    index = 0;
    while (!doneParsing) {
		status = getChar(fd, headers+pos);
		if (status == PR_FAILURE) {
			/* Error : */
			goto bail;
		}
		ch = (char)headers[pos];
		pos++;
        switch(ch)
		{
            case ':':
				if (inName) {
				  name[index] = '\0';
				  index = 0;
				  inName = PR_FALSE;

				  /* skip whitespace */
				  ch = ' ';
	
				/*  status = getChar(fd, headers+pos);
				  if (status == PR_FAILURE) {
					  goto bail;
				  }
				  ch = (char)headers[pos];
				  pos++; */

				  while(isWhiteSpace(ch)) {
						status = getChar(fd, headers+pos);
						if (status == PR_FAILURE) {
							/* Error : */
							goto bail;
						}
						ch = (char)headers[pos];
						pos++;
				  }
				  value[index++] = ch;
				} else {
				  value[index++] = ch;
				}
				break;
            case '\r':
				if (inName && !atEOL) {
				   return PR_FALSE;
				}
				break;
            case '\n':
				if (atEOL) {
				  doneParsing = PR_TRUE;
				  break;
				}
				if (inName) {
				   return PR_FALSE;
				}
				value[index] = '\0';
				index = 0;
				inName = PR_TRUE;
				LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> name=[%s] value=[%s]\n",name,value,0);
				if (isRedirect && !PL_strcasecmp(name,"location")) {
				  location = PL_strdup(value);
				}
				atEOL = PR_TRUE;
				break;
            default:
                atEOL = PR_FALSE;
                if (inName) {
                     name[index++] = ch;
				} else {
                     value[index++] = ch;
				}
                break;
        }
    }

	if (!isRedirect) {
		getBody(fd, resBUF, bytesRead);
	} else {
		*resBUF = PL_strdup(location);
		*bytesRead = strlen(location);
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Response Buffer=[%s] bytesRead=[%d] \n",*resBUF,*bytesRead,0);

bail:

	if (headers) {
		PR_Free(headers);
	}
    if (protocol) {
		PL_strfree(protocol);
	}
    if (statusNum) {
		PL_strfree(statusNum);
	}
	if (statusString) {
		PL_strfree(statusString);
	}
	if (location) {
		PL_strfree(location);
	}
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- processResponse -- END\n",0,0,0);
    return status;
}

static int nssReinitializationRequired()
{
	int nssReinitializationRequired = 0;
	int err = 0;
	float version = 0;
	const float DSVERSION = 6.1;
	char *str = NULL;
	char *value   = NULL;
	Slapi_Entry  **entry = NULL;
	Slapi_PBlock *resultpb= NULL;

	resultpb= slapi_search_internal( "", LDAP_SCOPE_BASE, "objectclass=*", NULL, NULL, 0);
	slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entry );
	slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_RESULT, &err);
	if ( err == LDAP_SUCCESS && entry!=NULL  && entry[0]!=NULL)
	{
		value = slapi_entry_attr_get_charptr(entry[0], "vendorVersion");
		if (value == NULL || strncmp(value, "Netscape", strlen("Netscape")))		
		{
			slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
				"nssReinitializationRequired: vendor is not Netscape \n");
			slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
				"or version is earlier than 6.0\n", value);
			nssReinitializationRequired = 1;
			slapi_free_search_results_internal(resultpb);
			slapi_pblock_destroy(resultpb);
			slapi_ch_free((void **)&value);
			return nssReinitializationRequired;
		}
 
		if ( (str = strstr(value,"/")) != NULL )
		{	
			str++;
			version = atof(str);
			slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
				"nssReinitializationRequired: version is %f. \n", version);
		}


		if (str == NULL || version < DSVERSION)
		{
			nssReinitializationRequired = 1;
		}
		slapi_ch_free((void **)&value);

	}
	slapi_free_search_results_internal(resultpb);
	slapi_pblock_destroy(resultpb);
	return nssReinitializationRequired;

}

static PRStatus sendGetReq(PRFileDesc *fd, const char *path)
{
	PRStatus status = PR_SUCCESS;
	char *reqBUF = NULL;
	PRInt32 http_connection_time_out = 0;
	int buflen = (HTTP_GET_STD_LEN + strlen(path));

	reqBUF = (char *)PR_Calloc(1, buflen);

	strcpy(reqBUF, HTTP_GET);
	strcat(reqBUF, " ");
	strcat(reqBUF, path);
	strcat(reqBUF, " ");
	strcat(reqBUF, HTTP_PROTOCOL);
	strcat(reqBUF, "\r\n\r\n\0");

	http_connection_time_out = httpConfig->connectionTimeOut;
	status = sendFullData( fd, reqBUF, http_connection_time_out);

	if (reqBUF) {
		PR_Free(reqBUF);
		reqBUF = 0;
	}
	return status;
}

static PRStatus sendFullData( PRFileDesc *fd, char *buf, int timeOut)
{
	int dataSent = 0;
	int bufLen = strlen(buf);
	int retVal = 0;
	PRInt32 errcode = 0;
	while (dataSent < bufLen)
	{
		retVal = PR_Send(fd, buf+dataSent, bufLen-dataSent, 0, PR_MillisecondsToInterval(timeOut));
		if (retVal == -1 )
			break;
		dataSent += retVal;
	}
	if (dataSent == bufLen )
		return PR_SUCCESS;
	else
	{
		errcode = PR_GetError();
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
			"sendFullData: dataSent=%d bufLen=%d -> NSPR Error code (%d)\n",
			dataSent, bufLen, errcode);
		LDAPDebug( LDAP_DEBUG_PLUGIN, "---------->NSPR Error code (%d) \n", errcode,0,0);
		return PR_FAILURE;
	}
}

static PRStatus sendPostReq(PRFileDesc *fd, const char *path, httpheader **httpheaderArray, char *body)
{
    PRStatus status = PR_SUCCESS;
	char body_len_str[20];
    char *reqBUF = NULL;
    PRInt32 http_connection_time_out = 0;
	int i = 0;
	int body_len, buflen = 0; 

	if (body) {
		body_len = strlen(body);
	} else {
		body_len = 0;
	}
	PR_snprintf(body_len_str, sizeof(body_len_str), "%d", body_len);

    buflen = (HTTP_POST_STD_LEN + strlen(path) + body_len + strlen(body_len_str));

    for (i = 0; httpheaderArray[i] != NULL; i++) {

                if (httpheaderArray[i]->name != NULL)
				{
                    buflen += strlen(httpheaderArray[i]->name) + 2;
                	if (httpheaderArray[i]->value != NULL)
                        buflen += strlen(httpheaderArray[i]->value) + 2;
				}

    }

    reqBUF = (char *)PR_Calloc(1, buflen);

    strcpy(reqBUF, HTTP_POST);
    strcat(reqBUF, " ");
    strcat(reqBUF, path);
    strcat(reqBUF, " ");
    strcat(reqBUF, HTTP_PROTOCOL);
    strcat(reqBUF, "\r\n");
    strcat(reqBUF, HTTP_CONTENT_LENGTH);
    strcat(reqBUF, " ");
    strcat(reqBUF, body_len_str);
    strcat(reqBUF, "\r\n");
    strcat(reqBUF, HTTP_CONTENT_TYPE_URL_ENCODED);
    strcat(reqBUF, "\r\n");
 
	for (i = 0; httpheaderArray[i] != NULL; i++) {

		if (httpheaderArray[i]->name != NULL)
			strcat(reqBUF, httpheaderArray[i]->name);
        	strcat(reqBUF, ": ");
		if (httpheaderArray[i]->value != NULL)
			strcat(reqBUF, httpheaderArray[i]->value);
       		strcat(reqBUF, "\r\n");

	}

    strcat(reqBUF, "\r\n");
	if (body) {
		strcat(reqBUF, body);
	}
    strcat(reqBUF, "\0");

	LDAPDebug( LDAP_DEBUG_PLUGIN, "---------->reqBUF is %s \n",reqBUF,0,0);
    http_connection_time_out = httpConfig->connectionTimeOut;

	status = sendFullData( fd, reqBUF, http_connection_time_out);

    if (reqBUF) {
            PR_Free(reqBUF);
            reqBUF = 0;
    }
    return status;
}


static PRStatus getChar(PRFileDesc *fd, char *buf)
{
    PRInt32 bytesRead = http_read(fd, buf, 1);
	if (bytesRead <=0) {
    	PRInt32 errcode = PR_GetError();
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
			"getChar: NSPR Error code (%d)\n", errcode);
		return PR_FAILURE;
	}
    return PR_SUCCESS;
}

static PRStatus getBody(PRFileDesc *fd, char **buf, int *actualBytesRead)
{
	int totalBytesRead = 0;
	int size = 4 * HTTP_DEFAULT_BUFFER_SIZE;
	int bytesRead = size;
	char *data = (char *) PR_Calloc(1, size);
	while (bytesRead == size) {
		bytesRead = http_read(fd, (data+totalBytesRead), size);
		if (bytesRead <= 0) {
			/* Read error */
			return PR_FAILURE;
		}
		if (bytesRead == size) {
			/* more data to be read so increase the buffer */
			size = size * 2 ;
			data = (char *) PR_Realloc(data, size);
		}
		totalBytesRead += bytesRead;
	}
	*buf = data;
	*actualBytesRead = totalBytesRead;

	return PR_SUCCESS;
}

static PRInt32 http_read(PRFileDesc *fd, char *buf, int size)
{
    PRInt32 http_read_time_out = 0;
    http_read_time_out = httpConfig->readTimeOut;
    return PR_Recv(fd, buf, size, 0, PR_MillisecondsToInterval(http_read_time_out));
}

static PRBool isWhiteSpace(char ch) 
{
	PRBool b = PR_FALSE;
	if (ch == ' ') {
		b = PR_TRUE;
	}
	return b;
}

static PRStatus parseURI(const char *urlstr, char **host, PRInt32 *port, char **path, int *sslOn)
{
	PRStatus status = PR_SUCCESS;
	char *brk;
    int len;
    static const char delimiters[] = ":/?#";
	char *url = isHttpReq(urlstr, sslOn);
	
	if (*sslOn)  {
		*port = 443;
	}
	else  {
		*port = 80; 
	}
	if (url == NULL) {
		/* Error : */
		status = PR_FAILURE;
		goto bail;
	}
	len = PL_strlen(url);
	/* Currently we do not support Ipv6 addresses */
    brk = PL_strpbrk(url, delimiters);
    if (!brk) { 
		*host = PL_strndup(url, len);
        toLowerCase(*host);
		goto bail;
    }
    switch (*brk) 
	{
		case '/' :
		case '?' :
		case '#' :
			/* Get the Host, the rest is Path */
			*host = PL_strndup(url, (brk - url));
			toLowerCase(*host);
			status = parseAtPath(brk, path);
			break;
		case ':' :
			/* Get the Host and process port, path */
			*host = PL_strndup(url, (brk - url));
			toLowerCase(*host);
			status = parseAtPort(brk+1, port, path);
			break;
		default:
			/* Error : HTTP_BAD_URL */
			break;
    }

bail:
	if (url) {
		PR_Free(url);
	}
	return status;
}

static PRStatus parseAtPort(const char* url, PRInt32 *port, char **path)
{
	PRStatus status = PR_SUCCESS;
    static const char delimiters[] = "/?#"; 
    char* brk = PL_strpbrk(url, delimiters);
    if (!brk) /* everything is a Port */
    {
        *port = getPort(url);
        if (*port <= 0) {
			/* Error : HTTP_BAD_URL */
            return PR_FAILURE;
		} else {
            return status;
		}
    }

    switch (*brk)
    {
		case '/' :
		case '?' :
		case '#' :
			/* Get the Port, the rest is Path */
			*port = getPort(url);
			if (*port <= 0) {
				/* Error : HTTP_BAD_URL */
				return PR_FAILURE;
			}
			status = parseAtPath(brk, path);
			break;
		default:
			/* Error : HTTP_BAD_URL */
			break;
	}
    return status;
}

static PRStatus parseAtPath(const char *url, char **path)
{
	PRStatus status = PR_SUCCESS;
	char *dir = "%s%s"; 
	*path = (char *)PR_Calloc(1, (strlen(dir) + 1024));

    /* Just write the path and check for a starting / */
    if ('/' != *url) {
		PR_sscanf(*path, dir, "/", url);
	} else {
		strcpy(*path, url);
	}
	if (!*path) {
		/* Error : HTTP_BAD_URL */
		status = PR_FAILURE;
	}
    return status;
}

static void toLowerCase(char* str)
{
    if (str) {
        char* lstr = str;
        PRInt8 shift = 'a' - 'A';
        for(; (*lstr != '\0'); ++lstr) {
            if ((*(lstr) <= 'Z') && (*(lstr) >= 'A')) {
                *(lstr) = *(lstr) + shift;
			}
        }
    }
}

static PRInt32 getPort(const char* src)
{
    /* search for digits up to a slash or the string ends */
    const char* port = src;
    PRInt32 returnValue = -1;
    char c;

    /* skip leading white space */
    while (isAsciiSpace(*port))
        port++;

    while ((c = *port++) != '\0') {
        /* stop if slash or ? or # reached */
        if (c == '/' || c == '?' || c == '#')
            break;
        else if (!isAsciiDigit(c))
            return returnValue;
    }
    return (0 < PR_sscanf(src, "%d", &returnValue)) ? returnValue : -1;
}


static PRBool isAsciiSpace(char aChar)
{
  if ((aChar == ' ') || (aChar == '\r') || (aChar == '\n') || (aChar == '\t')) {
    return PR_TRUE;
  }
  return PR_FALSE;
}

static PRBool isAsciiDigit(char aChar)
{
	if ((aChar >= '0') && (aChar <= '9')) {
		return PR_TRUE;
	}
	return PR_FALSE;
}

static void setTCPNoDelay(PRFileDesc* fd)
{
	PRStatus status = PR_SUCCESS;
    PRSocketOptionData opt;

    opt.option = PR_SockOpt_NoDelay;
    opt.value.no_delay = PR_FALSE;

    status = PR_GetSocketOption(fd, &opt);
    if (status == PR_FAILURE) {
        return;
    }

    opt.option = PR_SockOpt_NoDelay;
    opt.value.no_delay = PR_TRUE;
    status = PR_SetSocketOption(fd, &opt);
    if (status == PR_FAILURE) {
        return;
    }
    return;
}

static char * isHttpReq(const char *url, int *sslOn)
{
    static const char http_protopol_header[] = "http://"; 
    static const char https_protopol_header[] = "https://"; 
	char *newstr = NULL;
    /* skip leading white space */
    while (isAsciiSpace(*url))
        url++;

	if (strncmp(url, http_protopol_header, strlen(http_protopol_header)) == 0) {
		newstr = (char *)PR_Calloc(1, (strlen(url)-strlen(http_protopol_header) + 1));				
		strcpy(newstr, url+7);
		strcat(newstr,"\0");
		*sslOn = 0;
	}
	else if (strncmp(url, https_protopol_header, strlen(https_protopol_header)) == 0) {
		newstr = (char *)PR_Calloc(1, (strlen(url)-strlen(https_protopol_header) + 1));				
		strcpy(newstr, url+8);
		strcat(newstr,"\0");
		*sslOn = 1;
	}

	return newstr;
}

PRFileDesc* setupSSLSocket(PRFileDesc* fd)
{
	SECStatus   secStatus;
	PRFileDesc* sslSocket;
	PRSocketOptionData      socketOption;
	char *certNickname = NULL;

	socketOption.option                 = PR_SockOpt_Nonblocking;
	socketOption.value.non_blocking = PR_FALSE;
	if( PR_SetSocketOption(fd, &socketOption) != 0) {
        	slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
			"Cannot set socket option NSS \n");
		return NULL;
	}

	sslSocket = SSL_ImportFD(NULL, fd);
	if (!sslSocket) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: Cannot import to SSL Socket\n" );
				goto sslbail;
	}
	
    slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: setupssl socket created\n" );

	secStatus = SSL_OptionSet(sslSocket, SSL_SECURITY, 1);
	if (SECSuccess != secStatus) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: Cannot set SSL_SECURITY option\n");
				goto sslbail;
	}

	secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_CLIENT, 1);
	if (SECSuccess != secStatus) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: CAnnot set SSL_HANDSHAKE_AS_CLIENT option\n");
				goto sslbail;
	}
	
	/* Set SSL callback routines. */

    secStatus = SSL_GetClientAuthDataHook(sslSocket,
                                  (SSLGetClientAuthData)  getClientAuthData,
                                  (void *)certNickname);
    if (secStatus != SECSuccess) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
           		"setupSSLSocket: SSL_GetClientAuthDataHook Failed\n");
    	       	goto sslbail;
    }

    secStatus = SSL_AuthCertificateHook(sslSocket,
                           (SSLAuthCertificate)   authCertificate,
                           (void *)CERT_GetDefaultCertDB());
    if (secStatus != SECSuccess) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: SSL_AuthCertificateHook Failed\n");
                goto sslbail;
    }

	secStatus = SSL_BadCertHook(sslSocket,
                        (SSLBadCertHandler)  badCertHandler, NULL);
    if (secStatus != SECSuccess) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: SSL_BadCertHook Failed\n");
                goto sslbail;
    }

    secStatus = SSL_HandshakeCallback(sslSocket,
                        (SSLHandshakeCallback)  handshakeCallback, NULL);
    if (secStatus != SECSuccess) {
                slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                     "setupSSLSocket: SSL_HandshakeCallback Failed\n");
                goto sslbail;
    }

    return sslSocket;

sslbail:
    PR_Close(fd);
    return NULL;	
}

SECStatus
  authCertificate(void *arg, PRFileDesc *socket,
                  PRBool checksig, PRBool isServer)
{

    SECCertUsage        certUsage;
    CERTCertificate *   cert;
    void *              pinArg;
    char *              hostName;
    SECStatus           secStatus;

    if (!arg || !socket) {
    	slapi_log_error(SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                " authCertificate: Faulty socket in callback function \n");
        return SECFailure;
    }

    /* Define how the cert is being used based upon the isServer flag. */

    certUsage = isServer ? certUsageSSLClient : certUsageSSLServer;

    cert = SSL_PeerCertificate(socket);

    pinArg = SSL_RevealPinArg(socket);

    secStatus = CERT_VerifyCertNow((CERTCertDBHandle *)arg,
                                       cert,
                                       checksig,
                                       certUsage,
                                       pinArg);

    /* If this is a server, we're finished. */
    if (isServer || secStatus != SECSuccess) {
                return secStatus;
    }

    hostName = SSL_RevealURL(socket);

    if (hostName && hostName[0]) {
                secStatus = CERT_VerifyCertName(cert, hostName);
    } else {
                PR_SetError(SSL_ERROR_BAD_CERT_DOMAIN, 0);
                secStatus = SECFailure;
    }

    if (hostName)
                PR_Free(hostName);

    return secStatus;
}

SECStatus
  badCertHandler(void *arg, PRFileDesc *socket)
{

    SECStatus   secStatus = SECFailure;
    PRErrorCode err;

    /* log invalid cert here */

    if (!arg) {
                return secStatus;
    }

    *(PRErrorCode *)arg = err = PORT_GetError();
    switch (err) {
    case SEC_ERROR_INVALID_AVA:
    case SEC_ERROR_INVALID_TIME:
    case SEC_ERROR_BAD_SIGNATURE:
    case SEC_ERROR_EXPIRED_CERTIFICATE:
    case SEC_ERROR_UNKNOWN_ISSUER:
    case SEC_ERROR_UNTRUSTED_CERT:
    case SEC_ERROR_CERT_VALID:
    case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
    case SEC_ERROR_CRL_EXPIRED:
    case SEC_ERROR_CRL_BAD_SIGNATURE:
    case SEC_ERROR_EXTENSION_VALUE_INVALID:
    case SEC_ERROR_CA_CERT_INVALID:
    case SEC_ERROR_CERT_USAGES_INVALID:
    case SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION:
                secStatus = SECSuccess;
        break;
    default:
                secStatus = SECFailure;
        break;
    }

       	slapi_log_error(SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
        "Bad certificate: %d\n", err);

    return secStatus;
}

SECStatus
  getClientAuthData(void *arg,
                    PRFileDesc *socket,
                    struct CERTDistNamesStr *caNames,
                    struct CERTCertificateStr **pRetCert,
                    struct SECKEYPrivateKeyStr **pRetKey)
{
    CERTCertificate *  cert;
    SECKEYPrivateKey * privKey;
    char *             chosenNickName = (char *)arg;
    void *             proto_win      = NULL;
    SECStatus          secStatus      = SECFailure;
    proto_win = SSL_RevealPinArg(socket);

    if (chosenNickName) {
                cert = PK11_FindCertFromNickname(chosenNickName, proto_win);
                if (cert) {
                    privKey = PK11_FindKeyByAnyCert(cert, proto_win);
                    if (privKey) {
                                secStatus = SECSuccess;
                    } else {
                                CERT_DestroyCertificate(cert);
                    }
                }
    } else { /* no nickname given, automatically find the right cert */
        CERTCertNicknames *names;
        int                i;

        names = CERT_GetCertNicknames(CERT_GetDefaultCertDB(),
                                      SEC_CERT_NICKNAMES_USER, proto_win);

        if (names != NULL) {
            for(i = 0; i < names->numnicknames; i++ ) {

                cert = PK11_FindCertFromNickname(names->nicknames[i],
                                                 proto_win);
                if (!cert) {
                    continue;
                }

                /* Only check unexpired certs */
                if (CERT_CheckCertValidTimes(cert, PR_Now(), PR_FALSE)
                      != secCertTimeValid ) {
                    CERT_DestroyCertificate(cert);
                    continue;
                }

                secStatus = NSS_CmpCertChainWCANames(cert, caNames);
                if (secStatus == SECSuccess) {
                    privKey = PK11_FindKeyByAnyCert(cert, proto_win);
                    if (privKey) {
                        break;
                    }
                    secStatus = SECFailure;
                    break;
                }
                CERT_FreeNicknames(names);
            } /* for loop */
        }
    }

    if (secStatus == SECSuccess) {
                *pRetCert = cert;
                *pRetKey  = privKey;
    }

    return secStatus;
}

SECStatus
  handshakeCallback(PRFileDesc *socket, void *arg)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
    	"----------> Handshake has completed, ready to send data securely.\n");
    return SECSuccess;
}


/**
 * PUBLIC FUNCTIONS IMPLEMENTATION
 */
int http_impl_init(Slapi_ComponentId *plugin_id)
{
	int status = HTTP_IMPL_SUCCESS;
       	slapi_log_error(SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
		"-> http_impl_init \n");
	httpConfig = NULL;

   	httpConfig = (httpPluginConfig *) slapi_ch_calloc(1, sizeof(httpPluginConfig));

	status = readConfigLDAPurl(plugin_id, HTTP_PLUGIN_DN); 
    if (status != 0) {
       		 slapi_log_error(SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                        "http_impl_start: Unable to get HTTP config information \n");
        	 return HTTP_IMPL_FAILURE;
   	}
	
	status = readConfigLDAPurl(plugin_id, CONFIG_DN); 
    if (status != 0) {
       		 slapi_log_error(SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                        "http_impl_start: Unable to get config information \n");
        	 return HTTP_IMPL_FAILURE;
    }
	
       	slapi_log_error(SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
		"<- http_impl_init \n");

	return status;

}


int http_impl_get_text(char *url, char **data, int *bytesRead)
{
	int status = HTTP_IMPL_SUCCESS;
	status = doRequestRetry(url, NULL, NULL, data, bytesRead, HTTP_REQ_TYPE_GET);
	return status;
}

int http_impl_get_binary(char *url, char **data, int *bytesRead)
{
	int status = HTTP_IMPL_SUCCESS;
	status = doRequestRetry(url, NULL, NULL, data, bytesRead, HTTP_REQ_TYPE_GET);
	return status;
}

int http_impl_get_redirected_uri(char *url, char **data, int *bytesRead)
{
	int status = HTTP_IMPL_SUCCESS;
	status = doRequestRetry(url, NULL, NULL, data, bytesRead, HTTP_REQ_TYPE_REDIRECT);
	return status;
}

int http_impl_post(char *url, httpheader **httpheaderArray, char *body, char **data, int *bytesRead)
{
	int status = HTTP_IMPL_SUCCESS;
	status = doRequestRetry(url, httpheaderArray, body, data, bytesRead, HTTP_REQ_TYPE_POST);
	return status;
}

void http_impl_shutdown()
{
	/**
	 * Put cleanup code here
	 */
}

static int readConfigLDAPurl(Slapi_ComponentId *plugin_id, char *plugindn) {

	int rc = LDAP_SUCCESS;
	Slapi_DN *sdn = NULL;
	int status = HTTP_IMPL_SUCCESS;
	Slapi_Entry  *entry = NULL;
	
	sdn = slapi_sdn_new_dn_byref(plugindn);
	rc = slapi_search_internal_get_entry(sdn, NULL, &entry, plugin_id);
    	slapi_sdn_free(&sdn);
	if (rc != LDAP_SUCCESS) {
            slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                       "readConfigLDAPurl: Could not find entry %s (error %d)\n", plugindn, rc);
            status = HTTP_IMPL_FAILURE;
            return status;
   	}
	if (NULL == entry)
    	{
            slapi_log_error( SLAPI_LOG_PLUGIN, HTTP_PLUGIN_SUBSYSTEM,
                       "readConfigLDAPurl: No entries found for <%s>\n", plugindn);

            status = HTTP_IMPL_FAILURE;
            return status;
    	}

	if ((PL_strcasecmp(plugindn, HTTP_PLUGIN_DN) == 0))
		status = parseHTTPConfigEntry(entry);
	else
		status = parseConfigEntry(entry);

	slapi_entry_free(entry);
    	return status;

}

/* Retrieves the plugin configuration info */

/* Retrieves security info as well as the path info required for the SSL
config dir */
static int parseConfigEntry(Slapi_Entry *e)
{
    char *value = NULL;

    value = slapi_entry_attr_get_charptr(e, ATTR_DS_SECURITY);
    if (value) {
           httpConfig->DS_sslOn = value;
    }

    return HTTP_IMPL_SUCCESS;

}


static int parseHTTPConfigEntry(Slapi_Entry *e)
{
    int value = 0;


    value = slapi_entry_attr_get_int(e, ATTR_RETRY_COUNT);
    if (value) {
            httpConfig->retryCount = value;
    }

    value = slapi_entry_attr_get_int(e, ATTR_CONNECTION_TIME_OUT);
    if (value) {
            httpConfig->connectionTimeOut = value;
    }
   	else {
                 LDAPDebug( LDAP_DEBUG_PLUGIN, "parseHTTPConfigEntry: HTTP Connection Time Out cannot be read. Setting to default value of 5000 ms \n", 0,0,0);
                 httpConfig->connectionTimeOut = 5000;
   	}


    value = slapi_entry_attr_get_int(e, ATTR_READ_TIME_OUT);
    if (value) {
            httpConfig->readTimeOut = value;
    }
    else {
                 LDAPDebug( LDAP_DEBUG_PLUGIN, "parseHTTPConfigEntry: HTTP Read Time Out cannot be read. Setting to default value of 5000 ms \n", 0,0,0);
                 httpConfig->readTimeOut = 5000;
   	}
	
	httpConfig->nssInitialized = 0;

    return HTTP_IMPL_SUCCESS;

}

/**
 * Self Testing
 */
#ifdef BUILD_STANDALONE
int main(int argc, char **argv)
{
	PRStatus status = PR_SUCCESS;
	char *buf;
	int bytes;
	char *host;
	PRInt32 port;
	char *path;
	if (argc < 2) {
		printf("URL missing\n");
		return -1;
	}
	PR_Init(PR_USER_THREAD,PR_PRIORITY_NORMAL, 0);
	doRequest(argv[1], &buf, &bytes, 2);
	printf( "%s\n", buf );
	return -1;
}
#endif

