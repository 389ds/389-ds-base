/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
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
#include <netinet/in.h>
#include <arpa/inet.h>
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
        slapi_log_err(SLAPI_LOG_CONNS, "haproxy_receive", "Recieved header is too long: %ld\n", hdr_len);
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

/**
 * Check if an IPv4 address is within a subnet.
 *
 * *****************************************************************************
 * NOTE: This function exists ONLY for unit testing purposes.
 * Production code MUST use haproxy_ip_matches_parsed() for connection validation.
 * *****************************************************************************
 *
 * Used by haproxy_ip_matches_cidr() for test convenience and by unit tests
 * for granular testing of IPv4 subnet logic.
 *
 * @param ip: The IP address to check (network byte order)
 * @param subnet: The subnet address (network byte order)
 * @param prefix_len: The prefix length (e.g., 24 for /24)
 *
 * @return: 1 if IP is in subnet, 0 otherwise
 */
int
haproxy_ipv4_in_subnet(uint32_t ip, uint32_t subnet, int prefix_len)
{
    uint32_t mask;

    /* Handle exact match case */
    if (prefix_len == 32) {
        return ip == subnet;
    }

    /* Handle match-all case */
    if (prefix_len == 0) {
        return 1;
    }

    /* Validate prefix length */
    if (prefix_len < 0 || prefix_len > 32) {
        return 0;
    }

    /* Create subnet mask and compare network portions - safe bit shift */
    mask = 0xFFFFFFFFu << (32 - prefix_len);
    mask = htonl(mask);
    return (ip & mask) == (subnet & mask);
}

/**
 * Check if an IPv6 address is within a subnet.
 *
 * *****************************************************************************
 * NOTE: This function exists ONLY for unit testing purposes.
 * Production code MUST use haproxy_ip_matches_parsed() for connection validation.
 * *****************************************************************************
 *
 * Used by haproxy_ip_matches_cidr() for test convenience and by unit tests
 * for granular testing of IPv6 subnet logic.
 *
 * Optimized for efficiency using 32-bit word comparisons throughout.
 *
 * @param ip: The IPv6 address to check
 * @param subnet: The subnet address
 * @param prefix_len: The prefix length (e.g., 64 for /64)
 *
 * @return: 1 if IP is in subnet, 0 otherwise
 */
int
haproxy_ipv6_in_subnet(const struct in6_addr *ip, const struct in6_addr *subnet, int prefix_len)
{
    const uint32_t *ip_u32 = (const uint32_t *)ip->s6_addr;
    const uint32_t *subnet_u32 = (const uint32_t *)subnet->s6_addr;
    int full_u32_words;
    int remaining_bits;
    uint32_t mask32;
    int last_word_idx;

    /* Handle exact match case - compare as four 32-bit integers with early exit */
    if (prefix_len == 128) {
        return (ip_u32[0] == subnet_u32[0] &&
                ip_u32[1] == subnet_u32[1] &&
                ip_u32[2] == subnet_u32[2] &&
                ip_u32[3] == subnet_u32[3]);
    }

    /* Handle match-all case */
    if (prefix_len == 0) {
        return 1;
    }

    /* Validate prefix length */
    if (prefix_len < 0 || prefix_len > 128) {
        return 0;
    }

    /*
     * Optimized comparison using 32-bit words throughout.
     * This is consistent with haproxy_ip_matches_parsed and avoids
     * switching between word and byte comparisons.
     */
    full_u32_words = prefix_len >> 5;  /* prefix_len / 32 - number of full 32-bit words */
    remaining_bits = prefix_len & 31;  /* prefix_len % 32 - bits in partial word */

    /* Compare full 32-bit words with early exit on mismatch */
    for (int i = 0; i < full_u32_words; i++) {
        if (ip_u32[i] != subnet_u32[i]) {
            return 0;
        }
    }

    /* Handle partial 32-bit word if prefix not on word boundary */
    if (remaining_bits > 0) {
        last_word_idx = full_u32_words;

        /* Create mask for the partial word - safe bit shift (remaining_bits is 1-31) */
        mask32 = 0xFFFFFFFFu << (32 - remaining_bits);
        mask32 = htonl(mask32);

        if ((ip_u32[last_word_idx] & mask32) != (subnet_u32[last_word_idx] & mask32)) {
            return 0;
        }
    }

    return 1;
}

