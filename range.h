
/*
 * range.h
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

struct directory_range {
	char *filename;
	struct directory_range *parent;
};

#define EXTERNAL_TYPE_RANGE 0
#define INTERNAL_TYPE_RANGE	1
#define FD_TYPE_RANGE				2
struct entry_range {
	uint64_t start,startpluslen;
	unsigned int type:2;
	union {
		struct { unsigned char *data; unsigned int len; } internal;
		struct { struct directory_range *directory; char *filename; } external;
		struct { int fd; char *filename; } fd; // filename is for debugging, size is .startpluslen-.start
	};
};

struct match_range {
	int iserror:1;
	unsigned char *data;
	unsigned int len;
	struct mmapread mmapread;
};

struct range {
	struct {
		unsigned int num,max;
		struct entry_range *list;
		uint64_t nextstart;
	} entries;
	struct {
		unsigned int num,max;
		struct directory_range *list;
	} directories;
	struct {
		unsigned int num,max;
		unsigned char *data; // this can include superblock, includes file names and dir names, no need for symlinks
	} names;
	struct {
		unsigned int maxdepth_unwinddirs;
		struct directory_range **unwinddirs;
	} temp;
	struct {
		struct entry_range *entry;
		struct match_range match;
	} cache;
	struct {
		unsigned char *other; // this should be freed, use for sqfs tables
	} extra;
};

#define overclear_range(a) do { overclear_mmapread(&(a)->cache.match.mmapread); } while (0)
int init_range(struct range *range, unsigned int maxentries, unsigned int maxdirs, unsigned int maxnames, unsigned int maxdepth);
void deinit_range(struct range *range);
void reset_range(struct range *range);
int add_internal_range(struct range *range, unsigned char *data, unsigned int len);
struct directory_range *add_directory_range(struct range *range, struct directory_range *parent, char *name, unsigned int namelen);
int add_external_range(struct range *range, struct directory_range *directory, char *filename_in, unsigned int namelen, uint64_t len);
int noalloc_add_fd_range(struct range *range, int fd, char *filename, uint64_t len);
int noalloc_add_external_range(struct range *range, struct directory_range *directory, char *filename, uint64_t len);
unsigned char *alloc_name_range(struct range *range, unsigned int len);
int dump_range(struct range *range, char *filename);
struct match_range *finddata_range(struct range *range, uint64_t offset, struct options *options);
#define voidinit_match_range(a,b) do { voidinit_mmapread(&(a)->mmapread,b); } while (0)
#define reset_match_range(a) do { (a)->iserror=0; (void)reset_mmapread(&((a)->mmapread)); } while (0)
#define deinit_match_range(a) deinit_mmapread(&((a)->mmapread))
