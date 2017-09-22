/* nbnsd - A minimal NetBIOS Name Service responder
   Copyright (C) 2010 eGauge Systems LLC
	Contributed by David Mosberger-Tang <davidm@egauge.net>

This file is part of nbnsd.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#define _BSD_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#define NB_NAME_LEN    16

#define NB_NSRR        0x20    /* NetBIOS general Name Svc Resource Record */
#define NB_IN_CLASS    0x01    /* Internet class */

#define NM_FLAG_AA    (1 << 6)    /* Authoritative Answer */
#define NM_FLAG_TC    (1 << 5)    /* Truncated */
#define NM_FLAG_RD    (1 << 4)    /* Recursion Desired */
#define NM_FLAG_RA    (1 << 3)    /* Recursion Available */
#define NM_FLAG_B    (1 << 0)

struct nb_header {
    uint8_t name_trn_id[2];
    uint8_t flags[2];
    uint8_t qdcount[2];
    uint8_t ancount[2];
    uint8_t nscount[2];
    uint8_t arcount[2];
};

enum NB_OPCODE {
    NB_OPCODE_QUERY = 0,
    NB_OPCODE_REGISTRATION = 5,
    NB_OPCODE_RELEASE = 6,
    NB_OPCODE_WACK = 7,
    NB_OPCODE_REFRESH = 8,
    NB_OPCODE_RESPONSE = 0x10
};

/* NetBIOS Suffixes (last byte in a NetBIOS name) as per
   http://en.wikipedia.org/wiki/NetBIOS.  */
enum NB_SUFFIX {
    NB_WORKSTATION_SVC = 0x00,
    NB_MESSENGER_SVC = 0x03,
    NB_FILE_SVC = 0x20,
    NB_DOMAIN_MASTER_BROWSER = 0x1b,
    NB_DOMAIN_CONTROLLER = 0x1c,
    NB_MASTER_BROWSER = 0x01,
    NB_BROWSER_SVC_ELECTIONS = 0x1e
};

const char *prog_name;
int verbose = 0;

static unsigned int port = 137;

static inline uint16_t
get16(uint8_t *dp) {
    return dp[1] | ((uint16_t) dp[0] << 8);
}

static inline void
put16(uint8_t *dp, uint16_t val) {
    dp[0] = val >> 8;
    dp[1] = val;
}

static inline void
put32(uint8_t *dp, uint32_t val) {
    dp[0] = val >> 24;
    dp[1] = val >> 16;
    dp[2] = val >> 8;
    dp[3] = val;
}

static int
decode_nb_name(char *buf, size_t buf_size, void *data) {
    uint8_t *str = data, hi, lo;
    uint8_t len = *str++;
    char *dst, *end;

    if (buf_size < 1)
        return -1;

    dst = buf;
    end = dst + buf_size - 1;    /* leave space for trailing NUL */

    if (len & 1) {
        fprintf(stderr, "%s: query name length %u not a multiple of 2!?!",
                prog_name, len);
        return -1;
    }
    while (len > 0) {
        hi = *str++;
        lo = *str++;
        if (dst >= end)
            break;
        *dst++ = ((hi - 'A') << 4) + (lo - 'A');
        len -= 2;
    }
    *dst++ = '\0';
    len = str - (uint8_t *) data;
    if (len & 1)
        ++len;
    return len;
}

static int
set_nb_name(char *buf, size_t buf_size, const char *str) {
    int i;

    for (i = 0; i < buf_size - 1; ++i) {
        if (!str[i])
            break;
        buf[i] = str[i];
    }
    while (i < buf_size - 1)
        buf[i++] = ' ';
    buf[buf_size - 1] = NB_WORKSTATION_SVC;
    return 0;
}

static int
get_nb_name(char *buf, size_t buf_size) {
    char hostname[256], *dot;
    int ret;

    ret = gethostname(hostname, sizeof(hostname));
    if (ret < 0) {
        perror("gethostname");
        return -1;
    }
    hostname[sizeof(hostname) - 1] = '\0';
    dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';
    return set_nb_name(buf, buf_size, hostname);
}

static void
usage(int detailed) {
    fprintf(stderr, "Usage: %s [-hv] [-n name]\n", prog_name);
    if (detailed)
        fprintf(stderr,
                "\t-n:\tSet NAME as the NetBIOS name.\n"
                        "\t-m:\talso fallback to hostname.\n"
                        "\t-h:\tPrint this help message.\n"
                        "\t-v:\tBe more verbose.\n");
}