/**
 * Check if an IP address matches a CIDR notation or exact IP (string-based utility).
 *
 * *****************************************************************************
 * NOTE: This function exists ONLY for unit testing purposes.
 * Production code MUST use haproxy_ip_matches_parsed() for connection validation.
 * *****************************************************************************
 *
 * This string-based function is convenient for writing readable tests but has
 * significant overhead (string parsing, address conversion). It is NOT used
 * anywhere in the production connection validation path.
 *
 * @param ip_str: The IP address to check (string format)
 * @param cidr_str: The CIDR notation or exact IP (e.g., "192.168.1.0/24" or "192.168.1.1")
 *
 * @return: 1 if match, 0 otherwise
 */
int
haproxy_ip_matches_cidr(const char *ip_str, const char *cidr_str)
{
    const char *slash_pos;
    char cidr_copy[MAX_CIDR_STRING_LEN];
    char *subnet_str;
    int prefix_len;
    PRNetAddr ip_addr;
    PRNetAddr subnet_addr;
    size_t cidr_len;

    /* Check if the CIDR string contains a slash */
    slash_pos = strchr(cidr_str, '/');
    if (slash_pos == NULL) {
        /* No CIDR notation - perform exact IP match */
        return (strcasecmp(ip_str, cidr_str) == 0);
    }

    /* Process CIDR notation */
    cidr_len = strlen(cidr_str);
    if (cidr_len >= sizeof(cidr_copy)) {
        slapi_log_err(SLAPI_LOG_ERR, "haproxy_ip_matches_cidr",
                      "CIDR string exceeds maximum length (%zu bytes, max %d)\n",
                      cidr_len, MAX_CIDR_STRING_LEN);
        return 0;
    }

    /* Copy CIDR string for tokenization */
    memcpy(cidr_copy, cidr_str, cidr_len);
    cidr_copy[cidr_len] = '\0';

    /* Split at slash to separate subnet address and prefix */
    subnet_str = cidr_copy;
    cidr_copy[slash_pos - cidr_str] = '\0';

    /* Extract prefix length with validation */
    {
        char *endptr;
        long prefix_long;
        errno = 0;
        prefix_long = strtol(slash_pos + 1, &endptr, 10);

        if (errno != 0 || *endptr != '\0' || endptr == slash_pos + 1 ||
            prefix_long < 0 || prefix_long > 128) {
            return 0;
        }
        prefix_len = (int)prefix_long;
    }

    /* Parse both IP addresses */
    if (PR_StringToNetAddr(ip_str, &ip_addr) != PR_SUCCESS) {
        return 0;
    }

    if (PR_StringToNetAddr(subnet_str, &subnet_addr) != PR_SUCCESS) {
        return 0;
    }

    /* Verify both addresses are the same family */
    if (ip_addr.raw.family != subnet_addr.raw.family) {
        return 0;
    }

    /* Perform subnet matching based on address family */
    if (ip_addr.raw.family == PR_AF_INET) {
        return haproxy_ipv4_in_subnet(ip_addr.inet.ip, subnet_addr.inet.ip, prefix_len);
    } else if (ip_addr.raw.family == PR_AF_INET6) {
        return haproxy_ipv6_in_subnet((const struct in6_addr *)&ip_addr.ipv6.ip,
                                      (const struct in6_addr *)&subnet_addr.ipv6.ip,
                                      prefix_len);
    }

    return 0;
}

/**
 * Parse trusted IP addresses and subnets into binary format with pre-computed netmasks.
 *
 * Converts string representations (e.g., "192.168.1.0/24") into optimized binary
 * structures containing network addresses and netmasks. This parsing is done once
 * at configuration time to avoid repeated string operations during connection handling.
 *
 * @param ipaddress: Array of berval IP addresses/subnets from configuration
 * @param count_out: Output parameter for number of parsed entries
 * @param errorbuf: Buffer for error messages (validation done in libglobs.c)
 *
 * @return: Array of parsed entries, or NULL on error
 */
