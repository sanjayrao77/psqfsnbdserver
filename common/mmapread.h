/*
 * common/mmapread.h
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
struct mmapread {
	uint64_t filesize;
	uint64_t datasize; // may not be full size on 32bit or on filesystems without mmap
	unsigned char *data;
	struct {
		void *ptr_mmap;
		void *ptr_malloc;
		uint64_t addrsize,offset;
		int fd;
		unsigned int mallocsize;
	} cleanup;
};
#define overclear_mmapread(a) do { (a)->cleanup.fd=-1; } while (0)
void clear_mmapread(struct mmapread *s);
void voidinit_mmapread(struct mmapread *s, int mallocsize);
void deinit_mmapread(struct mmapread *s);
void reset_mmapread(struct mmapread *s);
int readoff_mmapread(struct mmapread *s, int fd, uint64_t offset, int fdcleanup);
int isoffsetchanged_mmapread(struct mmapread *s, uint64_t offset);