int
main(int argc, char **argv) {
    uint16_t tid, flags, qdcount, ancount, arcount, opcode, nm_flags;
    char nb_name[NB_NAME_LEN], my_nb_name[NB_NAME_LEN], my_nb_hostname[NB_NAME_LEN];
    uint16_t rcode, nb, in, ttl, rdlength, nb_flags;
    uint8_t pkt[2000], *data;
    struct sockaddr_in sin, from;
    int ret, sd, on = 1, opt;
    struct nb_header *hdr;
    socklen_t from_len;
    ssize_t len;
    int fallback_hostname = 0, found;

    struct iovec iov[1];
    iov[0].iov_base=pkt;
    iov[0].iov_len=sizeof(pkt);

    // the control data is dumped here
    char cmbuf[0x100];
    // the remote/source sockaddr is put here
    struct sockaddr_in peeraddr;
    // if you want access to the data you need to init the msg_iovec fields
    struct msghdr mh = {
            .msg_name = &peeraddr,
            .msg_namelen = sizeof(peeraddr),
            .msg_iov = iov,
            .msg_iovlen = 1,
            .msg_control = cmbuf,
            .msg_controllen = sizeof(cmbuf),
    };

    my_nb_name[0] = 0;
    my_nb_hostname[0] = 0;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    prog_name = strrchr(argv[0], '/');
    if (prog_name)
        ++prog_name;
    else
        prog_name = argv[0];

    if (get_nb_name(my_nb_hostname, sizeof(my_nb_hostname)) < 0)
        return -1;

    while ((opt = getopt(argc, argv, "hmn:v")) != -1) {
        switch (opt) {
            case 'h':
                usage(1);
                exit(0);

            case 'm':
                fallback_hostname = 1;
                break;

            case 'n':
                if (set_nb_name(my_nb_name, sizeof(my_nb_name), optarg) < 0)
                    return -1;
                break;

            case 'v':
                ++verbose;
                break;

            default:
                fprintf(stderr, "%s: unknown option `%c'\n", prog_name, opt);
                usage(0);
        }
    }
    if (my_nb_name[0] == 0 && !fallback_hostname) {
        set_nb_name(my_nb_name, sizeof(my_nb_name), my_nb_hostname);
    }
    if (verbose) {
        printf("%s: my_nb_name=`%s' (0x%x)\n",
                prog_name, my_nb_name, my_nb_name[sizeof(my_nb_name) - 1]);
        if (fallback_hostname) {
            printf("%s: my_nb_hostname=`%s' (0x%x)\n",
                    prog_name, my_nb_hostname,
                    my_nb_hostname[sizeof(my_nb_hostname) - 1]);
        }
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        fprintf(stderr, "%s: failed to create server port %d (%s)",
                prog_name, port, strerror(errno));
        return -1;
    }

    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        fprintf(stderr, "%s: failed to turn on SO_REUSEADDR on port %d (%s)\n",
                prog_name, port, strerror(errno));

    ret = bind(sd, (struct sockaddr *) &sin, sizeof(sin));
    if (ret < 0) {
        fprintf(stderr, "%s: failed to bind port %d (%s)\n",
                prog_name, port, strerror(errno));
        return -1;
    }

    if (setsockopt(sd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on))) {
        fprintf(stderr, "%s: failed to turn on IP_PKTINFO on port %d (%s)\n",
                prog_name, port, strerror(errno));
        return -1;
    }

    while (1) {
        from_len = sizeof(from);
        len = recvmsg(sd, &mh, MSG_WAITALL);
        if (len == -1) {
            printf("error receiving: %s\n", strerror(errno));
            return -1;
        } else if (mh.msg_flags & MSG_TRUNC) {
            if (verbose) {
                printf("error receiving: truncated large message\n");
            }
            continue;
        }
        //len = recvfrom(sd, pkt, sizeof(pkt), MSG_WAITALL,
        //        (struct sockaddr *) &from, &from_len);
        hdr = (struct nb_header *) pkt;
        tid = get16(hdr->name_trn_id);
        flags = get16(hdr->flags);
        qdcount = get16(hdr->qdcount);
        ancount = get16(hdr->ancount);
        arcount = get16(hdr->arcount);

        opcode = (flags >> 11) & 0x1f;
        nm_flags = (flags >> 4) & 0x7f;
        rcode = (flags >> 0) & 0xf;
        if (verbose)
            printf("%s: received message of size %zd bytes from %s (from_len=%d)\n",
                    prog_name, len, inet_ntoa(peeraddr.sin_addr), from_len);
        if (verbose > 1)
            printf(" Transaction id = 0x%x\n"
                            " Flags          = 0x%x\n"
                            "  opcode        = 0x%x\n"
                            "  nm_flags      = 0x%x\n"
                            "  rcode         = 0x%x\n"
                            " qdcount        = 0x%x\n"
                            " ancount        = 0x%x\n"
                            " arcount        = 0x%x\n", tid, flags, opcode, nm_flags,
                    rcode, qdcount, ancount, arcount);

        if (qdcount != 1)
            continue;
        if (ancount != 0 || arcount != 0)
            continue;

        if (opcode != NB_OPCODE_QUERY)
            continue;

        ret = decode_nb_name(nb_name, sizeof(nb_name), hdr + 1);
        if (ret < 0)
            continue;

        data = (uint8_t * )(hdr + 1) + ret;
        nb = get16(data);
        in = get16(data + 2);
        if (verbose > 1)
            printf(" nb             = 0x%x\n"
                    " in             = 0x%x\n", nb, in);
        if (nb != NB_NSRR)
            continue;
        if (in != NB_IN_CLASS)
            continue;

        if (verbose)
            printf("%s: looking for: `%.15s (0x%x)'\n",
                    prog_name, nb_name, nb_name[sizeof(nb_name) - 1]);

        found = 0;
        if (strncasecmp(nb_name, my_nb_name, sizeof(my_nb_name) - 1) == 0
                && nb_name[sizeof(nb_name) - 1] ==
                my_nb_name[sizeof(my_nb_name) - 1]) {
            found = 1;
        }

        if (fallback_hostname && !found
                && strncasecmp(nb_name, my_nb_hostname,
                sizeof(my_nb_hostname) - 1) == 0
                && nb_name[sizeof(nb_name) - 1] ==
                my_nb_hostname[sizeof(my_nb_hostname) - 1]) {
            found = 1;
        }
        if (!found) {
            continue;
        }

        opcode |= NB_OPCODE_RESPONSE;
        nm_flags &= ~(NM_FLAG_B | NM_FLAG_RA);
        nm_flags |= NM_FLAG_AA | NM_FLAG_RD;
        flags = (opcode << 11) | (nm_flags << 4);
        put16(hdr->flags, flags);
        put16(hdr->qdcount, 0);
        put16(hdr->ancount, 1);
        ttl = 3600;    /* one hour */
        put32(data + 4, ttl);
        rdlength = 6;
        put16(data + 8, rdlength);
        nb_flags = 0x2 << 1;    /* owner node type: B node */
        put16(data + 10, nb_flags);


        in_addr_t receive_addr = 0;
        //if (verbose) printf("iterating control messages\n");
        for ( // iterate through all the control headers
                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mh);
                cmsg != NULL;
                cmsg = CMSG_NXTHDR(&mh, cmsg))
        {
            // ignore the control headers that don't match what we want
            if (cmsg->cmsg_level != IPPROTO_IP ||
                    cmsg->cmsg_type != IP_PKTINFO)
            {
                continue;
            }
            struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
            receive_addr = pi->ipi_spec_dst.s_addr;
            // at this point, peeraddr is the source sockaddr
            // pi->ipi_spec_dst is the destination in_addr
            // pi->ipi_addr is the receiving interface in_addr
            //if (verbose) printf("found it\n");
            break;
        }
        if (!receive_addr) {
            if (verbose) {
                printf("could not get receive addr.\n");
            }
            continue;
        }

        *((in_addr_t*)(data+12)) = receive_addr;
        if (verbose)
            printf("%s: responding with IP address %s\n",
                    prog_name, inet_ntoa(*(struct in_addr *) (data + 12)));
        len = (data + 16) - pkt;
        if (len & 1) {
            data[20] = '\0';
            ++len;
        }

        ret = sendto(sd, pkt, len, 0, (struct sockaddr *)&peeraddr, sizeof(struct sockaddr));
        if (ret < 0)
            perror("sendto");
    }
    return 0;
}
