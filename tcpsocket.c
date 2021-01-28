/*
 * tcpsocket.c - code for managing sockets
 * Copyright (C) 2021 Sanjay Rao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include "common/conventions.h"
#include "misc.h"

#include "tcpsocket.h"

void clear_tcpsocket(struct tcpsocket *t) {
static struct sockaddr_in6 blank;
t->fd=-1;
t->port=0;
t->sa6=blank;
}
SICLEARFUNC(sockaddr_in6);

int init_tcpsocket(struct tcpsocket *t, unsigned short port, unsigned int searchfuse, unsigned int waitfuse) {
int fd=-1;
int isshown=0;

if (0>(fd=socket(AF_INET6,SOCK_STREAM,0))) GOTOERROR;
while (1) {
	struct sockaddr_in6 sa;
	clear_sockaddr_in6(&sa);
	sa.sin6_family=AF_INET6;
	sa.sin6_port=htons(port);
	if (!bind(fd,(struct sockaddr*)&sa,sizeof(sa))) break;
	if (errno==EADDRINUSE) {
		if (searchfuse) {
			searchfuse--;
			port++;
			continue;
		}
		if (waitfuse) {
			if (!isshown) { isshown=1; syslog(LOG_INFO,"Waiting for port (%d seconds)",waitfuse); }
			sleep(1);
			waitfuse--;
			continue;
		}
	}
	perror("bind");
	GOTOERROR;
}
if (listen(fd,5)) GOTOERROR;
t->fd=fd;
t->port=port;
return 0;
error:
	return -1;
}
void deinit_tcpsocket(struct tcpsocket *t) {
if (t->fd<0) return;
(ignore)close(t->fd);
}

int nodelay_tcpsocket(int fd) {
int yesint=1;
return setsockopt(fd,IPPROTO_TCP,TCP_NODELAY, (char*)&yesint,sizeof(int));
}

int keepalive_tcpsocket(int fd) {
int yesint=1;
return setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE, (char*)&yesint,sizeof(int));
}

int isipv4_tcpsocket(unsigned char **ipv4_out, struct tcpsocket *t) {
unsigned char *ipv6;
ipv6=t->sa6.sin6_addr.s6_addr;
if (!ismappedipv4_misc(ipv6)) return 0;
*ipv4_out=ipv6+12;
return 1;
}
int isipv6_tcpsocket(unsigned char **ipv6_out, struct tcpsocket *t) {
*ipv6_out=t->sa6.sin6_addr.s6_addr;
return 1;
}

void setupip_tcpsocket(struct tcpsocket *t) {
(void)iptostr_misc(t->iptext,t->sa6.sin6_addr.s6_addr);
if (isipv4_tcpsocket(&t->ip,t)) t->isipv4=1;
else {
	(ignore)isipv6_tcpsocket(&t->ip,t);
	t->isipv4=0;
}
}
