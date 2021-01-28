/*
 * mkfs.h
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
#if 0
#define UNCOMPRESSED_INODES_FLAG_SQFS_MKFS 0x0001 
#define UNCOMPRESSED_DATA_FLAG_SQFS_MKFS 0x0002
#define UNCOMPRESSED_FRAGMENTS_FLAG_SQFS_MKFS 0x0008
#define NO_FRAGMENTS_FLAG_SQFS_MKFS 0x0010
#define UNCOMPRESSED_XATTRS_FLAG_SQFS_MKFS 0x0100
#define NO_XATTRS_FLAG_SQFS_MKFS 0x0200
#define UNCOMPRESSED_IDS_FLAG_SQFS_MKFS 0x0800
#endif

#define DEFAULT_FLAGS_SQFS_MKFS (0x0001|0x0002|0x0008|0x0010|0x0100|0x0200|0x0800)
struct superblock_sqfs_mkfs {
	uint32_t magic;
	uint32_t inode_count;
	uint32_t modification_time;
	uint32_t block_size;
	uint32_t fragment_entry_count;
	uint16_t compression_id;
	uint16_t block_log;
	uint16_t flags;
	uint16_t id_count;
	uint16_t version_major;
	uint16_t version_minor;
	uint64_t root_inode_ref;
	uint64_t bytes_used;
	uint64_t id_table_start;
	uint64_t attr_id_table_start;
	uint64_t inode_table_start;
	uint64_t directory_table_start;
	uint64_t fragment_table_start;
	uint64_t export_table_start;
};
#define NUM_SUPERBLOCK_SQFS_MKFS	96

struct metablock_mkfs {
	unsigned short compressedsize; // 0 => not compressed
	unsigned char *data;
	struct metablock_mkfs *next;
};

struct table_mkfs { // total size will be .blockoffset+2+.blockfill
	unsigned int blockoffset; // increases by 2+8192 every new block
	unsigned int blockfill; // offset within current 8k block
	unsigned int blockleft; // bytes left in current block
	struct metablock_mkfs *first,*current;
};

struct temp_sqfs_mkfs {
	struct mapmem *mm;
	struct {
		unsigned int blocksize; // might as well use the highest value supported
	} config;
	struct {
		unsigned int bytessaved; // via compression
	} stats;
	struct table_mkfs inode_table;
	struct {
		signed int inodebasis; // can't deviate from basis by more than s16
		unsigned int entryfuse; // max of 256 entries per header
	} directory;
	struct table_mkfs directory_table;
	struct table_mkfs idblock_table;
	struct {
		unsigned int listsize; // number of bytes in list, =((scan.ids.count+1023)/1024)*8
	} idblocklist;
	struct {
		unsigned char *spareblock; // spare 8k for compressing
		struct z_stream_s zstream;
	} compress;
};

// used for pre-counting directory sizes
struct dirsize_mkfs {
	unsigned int size;

	unsigned int blocksoffset;
	unsigned int inodebasis;
	unsigned int entryfuse; // max is 256
	unsigned int *dirpeers_out; // save the count of entries here
};

int init_temp_sqfs_mkfs(struct temp_sqfs_mkfs *s, struct mapmem *mm, unsigned int blocksize, unsigned int gziplevel);
#if 1
#define deinit_temp_sqfs_mkfs(a) do{}while(0)
#else
void deinit_temp_sqfs_mkfs(struct temp_sqfs_mkfs *s);
#endif
int add_file_inode_mkfs(struct temp_sqfs_mkfs *temp, struct file_scan *f);
int add_directory_inode_mkfs(struct temp_sqfs_mkfs *temp, struct directory_scan *d, struct directory_scan *parent);
int add_symlink_inode_mkfs(struct temp_sqfs_mkfs *temp, struct symlink_scan *s, char *filename, unsigned int filenamelen);
int add_bdev_inode_mkfs(struct temp_sqfs_mkfs *temp, struct bdev_scan *b, char *filename, unsigned int filenamelen);
int add_cdev_inode_mkfs(struct temp_sqfs_mkfs *temp, struct cdev_scan *c, char *filename, unsigned int filenamelen);
void finish_dirsize_mkfs(struct dirsize_mkfs *dirsize);
int count_dirsize_mkfs(struct dirsize_mkfs *dirsize, struct common_scan *common, unsigned int filenamelen);
int add_directory_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de);
int add_file_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de);
int add_symlink_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de);
int add_bdev_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de);
int add_cdev_dirent_mkfs(struct dirsize_mkfs *dirsize, struct temp_sqfs_mkfs *temp, struct dirent_scan *de);
void fill_superblock_sqfs_mkfs(unsigned char *dest, struct superblock_sqfs_mkfs *sb);
unsigned int count_metablock_mkfs(struct metablock_mkfs *first);
int fixandadd_idblocks_mkfs(struct temp_sqfs_mkfs *temp, struct id_scan *idtop);
unsigned int size_table_mkfs(struct table_mkfs *table);
unsigned int almost_size_metablock_mkfs(struct metablock_mkfs *mb);
void copytable_mkfs(unsigned char **dest_inout, unsigned int *bytecount_inout, struct table_mkfs *table);
int compresstables_mkfs(struct temp_sqfs_mkfs *temp);
