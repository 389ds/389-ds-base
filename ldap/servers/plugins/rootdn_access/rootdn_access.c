/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2012 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *  Root DN Access Control plug-in
 */
#include "rootdn_access.h"
#include <nspr.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

/*
 * Add an entry like the following to dse.ldif to enable this plugin:
 *
 *   dn: cn=RootDN Access Control,cn=plugins,cn=config
 *   objectclass: top
 *   objectclass: nsSlapdPlugin
 *   objectclass: extensibleObject
 *   cn: RootDN Access Control
 *   nsslapd-pluginpath: librootdn-access-plugin.so
 *   nsslapd-plugininitfunc: rootdn_init
 *   nsslapd-plugintype: rootdnpreoperation
 *   nsslapd-pluginenabled: on
 *   nsslapd-plugin-depends-on-type: database
 *   nsslapd-pluginid: rootdn-access-control
 *   rootdn-open-time: 0800
 *   rootdn-close-time: 1700
 *   rootdn-days-allowed: Mon, Tue, Wed, Thu, Fri
 *   rootdn-allow-host: *.redhat.com
 *   rootdn-allow-host: *.fedora.com
 *   rootdn-deny-host: dangerous.boracle.com
 *   rootdn-allow-ip: 127.0.0.1
 *   rootdn-allow-ip: 2000:db8:de30::11
 *   rootdn-deny-ip: 192.168.1.*
 *
 */

/*
 *  Plugin Functions
 */
int32_t rootdn_init(Slapi_PBlock *pb);
static int32_t rootdn_start(Slapi_PBlock *pb);
static int32_t rootdn_close(Slapi_PBlock *pb);
static int32_t rootdn_load_config(Slapi_PBlock *pb);
static int32_t rootdn_check_access(Slapi_PBlock *pb);
static int32_t rootdn_check_host_wildcard(char *host, char *client_host);
static int rootdn_check_ip_wildcard(char *ip, char *client_ip);
static int32_t rootdn_preop_bind_init(Slapi_PBlock *pb);
void strToLower(char *str);

/*
 * Plug-in globals
 */
static void *_PluginID = NULL;
static char *_PluginDN = NULL;
static time_t open_time = 0;
static time_t close_time = 0;
static char *daysAllowed = NULL;
static char **hosts = NULL;
static char **hosts_to_deny = NULL;
static char **ips = NULL;
static char **ips_to_deny = NULL;

static Slapi_PluginDesc pdesc = {ROOTDN_FEATURE_DESC,
                                 VENDOR,
                                 DS_PACKAGE_VERSION,
                                 ROOTDN_PLUGIN_DESC};

/*
 * Plugin identity functions
 */
void
rootdn_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
rootdn_get_plugin_id(void)
{
    return _PluginID;
}

void
rootdn_set_plugin_dn(char *pluginDN)
{
    _PluginDN = pluginDN;
}

char *
rootdn_get_plugin_dn(void)
{
    return _PluginDN;
}


int32_t
rootdn_init(Slapi_PBlock *pb)
{
    int32_t status = 0;
    char *plugin_identity = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, ROOTDN_PLUGIN_SUBSYSTEM,
                  "--> rootdn_init\n");

    /* Store the plugin identity for later use.  Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    rootdn_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)rootdn_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)rootdn_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_init - Failed to register plugin\n");
        status = -1;
    }

    /* for this plugin we don't want to skip over root dn's when binding */
    slapi_set_plugin_open_rootdn_bind(pb);

    if (!status &&
        slapi_register_plugin("internalpreoperation",   /* op type */
                              1,                        /* Enabled */
                              "rootdn_preop_bind_init", /* this function desc */
                              rootdn_preop_bind_init,   /* init func */
                              ROOTDN_PLUGIN_DESC,       /* plugin desc */
                              NULL,                     /* ? */
                              plugin_identity           /* access control */
                              )) {
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                      "rootdn_init - Failed to register rootdn preoperation plugin\n");
        status = -1;
    }

    /*
     *  Load the config
     */
    if (rootdn_load_config(pb) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                      "rootdn_init - Unable to load plug-in configuration\n");
        return -1;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROOTDN_PLUGIN_SUBSYSTEM, "<-- rootdn_init\n");
    return status;
}

