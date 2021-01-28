
/*
 * scan.h
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
#define DIRECTORY_TYPE_SCAN		1
#define FILE_TYPE_SCAN				2
#define SYMLINK_TYPE_SCAN			3
#define BDEV_TYPE_SCAN				4
#define CDEV_TYPE_SCAN				5

struct id_scan {
	uint16_t index;
	uint32_t id;
	struct {
		signed char balance;
		struct id_scan *left,*right;
	} treevars;
};

struct inode_scan { // this is a _real_ inode, from underlying fs
	uint64_t number; // actual inode
	dev_t devnumber;

	uint32_t inodeindex; // for use in new fs, starts at 1

	unsigned int type:3; // _type_scan
	union {
		struct directory_scan *directory;
		struct file_scan *file;
		struct symlink_scan *symlink;
		struct bdev_scan *bdev;
		struct cdev_scan *cdev;
		void *vptr;
	};

	uint64_t dataoffset; // full offset for first data block, less than 96 means undefined

	unsigned int blocksoffset; // offset of metablock in inode table
	unsigned short offsetinblock; // offset of inode within metablock

	uint32_t hardlinkcount;
	struct {
		signed char balance;
		struct inode_scan *left,*right;
	} treevars;
};


struct common_scan {
	uint16_t mode;
	struct id_scan *uid,*gid;
	uint32_t mtime;
	struct inode_scan *inode;

	unsigned int dirpeers; // this many directory entries follow a header (max: 256)
};

struct directory_scan {
	struct common_scan common;
	uint32_t linkcount;

	unsigned int tablesize; // size of directory in directory table
	unsigned int blocksoffset; // offset of directory header in directory table
	unsigned short offsetinblock; // offset of directory header in directory table

	unsigned short namelens;
	int isnotzero:1; // a directory only needs to be stored in range if it has nonzero files

	struct directory_scan *parent;

	struct {
		struct dirent_scan *top;
		struct dirent_scan *first;
	} entries;
};

struct file_scan {
	struct common_scan common;
	uint64_t size; // of underlying data
};

struct symlink_scan {
	struct common_scan common;
	uint64_t size; // of symlink pointer
	char *pointer;
};

struct bdev_scan {
	struct common_scan common;
	unsigned short major,minor;
};

struct cdev_scan {
	struct common_scan common;
	unsigned short major,minor;
};

struct dirent_scan {
	char *filename;
	unsigned int filenamelen;
	unsigned int type:3; // _type_scan
	union {
		struct directory_scan *directory;
		struct file_scan *file;
		struct symlink_scan *symlink;
		struct bdev_scan *bdev;
		struct cdev_scan *cdev;
		struct common_scan *common;
	};
	char *overlay;
//	struct directory_scan *parent;
	struct {
		signed char balance;
		struct dirent_scan *left,*right;
	} treevars;
	struct dirent_scan *next;
};

struct scan {
	struct mapmem mapmem;
	struct {
		unsigned int maxfiles;
	} config;
	struct {
		unsigned int files,non0files;
		unsigned int subdirs;
		unsigned int namelens; // lens for files and subdirs that are (isnonzero)
		unsigned int maxdepth;
		unsigned int inodes; // note .counts.inodes vs .inodes.count, this is first count, before assignment
	} counts;
	struct {
		char *path;
		struct directory_scan directory;
	} rootdir;
	struct {
		struct id_scan *top;
		uint32_t count;
	} ids;
	struct {
		struct inode_scan *top;
		uint32_t count;
	} inodes;
};

int init_scan(struct scan *scan, unsigned int mapsize, unsigned int maxfiles);
void deinit_scan(struct scan *scan);
int setrootdir_scan(struct scan *scan, char *dirname, struct options *options);
int setnorootdir_scan(struct scan *scan, struct options *options);
void setlinearvars_scan(struct scan *scan, struct directory_scan *directory);
int applyoverlay_scan(struct scan *scan, char *realpath_in, char *fakepath_in, int israw, struct options *options);
