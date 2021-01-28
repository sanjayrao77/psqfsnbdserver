
/*
 * tcpsocket.h
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
struct tcpsocket {
	int fd;
	unsigned short port;
	struct sockaddr_in6 sa6;
	char iptext[40];
	unsigned char *ip;
	int isipv4;
};

void clear_tcpsocket(struct tcpsocket *t);
int init_tcpsocket(struct tcpsocket *t, unsigned short port, unsigned int searchfuse, unsigned int waitfuse);
void deinit_tcpsocket(struct tcpsocket *t);
int nodelay_tcpsocket(int fd);
int keepalive_tcpsocket(int fd);
int isipv4_tcpsocket(unsigned char **ipv4_out, struct tcpsocket *t);
int isipv6_tcpsocket(unsigned char **ipv6_out, struct tcpsocket *t);
void setupip_tcpsocket(struct tcpsocket *t);
