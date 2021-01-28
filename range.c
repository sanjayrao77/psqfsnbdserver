/*
 * range.c - map bytes of squashfs address space to other locations
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <syslog.h>
#include <errno.h>
// #define DEBUG2
#include "common/conventions.h"
#include "common/mmapread.h"
#include "options.h"

#include "range.h"

static inline unsigned char *alloc_name(struct range *range, unsigned int len) {
unsigned int num;
unsigned char *d;
num=range->names.num;
if (num+len>range->names.max) {
	syslog(LOG_ERR,"Wanted %u bytes but only %u are left",len,range->names.max-num);
	GOTOERROR;
}
d=range->names.data+num;
range->names.num+=len;
return d;
error:
	return NULL;
}

unsigned char *alloc_name_range(struct range *range, unsigned int len) {
return alloc_name(range,len);
}

static struct entry_range *nextfreeentry(struct range *range, uint64_t len) {
unsigned int num;
struct entry_range *e;
num=range->entries.num;
if (num==range->entries.max) GOTOERROR;
range->entries.num=num+1;
e=&range->entries.list[num];
e->start=range->entries.nextstart;
range->entries.nextstart+=len;
e->startpluslen=range->entries.nextstart;
return e;
error:
	return NULL;
}

int add_internal_range(struct range *range, unsigned char *data, unsigned int len) {
struct entry_range *e;
if (!(e=nextfreeentry(range,len))) GOTOERROR;
e->type=INTERNAL_TYPE_RANGE;
e->internal.data=data; // NULL means a hole (0s)
e->internal.len=len;
return 0;
error:
	return -1;
}

int noalloc_add_fd_range(struct range *range, int fd, char *filename, uint64_t len) {
struct entry_range *e;
if (!(e=nextfreeentry(range,len))) GOTOERROR;
e->type=FD_TYPE_RANGE;
e->fd.fd=fd;
e->fd.filename=filename;
#ifdef DEBUG2
// fprintf(stderr,"Added file %s of size %"PRIu64", offset=%"PRIu64"\n",filename,len,e->start);
#endif
return 0;
error:
	return -1;
}

int noalloc_add_external_range(struct range *range, struct directory_range *directory, char *filename,
		uint64_t len) {
struct entry_range *e;

#ifdef DEBUG
if (!len) GOTOERROR; // this should be removed by assemble/scan/mkfs process
#endif

if (!(e=nextfreeentry(range,len))) GOTOERROR;

e->type=EXTERNAL_TYPE_RANGE;
e->external.directory=directory;
e->external.filename=filename;
return 0;
error:
	return -1;
}

int add_external_range(struct range *range, struct directory_range *directory, char *filename_in,
		unsigned int namelen, uint64_t len) {
char *filename;
namelen+=1;
if (!(filename=(char *)alloc_name(range,namelen))) GOTOERROR;
memcpy(filename,filename_in,namelen);
#if 0
fprintf(stderr,"Allocated name: \"%s\" (%u of %u)\n",e->external.filename,namelen,range->names.max-range->names.num);
#endif
return noalloc_add_external_range(range,directory,filename,len);
error:
	return -1;
}

struct directory_range *add_directory_range(struct range *range, struct directory_range *parent,
		char *name, unsigned int namelen) {
struct directory_range *d;
unsigned int num;
char *fn;

#ifdef DEBUG2
if (!namelen) GOTOERROR;
#endif

namelen+=1;
if (!(fn=(char *)alloc_name(range,namelen))) GOTOERROR;
memcpy(fn,name,namelen);
#if 0
DEBUGOUT((stderr,"%s:%d Allocated name: \"%s\" (%u of %u)\n",__FILE__,__LINE__,fn,namelen,range->names.max-range->names.num));
#endif

num=range->directories.num;
if (num==range->directories.max) GOTOERROR;
range->directories.num=num+1;
d=&range->directories.list[num];
d->filename=fn;
d->parent=parent;

return d;
error:
	return NULL;
}

int init_range(struct range *range, unsigned int maxentries, unsigned int maxdirs, unsigned int maxnames, unsigned int maxdepth) {
if (!(range->entries.list=malloc(sizeof(struct entry_range)*maxentries))) GOTOERROR;
range->entries.num=0;
range->entries.max=maxentries;

if (!(range->directories.list=malloc(sizeof(struct directory_range)*maxdirs))) GOTOERROR;
range->directories.num=0;
range->directories.max=maxdirs;

if (!(range->names.data=malloc(maxnames))) GOTOERROR;
range->names.num=0;
range->names.max=maxnames; 

if (!(range->temp.unwinddirs=malloc(maxdepth*sizeof(struct directory_range *)))) GOTOERROR;
range->temp.maxdepth_unwinddirs=maxdepth;

voidinit_match_range(&range->cache.match,1<<16);

return 0;
error:
	return -1;
};

void deinit_range(struct range *range) {
iffree(range->entries.list);
iffree(range->directories.list);
iffree(range->names.data);
iffree(range->extra.other);
iffree(range->temp.unwinddirs);
deinit_match_range(&range->cache.match);
}

SICLEARFUNC(match_range);
void reset_range(struct range *range) {
(void)deinit_range(range);
range->entries.list=NULL;
range->directories.list=NULL;
range->names.data=NULL;
range->extra.other=NULL;
range->temp.unwinddirs=NULL;
(void)clear_match_range(&range->cache.match);
}

static int openexternalfile(int *fd_out, struct range *range, struct directory_range *directory, char *filename,
		struct options *options) {
// Not thread-safe
struct directory_range **list;
struct directory_range *d;
unsigned int depth=0;
int dfd=-1,ffd=-1;

list=range->temp.unwinddirs; // not thread safe
d=directory;
while (1) {
	list[depth]=d;
	d=d->parent;
	if (!d) break;
	depth++;
}
if (0>(dfd=open(list[depth]->filename,O_RDONLY|O_DIRECTORY))) {
	syslog(LOG_ERR,"Error opening directory: %s",list[depth]->filename);
	GOTOERROR;
}
while (1) {
	int newfd;
	if (!depth) break;
	depth--;
	if (0>(newfd=openat(dfd,list[depth]->filename,O_RDONLY|O_DIRECTORY))) {
		syslog(LOG_ERR,"Error opening subdirectory: %s",list[depth]->filename);
		GOTOERROR;
	}
	(ignore)close(dfd);
	dfd=newfd;
}

if (0>(ffd=openat(dfd,filename,O_RDONLY))) {
	if (options->isverbose) {
		syslog(LOG_ERR,"Error opening file: %s %s, directory path follows",filename,strerror(errno));
		while (directory) { syslog(LOG_DEBUG,"subdir: %s",directory->filename); directory=directory->parent; }
	} else {
		syslog(LOG_ERR,"Error opening file: %s %s",filename,strerror(errno));
	}
	GOTOERROR;
}

(ignore)close(dfd);

*fd_out=ffd;
return 0;
error:
	ignore_ifclose(ffd);
	ignore_ifclose(dfd);
	return -1;
}

#ifdef DEBUG2
#if 0
// this needs to be rewritten
SICLEARFUNC(mmapread);
static int dumpfile(struct range *range, FILE *fout, struct directory_range *directory, char *filename, uint64_t len) {
uint64_t u;
struct mmapread smmap;
int ffd=-1;

clear_mmapread(&smmap);

if (openexternalfile(&ffd,range,directory,filename,options)) GOTOERROR;

if (init_mmapread(&smmap,ffd,ffd)) {
	fprintf(stderr,"%s:%d Error mmaping file: %s, directory path follows\n",__FILE__,__LINE__,filename);
	while (directory) { fprintf(stderr,"\t%s\n",directory->filename); directory=directory->parent; }
	GOTOERROR;
}
ffd=-1;
u=_BADMIN(smmap.filesize,len);
if (1!=fwrite(smmap.addr,u,1,fout)) GOTOERROR;
if (u!=len) {
	u=len-u;
	while (1) { // this is not fast, but currently rare
		if (EOF==fputc(0,fout)) GOTOERROR; 
		u--;
		if (!u) break;
	}
}

deinit_mmapread(&smmap);
return 0;
error:
	deinit_mmapread(&smmap);
	ignore_ifclose(ffd);
	return -1;
}

int dump_range(struct range *range, char *filename) {
struct entry_range *list,*lastlist;
FILE *ff=NULL;
if (!(ff=fopen(filename,"wb"))) GOTOERROR;
list=range->entries.list;
lastlist=list+range->entries.num;
while (1) {
	switch (list->type) {
		case INTERNAL_TYPE_RANGE:
			if (list->internal.data) {
				if (1!=fwrite(list->internal.data,list->internal.len,1,ff)) GOTOERROR;
			} else {
				unsigned int ui;
				for (ui=list->internal.len;ui;ui--) if (EOF==fputc(0,ff)) GOTOERROR;
			}
			break;
		case EXTERNAL_TYPE_RANGE:
			if (dumpfile(range,ff,list->external.directory,list->external.filename,list->startpluslen-list->start)) GOTOERROR;
			break;
		case FD_TYPE_RANGE:
			GOTOERROR; // don't dump a block device
			break;
	}
	list+=1;
	if (list==lastlist) break;
}
if (fflush(ff)) GOTOERROR;
fclose(ff);
return 0;
error:
	iffclose(ff);
	return -1;
}
#endif
#endif

static int setfd_match(struct match_range *match_inout, uint64_t fileoffset, int fd, struct entry_range *entry,
		struct options *options) {
struct mmapread *smmap;
uint64_t u;

smmap=&match_inout->mmapread;
if (readoff_mmapread(smmap,fd,fileoffset,-1)) {
	syslog(LOG_ERR,"Error mmaping %s %s",entry->fd.filename,strerror(errno));
	GOTOERROR;
}
if (!smmap->datasize) GOTOERROR; // consider it an error here, not so much in setexternal. we had flock so this is a violation
#if 0
 fprintf(stderr,"setfdmatch, fileoffset:%"PRIu64" mmapoffset:%"PRIu64" addrsize:%u\n", fileoffset,smmap->offset,smmap->addrsize);
#endif
match_inout->data=smmap->data;
u=entry->startpluslen - entry->start - fileoffset;
if (smmap->datasize < u) u=smmap->datasize;
#if UINT_MAX==UINT32_MAX
	if (u>UINT32_MAX) u=UINT32_MAX;
#endif
match_inout->len=u;
return 0;
error:
	return -1;
}

static int setexternal_match(struct match_range *match_inout, struct range *range, uint64_t fileoffset, struct entry_range *entry,
		struct options *options) {
int fd=-1;
struct mmapread *smmap;
uint64_t u;

if (openexternalfile(&fd,range,entry->external.directory,entry->external.filename,options)) GOTOERROR;
// note that actual fileoffset may vary and length may be limited to 32bits
smmap=&match_inout->mmapread;
if (readoff_mmapread(smmap,fd,fileoffset,fd)) {
	syslog(LOG_ERR,"Error mmaping %s %s",entry->external.filename,strerror(errno));
	GOTOERROR; 
}
fd=-1;
if (!smmap->datasize) { // file got truncated under us, can send 0s to client and keep going
	match_inout->data=NULL;
	match_inout->len=0;
	return 0;
}
#if 0
 fprintf(stderr,"externalmatch, fileoffset:%"PRIu64" mmapoffset:%"PRIu64" addrsize:%u\n", fileoffset,smmap->offset,smmap->addrsize);
#endif
match_inout->data=smmap->data;
u=entry->startpluslen - entry->start - fileoffset;
if (u>smmap->datasize) u=smmap->datasize;
#if UINT_MAX==UINT32_MAX
	if (u>UINT32_MAX) u=UINT32_MAX;
#endif
match_inout->len=(unsigned int)u;
return 0;
error:
	ignore_ifclose(fd);
	return -1;
}

static int finddata2(struct range *range, uint64_t offset) {
struct entry_range *e;
struct match_range *m;
uint64_t left;
e=range->cache.entry;
if (!e) return 0;
if (e->type==INTERNAL_TYPE_RANGE) return 0;
if (offset < e->start) return 0;
if (offset >= e->startpluslen) return 0;
left=e->startpluslen-offset;
offset -= e->start; // now offset in file

m=&range->cache.match;
if (isoffsetchanged_mmapread(&m->mmapread,offset)) {
	uint64_t u;
	m->data=m->mmapread.data;
	u=m->mmapread.datasize;
	if (u>left) u=left;
#if UINT_MAX==UINT32_MAX
	if (u > UINT32_MAX) u=UINT32_MAX;
#endif
	m->len=(unsigned int)u;
	return 1;
}
return 0;
}

struct match_range *finddata_range(struct range *range, uint64_t offset, struct options *options) {
struct entry_range *list;
struct match_range *m;
unsigned int num;

// fprintf(stderr,"%s:%d %s looking for offset %"PRIu64"\n",__FILE__,__LINE__,__FUNCTION__,offset);

m=&range->cache.match;
if (finddata2(range,offset)) return m;
m->iserror=0;
#ifdef DEBUG2
fprintf(stderr,"Reset mmapread for offset %"PRIu64", last range was %"PRIu64" of size %"PRIu64"\n",
		offset,m->mmapread.cleanup.offset,m->mmapread.cleanup.addrsize);
#endif
(void)reset_mmapread(&m->mmapread);

list=range->entries.list;
num=range->entries.num;
if (offset >= range->entries.nextstart) return NULL;
while (1) {
	unsigned int ui;
	if (num==1) {
		uint64_t rangeoffset;
		rangeoffset=offset-list->start;
		switch (list->type) {
			case EXTERNAL_TYPE_RANGE:
#if 0
				fprintf(stderr,"Looking for offset %"PRIu64" found external start:%"PRIu64" length:%"PRIu64"\n",
						offset,list->start,list->startpluslen-list->start);
#endif
				if (setexternal_match(m,range,rangeoffset,list,options)) { m->iserror=1; return NULL; }
				break;
			case INTERNAL_TYPE_RANGE:
				m->data=list->internal.data+rangeoffset;
				m->len=list->internal.len-rangeoffset;
				return m;
			case FD_TYPE_RANGE:
				if (setfd_match(m,rangeoffset,list->fd.fd,list,options)) {
					// this is for block devices and similar; we can place higher expectations on corruption
					syslog(LOG_ERR,"Error reading file %s %s",list->fd.filename,strerror(errno));
					m->iserror=1;
					return NULL;
				}
				break;
		}
		range->cache.entry=list;
		return m;
	}
	ui=num/2;
	if (offset<list[ui].start) {
		num=ui;
	} else {
		list=&list[ui];
		num-=ui;
	}
}
return NULL;
}