static int32_t
rootdn_preop_bind_init(Slapi_PBlock *pb)
{
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN, (void *)rootdn_check_access) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_preop_bind_init - "
                                                              "Failed to register function\n");
        return -1;
    }

    return 0;
}

static int32_t
rootdn_start(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_PLUGIN, ROOTDN_PLUGIN_SUBSYSTEM, "--> rootdn_start\n");

    rootdn_set_plugin_dn(ROOTDN_PLUGIN_DN);

    slapi_log_err(SLAPI_LOG_PLUGIN, ROOTDN_PLUGIN_SUBSYSTEM, "<-- rootdn_start\n");

    return 0;
}

static void
rootdn_free(void)
{
    slapi_ch_free_string(&daysAllowed);
    daysAllowed = NULL;
    slapi_ch_array_free(hosts);
    hosts = NULL;
    slapi_ch_array_free(hosts_to_deny);
    hosts_to_deny = NULL;
    slapi_ch_array_free(ips);
    ips = NULL;
    slapi_ch_array_free(ips_to_deny);
    ips_to_deny = NULL;
}

static int32_t
rootdn_close(Slapi_PBlock *pb __attribute__((unused)))
{
    rootdn_free();
    return 0;
}

static int32_t
rootdn_load_config(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    char *daysAllowed_tmp = NULL;
    char **hosts_tmp = NULL;
    char **hosts_to_deny_tmp = NULL;
    char **ips_tmp = NULL;
    char **ips_to_deny_tmp = NULL;
    const char *openTime = NULL;
    const char *closeTime = NULL;
    char *token, *iter = NULL, *copy;
    char hour[3], min[3];
    size_t end;
    int32_t result = 0;
    int32_t time;


    slapi_log_err(SLAPI_LOG_PLUGIN, ROOTDN_PLUGIN_SUBSYSTEM, "--> rootdn_load_config\n");

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &e) == 0) && e) {
        /*
         *  Grab our plugin settings
         */
        openTime = slapi_entry_attr_get_ref(e, "rootdn-open-time");
        closeTime = slapi_entry_attr_get_ref(e, "rootdn-close-time");
        daysAllowed_tmp = slapi_entry_attr_get_charptr(e, "rootdn-days-allowed");
        hosts_tmp = slapi_entry_attr_get_charray(e, "rootdn-allow-host");
        hosts_to_deny_tmp = slapi_entry_attr_get_charray(e, "rootdn-deny-host");
        ips_tmp = slapi_entry_attr_get_charray(e, "rootdn-allow-ip");
        ips_to_deny_tmp = slapi_entry_attr_get_charray(e, "rootdn-deny-ip");
        /*
         *  Validate out settings
         */
        if (daysAllowed_tmp) {
            strToLower(daysAllowed_tmp);
            end = strspn(daysAllowed_tmp, "abcdefghijklmnopqrstuvwxyz ,");
            if (!end || daysAllowed_tmp[end] != '\0') {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid rootdn-days-allowed value (%s), must be all letters, and comma separators\n",
                              daysAllowed_tmp);
                result = -1;
                goto free_and_return;
            }
            /* make sure the "days" are valid "days" */
            copy = slapi_ch_strdup(daysAllowed_tmp);
            token = ldap_utf8strtok_r(copy, ", ", &iter);
            while (token) {
                if (strstr("mon tue wed thu fri sat sun", token) == 0) {
                    slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                          "Invalid rootdn-days-allowed day value(%s), must be \"Mon, Tue, Wed, Thu, Fri, Sat, or Sun\".\n",
                                  token);
                    slapi_ch_free_string(&copy);
                    result = -1;
                    goto free_and_return;
                }
                token = ldap_utf8strtok_r(iter, ", ", &iter);
            }
            slapi_ch_free_string(&copy);
        }
        if (openTime) {
            end = strspn(openTime, "0123456789");
            if (!end || openTime[end] != '\0') {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid rootdn-open-time value (%s), must be all digits\n",
                              openTime);
                result = -1;
                goto free_and_return;
            }
            time = atoi(openTime);
            if (time > 2359 || time < 0) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid value for rootdn-open-time value (%s), value must be between 0000-2359\n",
                              openTime);
                result = -1;
                goto free_and_return;
            }
            if (strlen(openTime) != 4) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid format for rootdn-open-time value (%s).  Should be HHMM\n",
                              openTime);
                result = -1;
                goto free_and_return;
            }
            /*
             *  convert the time to all seconds
             */
            strncpy(hour, openTime, 2);
            strncpy(min, openTime + 2, 2);
            open_time = (time_t)(atoi(hour) * 3600) + (atoi(min) * 60);
        }
        if (closeTime) {
            end = strspn(closeTime, "0123456789");
            if (!end || closeTime[end] != '\0') {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid rootdn-close-time value (%s), must be all digits, and should be HHMM\n",
                              closeTime);
                result = -1;
                goto free_and_return;
            }
            time = atoi(closeTime);
            if (time > 2359 || time < 0) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid value for rootdn-close-time value (%s), value must be between 0000-2359\n",
                              closeTime);
                result = -1;
                goto free_and_return;
            }
            if (strlen(closeTime) != 4) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                      "Invalid format for rootdn-close-time value (%s), should be HHMM\n",
                              closeTime);
                result = -1;
                goto free_and_return;
            }
            /*
             *  convert the time to all seconds
             */
            strncpy(hour, closeTime, 2);
            strncpy(min, closeTime + 2, 2);
            close_time = (time_t)(atoi(hour) * 3600) + (atoi(min) * 60);
        }
        if ((openTime && closeTime == NULL) || (openTime == NULL && closeTime)) {
            /* If you are using TOD access control, you must have a open and close time */
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                  "There must be a open and a close time.  Ignoring time based settings.\n");
            open_time = 0;
            close_time = 0;
            result = -1;
            goto free_and_return;
        }
        if (close_time && open_time && close_time <= open_time) {
            /* Make sure the closing time is greater than the open time */
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                  "The close time must be greater than the open time\n");
            result = -1;
            goto free_and_return;
        }
        if (hosts_tmp) {
            for (size_t i = 0; hosts_tmp[i] != NULL; i++) {
                end = strspn(hosts_tmp[i], "0123456789.*-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
                if (!end || hosts_tmp[i][end] != '\0') {
                    slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                          "Hostname (%s) contains invalid characters, skipping\n",
                                  hosts_tmp[i]);
                    result = -1;
                    goto free_and_return;
                }
            }
        }
        if (hosts_to_deny_tmp) {
            for (size_t i = 0; hosts_to_deny_tmp[i] != NULL; i++) {
                end = strspn(hosts_to_deny_tmp[i], "0123456789.*-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
                if (!end || hosts_to_deny_tmp[i][end] != '\0') {
                    slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                          "Hostname (%s) contains invalid characters, skipping\n",
                                  hosts_to_deny_tmp[i]);
                    result = -1;
                    goto free_and_return;
                }
            }
        }
        if (ips_tmp) {
            for (size_t i = 0; ips_tmp[i] != NULL; i++) {
                end = strspn(ips_tmp[i], "0123456789:ABCDEFabcdef.*");
                if (!end || ips_tmp[i][end] != '\0') {
                    slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                          "IP address contains invalid characters (%s), skipping\n",
                                  ips_tmp[i]);
                    result = -1;
                    goto free_and_return;
                }
                if (strstr(ips_tmp[i], ":") == 0) {
                    /*
                     *  IPv4 - make sure it's just numbers, dots, and wildcard
                     */
                    end = strspn(ips_tmp[i], "0123456789.*");
                    if (!end || ips_tmp[i][end] != '\0') {
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                              "IPv4 address contains invalid characters (%s), skipping\n",
                                      ips_tmp[i]);
                        result = -1;
                        goto free_and_return;
                    }
                }
            }
        }
        if (ips_to_deny_tmp) {
            for (size_t i = 0; ips_to_deny_tmp[i] != NULL; i++) {
                end = strspn(ips_to_deny_tmp[i], "0123456789:ABCDEFabcdef.*");
                if (!end || ips_to_deny_tmp[i][end] != '\0') {
                    slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                          "IP address contains invalid characters (%s), skipping\n",
                                  ips_to_deny_tmp[i]);
                    result = -1;
                    goto free_and_return;
                }
                if (strstr(ips_to_deny_tmp[i], ":") == 0) {
                    /*
                     *  IPv4 - make sure it's just numbers, dots, and wildcard
                     */
                    end = strspn(ips_to_deny_tmp[i], "0123456789.*");
                    if (!end || ips_to_deny_tmp[i][end] != '\0') {
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                                              "IPv4 address contains invalid characters (%s), skipping\n",
                                      ips_to_deny_tmp[i]);
                        result = -1;
                        goto free_and_return;
                    }
                }
            }
        }
    } else {
        /* failed to get the plugin entry */
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_load_config - "
                                                              "Failed to get plugin entry\n");
        result = -1;
    }

    if (result == 0) {
        /*
         * Free the existing global vars, and move the new ones over
         */
        rootdn_free();
        daysAllowed = daysAllowed_tmp;
        daysAllowed_tmp = NULL;
        hosts = hosts_tmp;
        hosts_tmp = NULL;
        hosts_to_deny = hosts_to_deny_tmp;
        hosts_to_deny_tmp = NULL;
        ips = ips_tmp;
        ips_tmp = NULL;
        ips_to_deny = ips_to_deny_tmp;
        ips_to_deny_tmp = NULL;
    }