haproxy_trusted_entry_t *
haproxy_parse_trusted_ips(struct berval **ipaddress, size_t *count_out, char *errorbuf)
{
    haproxy_trusted_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;

    if (!ipaddress) {
        *count_out = 0;
        return NULL;
    }

    /* Count entries */
    for (count = 0; ipaddress[count] != NULL; count++)
        ;

    if (count == 0) {
        *count_out = 0;
        return NULL;
    }

    /* Allocate array for parsed entries */
    entries = (haproxy_trusted_entry_t *)slapi_ch_calloc(count, sizeof(haproxy_trusted_entry_t));

    /* Parse each entry into binary format (validation already done in libglobs.c) */
    for (i = 0; i < count; i++) {
        char *ip_val = ipaddress[i]->bv_val;
        char *slash_pos = strchr(ip_val, '/');
        char ip_part[MAX_CIDR_STRING_LEN];
        int is_ipv6;

        /* Store original for logging */
        PL_strncpyz(entries[i].original, ip_val, sizeof(entries[i].original));

        /* Check for CIDR notation */
        if (slash_pos != NULL) {
            size_t ip_len = slash_pos - ip_val;
            char *endptr;
            long prefix_long;

            /* Validate IP portion length (need room for null terminator) */
            if (ip_len >= sizeof(ip_part) - 1) {
                slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                             "IP address portion too long in CIDR notation: %s\n", ip_val);
                slapi_ch_free((void **)&entries);
                *count_out = 0;
                return NULL;
            }

            memcpy(ip_part, ip_val, ip_len);
            ip_part[ip_len] = '\0';

            /* Parse prefix length with strict validation */
            errno = 0;
            prefix_long = strtol(slash_pos + 1, &endptr, 10);

            /* Validate: must be pure number, in valid range, no overflow */
            if (errno != 0 || *endptr != '\0' || endptr == slash_pos + 1 ||
                prefix_long < 0 || prefix_long > 128) {
                slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                             "Invalid CIDR prefix length in: %s (must be 0-128 for IPv6, 0-32 for IPv4)\n", ip_val);
                slapi_ch_free((void **)&entries);
                *count_out = 0;
                return NULL;
            }

            entries[i].prefix_len = (int)prefix_long;
            entries[i].is_subnet = 1;

            /* Parse network address */
            if (PR_StringToNetAddr(ip_part, &entries[i].network) != PR_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                             "Failed to parse network address: %s\n", ip_part);
                slapi_ch_free((void **)&entries);
                *count_out = 0;
                return NULL;
            }

            /* Create netmask based on address family and prefix */
            if (entries[i].network.raw.family == PR_AF_INET) {
                /* Validate IPv4 prefix range before bit operations */
                if (entries[i].prefix_len > 32) {
                    slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                                 "IPv4 CIDR prefix must be 0-32, got %d in: %s\n",
                                 entries[i].prefix_len, ip_val);
                    slapi_ch_free((void **)&entries);
                    *count_out = 0;
                    return NULL;
                }

                /* Calculate netmask safely - avoid undefined behavior */
                uint32_t mask;
                if (entries[i].prefix_len == 0) {
                    mask = 0;
                } else {
                    /* Safe bit shift: prefix_len is validated to be 1-32 */
                    mask = 0xFFFFFFFFu << (32 - entries[i].prefix_len);
                }
                entries[i].netmask.inet.family = PR_AF_INET;
                entries[i].netmask.inet.ip = htonl(mask);
                /* Pre-apply mask to network address for faster matching */
                entries[i].network.inet.ip &= entries[i].netmask.inet.ip;
            } else if (entries[i].network.raw.family == PR_AF_INET6) {
                /* Validate IPv6 prefix range */
                if (entries[i].prefix_len > 128) {
                    slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                                 "IPv6 CIDR prefix must be 0-128, got %d in: %s\n",
                                 entries[i].prefix_len, ip_val);
                    slapi_ch_free((void **)&entries);
                    *count_out = 0;
                    return NULL;
                }
                /*
                 * Build IPv6 netmask from CIDR prefix length.
                 * For /48: first 6 bytes = 0xFF, remaining bytes = 0x00
                 * For /33: first 4 bytes = 0xFF, 5th byte = 0x80, rest = 0x00
                 */
                int full_bytes = entries[i].prefix_len >> 3;      /* prefix_len / 8 */
                int remaining_bits = entries[i].prefix_len & 7;   /* prefix_len % 8 */
                int j;

                entries[i].netmask.ipv6.family = PR_AF_INET6;
                memset(&entries[i].netmask.ipv6.ip, 0, sizeof(entries[i].netmask.ipv6.ip));

                /* Set full bytes to 0xFF */
                for (j = 0; j < full_bytes; j++) {
                    entries[i].netmask.ipv6.ip.pr_s6_addr[j] = 0xFF;
                }

                /* Set partial byte mask if prefix not on byte boundary */
                if (remaining_bits > 0) {
                    entries[i].netmask.ipv6.ip.pr_s6_addr[full_bytes] =
                        (uint8_t)(0xFF << (8 - remaining_bits));
                }
                /* Remaining bytes are already 0x00 from memset */

                /* Pre-apply mask to network address for faster matching */
                uint32_t *net_u32 = (uint32_t *)entries[i].network.ipv6.ip.pr_s6_addr;
                const uint32_t *mask_u32 = (const uint32_t *)entries[i].netmask.ipv6.ip.pr_s6_addr;
                for (j = 0; j < 4; j++) {
                    net_u32[j] &= mask_u32[j];
                }
            }
        } else {
            /* Single IP address (no CIDR) */
            entries[i].is_subnet = 0;
            entries[i].prefix_len = -1;

            /* Parse as regular IP address */
            if (PR_StringToNetAddr(ip_val, &entries[i].network) != PR_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "haproxy_parse_trusted_ips",
                             "Failed to parse IP address: %s\n", ip_val);
                slapi_ch_free((void **)&entries);
                *count_out = 0;
                return NULL;
            }
        }
    }

    *count_out = count;
    return entries;
}

