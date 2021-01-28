/*
 * assemble.c - create pseudo squashfs from scan and range
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
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <syslog.h>
#include <zlib.h>
// #define DEBUG2
#include "common/conventions.h"
#include "common/mmapread.h"
#include "common/mapmem.h"
#include "options.h"
#include "scan.h"
#include "range.h"
#include "mkfs.h"

#include "assemble.h"


void voidinit_assemble(struct assemble *a, struct scan *s, struct temp_sqfs_mkfs *m, struct range *r, unsigned int log_blocksize) {
if (log_blocksize<12) log_blocksize=17;
if (log_blocksize>20) log_blocksize=20;
a->scan=s;
a->mkfs=m;
a->range=r;
a->blocksize=(1<<log_blocksize);
a->log_blocksize=log_blocksize;
}

SICLEARFUNC(dirsize_mkfs);
static int directory_build(struct assemble *a, struct directory_scan *d, struct directory_range *parent,
		char *dirname, unsigned int dirnamelen, char *overlay) {
struct scan *scan=a->scan;
struct dirent_scan *de;
struct directory_range *rd;
struct dirsize_mkfs dirsize,w_dirsize;
unsigned int blocksoffset_d;
unsigned short offsetinblock_d;

clear_dirsize_mkfs(&dirsize);
clear_dirsize_mkfs(&w_dirsize);

// DEBUGOUT((stderr,"Entering directory i:%u\n",d->common.inodeindex));
if (!d->isnotzero) rd=NULL; // no need to save directory in range (directory not necessarily empty for fs)
else if (overlay) {
	 if (!(rd=add_directory_range(a->range,NULL,overlay,strlen(overlay)))) GOTOERROR;
} else if (!(rd=add_directory_range(a->range,parent,dirname,dirnamelen))) GOTOERROR;

(void)setlinearvars_scan(scan,d); // sets next pointers and inodeindex vals

// we need to have inodeindex values assigned at this point: subdirs need parent dir inodeindex
// we add subdirs first because we need to know their inode location to make dirents
for (de=d->entries.first;de;de=de->next) {
	switch (de->type) {
		case DIRECTORY_TYPE_SCAN:
			if (directory_build(a,de->directory,rd,de->filename,de->filenamelen,de->overlay)) GOTOERROR; // build subdirs first
		break;
	}
}

blocksoffset_d=a->mkfs->directory_table.blockoffset;
offsetinblock_d=a->mkfs->directory_table.blockfill;

// add inodes, ranges and calculate dirent sizes
// this could be backfilled but this way we can fill dirhead values as we write them
for (de=d->entries.first;de;de=de->next) {
// DEBUGOUT((stderr,"%s:%d Entry %s\n",__FILE__,__LINE__,de->filename));
	switch (de->type) {
		case FILE_TYPE_SCAN:
			if (!de->file->common.inode->dataoffset) {
				de->file->common.inode->dataoffset=a->range->entries.nextstart;
				if (de->file->size) {
					if (de->overlay) {
						if (add_external_range(a->range,NULL,de->overlay,strlen(de->overlay),de->file->size)) GOTOERROR;
					} else {
#ifdef DEBUG
						if (!rd) GOTOERROR;
#endif
						if (add_external_range(a->range,rd,de->filename,de->filenamelen,de->file->size)) GOTOERROR;
					}
				}
				if (add_file_inode_mkfs(a->mkfs,de->file)) GOTOERROR;
			}
			(ignore)count_dirsize_mkfs(&dirsize,&de->file->common,de->filenamelen);
			break;
		case SYMLINK_TYPE_SCAN:
			if (!de->symlink->common.inode->dataoffset) {
				de->symlink->common.inode->dataoffset=96; // there is no actual data but hardlinks need to mark this as written
				if (add_symlink_inode_mkfs(a->mkfs,de->symlink,de->filename,de->filenamelen)) GOTOERROR;
			}
			(ignore)count_dirsize_mkfs(&dirsize,&de->symlink->common,de->filenamelen);
			break;
		case DIRECTORY_TYPE_SCAN:
			if (add_directory_inode_mkfs(a->mkfs,de->directory,d)) GOTOERROR;
			(ignore)count_dirsize_mkfs(&dirsize,&de->directory->common,de->filenamelen);
			break;
		case BDEV_TYPE_SCAN:
			if (!de->bdev->common.inode->dataoffset) {
				de->bdev->common.inode->dataoffset=96;
				if (add_bdev_inode_mkfs(a->mkfs,de->bdev,de->filename,de->filenamelen)) GOTOERROR;
			}
			(ignore)count_dirsize_mkfs(&dirsize,&de->bdev->common,de->filenamelen);
			break;
		case CDEV_TYPE_SCAN:
			if (!de->cdev->common.inode->dataoffset) {
				de->cdev->common.inode->dataoffset=96;
				if (add_cdev_inode_mkfs(a->mkfs,de->cdev,de->filename,de->filenamelen)) GOTOERROR;
			}
			(ignore)count_dirsize_mkfs(&dirsize,&de->cdev->common,de->filenamelen);
			break;
	}
}
(void)finish_dirsize_mkfs(&dirsize);
// now everything is done except dirheads/dirents and we have all the values
// we'll want to save the offset of the rootdir to the superblock
for (de=d->entries.first;de;de=de->next) {
	switch (de->type) {
		case FILE_TYPE_SCAN:
//			fprintf(stderr,"adding file %s (i:%u)\n",de->filename,de->file->common.inodeindex);
			if (add_file_dirent_mkfs(&w_dirsize,a->mkfs,de)) GOTOERROR;
			break;
		case SYMLINK_TYPE_SCAN:
//			fprintf(stderr,"adding symlink %s (i:%u)\n",de->filename,de->symlink->common.inodeindex);
			if (add_symlink_dirent_mkfs(&w_dirsize,a->mkfs,de)) GOTOERROR;
			break;
		case DIRECTORY_TYPE_SCAN:
//			fprintf(stderr,"adding directory %s (i:%u)\n",de->filename,de->directory->common.inodeindex);
			if (add_directory_dirent_mkfs(&w_dirsize,a->mkfs,de)) GOTOERROR;
			break;
		case BDEV_TYPE_SCAN:
//			fprintf(stderr,"adding block device %s (i:%u)\n",de->filename,de->file->common.inodeindex);
			if (add_bdev_dirent_mkfs(&w_dirsize,a->mkfs,de)) GOTOERROR;
			break;
		case CDEV_TYPE_SCAN:
//			fprintf(stderr,"adding character device %s (i:%u)\n",de->filename,de->file->common.inodeindex);
			if (add_cdev_dirent_mkfs(&w_dirsize,a->mkfs,de)) GOTOERROR;
			break;
	}
}

if (w_dirsize.size!=dirsize.size) {
#ifdef DEBUG
	fprintf(stderr,"%s:%d expected size: %u, actual size: %u\n",__FILE__,__LINE__,dirsize.size,w_dirsize.size);
#endif
	GOTOERROR;
}
d->tablesize=dirsize.size;
d->blocksoffset=blocksoffset_d;
d->offsetinblock=offsetinblock_d;
return 0;
error:
	return -1;
}

static int idblocks_build(struct assemble *a, struct id_scan *top) {
return fixandadd_idblocks_mkfs(a->mkfs,top);
}

int build_assemble(struct assemble *a) {
unsigned char *superblock;
struct superblock_sqfs_mkfs sb;
uint64_t rootref;
unsigned int tablesizes,pad4k;
#ifdef DEBUG
unsigned int idblockoffset;
#endif
uint64_t id_table_start,idblock_table_start,inode_table_start,directory_table_start,archivesize;
uint64_t archivebase;

if (a->isbuilt) return 0;
a->isbuilt=1;

archivebase=a->range->entries.nextstart; // usually 0, but inject can push this up
if (!(superblock=alloc_name_range(a->range,NUM_SUPERBLOCK_SQFS_MKFS))) GOTOERROR;
if (add_internal_range(a->range,superblock,NUM_SUPERBLOCK_SQFS_MKFS)) GOTOERROR;
if (idblocks_build(a,a->scan->ids.top)) GOTOERROR;
// TODO move rootdir.path into a fake directory_range
if (directory_build(a,&a->scan->rootdir.directory,NULL,NULL,0,a->scan->rootdir.path)) GOTOERROR;
rootref=((uint64_t)a->mkfs->inode_table.blockoffset<<16)|a->mkfs->inode_table.blockfill;
a->scan->rootdir.directory.common.inode->inodeindex=++a->scan->inodes.count;
if (add_directory_inode_mkfs(a->mkfs,&a->scan->rootdir.directory,NULL)) GOTOERROR;
if (a->scan->inodes.count!=a->scan->counts.inodes) GOTOERROR;
a->mkfs->idblocklist.listsize=count_metablock_mkfs(a->mkfs->idblock_table.first)*8;
#ifdef DEBUG2
if (a->mkfs->idblocklist.listsize!=(a->scan->ids.count+1023/1024)*8) GOTOERROR;
#endif

if (compresstables_mkfs(a->mkfs)) GOTOERROR;


inode_table_start=a->range->entries.nextstart - archivebase;
tablesizes=size_table_mkfs(&a->mkfs->inode_table);

// fprintf(stderr,"inode_table_start=%"PRIu64"\n",inode_table_start);

directory_table_start=inode_table_start+tablesizes;
tablesizes+=size_table_mkfs(&a->mkfs->directory_table);

#ifdef DEBUG
idblockoffset=tablesizes;
#endif
idblock_table_start=inode_table_start+tablesizes;
tablesizes+=size_table_mkfs(&a->mkfs->idblock_table); // idblock_table is not id_table
id_table_start=inode_table_start+tablesizes;

tablesizes+=a->mkfs->idblocklist.listsize;
archivesize=inode_table_start+tablesizes;
pad4k=4095^((archivesize-1)&4095); // aka 4095-((archivesize-1)%4095)

if (!(a->range->extra.other=malloc(tablesizes+pad4k))) GOTOERROR;
{
	unsigned char *dest=a->range->extra.other;
	uint64_t idblockcur;
	struct metablock_mkfs *metablock;
	unsigned int bytecount=0;

	(void)copytable_mkfs(&dest,&bytecount,&a->mkfs->inode_table);
	(void)copytable_mkfs(&dest,&bytecount,&a->mkfs->directory_table);
#ifdef DEBUG
	if (bytecount!=idblockoffset) GOTOERROR;
#endif
	(void)copytable_mkfs(&dest,&bytecount,&a->mkfs->idblock_table);
//	idblockcur=a->range->entries.nextstart+idblockoffset;
	idblockcur=idblock_table_start;
	metablock=a->mkfs->idblock_table.first;
#define setu64(a,b) *(uint64_t*)(a)=htole64(b)
	while (1) {
		setu64(dest,idblockcur);
		dest+=8;
		bytecount+=8;

		idblockcur+=2+almost_size_metablock_mkfs(metablock);

		metablock=metablock->next;
		if (!metablock) break;
	}
	if (bytecount!=tablesizes) GOTOERROR;
	memset(dest,0,pad4k);
}
if (add_internal_range(a->range,a->range->extra.other,tablesizes+pad4k)) GOTOERROR;

sb.magic=0x73717368;
sb.inode_count=a->scan->inodes.count;
sb.modification_time=(unsigned int)time(NULL);
sb.block_size=a->blocksize;
sb.fragment_entry_count=0;
sb.compression_id=1;
sb.block_log=a->log_blocksize;
sb.flags=DEFAULT_FLAGS_SQFS_MKFS; // only "0x0400: Compressor options" is necessary, not present
sb.id_count=a->scan->ids.count;
sb.version_major=4;
sb.version_minor=0;
sb.root_inode_ref=rootref;
sb.bytes_used=archivesize;
sb.id_table_start=id_table_start;
sb.attr_id_table_start=0xFFFFFFFFFFFFFFFF;
sb.inode_table_start=inode_table_start;
sb.directory_table_start=directory_table_start;
sb.fragment_table_start=0xFFFFFFFFFFFFFFFF;
sb.export_table_start=0xFFFFFFFFFFFFFFFF;
(void)fill_superblock_sqfs_mkfs(superblock,&sb);

a->stats.bytecounts.archive=sb.bytes_used;
a->stats.bytecounts.files=inode_table_start-NUM_SUPERBLOCK_SQFS_MKFS;
a->stats.bytecounts.squashfs=tablesizes+NUM_SUPERBLOCK_SQFS_MKFS;
a->stats.bytecounts.padding=pad4k;
a->stats.bytecounts.bytessaved=a->mkfs->stats.bytessaved;

return 0;
error:
	return -1;
}
