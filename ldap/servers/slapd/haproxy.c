/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* haproxy.c - process connection PROXY header if present */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include "slap.h"

/* Function to parse IPv4 addresses in version 2 */
static int haproxy_parse_v2_addr_v4(uint32_t in_addr, unsigned in_port, PRNetAddr *pr_netaddr)
{
    char addr[INET_ADDRSTRLEN];

    /* Check if the port is valid */
    if (in_port > 65535) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v4", "Port number exceeds maximum value.\n");
        return -1;
    }

    /* Assign the input address and port to the PRNetAddr structure */
    pr_netaddr->inet.family = PR_AF_INET;
    pr_netaddr->inet.port = in_port;
    pr_netaddr->inet.ip = in_addr;

    /* Print the address in a human-readable format */
    if (inet_ntop(AF_INET, &(pr_netaddr->inet.ip), addr, INET_ADDRSTRLEN) == NULL) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v4", "Failed to print address.\n");
    } else {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v4", "Address: %s\n", addr);
    }
    return 0;
}


/* Function to parse IPv6 addresses in version 2 */
static int haproxy_parse_v2_addr_v6(uint8_t *in6_addr, unsigned in6_port, PRNetAddr *pr_netaddr)
{
    struct sockaddr_in6 sin6;
    char addr[INET6_ADDRSTRLEN];

    /* Check if the port is valid */
    if (in6_port > 65535) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v6", "Port number exceeds maximum value.\n");
        return -1;
    }

    /* Assign the input address and port to the PRNetAddr structure */
    memset((void *) &sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    memcpy(&sin6.sin6_addr, in6_addr, 16);
    memcpy(&pr_netaddr->ipv6.ip, &sin6.sin6_addr, sizeof(pr_netaddr->ipv6.ip));
    pr_netaddr->ipv6.port = in6_port;
    pr_netaddr->ipv6.family = PR_AF_INET6;

    /* Print the address in a human-readable format */
    if (inet_ntop(AF_INET6, &(pr_netaddr->ipv6.ip), addr, INET6_ADDRSTRLEN) == NULL) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v6", "Failed to print address.\n");
    } else {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_addr_v6", "Address: %s\n", addr);
    }
    return 0;
}


/* Function to parse the header in version 2 */
int haproxy_parse_v2_hdr(const char *str, size_t *str_len, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest)
{
    struct proxy_hdr_v2 *hdr_v2 = (struct proxy_hdr_v2 *) str;
    uint16_t hdr_v2_len = 0;
    PRNetAddr parsed_addr_from = {{0}};
    PRNetAddr parsed_addr_dest = {{0}};
    int rc = HAPROXY_ERROR;

    *proxy_connection = 0;

    /* Check if we received enough bytes to contain the HAProxy v2 header */
    if (*str_len < PP2_HEADER_LEN) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Protocol header is short\n");
        rc = HAPROXY_NOT_A_HEADER;
        goto done;
    }
    hdr_v2_len = ntohs(hdr_v2->len);

    if (memcmp(hdr_v2->sig, PP2_SIGNATURE, PP2_SIGNATURE_LEN) != 0) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Protocol header is invalid\n");
        rc = HAPROXY_NOT_A_HEADER;
        goto done;
    }

    /* Check if the header has the correct signature */
    if ((hdr_v2->ver_cmd & 0xF0) != PP2_VERSION) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Protocol version is invalid\n");
        goto done;
    }
    /* Check if we received enough bytes to contain the entire HAProxy v2 header, including the address information */
    if (*str_len < PP2_HEADER_LEN + hdr_v2_len) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Protocol header v2 is short\n");
        goto done;
    }

    switch (hdr_v2->ver_cmd & 0x0F) {
        case PP2_VER_CMD_PROXY:
            /* Process the header based on the address family */
            switch (hdr_v2->fam) {
            case PP2_FAM_INET | PP2_TRANS_STREAM:{	/* TCP over IPv4 */
                if (hdr_v2_len < PP2_ADDR_LEN_INET) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Address field is short\n");
                    goto done;
                }
                if (haproxy_parse_v2_addr_v4(hdr_v2->addr.ip4.src_addr, hdr_v2->addr.ip4.src_port, &parsed_addr_from) < 0) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Client address is invalid\n");
                    goto done;
                }
                if (haproxy_parse_v2_addr_v4(hdr_v2->addr.ip4.dst_addr, hdr_v2->addr.ip4.dst_port, &parsed_addr_dest) < 0) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Server address is invalid\n");
                    goto done;
                }
                break;
                }
            case PP2_FAM_INET6 | PP2_TRANS_STREAM:{/* TCP over IPv6 */
                if (hdr_v2_len < PP2_ADDR_LEN_INET6) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Address field is short\n");
                    goto done;
                }
                if (haproxy_parse_v2_addr_v6(hdr_v2->addr.ip6.src_addr, hdr_v2->addr.ip6.src_port, &parsed_addr_from) < 0) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Client address is invalid\n");
                    goto done;
                }
                if (haproxy_parse_v2_addr_v6(hdr_v2->addr.ip6.dst_addr, hdr_v2->addr.ip6.dst_port, &parsed_addr_dest) < 0) {
                    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Server address is invalid\n");
                    goto done;
                }
                break;
                }
            default:
                slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Unsupported address family\n");
                goto done;
            }
            /* Update the received string length to include the address information */
            *str_len = PP2_HEADER_LEN + hdr_v2_len;
            rc = HAPROXY_HEADER_PARSED;
            *proxy_connection = 1;
            /* Copy the parsed addresses to the output parameters */
            memcpy(pr_netaddr_from, &parsed_addr_from, sizeof(PRNetAddr));
            memcpy(pr_netaddr_dest, &parsed_addr_dest, sizeof(PRNetAddr));
            goto done;
        /* If it's a LOCAL command, there's no address information to parse, so just update the received string length */
        case PP2_VER_CMD_LOCAL:
            *str_len = PP2_HEADER_LEN + hdr_v2_len;
            rc = HAPROXY_HEADER_PARSED;
            goto done;
        default:
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v2_hdr", "Invalid header command\n");
            goto done;
    }
