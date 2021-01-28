/*
 * assemble.h
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
struct assemble {
	int isbuilt:1;
	struct {
		struct {
			uint64_t archive,files;
			unsigned int squashfs,padding;
			unsigned int bytessaved;
		} bytecounts;
	} stats;
	struct scan *scan;
	struct temp_sqfs_mkfs *mkfs;
	struct range *range;

	unsigned int blocksize; // default:128k
	unsigned int log_blocksize; // log_2(.blocksize)
};

void voidinit_assemble(struct assemble *a, struct scan *s, struct temp_sqfs_mkfs *m, struct range *r, unsigned int blocksize);
#define deinit_assemble(a) do{}while(0)
int build_assemble(struct assemble *a);