free_and_return:
    slapi_log_err(SLAPI_LOG_PLUGIN, ROOTDN_PLUGIN_SUBSYSTEM, "<-- rootdn_load_config (%d)\n", result);
    slapi_ch_free_string(&daysAllowed_tmp);
    if (hosts_tmp != NULL) {
        slapi_ch_array_free(hosts_tmp);
    }
    if (hosts_to_deny_tmp != NULL) {
        slapi_ch_array_free(hosts_to_deny_tmp);
    }
    if (ips_tmp != NULL) {
        slapi_ch_array_free(ips_tmp);
    }
    if (ips_to_deny_tmp != NULL) {
        slapi_ch_array_free(ips_to_deny_tmp);
    }
    return result;
}


static int32_t
rootdn_check_access(Slapi_PBlock *pb)
{
    PRNetAddr *server_addr = NULL;
    PRNetAddr *client_addr = NULL;
    PRHostEnt *host_entry = NULL;
    time_t curr_time;
    struct tm *timeinfo = NULL;
    char *dnsName = NULL;
    int32_t isRoot = 0;
    int32_t rc = SLAPI_PLUGIN_SUCCESS;

    /*
     *  Verify this is a root DN
     */
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isRoot);
    if (!isRoot) {
        return SLAPI_PLUGIN_SUCCESS;
    }
    /*
     *  grab the current time/info if we need it
     */
    if (open_time || daysAllowed) {
        curr_time = slapi_current_utc_time();
        timeinfo = localtime(&curr_time);
        if (timeinfo == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                "rootdn_check_access - Failed to get localtime\n");
            return -1;
        }
    }
    /*
     *  First check TOD restrictions, continue through if we are in the open "window"
     */
    if (open_time) {
        time_t curr_total;

        curr_total = (time_t)(timeinfo->tm_hour * 3600) + (timeinfo->tm_min * 60);

        if ((curr_total < open_time) || (curr_total >= close_time)) {
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                    "rootdn_check_access - Bind not in the allowed time window\n");
            return -1;
        }
    }
    /*
     *  Check if today is an allowed day
     */
    if (daysAllowed) {
        char *timestr;
        char today[4] = {0};

        timestr = asctime(timeinfo);  // DDD MMM dd hh:mm:ss YYYY
        memmove(today, timestr, 3);   // we only want the day
        strToLower(today);
        strToLower(daysAllowed);

        if (!strstr(daysAllowed, today)) {
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_check_access - "
                          "Bind not allowed for today(%s), only allowed on days: %s\n",
                          today, daysAllowed);
            return -1;
        }
    }

    server_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
    if (slapi_pblock_get(pb, SLAPI_CONN_SERVERNETADDR, server_addr) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                "rootdn_check_access - Could not get server address.\n");
        rc = -1;
        goto free_and_return;
    }
    /*
     *  Remaining checks are only relevant for AF_INET/AF_INET6 connections
     */
    if (PR_NetAddrFamily(server_addr) == PR_AF_LOCAL) {
        rc = 0;
        goto free_and_return;
    }

    /*
     *  Check the host restrictions, deny always overrides allow
     */
    if (hosts || hosts_to_deny) {
        char buf[PR_NETDB_BUF_SIZE] = {0};
        char *host;

        /*
         *  Get the client address
         */
        client_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
        if (slapi_pblock_get(pb, SLAPI_CONN_CLIENTNETADDR, client_addr) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                    "rootdn_check_access - Could not get client address for hosts.\n");
            rc = -1;
            goto free_and_return;
        }
        /*
         *  Get the hostname from the client address
         */
        host_entry = (PRHostEnt *)slapi_ch_malloc(sizeof(PRHostEnt));
        if (PR_GetHostByAddr(client_addr, buf, sizeof(buf), host_entry) == PR_SUCCESS) {
            if (host_entry->h_name != NULL) {
                /* copy the hostname */
                dnsName = slapi_ch_strdup(host_entry->h_name);
            } else {
                /* no hostname */
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                        "rootdn_check_access - Client address missing hostname\n");
                rc = -1;
                goto free_and_return;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                    "rootdn_check_access - client IP address could not be resolved\n");
            rc = -1;
            goto free_and_return;
        }
        /*
         *  Now we have our hostname, now do our checks
         */
        if (hosts_to_deny) {
            for (size_t i = 0; hosts_to_deny[i] != NULL; i++) {
                host = hosts_to_deny[i];
                /* check for wild cards */
                if (host[0] == '*') {
                    if (rootdn_check_host_wildcard(host, dnsName) == 0) {
                        /* match, return failure */
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_check_access - "
                             "hostname (%s) matched denied host (%s)\n", dnsName, host);
                        rc = -1;
                        goto free_and_return;
                    }
                } else {
                    if (strcasecmp(host, dnsName) == 0) {
                        /* we have a match, return failure */
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_check_access - "
                             "hostname (%s) matched denied host (%s)\n", dnsName, host);
                        rc = -1;
                        goto free_and_return;
                    }
                }
            }
            rc = 0;
        }
        if (hosts) {
            for (size_t i = 0; hosts[i] != NULL; i++) {
                host = hosts[i];
                /* check for wild cards */
                if (host[0] == '*') {
                    if (rootdn_check_host_wildcard(host, dnsName) == 0) {
                        /* match */
                        rc = 0;
                        goto free_and_return;
                    }
                } else {
                    if (strcasecmp(host, dnsName) == 0) {
                        /* we have a match, */
                        rc = 0;
                        goto free_and_return;
                    }
                }
            }
            rc = -1;
        }
    }
    /*
     *  Check the IP address restrictions, deny always overrides allow
     */
    if (ips || ips_to_deny) {
        char ip_str[256] = {0};
        char *ip;
        int32_t ip_len;

        if (client_addr == NULL) {
            client_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
            if (slapi_pblock_get(pb, SLAPI_CONN_CLIENTNETADDR, client_addr) != 0) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                        "rootdn_check_access - Could not get client address for IP.\n");
                rc = -1;
                goto free_and_return;
            }
        }
        /*
         *  Check if we are IPv4, so we can grab the correct IP addr for "ip_str"
         */
        if (PR_IsNetAddrType(client_addr, PR_IpAddrV4Mapped)) {
            PRNetAddr v4addr = {{0}};
            v4addr.inet.family = PR_AF_INET;
            v4addr.inet.ip = client_addr->ipv6.ip.pr_s6_addr32[3];
            if (PR_NetAddrToString(&v4addr, ip_str, sizeof(ip_str)) != PR_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                        "rootdn_check_access - Could not get IPv4 from client address.\n");
                rc = -1;
                goto free_and_return;
            }
        } else {
            if (PR_NetAddrToString(client_addr, ip_str, sizeof(ip_str)) != PR_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM,
                        "rootdn_check_access - Could not get IPv6 from client address.\n");
                rc = -1;
                goto free_and_return;
            }
        }
        /*
         *  Now we have our IP address, do our checks
         */
        if (ips_to_deny) {
            for (size_t i = 0; ips_to_deny[i] != NULL; i++) {
                ip = ips_to_deny[i];
                ip_len = strlen(ip);
                if (ip[ip_len - 1] == '*') {
                    if (rootdn_check_ip_wildcard(ips_to_deny[i], ip_str) == 0) {
                        /* match, return failure */
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_check_access - "
                             "ip address (%s) matched denied IP address (%s)\n", ip_str, ip);
                        rc = -1;
                        goto free_and_return;
                    }
                } else {
                    if (strcasecmp(ip_str, ip) == 0) {
                        /* match, return failure */
                        slapi_log_err(SLAPI_LOG_ERR, ROOTDN_PLUGIN_SUBSYSTEM, "rootdn_check_access - "
                             "ip address (%s) matched denied IP address (%s)\n", ip_str, ip);
                        rc = -1;
                        goto free_and_return;
                    }
                }
            }
            rc = 0;
        }
        if (ips) {
            for (size_t i = 0; ips[i] != NULL; i++) {
                ip = ips[i];
                ip_len = strlen(ip);
                if (ip[ip_len - 1] == '*') {
                    if (rootdn_check_ip_wildcard(ip, ip_str) == 0) {
                        /* match, return success */
                        rc = 0;
                        goto free_and_return;
                    }

                } else {
                    if (strcasecmp(ip_str, ip) == 0) {
                        /* match, return success */
                        rc = 0;
                        goto free_and_return;
                    }
                }
            }
            rc = -1;
        }
    }