done:
    return rc;
}


/* Function to parse the protocol in version 1 */
static int haproxy_parse_v1_protocol(const char *str, const char *protocol)
{
    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_protocol", "HAProxy protocol - %s\n", str ? str : "(null)");
    if ((str != 0) && (strcasecmp(str, protocol) == 0)) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_protocol", "HAProxy protocol is valid\n");
        return 0;
    }
    return -1;
}


/* Function to parse the family (i.e., IPv4 or IPv6) in version 1 */
static int haproxy_parse_v1_fam(const char *str, int *addr_family)
{
    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_fam", "Address family - %s\n", str ? str : "(null)");
    if (str == 0) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_fam", "Address family is missing\n");
        return -1;
    }

    if (strcasecmp(str, "TCP4") == 0) {
        *addr_family = AF_INET;
        return 0;
    } else if (strcasecmp(str, "TCP6") == 0) {
        *addr_family = AF_INET6;
        return 0;
    } else {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_fam", "Address family %s is unsupported\n", str);
        return -1;
    }
}


/* Function to parse addresses in version 1 */
static int haproxy_parse_v1_addr(const char *str, PRNetAddr *pr_netaddr, int addr_family)
{
    char addrbuf[256];

    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "addr=%s proto=%d\n", str ? str : "(null)", addr_family);
    if (str == 0 || strlen(str) >= sizeof(addrbuf)) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "incorrect IP address: %s\n", str);
        return -1;
    }

    switch (addr_family) {
        case AF_INET6:
            if (slapi_is_ipv6_addr(str)) {
                slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "ipv6 address: %s\n", str);
                pr_netaddr->ipv6.family = PR_AF_INET6;
            }
            break;
        case AF_INET:
            if (slapi_is_ipv4_addr(str)) {
                slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "ipv4 address: %s\n", str);
                pr_netaddr->inet.family = PR_AF_INET;
            }
            break;
        default:
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "incorrect address family: %d\n", addr_family);
            return -1;
    }

    if (PR_StringToNetAddr(str, pr_netaddr) != PR_SUCCESS) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_addr", "Failed to set IP address: %s\n", str);
        return -1;
    }

    return 0;
}


/* Function to parse port numbers in version 1 */
static int haproxy_parse_v1_port(const char *str, PRNetAddr *pr_netaddr)
{
    char *endptr;
    long port;

    slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_port", "port=%s\n", str ? str : "(null)");
    errno = 0;  /* Reset errno to 0 before calling strtol */
    port = strtol(str, &endptr, 10);

    /* Check for conversion errors */
    if (errno == ERANGE || port < 0 || port > 65535) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_port", "Port is out of range: %s\n", str);
        return -1;
    }
    if (endptr == str || *endptr != '\0') {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_port", "No digits were found: %s\n", str);
        return -1;
    }

    /* Successfully parsed the port number. Set it */
    PRLDAP_SET_PORT(pr_netaddr, port);
    return 0;
}


static inline char *get_next_token(char **copied) {
    return tokenize_string(copied, " \r");
}


