/*
 * mkfs.c - calls to make squashfs
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
#include <stdint.h>
#include <endian.h>
#include <syslog.h>
#include <zlib.h>
#include "common/conventions.h"
#include "common/mapmem.h"
#include "options.h"
#include "scan.h"

#include "mkfs.h"

#define SIZE_METABLOCK	8192

static struct metablock_mkfs *new_metablock(struct mapmem *mm, unsigned int size) {
struct metablock_mkfs *metablock;
if (!(metablock=alloc_mapmem(mm,size+sizeof(struct metablock_mkfs)))) GOTOERROR;
metablock->compressedsize=0;
metablock->data=(unsigned char *)metablock+sizeof(struct metablock_mkfs);
metablock->next=NULL;
return metablock;
error:
	return NULL;
}

static int init_table(struct table_mkfs *t, struct mapmem *mm) {
if (!(t->first=t->current=new_metablock(mm,SIZE_METABLOCK))) GOTOERROR;
t->blockleft=SIZE_METABLOCK;
return 0;
error:
	return -1;
}

static void *zalloc(void *mm_in, unsigned int count, unsigned int size) {
struct mapmem *mm=(struct mapmem*)mm_in;
// fprintf(stderr,"%s:%d %s allocating %u (%u * %u) bytes\n",__FILE__,__LINE__,__FUNCTION__,count*size,count,size);
return alloc_mapmem(mm,count*size); // TODO think about alignment
}
static void zfree(void *mm_in, void *ptr) {
// gzip allocates about 256k, it'll be freed when mapmem unmaps, after assemble
// fprintf(stderr,"%s:%d %s %p\n",__FILE__,__LINE__,__FUNCTION__,ptr);
}

int init_temp_sqfs_mkfs(struct temp_sqfs_mkfs *s, struct mapmem *mm, unsigned int blocksize, unsigned int gziplevel) {
s->mm=mm;
s->config.blocksize=blocksize;
if (init_table(&s->inode_table,mm)) GOTOERROR;
if (init_table(&s->directory_table,mm)) GOTOERROR;
if (init_table(&s->idblock_table,mm)) GOTOERROR;
if (gziplevel) {
	if (!(s->compress.spareblock=alloc_mapmem(mm,SIZE_METABLOCK))) GOTOERROR;
	s->compress.zstream.zalloc=zalloc;
	s->compress.zstream.zfree=zfree;
	s->compress.zstream.opaque=(void *)mm;
	if (Z_OK!=deflateInit(&s->compress.zstream,gziplevel)) {
		s->compress.zstream.opaque=NULL;
		GOTOERROR; // could also disable gzip and ignore error
	}
}
return 0;
error:
	return -1;
}
#if 0
void deinit_temp_sqfs_mkfs(struct temp_sqfs_mkfs *s) {
if (s->compress.zstream.opaque) {
//	(ignore)deflateEnd(&s->compress.zstream); // there's no need for this as it doesn't free anything anyway
}
}
#endif

static int compress_metablock(struct temp_sqfs_mkfs *temp, struct metablock_mkfs *current, unsigned int blockfill) {
struct z_stream_s *zs;
int isworthit=0;

zs=&temp->compress.zstream;

zs->next_in=current->data;
zs->avail_in=blockfill;
zs->next_out=temp->compress.spareblock;
zs->avail_out=SIZE_METABLOCK;
zs->total_out=0; // is this necessary? it isn't 0d by deflateReset

switch (deflate(zs,Z_FINISH)) {
	case Z_STREAM_END: // successful compression
		if (zs->total_out<blockfill) isworthit=1;
		break;
	case Z_BUF_ERROR: break;
	default: GOTOERROR;
}
if (isworthit) {
	current->compressedsize=zs->total_out;
// fprintf(stderr,"%s:%d Successful gzip, %u -> %u\n",__FILE__,__LINE__blockfill,current->compressedsize);
	temp->stats.bytessaved+=blockfill-current->compressedsize;
	memcpy(current->data,temp->compress.spareblock,current->compressedsize);
}

if (Z_OK!=deflateReset(zs)) GOTOERROR;
return 0;
error:
	return -1;
}

int compresstables_mkfs(struct temp_sqfs_mkfs *temp) {
if (!temp->compress.zstream.opaque) return 0; // no gzip
if (compress_metablock(temp,temp->inode_table.current,temp->inode_table.blockfill)) GOTOERROR;
if (compress_metablock(temp,temp->directory_table.current,temp->directory_table.blockfill)) GOTOERROR;
if (compress_metablock(temp,temp->idblock_table.current,temp->idblock_table.blockfill)) GOTOERROR;
return 0;
error:
	return -1;
}

static struct metablock_mkfs *swap_metablock(unsigned short *blocksize_out, struct temp_sqfs_mkfs *temp,
		struct metablock_mkfs *current) {
struct z_stream_s *zs;
struct metablock_mkfs *nmb;
unsigned char *data;
unsigned short csize;
int r;

zs=&temp->compress.zstream;

if (!zs->opaque) { // gzip disabled
	*blocksize_out=SIZE_METABLOCK;
	return new_metablock(temp->mm,SIZE_METABLOCK);
}

zs->next_in=current->data;
zs->avail_in=SIZE_METABLOCK;
zs->next_out=temp->compress.spareblock;
zs->avail_out=SIZE_METABLOCK;
zs->total_out=0; // is this necessary? it isn't 0d by deflateReset

r=deflate(zs,Z_FINISH);
csize=zs->total_out;
if ((r!=Z_STREAM_END) || (csize>=SIZE_METABLOCK)) {
	if ((r!=Z_BUF_ERROR)&&(r!=Z_STREAM_END)) GOTOERROR;

	if (Z_OK!=deflateReset(zs)) GOTOERROR;
	*blocksize_out=SIZE_METABLOCK;
	return new_metablock(temp->mm,SIZE_METABLOCK);
}

if (!(nmb=new_metablock(temp->mm,csize))) GOTOERROR;
current->compressedsize=csize;
// fprintf(stderr,"%s:%d Successful gzip, %u -> %u\n",__FILE__,__LINE__,SIZE_METABLOCK,csize);
temp->stats.bytessaved+=SIZE_METABLOCK-csize;
data=nmb->data;
memcpy(data,temp->compress.spareblock,csize);
nmb->data=current->data;
current->data=data;

if (Z_OK!=deflateReset(zs)) GOTOERROR;
*blocksize_out=csize;
return nmb;
error:
	return NULL;
}

static int append_table_mkfs(struct temp_sqfs_mkfs *temp, struct table_mkfs *table, unsigned char *packet, unsigned int num) {
struct metablock_mkfs *current;

current=table->current;
while (1) {
	unsigned int k;
	if (!num) break;
	k=num;
	if (!table->blockleft) {
		struct metablock_mkfs *nmb;
		unsigned short blocksize;
		if (!(nmb=swap_metablock(&blocksize,temp,current))) GOTOERROR;
		current->next=nmb;
		current=table->current=nmb;
		table->blockfill=0;
		table->blockleft=SIZE_METABLOCK;
		table->blockoffset+=blocksize+2;
	}
	if (k>table->blockleft) k=table->blockleft;
	memcpy(current->data+table->blockfill,packet,k);
	table->blockfill+=k;
	table->blockleft-=k;
	num-=k;
	packet+=k;
}
return 0;
error:
	return -1;
}
static int add_inode_mkfs(struct temp_sqfs_mkfs *temp, unsigned char *packet, unsigned int num) {
return append_table_mkfs(temp,&temp->inode_table,packet,num);
}
static int add_directory_mkfs(struct temp_sqfs_mkfs *temp, unsigned char *packet, unsigned int num) {
return append_table_mkfs(temp,&temp->directory_table,packet,num);
}
static int add_idblock_mkfs(struct temp_sqfs_mkfs *temp, unsigned char *packet, unsigned int num) {
return append_table_mkfs(temp,&temp->idblock_table,packet,num);
}

#define BASICDIRECTORY_TYPE_COMMON_INODE	1 //   | Basic Directory                              |
#define BASICFILE_TYPE_COMMON_INODE	2 //   | Basic File                                   |
#define BASICSYMLINK_TYPE_COMMON_INODE	3 //   | Basic Symlink                                |
#define BASICBLOCKDEVICE_TYPE_COMMON_INODE	4 //   | Basic Block Device                           |
#define BASICCHARDEVICE_TYPE_COMMON_INODE	5 //   | Basic Character Device                       |
#define BASICFIFO_TYPE_COMMON_INODE	6 //   | Basic Named Pipe (FIFO)                      |
#define BASICSOCKET_TYPE_COMMON_INODE	7 //   | Basic Socket                                 |
#define EXDIRECTORY_TYPE_COMMON_INODE	8 //   | Extended Directory                           |
#define EXFILE_TYPE_COMMON_INODE	9 //   | Extended File                                |
#define EXSYMLINK_TYPE_COMMON_INODE	10 //  | Extended Symlink                             |
#define EXBLOCKDEVICE_TYPE_COMMON_INODE	11 //  | Extended Block Device                        |
#define EXCHARDEVICE_TYPE_COMMON_INODE	12 //  | Extended Character Device                    |
#define EXFIFO_TYPE_COMMON_INODE	13 //  | Extended Named Pipe (FIFO)                   |
#define EXSOCKET_TYPE_COMMON_INODE	14 //  | Extended Socket                              |

struct common_inode {
	uint16_t	type;
	uint16_t	permissions;
	uint16_t	uid;
	uint16_t	gid;
	uint32_t	mtime;
	uint32_t	inode_number;
};

#define setu64(a,b) *(uint64_t*)(a)=htole64(b)
#define setu32(a,b) *(uint32_t*)(a)=htole32(b)
#define setu16(a,b) *(uint16_t*)(a)=htole16(b)

static inline int addcommon(struct temp_sqfs_mkfs *temp, struct common_scan *cs, uint16_t type) {
unsigned char buffer[16];

cs->inode->blocksoffset=temp->inode_table.blockoffset;
cs->inode->offsetinblock=temp->inode_table.blockfill;

setu16(buffer+0,type);
setu16(buffer+2,cs->mode);
setu16(buffer+4,cs->uid->index);
setu16(buffer+6,cs->gid->index);
setu32(buffer+8,cs->mtime);
setu32(buffer+12,cs->inode->inodeindex);
if (add_inode_mkfs(temp,buffer,16)) GOTOERROR;
return 0;
error:
	return -1;
}

struct exdirectory_inode {
	uint32_t link_count;
	uint32_t file_size;
	uint32_t block_index;
	uint32_t parent_inode;
	uint16_t index_count;
	uint16_t block_offset;
	uint32_t xattr_index;
};

static inline int addexdirectory(struct temp_sqfs_mkfs *temp, struct exdirectory_inode *exd) {
unsigned char buffer[24];
setu32(buffer+0,exd->link_count);
setu32(buffer+4,exd->file_size);
setu32(buffer+8,exd->block_index);
setu32(buffer+12,exd->parent_inode);
setu16(buffer+16,exd->index_count);
setu16(buffer+18,exd->block_offset);
setu32(buffer+20,exd->xattr_index);
if (add_inode_mkfs(temp,buffer,24)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_directory_inode_mkfs(struct temp_sqfs_mkfs *temp, struct directory_scan *d, struct directory_scan *parent) {
// dirsize: size in directory table, dirblock: start of block in directory table, diroffset: offset in block
struct exdirectory_inode exd;

if (addcommon(temp,&d->common,EXDIRECTORY_TYPE_COMMON_INODE)) GOTOERROR;

exd.link_count=d->linkcount;
exd.file_size=d->tablesize;
exd.block_index=d->blocksoffset;
exd.parent_inode=(parent)?parent->common.inode->inodeindex:0;
exd.index_count=0;
exd.block_offset=d->offsetinblock;
exd.xattr_index=0;

if (addexdirectory(temp,&exd)) GOTOERROR;
return 0;
error:
	return -1;
}

struct exfile_inode {
	uint64_t blocks_start;
	uint64_t file_size;
	uint64_t sparse;
	uint32_t link_count;
	uint32_t frag_index;
	uint32_t block_offset;
	uint32_t xattr_index;
// follow this with an array of compressed sizes of blocks (if not file_size:0)
};

#define FS_UINT32	0xffffffff

static inline int addexfile(struct temp_sqfs_mkfs *temp, struct exfile_inode *exfile) {
unsigned char buffer[40];
setu64(buffer+0,exfile->blocks_start);
setu64(buffer+8,exfile->file_size);
setu64(buffer+16,exfile->sparse);
setu32(buffer+24,exfile->link_count);
setu32(buffer+28,exfile->frag_index);
setu32(buffer+32,exfile->block_offset);
setu32(buffer+36,exfile->xattr_index);
if (add_inode_mkfs(temp,buffer,40)) GOTOERROR;
return 0;
error:
	return -1;
}

static int addblocksizes(struct temp_sqfs_mkfs *temp, uint64_t size) {
const unsigned int blocksize=temp->config.blocksize;
while (size) {
	unsigned char buff4[4];
	unsigned int k;
	k=blocksize;
	if (size<k) k=size;
	setu32(buff4,k|(1<<24)); // 16777216 is uncompressed bit
	if (add_inode_mkfs(temp,buff4,4)) GOTOERROR;
	size-=k;
}
return 0;
error:
	return -1;
}

int add_file_inode_mkfs(struct temp_sqfs_mkfs *temp, struct file_scan *f) {
struct exfile_inode exfile;

if (addcommon(temp,&f->common,EXFILE_TYPE_COMMON_INODE)) GOTOERROR;

exfile.blocks_start=f->common.inode->dataoffset;
exfile.file_size=f->size;
exfile.sparse=0;
exfile.link_count=f->common.inode->hardlinkcount;
exfile.frag_index=FS_UINT32;
exfile.block_offset=0;
exfile.xattr_index=FS_UINT32;

if (addexfile(temp,&exfile)) GOTOERROR;
if (addblocksizes(temp,f->size)) GOTOERROR;
return 0;
error:
	return -1;
}

struct symlink_inode {
	uint32_t link_count;
	uint32_t target_size;
};

static int addsymlink(struct temp_sqfs_mkfs *temp, struct symlink_inode *symlink) {
unsigned char buffer[8];
setu32(buffer+0,symlink->link_count);
setu32(buffer+4,symlink->target_size);
if (add_inode_mkfs(temp,buffer,8)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_symlink_inode_mkfs(struct temp_sqfs_mkfs *temp, struct symlink_scan *s, char *filename, unsigned int filenamelen) {
struct symlink_inode symlink;

if (addcommon(temp,&s->common,BASICSYMLINK_TYPE_COMMON_INODE)) GOTOERROR;

symlink.link_count=s->common.inode->hardlinkcount;
symlink.target_size=filenamelen;

if (addsymlink(temp,&symlink)) GOTOERROR;
if (add_inode_mkfs(temp,(unsigned char *)filename,filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

struct device_inode {
	uint32_t link_count;
	uint32_t majorminor; // according to format.txt, this is 12:minor_high,12:major,8:minor_low // TODO test this
};

static int adddevice(struct temp_sqfs_mkfs *temp, struct device_inode *device) {
unsigned char buffer[8];
setu32(buffer+0,device->link_count);
setu32(buffer+4,device->majorminor);
if (add_inode_mkfs(temp,buffer,8)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_bdev_inode_mkfs(struct temp_sqfs_mkfs *temp, struct bdev_scan *b, char *filename, unsigned int filenamelen) {
struct device_inode device;
unsigned int mm;

if (addcommon(temp,&b->common,BASICBLOCKDEVICE_TYPE_COMMON_INODE)) GOTOERROR;

device.link_count=b->common.inode->hardlinkcount;
// TODO verify this
mm=(b->minor&0xFFF00)<<12;
mm|=(b->major&0xFFF)<<8;
mm|=b->minor&0xFF;
device.majorminor=mm;

if (adddevice(temp,&device)) GOTOERROR;;
if (add_inode_mkfs(temp,(unsigned char *)filename,filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_cdev_inode_mkfs(struct temp_sqfs_mkfs *temp, struct cdev_scan *c, char *filename, unsigned int filenamelen) {
struct device_inode device;
unsigned int mm;

if (addcommon(temp,&c->common,BASICBLOCKDEVICE_TYPE_COMMON_INODE)) GOTOERROR;

device.link_count=c->common.inode->hardlinkcount;
// TODO verify this
mm=(c->minor&0xFFF00)<<12;
mm|=(c->major&0xFFF)<<8;
mm|=c->minor&0xFF;
device.majorminor=mm;

if (adddevice(temp,&device)) GOTOERROR;;
if (add_inode_mkfs(temp,(unsigned char *)filename,filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

struct header_directory {
	uint32_t countm1; // 0..255 corresponds to 1..256
	uint32_t start; // inode block start
	int32_t	inode_number; // basis for s16 deltas
};

// a new directory header needs to be written every time one or more happen:
// 1. 257th entry
// 2. an entry's inode is in a new inode block 
// 3. an entry's inode number is indescribable with header.inode_number+s16

struct entry_directory {
	uint16_t offset; // inode offset in header.start's block
	int16_t inode_offset; // header.inode_number+this = inode_number
	uint16_t type; // store the basic(inode.common.type) value
	uint16_t name_sizem1; // bytes of following name, minus 1
// follow this with name_sizem1+1 bytes for the name (no null follows)
};

void finish_dirsize_mkfs(struct dirsize_mkfs *dirsize) {
if (dirsize->dirpeers_out) {
	*dirsize->dirpeers_out=256-dirsize->entryfuse;
}
}

int count_dirsize_mkfs(struct dirsize_mkfs *dirsize, struct common_scan *common, unsigned int filenamelen) {
signed short indexdelta;
indexdelta=(signed short)(common->inode->inodeindex-dirsize->inodebasis);
if ( (!dirsize->entryfuse)
		|| ( common->inode->blocksoffset!=dirsize->blocksoffset)
		|| ( common->inode->inodeindex!=dirsize->inodebasis+indexdelta) ) { // we need a new header
	dirsize->size+=12;
	if (dirsize->dirpeers_out) *dirsize->dirpeers_out=256-dirsize->entryfuse;
	dirsize->blocksoffset=common->inode->blocksoffset;
	dirsize->inodebasis=common->inode->inodeindex;
	dirsize->entryfuse=256;
	dirsize->dirpeers_out=&common->dirpeers;

	dirsize->size+=8+filenamelen;
	dirsize->entryfuse-=1;
	return 1;
}
dirsize->size+=8+filenamelen;
dirsize->entryfuse-=1;
return 0;
}

static inline int adddirheader(struct temp_sqfs_mkfs *temp, struct header_directory *hd) {
unsigned char buffer[12];
setu32(buffer+0,hd->countm1);
setu32(buffer+4,hd->start);
setu32(buffer+8,hd->inode_number); // signed
if (add_directory_mkfs(temp,buffer,12)) GOTOERROR;
return 0;
error:
	return -1;
}

static inline int adddirentry(struct temp_sqfs_mkfs *temp, struct entry_directory *e) {
unsigned char buffer[8];
setu16(buffer+0,e->offset);
setu16(buffer+2,e->inode_offset); // signed
setu16(buffer+4,e->type);
setu16(buffer+6,e->name_sizem1);
if (add_directory_mkfs(temp,buffer,8)) GOTOERROR;
return 0;
error:
	return -1;
}

static int add_directory_entry_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct common_scan *common,
		unsigned short type, char *filename, unsigned int filenamelen) { 
struct entry_directory e;
if (count_dirsize_mkfs(dirsize,common,filenamelen)) {
	struct header_directory hd;
	hd.countm1=common->dirpeers -1;
	hd.start=common->inode->blocksoffset;
	hd.inode_number=common->inode->inodeindex;
	if (adddirheader(temp,&hd)) GOTOERROR;
}
e.offset=common->inode->offsetinblock;
e.inode_offset=(signed short)(common->inode->inodeindex-dirsize->inodebasis); // TODO check sign
e.type=type;
e.name_sizem1=filenamelen-1;
if (adddirentry(temp,&e)) GOTOERROR;
if (add_directory_mkfs(temp,(unsigned char *)filename,filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_directory_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de) {
struct directory_scan *d;
d=de->directory;
if (add_directory_entry_mkfs(dirsize,temp,&d->common,BASICDIRECTORY_TYPE_COMMON_INODE,
		de->filename,de->filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_file_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de) {
struct file_scan *f;
f=de->file;
if (add_directory_entry_mkfs(dirsize,temp,&f->common,BASICFILE_TYPE_COMMON_INODE,
		de->filename,de->filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_symlink_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de) {
struct symlink_scan *s;
s=de->symlink;
if (add_directory_entry_mkfs(dirsize,temp,&s->common,BASICSYMLINK_TYPE_COMMON_INODE,
		de->filename,de->filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_bdev_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de) {
struct bdev_scan *b;
b=de->bdev;
if (add_directory_entry_mkfs(dirsize,temp,&b->common,BASICBLOCKDEVICE_TYPE_COMMON_INODE,
		de->filename,de->filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

int add_cdev_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de) {
struct cdev_scan *c;
c=de->cdev;
if (add_directory_entry_mkfs(dirsize,temp,&c->common,BASICCHARDEVICE_TYPE_COMMON_INODE,
		de->filename,de->filenamelen)) GOTOERROR;
return 0;
error:
	return -1;
}

void fill_superblock_sqfs_mkfs(unsigned char *dest, struct superblock_sqfs_mkfs *sb) {
setu32(dest,sb->magic); dest+=4;
setu32(dest,sb->inode_count); dest+=4;
setu32(dest,sb->modification_time); dest+=4;
setu32(dest,sb->block_size); dest+=4;
setu32(dest,sb->fragment_entry_count); dest+=4;
setu16(dest,sb->compression_id); dest+=2;
setu16(dest,sb->block_log); dest+=2;
setu16(dest,sb->flags); dest+=2;
setu16(dest,sb->id_count); dest+=2;
setu16(dest,sb->version_major); dest+=2;
setu16(dest,sb->version_minor); dest+=2;
setu64(dest,sb->root_inode_ref); dest+=8;
setu64(dest,sb->bytes_used); dest+=8;
setu64(dest,sb->id_table_start); dest+=8;
setu64(dest,sb->attr_id_table_start); dest+=8;
setu64(dest,sb->inode_table_start); dest+=8;
setu64(dest,sb->directory_table_start); dest+=8;
setu64(dest,sb->fragment_table_start); dest+=8;
setu64(dest,sb->export_table_start);
}

unsigned int count_metablock_mkfs(struct metablock_mkfs *first) {
unsigned int count=0;
while (first) {
	count+=1;
	first=first->next;
}
return count;
}

static int fixandaddidblocks(unsigned int *index_inout, struct temp_sqfs_mkfs *temp, struct id_scan *idtop) {
unsigned char buff4[4];
if (idtop->treevars.left) {
	if (fixandaddidblocks(index_inout,temp,idtop->treevars.left)) return -1;
}

// fix
idtop->index=*index_inout;
*index_inout+=1;

// add
setu32(buff4,idtop->id);
if (add_idblock_mkfs(temp,buff4,4)) return -1;

if (idtop->treevars.right) {
	if (fixandaddidblocks(index_inout,temp,idtop->treevars.right)) return -1;
}
return 0;
}

int fixandadd_idblocks_mkfs(struct temp_sqfs_mkfs *temp, struct id_scan *idtop) {
unsigned int index=0;
if (fixandaddidblocks(&index,temp,idtop)) GOTOERROR;
return 0;
error:
	return -1;
}

unsigned int size_table_mkfs(struct table_mkfs *table) {
if (table->current->compressedsize) return table->blockoffset+2+table->current->compressedsize;
return table->blockoffset+2+table->blockfill;
}

unsigned int almost_size_metablock_mkfs(struct metablock_mkfs *mb) {
// no need for last size in assemble
if (mb->compressedsize) return mb->compressedsize;
if (mb->next) return SIZE_METABLOCK;
return 0;
}

void copytable_mkfs(unsigned char **dest_inout, unsigned int *bytecount_inout, struct table_mkfs *table) {
struct metablock_mkfs *mb;

mb=table->first;
while (mb) {
	unsigned short us;

	if (mb->compressedsize) {
		us=mb->compressedsize;
		setu16(*dest_inout,us);
	} else {
		if (mb->next) us=SIZE_METABLOCK;
		else us=table->blockfill;
		setu16(*dest_inout,32768|us);
	}


// fprintf(stderr,"%s:%d writing metablock of size %u -> %u\n",__FILE__,__LINE__,us,us|32768);
	*dest_inout+=2;
	*bytecount_inout+=2;
	memcpy(*dest_inout,mb->data,us);
	*dest_inout+=us;
	*bytecount_inout+=us;
	mb=mb->next;
}
}
