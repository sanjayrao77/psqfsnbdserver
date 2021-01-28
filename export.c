/*
 * export.c - store export info and access lists
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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "common/conventions.h"
#include "common/mmapread.h"
#include "common/mapmem.h"
#include "common/blockmem.h"
#include "misc.h"
#include "options.h"
#include "scan.h"
#include "mkfs.h"
#include "range.h"
#include "assemble.h"

#include "export.h"

SICLEARFUNC(scan);
SICLEARFUNC(temp_sqfs_mkfs);
SICLEARFUNC(assemble);

int init_all_export(struct all_export *all) {
// all->config.istlsrequired=0;
all->config.shorttimeout=60;

all->defaults.longtimeout=7*24*60*60;
// all->defaults.isdenydefault=0;
// all->defaults.ispreload=0;
all->defaults.isnodelay=1;
all->defaults.iskeepalive=1;
all->defaults.islisted=1;
// all->defaults.iskeyrequired=0;
// all->defaults.istlsrequired=0;
all->defaults.gziplevel=6; // Z_DEFAULT_COMPRESSION = -1, => 6
// all->defaults.maxfiles=0; // no max
if (init_blockmem(&all->tofree.blockmem,8192)) GOTOERROR;
return 0;
error:
	return -1;
}

void deinit_all_export(struct all_export *all) {
struct one_export *export;
export=all->exports.first;
while (export) {
	deinit_range(&export->range);

	export=export->next;
}
deinit_blockmem(&all->tofree.blockmem);
}

static int ipv6_allowhost_add_export(struct all_export *all, 
		uint64_t high_ipv6, uint64_t low_ipv6, uint64_t high_netmask, uint64_t low_netmask, int isonlyiftls) {
struct iprange6_export *host;
if (!(host=FALLOC(&all->tofree.blockmem,struct iprange6_export))) GOTOERROR;
host->high_ipv6=high_ipv6;
host->low_ipv6=low_ipv6;
host->high_netmask=high_netmask;
host->low_netmask=low_netmask;
host->isonlyiftls=isonlyiftls;
host->next=all->allowedhosts.firstipv6;
all->allowedhosts.firstipv6=host;
return 0;
error:
	return -1;
}
static int ipv4_allowhost_add_export(struct all_export *all, unsigned int ui32_ipv4, unsigned int netmask, int isonlytls) {
struct iprange4_export *host;
if (!(host=FALLOC(&all->tofree.blockmem,struct iprange4_export))) GOTOERROR;
host->ui32_ipv4=ui32_ipv4;
host->netmask=netmask;
host->isonlyiftls=isonlytls;
host->next=all->allowedhosts.firstipv4;
all->allowedhosts.firstipv4=host;
return 0;
error:
	return -1;
}
static int ipv6_allowhost_add_one_export(struct all_export *all, struct one_export *one,
		uint64_t high_ipv6, uint64_t low_ipv6, uint64_t high_netmask, uint64_t low_netmask, int isonlytls) {
struct iprange6_export *host;
if (!(host=FALLOC(&all->tofree.blockmem,struct iprange6_export))) GOTOERROR;
host->high_ipv6=high_ipv6;
host->low_ipv6=low_ipv6;
host->high_netmask=high_netmask;
host->low_netmask=low_netmask;
host->isonlyiftls=isonlytls;
host->next=one->allowedhosts.firstipv6;
one->allowedhosts.firstipv6=host;
return 0;
error:
	return -1;
}
static int ipv4_allowhost_add_one_export(struct all_export *all, struct one_export *one, unsigned int ui32_ipv4,
		unsigned int netmask, int isonlyiftls) {
struct iprange4_export *host;
if (!(host=FALLOC(&all->tofree.blockmem,struct iprange4_export))) GOTOERROR;
host->ui32_ipv4=ui32_ipv4;
host->netmask=netmask;
host->isonlyiftls=isonlyiftls;
host->next=one->allowedhosts.firstipv4;
one->allowedhosts.firstipv4=host;
return 0;
error:
	return -1;
}

int text_allowhost_add_export(struct all_export *all, char *text, int isonlyiftls) {
uint64_t high_ipv6,low_ipv6,high_netmask,low_netmask;
unsigned int be32;
unsigned int netmask;
if (!text) { // reset hosts, useful for multiple [global] sections
	all->allowedhosts.firstipv4=NULL;
	all->allowedhosts.firstipv6=NULL;
	return 0;
}
if (strchr(text,':')) {
	if (!isipv6_misc(&high_ipv6,&low_ipv6,&high_netmask,&low_netmask,text)) return 0;
	return ipv6_allowhost_add_export(all,high_ipv6,low_ipv6,high_netmask,low_netmask,isonlyiftls);
}
if (!isipv4_misc(&be32,&netmask,text)) return 0;
return ipv4_allowhost_add_export(all,be32,netmask,isonlyiftls);
}
int text_allowhost_add_one_export(struct all_export *all, struct one_export *one, char *text, int isonlyiftls) {
uint64_t high_ipv6,low_ipv6,high_netmask,low_netmask;
unsigned int be32;
unsigned int netmask;
if (strchr(text,':')) {
	if (!isipv6_misc(&high_ipv6,&low_ipv6,&high_netmask,&low_netmask,text)) return 0;
	return ipv6_allowhost_add_one_export(all,one,high_ipv6,low_ipv6,high_netmask,low_netmask,isonlyiftls);
}
if (!isipv4_misc(&be32,&netmask,text)) return 0;
return ipv4_allowhost_add_one_export(all,one,be32,netmask,isonlyiftls);
}

static void addchunk(struct one_export *one, struct chunk_export *chunk) {
struct chunk_export **pchunk;
chunk->next=NULL;
pchunk=&one->chunks.first;
while (*pchunk) pchunk=&(*pchunk)->next;
*pchunk=chunk;
one->chunks.num+=1;
}

static void void_directoryname_set_export(struct one_export *one, char *dname) {
// dname should be static or mallocd
struct chunk_export *chunk;
chunk=&one->chunks._directory;
one->chunks.directory=chunk;
chunk->type=DIR_TYPE_CHUNK_EXPORT;
chunk->directoryname=dname;
(void)addchunk(one,chunk);
}
int directoryname_set_export(struct all_export *all, struct one_export *one, char *directoryname_in) {
char *dname;
if (one->chunks.directory) return 0; // already set, ignore redef
if (!(dname=strdup_blockmem(&all->tofree.blockmem,directoryname_in))) GOTOERROR;
(void)void_directoryname_set_export(one,dname);
return 0;
error:
	return -1;
}

int padto4k_set_export(struct all_export *all, struct one_export *one) {
struct chunk_export *chunk;
if (!(chunk=FALLOC(&all->tofree.blockmem,struct chunk_export))) GOTOERROR;
chunk->type=PADTO4K_TYPE_CHUNK_EXPORT;
(void)addchunk(one,chunk);
return 0;
error:
	return -1;
}

int filename_set_export(struct all_export *all, struct one_export *one, char *filename) {
struct chunk_export *chunk;
if (!(chunk=FALLOC(&all->tofree.blockmem,struct chunk_export))) GOTOERROR;
chunk->type=FILE_TYPE_CHUNK_EXPORT;
if (!(chunk->filename=(char *)memdup_blockmem(&all->tofree.blockmem,
		(unsigned char *)filename,strlen(filename)+1))) GOTOERROR;
(void)addchunk(one,chunk);
return 0;
error:
	return -1;
}

int add_export(struct one_export **one_out, struct all_export *all, char *exportname) {
static struct one_export blankone;
struct one_export *one=NULL;
if (!(one=FALLOC(&all->tofree.blockmem,struct one_export))) GOTOERROR;
*one=blankone;
overclear_range(&one->range); // there's some room for improvement but not much
if (!(one->name=strdup_blockmem(&all->tofree.blockmem,exportname))) GOTOERROR;

one->istlsrequired=all->config.istlsrequired;
one->shorttimeout=all->config.shorttimeout;

one->longtimeout=all->defaults.longtimeout;
one->isdenydefault=all->defaults.isdenydefault;
one->ispreload=all->defaults.ispreload;
one->isnodelay=all->defaults.isnodelay;
one->iskeepalive=all->defaults.iskeepalive;
one->islisted=all->defaults.islisted;
one->iskeyrequired=all->defaults.iskeyrequired;
one->gziplevel=all->defaults.gziplevel;
one->maxfiles=all->defaults.maxfiles;

one->id=all->exports.count;
all->exports.count+=1;

one->overlays=all->overlays;
one->keys=all->keys;
one->allowedhosts=all->allowedhosts;

one->next=all->exports.first;
all->exports.first=one;

*one_out=one;
return 0;
error:
	return -1;
}

static int applyoverlays(struct scan *scan, struct one_export *one, struct options *options) {
struct overlay_export *o;
for (o=one->overlays.first;o;o=o->next) {
	if (applyoverlay_scan(scan,o->realpath,o->fakepath,o->israw,options)) GOTOERROR;
}
return 0;
error:
	return -1;
}

static int insert4kpad(struct range *range) {
unsigned int ns;
ns=(unsigned int)range->entries.nextstart;
ns=((ns-1)&4095)^4095;
if (!ns) return 0;
if (add_internal_range(range,NULL,ns)) GOTOERROR;
return 0;
error:
	return -1;
}

static int insertfile(uint64_t *stamp_out, struct range *range, char *filename) {
struct stat st;
uint64_t u64;
int fd=-1;

if (0>(fd=open(filename,O_RDONLY))) {
	syslog(LOG_ERR,"Unabled to open %s %s",filename,strerror(errno));
	GOTOERROR;
}
if (fstat(fd,&st)) GOTOERROR;
switch (st.st_mode&S_IFMT) {
	case S_IFREG: break;
	case S_IFBLK:
		if (getsize2_blockdevice(&u64,fd)) GOTOERROR;
		st.st_size=u64;
//		if (0>ioctl(fd,BLKGETSIZE64,&st.st_size)) GOTOERROR;
		break;
	default: GOTOERROR;
}
if (!st.st_size) {
	*stamp_out=st.st_mtim.tv_sec+(st.st_mtim.tv_nsec/(1000*1000));
	(ignore)close(fd);
	return 0;
}

if (flock(fd,LOCK_SH)) { // continue on error for readlock
	syslog(LOG_ERR,"Couldn't lock file for reading %s %s",filename,strerror(errno));
}
// filename is stored in export's blockmem, no need to realloc it in range
if (noalloc_add_fd_range(range,fd,filename,st.st_size)) GOTOERROR;
*stamp_out=st.st_mtim.tv_sec+(st.st_mtim.tv_nsec/(1000*1000));
return 0;
error:
	ignore_ifclose(fd);
	return -1;
}

int build_one_export(struct one_export *one, struct options *options) {
// check one->isbuilt before calling this
struct scan scan;
struct temp_sqfs_mkfs mkfs;
struct assemble assemble;
unsigned int log_blocksize=17;
struct timespec start_time,end_time;
struct chunk_export *chunk;
uint64_t highestfilestamp=0;

clear_scan(&scan);
clear_temp_sqfs_mkfs(&mkfs);
clear_assemble(&assemble);

chunk=one->chunks.first;
#if 0
if (!(chunk=one->chunks.first)) return 0; // finalize_overlays() obviates this
#endif

if (one->chunks.directory) {
	if (clock_gettime(CLOCK_MONOTONIC_RAW,&start_time)) GOTOERROR;
	if (init_scan(&scan,(1<<20),one->maxfiles)) GOTOERROR;
	if (setrootdir_scan(&scan,one->chunks.directory->directoryname,options)) GOTOERROR;
	if (applyoverlays(&scan,one,options)) GOTOERROR;
	// if (finalize_scan(&scan)) GOTOERROR;
	if (init_temp_sqfs_mkfs(&mkfs,&scan.mapmem,1<<log_blocksize,one->gziplevel)) GOTOERROR;
	voidinit_assemble(&assemble,&scan,&mkfs,&one->range,log_blocksize);
}
if (init_range(&one->range,3+scan.counts.non0files + (one->chunks.num - 1), // maxentries: 1: superblock, scan.counts.non0files: 1 per file, 1: inodes+dirs+tables, 1: 4k padding
		1+scan.counts.subdirs,
		NUM_SUPERBLOCK_SQFS_MKFS+scan.counts.namelens, // maxnames: 96: superblock, >0 filenames and !iszero subdirs
		scan.counts.maxdepth)) GOTOERROR;

while (1) {
	uint64_t stamp;
	switch (chunk->type) {
		case DIR_TYPE_CHUNK_EXPORT: if (build_assemble(&assemble)) GOTOERROR; break;
		case FILE_TYPE_CHUNK_EXPORT:
			if (insertfile(&stamp,&one->range,chunk->filename)) {
				syslog(LOG_ERR,"Unable to insert chunk file \"%s\"",chunk->filename);
				GOTOERROR;
			}
			if (stamp>highestfilestamp) highestfilestamp=stamp;
			break;
		case PADTO4K_TYPE_CHUNK_EXPORT: if (insert4kpad(&one->range)) GOTOERROR; break;
	}
	chunk=chunk->next;
	if (!chunk) break;
}

if (options->isverbose) {
	if (assemble.isbuilt) {
		syslog(LOG_INFO,"[%s] byte sizes: { archive:%"PRIu64", files:%"PRIu64", squashfs:%u, 4kpad:%u, gzip:%u }",
				one->name,
				assemble.stats.bytecounts.archive, assemble.stats.bytecounts.files,
				assemble.stats.bytecounts.squashfs, assemble.stats.bytecounts.padding,
				assemble.stats.bytecounts.bytessaved);
	}
}

one->stats.filecount=scan.counts.files;
one->stats.subdircount=scan.counts.subdirs;


if (one->chunks.directory) {
	deinit_assemble(&assemble); clear_assemble(&assemble);
	deinit_temp_sqfs_mkfs(&mkfs); clear_temp_sqfs_mkfs(&mkfs);
	deinit_scan(&scan); clear_scan(&scan);

	if (clock_gettime(CLOCK_MONOTONIC_RAW,&end_time)) GOTOERROR;
	{
		unsigned int msec;
		msec=(end_time.tv_sec-start_time.tv_sec)*1000;
		if (end_time.tv_nsec >= start_time.tv_nsec) msec+=(end_time.tv_nsec-start_time.tv_nsec)/(1000*1000);
		msec-=(start_time.tv_nsec-end_time.tv_nsec)/(1000*1000);
		one->stats.msec_buildtime=msec;
	}
	if (options->isverbose) {
		syslog(LOG_INFO,"Export %s (%u subdirs, %u files, %u msec).",
			one->name,one->stats.subdircount,one->stats.filecount,one->stats.msec_buildtime);
	}
	one->timestamp=msecstamp();
} else {
	one->timestamp=highestfilestamp;
}
one->isbuilt=1;
return 0;
error:
	deinit_assemble(&assemble);
	deinit_temp_sqfs_mkfs(&mkfs);
	deinit_scan(&scan);
	return -1;
}

#define getu64(a) *(uint64_t*)(a)
static inline int isiniprange6(struct iprange6_export *iprange6, unsigned char *ipv6) {
uint64_t high,low;

high=htobe64(getu64(ipv6+0)) & iprange6->high_netmask;
if (high!=iprange6->high_ipv6) return 0;
low=htobe64(getu64(ipv6+8)) & iprange6->low_netmask;
if (low!=iprange6->low_ipv6) return 0;
return 1;
}

static int isipv6inlist(struct iprange6_export *ipv6host, unsigned char *ipv6, int ignoreonlytls) {
if (ignoreonlytls) {
	for (;ipv6host;ipv6host=ipv6host->next) {
		if (ipv6host->isonlyiftls) continue;
		if (isiniprange6(ipv6host,ipv6)) return 1;
	}
} else {
	for (;ipv6host;ipv6host=ipv6host->next) {
		if (isiniprange6(ipv6host,ipv6)) return 1;
	}
}
return 0;
}


#define getu32(a) *(uint32_t*)(a)
static inline int isiniprange4(struct iprange4_export *iprange4, unsigned char *ipv4) {
uint32_t hui;
hui=htobe32(getu32(ipv4));
if (iprange4->ui32_ipv4!=(hui&iprange4->netmask)) return 0;
return 1;
}

static int isipv4inlist(struct iprange4_export *ipv4host, unsigned char *ipv4, int ignoreonlytls) {
if (ignoreonlytls) {
	for (;ipv4host;ipv4host=ipv4host->next) {
		if (ipv4host->isonlyiftls) continue;
		if (isiniprange4(ipv4host,ipv4)) return 1;
	}
} else {
	for (;ipv4host;ipv4host=ipv4host->next) {
		if (isiniprange4(ipv4host,ipv4)) return 1;
	}
}
return 0;
}

static inline char *startswith(char *str, char *sub) {
while (1) {
	if (!*sub) return str;
	if (*str!=*sub) return NULL;
	str++;
	sub++;
}
}

static inline struct key_export *findkeyinlist(struct key_export *first, char *key) {
for (;first;first=first->next) {
	if (!strcmp(first->key,key)) return first;	
}
return NULL;
}

struct one_export *ipv6_findone_export(int *ismissingkey_out, struct all_export *all, char *exportname, unsigned char *ipv6, int istls) {
struct one_export *one;
int isnontls=~istls;
int missingkey=0;

for (one=all->exports.first;one;one=one->next) {
	char *tail;
	if (one->isdisabled) continue;
	if (one->istlsrequired && isnontls) continue;
	if (!(tail=startswith(exportname,one->name))) continue;
	if (one->isdenydefault) {
		if (!isipv6inlist(one->allowedhosts.firstipv6,ipv6,isnontls)) continue;
	}
	if (one->iskeyrequired) {
		if (one->islisted) missingkey=1;
		if (*tail!=']') continue;
		tail++;
		if (!findkeyinlist(one->keys.first,tail)) continue;
		return one;
	}
	if (!*tail) return one;
}
*ismissingkey_out=missingkey;
return NULL;
}
struct one_export *ipv4_findone_export(int *ismissingkey_out, struct all_export *all, char *exportname, unsigned char *ipv4, int istls) {
struct one_export *one;
int isnontls=~istls;
int missingkey=0;

for (one=all->exports.first;one;one=one->next) {
	char *tail;
	if (one->isdisabled) continue;
	if (one->istlsrequired && isnontls) continue;
	if (!(tail=startswith(exportname,one->name))) continue;
	if (one->isdenydefault) {
		if (!isipv4inlist(one->allowedhosts.firstipv4,ipv4,isnontls)) continue;
	}
	if (one->iskeyrequired) {
		if (one->islisted) missingkey=1;
		if (*tail!=']') continue;
		tail++;
		if (!findkeyinlist(one->keys.first,tail)) continue;
		return one;
	}
	if (!*tail) return one;
}
*ismissingkey_out=missingkey;
return NULL;
}

// findany is used to show if there's any reason to communicate with this client at all
struct one_export *ipv6_findany_export(struct all_export *all, unsigned char *ipv6) {
struct one_export *one;

for (one=all->exports.first;one;one=one->next) {
	if (!one->isdenydefault) return one;
	if (isipv6inlist(one->allowedhosts.firstipv6,ipv6,0)) return one;
}

return NULL;
}
struct one_export *ipv4_findany_export(struct all_export *all, unsigned char *ipv4) {
struct one_export *one;

for (one=all->exports.first;one;one=one->next) {
	if (!one->isdenydefault) return one;
	if (isipv4inlist(one->allowedhosts.firstipv4,ipv4,0)) return one;
}

return NULL;
}

int preload_export(struct all_export *exports, struct options *options) {
struct one_export *one;
for (one=exports->exports.first;one;one=one->next) {
	if (!one->ispreload) continue;
	if (one->isbuilt) continue;
	if (build_one_export(one,options)) GOTOERROR;
}
return 0;
error:
	return -1;
}

int rebuild_one_export(struct one_export *one, struct options *options) {
(void)reset_range(&one->range);
one->isdisabled=0;
if (build_one_export(one,options)) {
	one->isdisabled=1;
}
return 0;
}

struct one_export *findbyid_one_export(struct all_export *exports, uint32_t id) {
struct one_export *one;
for (one=exports->exports.first;one;one=one->next) {
	if (one->id!=id) continue;
	return one;
}
return NULL;
}

// isallowed_ is used for listing exports, we don't list ones that aren't allowed
int isallowed_export(struct one_export *one, unsigned char *ip, int isipv4) {
if (!one->isdenydefault) return 1;
if (isipv4) {
	if (isipv4inlist(one->allowedhosts.firstipv4,ip,0)) return 1;
} else {
	if (isipv6inlist(one->allowedhosts.firstipv6,ip,0)) return 1;
}
return 0;
}

static int makeoverlay(struct overlay_export **o_out, struct all_export *exports, char *str, int israw, struct options *opts) {
char *start,*mid,*end;
struct overlay_export *o;
for (start=str;isspace(*start);start++);
mid=strstr(start," -> ");
if (!mid) {
	*o_out=NULL;
	return 0;
}
for (end=mid+4;isspace(*end);end++);
if (!(o=FALLOC(&exports->tofree.blockmem,struct overlay_export))) GOTOERROR;
if (!(o->realpath=strdup2_blockmem(&exports->tofree.blockmem,(unsigned char *)start,(unsigned int)(mid-start)))) GOTOERROR;
if (!(o->fakepath=strdup_blockmem(&exports->tofree.blockmem,end))) GOTOERROR;;
o->israw=israw;
if (opts->isverbose && (!israw)) {
	if (access(o->realpath,F_OK)) { // it may be accessible later, so don't error
		syslog(LOG_INFO,"Unable to access overlay file \"%s\"",o->realpath);
	}
}
#if 0
// fprintf(stderr,"%s:%d realpath:\"%s\" fakepath=\"%s\"\n",__FILE__,__LINE__,o->realpath,o->fakepath);
#endif
// o->next=NULL;
*o_out=o;
return 0;
error:
	return -1;
}

int overlay_add_one_export(struct all_export *exports, struct one_export *one, char *str, int israw, struct options *options) {
struct overlay_export *o;
if (!one->chunks.directory) { // if we don't have a directory chunk, add a blank one
	(void)void_directoryname_set_export(one,"");
}
if (makeoverlay(&o,exports,str,israw,options)) GOTOERROR;
if (!o) return 0;
o->next=one->overlays.first;
one->overlays.first=o;
return 0;
error:
	return -1;
}
int overlay_add_export(struct all_export *exports, char *str, int israw, struct options *options) {
struct overlay_export *o;
if (!str) { // => reset overlays
	exports->overlays.first=NULL;
	return 0;
}
if (makeoverlay(&o,exports,str,israw,options)) GOTOERROR;
if (!o) return 0;
o->next=exports->overlays.first;
exports->overlays.first=o;
return 0;
error:
	return -1;
}

int key_add_export(struct all_export *all, char *str) {
struct key_export *key;
if (!str) { // !str => reset keys
	all->keys.first=NULL;
	return 0;
}
if (!(key=FALLOC(&all->tofree.blockmem,struct key_export))) GOTOERROR;
if (!(key->key=strdup_blockmem(&all->tofree.blockmem,str))) GOTOERROR;
key->next=all->keys.first;
all->keys.first=key;
return 0;
error:
	return -1;
}

int key_add_one_export(struct all_export *all, struct one_export *one, char *str) {
struct key_export *key;
if (!*str) GOTOERROR; // this is a bad idea
if (!(key=FALLOC(&all->tofree.blockmem,struct key_export))) GOTOERROR;
if (!(key->key=strdup_blockmem(&all->tofree.blockmem,str))) GOTOERROR;
key->next=one->keys.first;
one->keys.first=key;
return 0;
error:
	return -1;
}

int setfilename_export(char **filename_out, struct all_export *all, char *filename) {
if (access(filename,F_OK)) {
	syslog(LOG_ERR,"Couldn't access given file \"%s\"",filename); 
	GOTOERROR;
}
if (!(filename=strdup_blockmem(&all->tofree.blockmem,filename))) GOTOERROR;
*filename_out=filename;
return 0;
error:
	return -1;
}

static inline void finalize_overlays(struct all_export *all) {
struct one_export *one,**ppn;
ppn=&all->exports.first;
for (one=all->exports.first;one;one=one->next) {
	if (!one->chunks.first) {
		if (one->overlays.first) { // we had global overlays but nothing else
			(void)void_directoryname_set_export(one,""); // setting a directory helps build_one_export()
		} else {
			syslog(LOG_INFO,"Ignoring empty export: %s",one->name);
			*ppn=one->next; // removing export lets build_one_export assume non-emptiness
			continue;
		}
	}
	ppn=&one->next;
}
}

int finalize_all_export(struct all_export *all) {
(void)finalize_overlays(all);
return 0;
}