/* Function to parse the header in version 1 */
int haproxy_parse_v1_hdr(const char *str, size_t *str_len, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest)
{
    PRNetAddr parsed_addr_from = {{0}};
    PRNetAddr parsed_addr_dest = {{0}};
    char *str_saved = NULL;
    char *copied = NULL;
    char *after_header = NULL;
    int addr_family;
    int rc = HAPROXY_ERROR;

    *proxy_connection = 0;
    if (strncmp(str, "PROXY ", 6) == 0) {
        str_saved = slapi_ch_strdup(str);
        copied = str_saved;
        after_header = split_string_at_delim(str_saved, '\n');

        /* Check if the header is valid */
        if (after_header == 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing protocol header terminator\n");
            goto done;
        }
        /* Parse the protocol, family, addresses, and ports */
        if (haproxy_parse_v1_protocol(get_next_token(&copied), "PROXY") < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad protocol header\n");
            goto done;
        }
        /* Parse the family */
        if (haproxy_parse_v1_fam(get_next_token(&copied), &addr_family) < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad protocol type\n");
            goto done;
        }
        /* Parse the addresses */
        if (haproxy_parse_v1_addr(get_next_token(&copied), &parsed_addr_from, addr_family) < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad client address\n");
            goto done;
        }
        if (haproxy_parse_v1_addr(get_next_token(&copied), &parsed_addr_dest, addr_family) < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad server address\n");
            goto done;
        }
        /* Parse the ports */
        if (haproxy_parse_v1_port(get_next_token(&copied), &parsed_addr_from) < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad client port\n");
            goto done;
        }
        if (haproxy_parse_v1_port(get_next_token(&copied), &parsed_addr_dest) < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "haproxy_parse_v1_hdr", "Missing or bad server port\n");
            goto done;
        }
        rc = HAPROXY_HEADER_PARSED;
        *proxy_connection = 1;
        *str_len = after_header - str_saved;
        /* Copy the parsed addresses to the output parameters */
        memcpy(pr_netaddr_from, &parsed_addr_from, sizeof(PRNetAddr));
        memcpy(pr_netaddr_dest, &parsed_addr_dest, sizeof(PRNetAddr));

done:
        slapi_ch_free_string(&str_saved);
    } else {
        rc = HAPROXY_NOT_A_HEADER;
    }
    return rc;
}

/**
 * Function to receive and parse HAProxy headers, supporting both v1 and v2 of the protocol.
 * 
 * @param fd: The file descriptor of the socket from which to read.
 * @param proxy_connection: A pointer to an integer to store the proxy connection status (0 or 1).
 * @param pr_netaddr_from: A pointer to a PRNetAddr structure to store the source address info.
 * @param pr_netaddr_dest: A pointer to a PRNetAddr structure to store the destination address info.
 * 
 * @return: Returns 0 on successful operation, -1 on error.
 */
int haproxy_receive(int fd, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest)
{
    /* Buffer to store the header received from the HAProxy server */
    char hdr[HAPROXY_HEADER_MAX_LEN + 1] = {0};
    ssize_t recv_result = 0;
    size_t hdr_len;
    int rc = HAPROXY_ERROR;

    /* Attempt to receive the header from the HAProxy server */
    recv_result = recv(fd, hdr, sizeof(hdr) - 1,  MSG_PEEK | MSG_DONTWAIT);
    if (recv_result <= 0) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_receive", "EOF or error on haproxy socket: %s\n", strerror(errno));
        return rc;
    } else {
        hdr_len = recv_result;
    }

    /* Null-terminate the header string */
    if (hdr_len < sizeof(hdr)) {
        hdr[hdr_len] = 0;
    } else {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_receive", "Recieved header is too long: %d\n", hdr_len);
        rc = HAPROXY_NOT_A_HEADER;
        return rc;
    }

    rc = haproxy_parse_v1_hdr(hdr, &hdr_len, proxy_connection, pr_netaddr_from, pr_netaddr_dest);
    if (rc == HAPROXY_NOT_A_HEADER) {
        rc = haproxy_parse_v2_hdr(hdr, &hdr_len, proxy_connection, pr_netaddr_from, pr_netaddr_dest);
    }

    if (rc == HAPROXY_HEADER_PARSED) {
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_receive", "HAProxy header parsed successfully\n");
        /* Consume the data from the socket */
        recv_result = recv(fd, hdr, hdr_len, MSG_DONTWAIT);

        if (recv_result != hdr_len) {
            slapi_log_err(SLAPI_LOG_ERR, "haproxy_receive", "Read error: %s: %s\n", hdr, strerror(errno));
            return HAPROXY_ERROR;
        }
    }
    return rc;
}
