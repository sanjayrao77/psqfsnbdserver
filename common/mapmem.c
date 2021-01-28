/*
 * common/mapmem.c - malloc alternative, lets us unmap memory explicitly when done
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
// #define DEBUG2
#include "conventions.h"

#include "mapmem.h"

#ifdef MAP_UNINITIALIZED
#define FLAGS	(MAP_PRIVATE|MAP_ANONYMOUS|MAP_UNINITIALIZED)
#else
#define FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)
#endif

static struct node_mapmem *makenode(unsigned int size) {
struct node_mapmem *ptr;
if (MAP_FAILED==(ptr=mmap(NULL,size,PROT_READ|PROT_WRITE,FLAGS,-1,0))) GOTOERROR;
ptr->num=sizeof(struct node_mapmem);
ptr->max=size;
ptr->next=NULL;
#ifdef DEBUG2
	fprintf(stderr,"%s:%d mmaping %u bytes\n",__FILE__,__LINE__,size);
#endif
return ptr;
error:
	return NULL;
}

int init_mapmem(struct mapmem *m, unsigned int size) {
if (size<sizeof(struct node_mapmem)) size=DEFAULTSIZE_MAPMEM;
if (!(m->current=m->first=makenode(size))) GOTOERROR;
return 0;
error:
	return -1;
}

void deinit_mapmem(struct mapmem *m) {
struct node_mapmem *cur,*next;
cur=m->current;
while (cur) {
	next=cur->next;
#ifdef DEBUG2
	fprintf(stderr,"%s:%d unmapping %u bytes\n",__FILE__,__LINE__,cur->max);
#endif
	(ignore)munmap(cur,cur->max);
	if (!next) break;
	cur=next;
}
}

void *alloc_mapmem(struct mapmem *m, unsigned int size) {
struct node_mapmem *node;
unsigned int max;
void *ret;
node=m->current;
if (!node) return NULL;
if (node->num+size<=node->max) {
	ret=(void *)node+node->num;
	node->num+=size;
	return ret;
}
max=m->first->max;
while (max<size) max+=m->first->max;
if (!(node=makenode(max))) GOTOERROR;
m->current->next=node;
m->current=node;
ret=(void *)node+node->num;
node->num+=size;
return ret;
error:
	return NULL;
}

unsigned char *memdup_mapmem(struct mapmem *m, unsigned char *str, unsigned int len) {
unsigned char *clone;
if (!(clone=alloc_mapmem(m,len))) GOTOERROR;
memcpy(clone,str,len);
return clone;
error:
	return NULL;
}

char *strdup3_mapmem(struct mapmem *m, char *str, unsigned int len) {
char *clone;
len+=1;
if (!(clone=alloc_mapmem(m,len))) GOTOERROR;
memcpy(clone,str,len);
return clone;
error:
	return NULL;
}

char *strdup2_mapmem(struct mapmem *m, unsigned char *data, unsigned int len) {
char *str;
if (!(str=alloc_mapmem(m,len+1))) GOTOERROR;
memcpy(str,data,len);
data[len]='\0';
return str;
error:
	return NULL;
}
