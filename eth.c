/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 */

#include <assert.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "eth.h"
#include "util.h"

static int sockfd = -1;

/* open a socket for access to the local ethernet */
void eth_open(const char *netif)
{
	struct sockaddr_ll sa;
	struct ifreq ifr;
	int i;

	assert(sockfd == -1);

	/* open the socket */
	sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sockfd < 0)
		perror_exit("socket");

	/* bind it to the local interface */
	memset(&sa, 0, sizeof sa);
	sa.sll_family   = AF_PACKET;
	sa.sll_protocol = htons(ETH_P_ALL);
	sa.sll_ifindex  = if_nametoindex(netif);

	/* bind socket to name (socket address) */
	if (bind(sockfd, (struct sockaddr*) &sa, sizeof sa) < 0)
		perror_exit("bind");

	/* get mac address of the interface; confirm Ethernet */
	memset(&ifr, 0, sizeof ifr);
	strncpy(ifr.ifr_name, netif, sizeof ifr.ifr_name);
	if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0)
		perror_exit("ioctl(SIOCGIFHWADDR)");
	if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
		fprintf(stderr, "%s is not an Ethernet interface\n", netif);
		exit(1);
	}
	printf("MAC address for %s is ", netif);
	for (i=0; i<6; ++i) {
		printf("%02X%c", ifr.ifr_hwaddr.sa_data[i] & 0xFF,
			(i == 5) ? '\n' : ':');
	}

}

/* write onto the local ethernet */
void eth_write(const void *buf, size_t count)
{
	/* could just be an incomplete write, but with packet
	   socket I imagine that's probably not a good thing. */
	if (write(sockfd, buf, count) != count)
		perror_exit("write");
}
 
/* close ethernet socket */
void eth_close(void)
{
	xclose(sockfd);
	sockfd = -1;
}