free_and_return:
    slapi_ch_free((void **)&server_addr);
    slapi_ch_free((void **)&client_addr);
    slapi_ch_free((void **)&host_entry);
    slapi_ch_free_string(&dnsName);

    return rc;
}

static int32_t
rootdn_check_host_wildcard(char *host, char *client_host)
{
    size_t host_len = strlen(host);
    size_t client_len = strlen(client_host);
    size_t i, j;

    /*
     *  Start at the end of the string and move backwards, and skip the first char "*"
     */
    if (client_len < host_len) {
        /* this can't be a match */

        return -1;
    }
    for (i = host_len - 1, j = client_len - 1; i > 0; i--, j--) {
        if (host[i] != client_host[j]) {
            return -1;
        }
    }

    return 0;
}

static int
rootdn_check_ip_wildcard(char *ip, char *client_ip)
{
    size_t ip_len = strlen(ip);

    /*
     *  Start at the beginning of the string and move forward, and skip the last char "*"
     */
    if (strlen(client_ip) < ip_len) {
        /* this can't be a match */
        return -1;
    }
    for (size_t i = 0; i < ip_len - 1; i++) {
        if (ip[i] != client_ip[i]) {
            return -1;
        }
    }

    return 0;
}

void
strToLower(char *str)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-out-of-bounds"
    if (NULL != str) {
        for (char *pt = str; *pt != '\0'; pt++) {
            *pt = tolower(*pt);
        }
    }
#pragma GCC diagnostic pop
}
