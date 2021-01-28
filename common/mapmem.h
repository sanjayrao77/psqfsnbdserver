/*
 * common/mapmem.h
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
struct node_mapmem {
	unsigned int num,max;
	struct node_mapmem *next;
};

struct mapmem {
	struct node_mapmem *current,*first;
};
#define DEFAULTSIZE_MAPMEM	(1<<24)

int init_mapmem(struct mapmem *mapmem, unsigned int size);
void deinit_mapmem(struct mapmem *m);
#define S_MAPMEM(a,b) (b*)alloc_mapmem(a,sizeof(b))
void *alloc_mapmem(struct mapmem *m, unsigned int size);
unsigned char *memdup_mapmem(struct mapmem *m, unsigned char *str, unsigned int len);
#define strdup_mapmem(a,b) strdup3_mapmem(a,b,strlen(b))
char *strdup2_mapmem(struct mapmem *m, unsigned char *data, unsigned int len);
char *strdup3_mapmem(struct mapmem *m, char *str, unsigned int len);