/**
 * Check if an IP address matches any trusted entry using parsed binary format.
 *
 * Compares the given IP address against an array of parsed trusted entries.
 * Supports exact IP matching and CIDR subnet matching.
 * Handles IPv4, IPv6, and IPv4-mapped IPv6 addresses.
 *
 * @param ip_addr: The IP address to check (from connection)
 * @param entries: Array of parsed trusted entries (from config_get_haproxy_trusted_ip_parsed)
 * @param entry_count: Number of entries in the array
 *
 * @return: 1 if IP matches any entry, 0 otherwise
 */
int
haproxy_ip_matches_parsed(const PRNetAddr *ip_addr, const haproxy_trusted_entry_t *entries, size_t entry_count)
{
    size_t i;
    PRNetAddr normalized_ip;
    char ip_str[INET6_ADDRSTRLEN];

    if (!entries || entry_count == 0 || !ip_addr) {
        return 0;
    }

    /* Normalize IPv4-mapped IPv6 addresses for consistent matching */
    if (PR_IsNetAddrType(ip_addr, PR_IpAddrV4Mapped)) {
        normalized_ip.inet.family = PR_AF_INET;
        normalized_ip.inet.ip = ip_addr->ipv6.ip.pr_s6_addr32[3];
    } else {
        memcpy(&normalized_ip, ip_addr, sizeof(PRNetAddr));
    }

    /* Check against each trusted entry */
    for (i = 0; i < entry_count; i++) {
        const haproxy_trusted_entry_t *entry = &entries[i];

        /* Skip if address families don't match */
        if (normalized_ip.raw.family != entry->network.raw.family) {
            continue;
        }

        /* Exact IP matching (non-subnet) */
        if (!entry->is_subnet) {
            if (normalized_ip.raw.family == PR_AF_INET) {
                if (normalized_ip.inet.ip == entry->network.inet.ip) {
                    return 1;
                }
            } else if (normalized_ip.raw.family == PR_AF_INET6) {
                /* Compare IPv6 as four 32-bit words with early exit on mismatch */
                const uint32_t *ip_u32 = (const uint32_t *)normalized_ip.ipv6.ip.pr_s6_addr;
                const uint32_t *net_u32 = (const uint32_t *)entry->network.ipv6.ip.pr_s6_addr;
                if (ip_u32[0] != net_u32[0]) continue;
                if (ip_u32[1] != net_u32[1]) continue;
                if (ip_u32[2] != net_u32[2]) continue;
                if (ip_u32[3] != net_u32[3]) continue;
                return 1;
            }
        } else {
            /* CIDR subnet matching using stored netmask */
            if (normalized_ip.raw.family == PR_AF_INET) {
                /*
                 * IPv4 subnet check: apply netmask to incoming IP only.
                 * Network address was already masked during parsing.
                 */
                uint32_t mask = entry->netmask.inet.ip;
                if ((normalized_ip.inet.ip & mask) == entry->network.inet.ip) {
                    return 1;
                }
            } else if (normalized_ip.raw.family == PR_AF_INET6) {
                /*
                 * IPv6 subnet check: apply netmask to incoming IP only.
                 * Network address was already masked during parsing.
                 * Compare as four 32-bit words with early exit for efficiency.
                 */
                const uint32_t *ip_u32 = (const uint32_t *)normalized_ip.ipv6.ip.pr_s6_addr;
                const uint32_t *net_u32 = (const uint32_t *)entry->network.ipv6.ip.pr_s6_addr;
                const uint32_t *mask_u32 = (const uint32_t *)entry->netmask.ipv6.ip.pr_s6_addr;

                /* Check each 32-bit word with early exit on mismatch */
                if ((ip_u32[0] & mask_u32[0]) != net_u32[0]) continue;
                if ((ip_u32[1] & mask_u32[1]) != net_u32[1]) continue;
                if ((ip_u32[2] & mask_u32[2]) != net_u32[2]) continue;
                if ((ip_u32[3] & mask_u32[3]) != net_u32[3]) continue;
                return 1;
            }
        }
    }

    return 0;
}
