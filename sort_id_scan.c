/*
 * sort_id_scan.c - btree for sorting ids in scan
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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include "common/conventions.h"
#include "common/mapmem.h"
#include "options.h"
#include "scan.h"

#include "sort_id_scan.h"

#define LEFT(a)	a->treevars.left
#define RIGHT(a)	a->treevars.right
#define BALANCE(a)	a->treevars.balance

static inline int cmp(struct id_scan *a, struct id_scan *b) {
return _FASTCMP(a->id,b->id);
}

struct id_scan *find_sort_id_scan(struct id_scan *root, uint32_t id) {
while (root) {
	if (id<root->id) root=LEFT(root);
	else if (id==root->id) return root;
	else root=RIGHT(root);
}
return NULL;
}

static inline void rebalanceleftleft(struct id_scan **root_inout) {
struct id_scan *a=*root_inout;
struct id_scan *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=0;
BALANCE(a)=0;
}

static inline void rebalancerightright(struct id_scan **root_inout) {
struct id_scan *a=*root_inout;
struct id_scan *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=0;
BALANCE(a)=0;
}

static inline void rebalanceleftright(struct id_scan **root_inout) {
struct id_scan *a=*root_inout;
struct id_scan *left=LEFT(a);
struct id_scan *gchild=RIGHT(left);
int b;
RIGHT(left)=LEFT(gchild);
LEFT(gchild)=left;
LEFT(a)=RIGHT(gchild);
RIGHT(gchild)=a;
*root_inout=gchild;
b=BALANCE(gchild);
if (b>0) {
		BALANCE(a)=-1;
		BALANCE(left)=0;
} else if (!b) {
		BALANCE(a)=BALANCE(left)=0;
} else {
		BALANCE(a)=0;
		BALANCE(left)=1;
}
BALANCE(gchild)=0;
}

static inline void rebalancerightleft(struct id_scan **root_inout) {
struct id_scan *a=*root_inout;
struct id_scan *right=RIGHT(a);
struct id_scan *gchild=LEFT(right);
int b;
LEFT(right)=RIGHT(gchild);
RIGHT(gchild)=right;
RIGHT(a)=LEFT(gchild);
LEFT(gchild)=a;
*root_inout=gchild;
b=BALANCE(gchild);
if (b<0) {
		BALANCE(a)=1;
		BALANCE(right)=0;
} else if (!b) {
		BALANCE(a)=BALANCE(right)=0;
} else {
		BALANCE(a)=0;
		BALANCE(right)=-1;
}
BALANCE(gchild)=0;
}

static int addnode(struct id_scan **root_inout, struct id_scan *node, int (*cmp)(struct id_scan *, struct id_scan *)) {
/* returns 1 if depth increased, else 0 */
struct id_scan *root=*root_inout;
int r=0;

if (!root) {
	*root_inout=node;
	return 1;
}

if (cmp((node),(root))<0) {
	if (addnode(&LEFT(root),node,cmp)) {
		int b;
		b=BALANCE(root);
		if (!b) {
			BALANCE(root)=1; r=1;
		} else if (b>0) {
				if (BALANCE(LEFT(root))>0) (void)rebalanceleftleft(root_inout); else (void)rebalanceleftright(root_inout);
		} else {
			BALANCE(root)=0;
		}
	}
} else {
	if (addnode(&RIGHT(root),node,cmp)) {
		int b;
		b=BALANCE(root);
		if (!b) {
			BALANCE(root)=-1; r=1;
		} else if (b>0) {
			BALANCE(root)=0;
		} else {
				if (BALANCE(RIGHT(root))<0) (void)rebalancerightright(root_inout); else (void)rebalancerightleft(root_inout);
		}
	}
}

return r;
}

void add_sort_id_scan(struct id_scan **root_inout, struct id_scan *node) {
/* node should be 0'd out already (except for data) */
(ignore)addnode(root_inout,node,cmp);
}
