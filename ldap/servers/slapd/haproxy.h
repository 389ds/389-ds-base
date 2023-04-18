/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include <prnetdb.h>

 /*
  * Begin protocol v2 definitions from haproxy/include/types/connection.h.
  */
#define PP2_SIGNATURE		"\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A"
#define PP2_SIGNATURE_LEN	12
#define PP2_HEADER_LEN		16

/* ver_cmd byte */
#define PP2_VER_CMD_LOCAL	0x00
#define PP2_VER_CMD_PROXY	0x01

#define PP2_VERSION			0x20

/* Family byte */
#define PP2_TRANS_UNSPEC	0x00
#define PP2_TRANS_STREAM	0x01
#define PP2_FAM_UNSPEC		0x00
#define PP2_FAM_INET		0x10
#define PP2_FAM_INET6		0x20

/* len field (2 bytes) */
#define PP2_ADDR_LEN_UNSPEC	(0)
#define PP2_ADDR_LEN_INET	(4 + 4 + 2 + 2)
#define PP2_ADDR_LEN_INET6	(16 + 16 + 2 + 2)
#define PP2_HDR_LEN_UNSPEC	(PP2_HEADER_LEN + PP2_ADDR_LEN_UNSPEC)
#define PP2_HDR_LEN_INET	(PP2_HEADER_LEN + PP2_ADDR_LEN_INET)
#define PP2_HDR_LEN_INET6	(PP2_HEADER_LEN + PP2_ADDR_LEN_INET6)

/* Both formats (v1 and v2) are designed to fit in the smallest TCP segment
 * that any TCP/IP host is required to support (576 - 40 = 536 bytes).
 */
#define HAPROXY_HEADER_MAX_LEN 536

/* Define struct for the proxy header */
struct proxy_hdr_v2 {
    uint8_t sig[PP2_SIGNATURE_LEN];	/* PP2_SIGNATURE */
    uint8_t ver_cmd;			/* protocol version | command */
    uint8_t fam;			/* protocol family and transport */
    uint16_t len;			/* length of remainder */
    union {
    struct {			/* for TCP/UDP over IPv4, len = 12 */
        uint32_t src_addr;
        uint32_t dst_addr;
        uint16_t src_port;
        uint16_t dst_port;
    }       ip4;
    struct {			/* for TCP/UDP over IPv6, len = 36 */
        uint8_t src_addr[16];
        uint8_t dst_addr[16];
        uint16_t src_port;
        uint16_t dst_port;
    }       ip6;
    struct {			/* for AF_UNIX sockets, len = 216 */
        uint8_t src_addr[108];
        uint8_t dst_addr[108];
    }       unx;
    }       addr;
};

int haproxy_parse_v1_hdr(const char *str, size_t *str_len, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest);
int haproxy_parse_v2_hdr(const char *str, size_t *str_len, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest);
int haproxy_receive(int fd, int *proxy_connection, PRNetAddr *pr_netaddr_from, PRNetAddr *pr_netaddr_dest);
