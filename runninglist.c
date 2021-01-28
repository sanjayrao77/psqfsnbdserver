/*
 * runninglist.c - walk /proc to get a list of running servers
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include "common/conventions.h"

#define ENVBUFFSIZE	32768

#if 0
static void printnamevalue(char *nv) {
char *eq;
eq=strchr(nv,'=');
if (!eq) return;
if (strncmp(nv,"NBD_",4)) return;
fwrite(nv,(unsigned int)(eq-nv),1,stdout);
fprintf(stdout," -> ");
fputs(eq+1,stdout);
fputc('\n',stdout);
}
#endif

static int printenviron(char *path, char *envbuff, char *pidstr) {
FILE *ff=NULL;
char *cur;
char *export=NULL,*clientip=NULL,*tlsmode=NULL,*key=NULL;

if (!(ff=fopen(path,"rb"))) goto error;
if (2>fread(envbuff,1,ENVBUFFSIZE,ff)) goto error;
fclose(ff); ff=NULL;
for (cur=envbuff;*cur;cur+=strlen(cur)+1) {
	if (!strncmp(cur,"NBD_CLIENTIP=",13)) clientip=cur+13;
	else if (!strncmp(cur,"NBD_EXPORTNAME=",15)) export=cur+15;
	else if (!strncmp(cur,"NBD_TLSMODE=",12)) tlsmode=cur+12;
	else if (!strncmp(cur,"NBD_KEY=",8)) key=cur+8;
}
// TODO it would be nice to group exports
if (export) {
	fprintf(stdout,"[%s]",export);
	if (clientip) fprintf(stdout,"\t%s",clientip);
	if (tlsmode) fprintf(stdout,"\ttls=%s",tlsmode);
	if (key) fprintf(stdout,"\tkey=%s",key);
	fprintf(stdout," (%s)\n",pidstr);
}
return 0;
error:
	ignore_iffclose(ff);
	return -1;
}

int print_runninglist(void) {
char onepath[128];
char linkpath[128];
unsigned int pid;
DIR *dir=NULL;
char *envbuff=NULL;
int linkn;

if (!(envbuff=malloc(ENVBUFFSIZE))) GOTOERROR;
pid=getpid();
snprintf(onepath,128,"/proc/%u/exe",pid);
linkn=readlink(onepath,linkpath,128);
if (linkn<0) GOTOERROR;
if (linkn==128) GOTOERROR;

if (!(dir=opendir("/proc"))) GOTOERROR;
while (1) {
	struct dirent *de;
	char dlink[128];
	int r;
	errno=0;
	if (!(de=readdir(dir))) break;
	if (de->d_type!=DT_DIR) continue;
	r=snprintf(onepath,128,"/proc/%s/exe",de->d_name);
	if ((r<0)||(r>128)) continue;
	r=readlink(onepath,dlink,128);
	if (r!=linkn) continue;
	if (memcmp(dlink,linkpath,linkn)) continue;
	r=snprintf(onepath,128,"/proc/%s/environ",de->d_name);
	if ((r<0)||(r>128)) continue;
	(ignore)printenviron(onepath,envbuff,de->d_name);
}
if (errno) GOTOERROR;
closedir(dir);
free(envbuff);
return 0;
error:
	if (dir) closedir(dir);
	iffree(envbuff);
	return -1;
}
