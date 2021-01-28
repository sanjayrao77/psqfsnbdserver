/*
 * common/overwrite_environ.c - hack to show server status
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
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "overwrite_environ.h"

#ifndef __environ
extern char **environ;
#define __environ environ
#endif

void voidinit_overwrite_environ(struct overwrite_environ *oe) {
char * highest=0,*lowest=(char *)-1;
char **strp;

if (__environ) for (strp=__environ;*strp;strp++) {
	if (*strp>highest) highest=*strp;
	if (*strp<lowest) lowest=*strp;
}
if (!highest) return;
highest+=strlen(highest)+1;
oe->start=oe->cursor=lowest;
oe->totalsize=(unsigned int)(highest-lowest);
memset(lowest,0,oe->totalsize);
oe->bytesleft=oe->totalsize-1; // -1: reserve one \0 at the end
}

int setenv_overwrite_environ(struct overwrite_environ *oe, char *name, char *value) {
unsigned int nlen,vlen,tlen;
char *cur;
nlen=strlen(name);
vlen=strlen(value);
tlen=nlen+vlen+2;
if (oe->bytesleft<tlen) return -1;
oe->bytesleft-=tlen;
cur=oe->cursor;
memcpy(cur,name,nlen);
cur+=nlen;
*cur='=';
cur+=1;
memcpy(cur,value,vlen);
cur+=vlen;
*cur='\0';
cur+=1;
oe->cursor=cur;
return 0;
}

int setenv2_overwrite_environ(struct overwrite_environ *oe, char *namevalue) {
unsigned int tlen;
tlen=strlen(namevalue)+1;
if (oe->bytesleft<tlen) return -1;
oe->bytesleft-=tlen;
memcpy(oe->cursor,namevalue,tlen);
oe->cursor+=tlen;
return 0;
}
