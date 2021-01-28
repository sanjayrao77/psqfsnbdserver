/*
 * common/mmapread.c - wrapper for mmap reading
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
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <syslog.h>
#include "conventions.h"

#include "mmapread.h"

static inline int readn(int fd, unsigned char *dest, unsigned int n) {
int k;
while (n) {
	k=read(fd,dest,n);
	if (k<=0) GOTOERROR;
	dest+=k;
	n-=k;
}
return 0;
error:
	return -1;
}

static int readoff_nommap(struct mmapread *s, int fd, uint64_t offset, uint64_t filesize) {
uint64_t u,smoff;
u=s->cleanup.mallocsize;
if (!s->cleanup.ptr_malloc) {
	if (!u) GOTOERROR;
	if (!(s->cleanup.ptr_malloc=malloc(u))) GOTOERROR;
}
smoff=filesize-offset;
if (smoff < u) u=smoff;
s->filesize=filesize;
s->datasize=s->cleanup.addrsize=u;
s->cleanup.offset=offset;
if (0>lseek64(fd,offset,SEEK_SET)) GOTOERROR;
s->data=s->cleanup.ptr_malloc;
if (readn(fd,s->data,(unsigned int)u)) GOTOERROR;
return 0;
error:
	return -1;
}

#if UINTPTR_MAX == 0xffffffffffffffff
int readoff_mmapread(struct mmapread *s, int fd, uint64_t offset, int fdcleanup) {
off64_t filesize;
if (0>(filesize=lseek64(fd,0,SEEK_END))) {
	syslog(LOG_ERR,"Error seeking end of file %s",strerror(errno));
	GOTOERROR;
}
if (!filesize) {
	s->filesize=0;
	s->datasize=0;
	ignore_ifclose(fdcleanup);
	return 0;
}
s->cleanup.addrsize= s->datasize= s->filesize= (uint64_t)filesize;
s->cleanup.offset=0;
if (MAP_FAILED==(s->data=s->cleanup.ptr_mmap=mmap(NULL,addrsize,PROT_READ,MAP_SHARED,fd,0))) {
	if (errno!=ENODEV) GOTOERROR;
	if (readoff_nommap(s,fd,offset,s->filesize)) GOTOERROR;
}
s->cleanup.fd=fdcleanup;
return 0;
error:
	return -1;
}
#endif

#if UINTPTR_MAX == 0xffffffff
int readoff_mmapread(struct mmapread *s, int fd, uint64_t offset_in, int fdcleanup) {
uint64_t addrsize;
off64_t filesize,offset;
// int pagesizem1;

// pagesizem1=sysconf(_SC_PAGESIZE) -1;
// offset=offset_in & ~pagesizem1;

// we may reduce the offset: 1> to page-align the offset, 2> to allow some back-seeking without unmap
offset=offset_in & ~((1<<26)-1); // pages should be smaller than 64MB, checked in voidinit_
if (0>(filesize=lseek64(fd,0,SEEK_END))) {
	syslog(LOG_ERR,"Error seeking end of file %s",strerror(errno));
	GOTOERROR;
}

if (filesize<=offset_in) {
	s->filesize=(uint64_t)filesize;
	s->datasize=0;
	ignore_ifclose(fdcleanup);
	return 0;
}

addrsize=filesize-offset; // addrsize > 0
// #define maxaddrsize	(UINT32_MAX&~4095) // on a 32bit machine, this leaves nothing for anything else
#define maxaddrsize ((UINTPTR_MAX>>1)+1)
if (addrsize>maxaddrsize) addrsize=maxaddrsize;
#if 0 // addrsize doesn't have to be page-aligned
else {
//	if (addrsize&pagesizem1) addrsize=(addrsize|pagesizem1)+1;
	addrsize=((addrsize-1)|pagesizem1)+1;
}
#endif
#undef maxaddrsize
s->filesize=(uint64_t)filesize;
s->cleanup.addrsize=addrsize;
s->cleanup.offset=offset;
if (MAP_FAILED==(s->cleanup.ptr_mmap=mmap(NULL,addrsize,PROT_READ,MAP_SHARED,fd,offset))) {
	if (errno!=ENODEV) GOTOERROR;
	if (readoff_nommap(s,fd,offset,s->filesize)) GOTOERROR;
} else {
	uint64_t adj;
	adj=offset_in-offset;
	s->data=s->cleanup.ptr_mmap+adj;
	s->datasize=addrsize-adj;
}
s->cleanup.fd=fdcleanup;
return 0;
error:
	return -1;
}
#endif

void clear_mmapread(struct mmapread *s) {
static struct mmapread blank={.cleanup.fd=-1};
*s=blank;
}

void voidinit_mmapread(struct mmapread *s, int mallocsize) {
s->cleanup.mallocsize=mallocsize;
if (sysconf(_SC_PAGESIZE) > (1<<26)) { WHEREAMI; _exit(0); }
}

void deinit_mmapread(struct mmapread *s) {
if (s->cleanup.ptr_mmap) {
	(ignore)munmap(s->cleanup.ptr_mmap,s->cleanup.addrsize);
}
if (s->cleanup.ptr_malloc) {
	(ignore)free(s->cleanup.ptr_malloc);
}
ignore_ifclose(s->cleanup.fd);
}

void reset_mmapread(struct mmapread *s) {
if (s->cleanup.ptr_mmap) {
	(ignore)munmap(s->cleanup.ptr_mmap,s->cleanup.addrsize);
	s->cleanup.ptr_mmap=NULL;
}
if (s->cleanup.fd>=0) {
	(ignore)close(s->cleanup.fd);
	s->cleanup.fd=-1;
}
}

int isoffsetchanged_mmapread(struct mmapread *s, uint64_t offset) {
uint64_t max;
if (offset < s->cleanup.offset) return 0;
max=s->cleanup.offset+s->cleanup.addrsize;
if (max <= offset) return 0;
if (s->cleanup.ptr_mmap) {
	s->data=s->cleanup.ptr_mmap+offset-s->cleanup.offset;
	s->datasize=max-offset;
	return 1;
}
if (s->cleanup.ptr_malloc) {
	s->data=s->cleanup.ptr_malloc+offset-s->cleanup.offset;
	s->datasize=max-offset;
	return 1;
}
return 0;
}
