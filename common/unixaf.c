/*
 * common/unixaf.c - socket server over unix sockets
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
#include <sys/types.h>
#include <sys/socket.h>
#include "conventions.h"

#include "unixaf.h"

void clear_unixaf(struct unixaf *u) {
u->listenfd = u->sendfd =-1;
}

int init_unixaf(struct unixaf *u) {
int sv[2];
if (socketpair(AF_UNIX,SOCK_STREAM,0,sv)) GOTOERROR;
u->listenfd=sv[0];
u->sendfd=sv[1];
return 0;
error:
	return -1;
}

void voidinit_unixaf(struct unixaf *u, int listenfd, int sendfd) {
u->listenfd=listenfd;
u->sendfd=sendfd;
}

void deinit_unixaf(struct unixaf *u) {
ifclose(u->listenfd);
ifclose(u->sendfd);
}

static int recvfd(int *fd_out, int *pid_out, int sock) {
struct msghdr msg;
struct iovec iov;
unsigned char buffint[sizeof(int)];
unsigned char cmsg[CMSG_SPACE(sizeof(int))];
int fd=-1;
int pid=-1;
int k;

iov.iov_base=buffint;
iov.iov_len=sizeof(int);
msg.msg_name=NULL;
msg.msg_namelen=0;
msg.msg_iov=&iov;
msg.msg_iovlen=1;
msg.msg_control=cmsg;
msg.msg_controllen=CMSG_SPACE(sizeof(int));
msg.msg_flags=0;

k=recvmsg(sock,&msg,0);
if (k<0) GOTOERROR;
if (k==4) {
	struct cmsghdr *hdr;
	memcpy(&pid,buffint,sizeof(int));
	hdr=CMSG_FIRSTHDR(&msg);
	if ((hdr)
			&& (hdr->cmsg_len==CMSG_LEN(sizeof(int)))
			&& (hdr->cmsg_level==SOL_SOCKET)
			&& (hdr->cmsg_type==SCM_RIGHTS)) memcpy(&fd,CMSG_DATA(hdr),sizeof(int));
}

*fd_out=fd;
*pid_out=pid;
return 0;
error:
	return -1;
}

int recvfd_unixaf(int *fd_out, int *pid_out, int sock) {
return recvfd(fd_out,pid_out,sock);
}

int accept_unixaf(int *fd_out, int *pid_out, struct unixaf *u) {
return recvfd(fd_out,pid_out,u->listenfd);
}

void afterfork_unixaf(struct unixaf *u) {
if (u->listenfd<0) return;
(ignore)close(u->listenfd);
u->listenfd=-1;
}

static int sendfd(int sock, int fd) {
unsigned char pidbuff[sizeof(int)];
unsigned char fdbuff[sizeof(int)];
struct msghdr msg;
struct iovec iov;
unsigned char cmsg[CMSG_SPACE(sizeof(int))];
int pid;

pid=getpid();
memcpy(pidbuff,&pid,sizeof(int));
memcpy(fdbuff,&fd,sizeof(int));

iov.iov_base=pidbuff;
iov.iov_len=sizeof(int);

msg.msg_name=NULL;
msg.msg_namelen=0;
msg.msg_iov=&iov;
msg.msg_iovlen=1;
msg.msg_control=cmsg;
msg.msg_controllen=CMSG_SPACE(sizeof(int));
msg.msg_flags=0;

{
	struct cmsghdr *hdr;
	hdr=CMSG_FIRSTHDR(&msg);
	hdr->cmsg_len=CMSG_LEN(sizeof(int));
	hdr->cmsg_level=SOL_SOCKET;
	hdr->cmsg_type=SCM_RIGHTS;
	memcpy(CMSG_DATA(hdr),fdbuff,sizeof(int));
}

if (0>sendmsg(sock,&msg,0)) GOTOERROR;
return 0;
error:
	return -1;
}

int sendfd_unixaf(int sock, int payload) {
return sendfd(sock,payload);
}

int connect_unixaf(int *fd_out, struct unixaf *u) {
int sv[2]={-1,-1};

if (socketpair(AF_UNIX,SOCK_STREAM,0,sv)) GOTOERROR;
if (sendfd(u->sendfd,sv[0])) GOTOERROR;
(ignore)close(sv[0]);
*fd_out=sv[1];
return 0;
error:
	ifclose(sv[0]);
	ifclose(sv[1]);
	return -1;
}
