/*
 * scan.c - read through the local fs to collect file and subdir info
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
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <time.h>
#include <syslog.h>
#define __USE_GNU
// GNU is for O_PATH
#include <fcntl.h>
#undef __USE_GNU
#include <errno.h>
// #define DEBUG2
#include "common/conventions.h"
#include "common/mapmem.h"
#include "options.h"
#include "misc.h"

#include "scan.h"

#include "sort_id_scan.h"
#include "sort_inode_scan.h"
#include "sort_dirent_scan.h"

// #define OPENDIRFLAGS (O_RDONLY|O_DIRECTORY|O_NOATIME)
#define OPENDIRFLAGS (O_RDONLY|O_DIRECTORY)
#define MODEMASK	(S_IRWXU|S_IRWXG|S_IRWXO|S_ISUID|S_ISGID|S_ISVTX)

SICLEARFUNC(directory_scan);
#if 1
// TODO check to make sure these aren't needed
SICLEARFUNC(id_scan);
SICLEARFUNC(inode_scan);
SICLEARFUNC(file_scan);
SICLEARFUNC(symlink_scan);
SICLEARFUNC(bdev_scan);
SICLEARFUNC(cdev_scan);
SICLEARFUNC(dirent_scan);
#else
#define clear_inode_scan(a)	do {} while (0)
#define clear_id_scan(a) do {} while (0)
#define clear_file_scan(a) do {} while (0)
#define clear_symlink_scan(a) do {} while (0)
#define clear_bdev_scan(a) do {} while (0)
#define clear_cdev_scan(a) do {} while (0)
#define clear_dirent_scan(a) do {} while (0)
#endif

static int register_id_scan(struct id_scan **id_out, struct scan *scan, uint32_t id_in) {
struct id_scan *id;
id=find_sort_id_scan(scan->ids.top,id_in);
if (id) {
	*id_out=id;
	return 0;
}
if (!(id=S_MAPMEM(&scan->mapmem,struct id_scan))) GOTOERROR;
clear_id_scan(id);
scan->ids.count+=1;
// id->index=0; // set fix this at assemble
id->id=id_in;
id->treevars.balance=0;
id->treevars.left=id->treevars.right=NULL;
(void)add_sort_id_scan(&scan->ids.top,id);
*id_out=id;
return 0;
error:
	return -1;
}

static inline struct inode_scan *pseudo_inode(struct scan *scan, unsigned int type, void *vptr) {
struct inode_scan *inode;
scan->counts.inodes+=1;
if (!(inode=S_MAPMEM(&scan->mapmem,struct inode_scan))) return NULL;
clear_inode_scan(inode);
inode->type=type;
inode->vptr=vptr;
inode->hardlinkcount=1;
return inode;
}

static struct inode_scan *new_inode(struct scan *scan, struct stat *st, unsigned int type, void *vptr) {
struct inode_scan *inode;
if (!(inode=pseudo_inode(scan,type,vptr))) GOTOERROR;
inode->number=st->st_ino;
inode->devnumber=st->st_dev;
(void)add_sort_inode_scan(&scan->inodes.top,inode);
return inode;
error:
	return NULL;
}

static int addcdev_directory_scan(struct directory_scan *directory, struct scan *scan, struct stat *st, char *filename) {
struct dirent_scan *de;
struct cdev_scan *c;
struct inode_scan *inode;

directory->linkcount+=1;
scan->counts.files+=1;
if (scan->counts.files==scan->config.maxfiles) GOTOERROR;

inode=find_sort_inode_scan(scan->inodes.top,st->st_ino,st->st_dev);
if (inode) {
	if (inode->type!=CDEV_TYPE_SCAN) GOTOERROR;
	inode->hardlinkcount+=1;
	c=inode->cdev;
} else {
	if (!(c=S_MAPMEM(&scan->mapmem,struct cdev_scan))) GOTOERROR;
	clear_cdev_scan(c);

	c->common.mode=st->st_mode&MODEMASK;
	if (register_id_scan(&c->common.uid,scan,st->st_uid)) GOTOERROR;
	if (register_id_scan(&c->common.gid,scan,st->st_gid)) GOTOERROR;
	c->common.mtime=st->st_mtim.tv_sec;
	c->major=major(st->st_rdev);
	c->minor=minor(st->st_rdev);

	if (!(inode=new_inode(scan,st,CDEV_TYPE_SCAN,(void *)c))) GOTOERROR;

	c->common.inode=inode;
}

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=strlen(filename);
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,de->filenamelen))) GOTOERROR;
de->type=CDEV_TYPE_SCAN;
de->cdev=c;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&directory->entries.top,de);

// scan->counts.namelens+=de->filenamelen; // devices aren't stored in range
return 0;
error:
	return -1;
}

static int addbdev_directory_scan(struct directory_scan *directory, struct scan *scan, struct stat *st, char *filename) {
struct dirent_scan *de;
struct bdev_scan *b;
struct inode_scan *inode;

directory->linkcount+=1;
scan->counts.files+=1;
if (scan->counts.files==scan->config.maxfiles) GOTOERROR;

inode=find_sort_inode_scan(scan->inodes.top,st->st_ino,st->st_dev);
if (inode) {
	if (inode->type!=BDEV_TYPE_SCAN) GOTOERROR;
	inode->hardlinkcount+=1;
	b=inode->bdev;
} else {
	if (!(b=S_MAPMEM(&scan->mapmem,struct bdev_scan))) GOTOERROR;
	clear_bdev_scan(b);

	b->common.mode=st->st_mode&MODEMASK;
	if (register_id_scan(&b->common.uid,scan,st->st_uid)) GOTOERROR;
	if (register_id_scan(&b->common.gid,scan,st->st_gid)) GOTOERROR;
	b->common.mtime=st->st_mtim.tv_sec;
	b->major=major(st->st_rdev);
	b->minor=minor(st->st_rdev);

	if (!(inode=new_inode(scan,st,BDEV_TYPE_SCAN,b))) GOTOERROR;

	b->common.inode=inode;
}

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=strlen(filename);
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,de->filenamelen))) GOTOERROR;
de->type=BDEV_TYPE_SCAN;
de->bdev=b;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&directory->entries.top,de);

// scan->counts.namelens+=de->filenamelen; // devices aren't stored in range
return 0;
error:
	return -1;
}
static int addsymlink_directory_scan(struct directory_scan *directory, struct scan *scan, DIR *dir, char *filename, char *fullfilename) {
struct dirent_scan *de;
struct symlink_scan *l;
struct inode_scan *inode;
struct stat st;
int fd=-1;

if (dir) {
	if (0>(fd=openat(dirfd(dir),filename,O_RDONLY|O_PATH|O_NOFOLLOW))) GOTOERROR;
} else {
	if (0>(fd=open(fullfilename,O_RDONLY|O_PATH|O_NOFOLLOW))) GOTOERROR;
}
if (fstat(fd,&st)) GOTOERROR;

directory->linkcount+=1;
scan->counts.files+=1;
if (scan->counts.files==scan->config.maxfiles) GOTOERROR;

inode=find_sort_inode_scan(scan->inodes.top,st.st_ino,st.st_dev);
if (inode) {
	if (inode->type!=SYMLINK_TYPE_SCAN) GOTOERROR;
	inode->hardlinkcount+=1;
	l=inode->symlink;
} else {
	if (!(l=S_MAPMEM(&scan->mapmem,struct symlink_scan))) GOTOERROR;
	clear_symlink_scan(l);

	l->common.mode=st.st_mode&MODEMASK;
	if (register_id_scan(&l->common.uid,scan,st.st_uid)) GOTOERROR;
	if (register_id_scan(&l->common.gid,scan,st.st_gid)) GOTOERROR;
	l->common.mtime=st.st_mtim.tv_sec;
	l->size=st.st_size;
	if (!(l->pointer=alloc_mapmem(&scan->mapmem,l->size+1))) GOTOERROR;
	l->pointer[l->size]='\0';
	if (0>readlinkat(fd,"",l->pointer,l->size)) GOTOERROR;

	if (!(inode=new_inode(scan,&st,SYMLINK_TYPE_SCAN,l))) GOTOERROR;

	l->common.inode=inode;
}

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=strlen(filename);
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,de->filenamelen))) GOTOERROR;
de->type=SYMLINK_TYPE_SCAN;
de->symlink=l;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&directory->entries.top,de);

// scan->counts.namelens+=de->filenamelen; // symlinks aren't stored in range
(ignore)close(fd);
return 0;
error:
	if (fd!=-1) close(fd);
	return -1;
}

static void markisnotzero(struct scan *scan, struct directory_scan *d) {
do {
	if (d->isnotzero) return;
	d->isnotzero=1;
	scan->counts.namelens+=d->namelens;
	d=d->parent;
} while (d);
}

static int addfile_directory_scan(struct directory_scan *directory, struct scan *scan, struct stat *st,
		char *filename, char *overlay) {
struct dirent_scan *de;
struct file_scan *f;
struct inode_scan *inode;

directory->linkcount+=1;
scan->counts.files+=1;
if (scan->counts.files==scan->config.maxfiles) GOTOERROR;

inode=find_sort_inode_scan(scan->inodes.top,st->st_ino,st->st_dev);
if (inode) {
	if (inode->type!=FILE_TYPE_SCAN) GOTOERROR;
	inode->hardlinkcount+=1;
	f=inode->file;
} else {
	if (!(f=S_MAPMEM(&scan->mapmem,struct file_scan))) GOTOERROR;
	clear_file_scan(f);
	f->common.mode=st->st_mode&MODEMASK;
	if (register_id_scan(&f->common.uid,scan,st->st_uid)) GOTOERROR;
	if (register_id_scan(&f->common.gid,scan,st->st_gid)) GOTOERROR;
	f->common.mtime=st->st_mtim.tv_sec;
	f->size=st->st_size;

	if (f->size && (!overlay)) {
		(void)markisnotzero(scan,directory);
		scan->counts.non0files+=1;
	}

	if (!(inode=new_inode(scan,st,FILE_TYPE_SCAN,f))) GOTOERROR;

	f->common.inode=inode;
}

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=strlen(filename);
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,de->filenamelen))) GOTOERROR;
de->type=FILE_TYPE_SCAN;
de->file=f;
de->overlay=overlay;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&directory->entries.top,de);

// this is for range
if (f->size) {
	if (overlay) scan->counts.namelens+=strlen(overlay)+1;
	else scan->counts.namelens+=de->filenamelen+1;
}
return 0;
error:
	return -1;
}

static int adddirectory_directory_scan(struct directory_scan **d_out, struct directory_scan *directory,
		struct scan *scan, struct stat *st, char *filename, char *overlay) {
struct dirent_scan *de;
struct directory_scan *d;
struct inode_scan *inode;

inode=find_sort_inode_scan(scan->inodes.top,st->st_ino,st->st_dev);
if (inode) GOTOERROR; // this happens if we have a directory loop

directory->linkcount+=1;
scan->counts.subdirs+=1;

if (!(d=S_MAPMEM(&scan->mapmem,struct directory_scan))) GOTOERROR;
clear_directory_scan(d);
// the rest of the common vars will be set later, when the dir is opened
d->linkcount=2;

d->parent=directory;

d->common.mode=st->st_mode&MODEMASK;
if (register_id_scan(&d->common.uid,scan,st->st_uid)) GOTOERROR;
if (register_id_scan(&d->common.gid,scan,st->st_gid)) GOTOERROR;
d->common.mtime=st->st_mtim.tv_sec;

if (!(inode=new_inode(scan,st,DIRECTORY_TYPE_SCAN,d))) GOTOERROR;

d->common.inode=inode;

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=strlen(filename);
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,de->filenamelen))) GOTOERROR;
de->type=DIRECTORY_TYPE_SCAN;
de->directory=d;
de->overlay=overlay;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&directory->entries.top,de);

// this is for range, this space will be unused if it ends up being !.isnotzero
d->namelens=de->filenamelen+1;
if (overlay) d->namelens+=strlen(overlay)+1;
// scan->counts.namelens+=de->filenamelen+1;

*d_out=d;
return 0;
error:
	return -1;
}

static int add_dir(unsigned int *curdepth_inout, struct scan *scan, DIR *dir, struct directory_scan *directory,
		struct options *options);

static int addsubdir(unsigned int *curdepth_inout, struct scan *scan, DIR *parentdir, char *filename, struct directory_scan *d,
		struct options *options) {
int fd=-1;
DIR *dir2=NULL;
if (parentdir) {
	if (0>(fd=openat(dirfd(parentdir),filename,OPENDIRFLAGS))) GOTOERROR;
} else {
	if (0>(fd=open(filename,OPENDIRFLAGS))) GOTOERROR;
}
if (!(dir2=fdopendir(fd))) GOTOERROR;
fd=-1;
if (add_dir(curdepth_inout,scan,dir2,d,options)) GOTOERROR;
(ignore)closedir(dir2);
return 0;
error:
	if (dir2) closedir(dir2);
	ignore_ifclose(fd);
	return -1;
}

#if 0
static int addsubdirs(unsigned int *curdepth_inout, struct scan *scan, DIR *dir, struct directory_scan *directory,
		struct options *options) {
struct dirent_scan *de;
DIR *dir2=NULL;
int fd=-1;

de=directory->entries.first;
while (de) {
	if (de->type==DIRECTORY_TYPE_SCAN) {
		struct directory_scan *d;
		d=de->directory;
#ifdef DEBUG2
	fprintf(stderr,"Entering directory %s\n",de->filename);
#endif
		fd=openat(dirfd(dir),de->filename,OPENDIRFLAGS);
		if (fd<0) GOTOERROR;
		if (!(dir2=fdopendir(fd))) GOTOERROR; fd=-1;
		if (add_dir(curdepth_inout,scan,dir2,d,options)) GOTOERROR;
		(ignore)closedir(dir2); dir2=NULL;
	}
	de=de->next;
}
return 0;
error:
	if (dir2) closedir(dir2);
	if (fd!=-1) close(fd);
	return -1;
}
#endif

static void setlinearvars(struct scan *scan, struct dirent_scan ***deptr_inout, struct dirent_scan *de) {
if (de->treevars.left) (void)setlinearvars(scan,deptr_inout,de->treevars.left);
	// we want consecutive inodeindex values
	if (!de->common->inode->inodeindex) {
		scan->inodes.count+=1;
		de->common->inode->inodeindex=scan->inodes.count;
	}
	**deptr_inout=de;
	*deptr_inout=&de->next;
if (de->treevars.right) (void)setlinearvars(scan,deptr_inout,de->treevars.right);
}

void setlinearvars_scan(struct scan *scan, struct directory_scan *directory) {
struct dirent_scan **deptr;
if (!directory->entries.top) return;
deptr=&directory->entries.first;
(void)setlinearvars(scan,&deptr,directory->entries.top);
}

static int add_dir(unsigned int *curdepth_inout, struct scan *scan, DIR *dir, struct directory_scan *directory,
		struct options *options) {
struct dirent *dirent;
int fd;

*curdepth_inout+=1;
if (*curdepth_inout > scan->counts.maxdepth) scan->counts.maxdepth=*curdepth_inout;

fd=dirfd(dir);

while (1) {
	struct stat st;

	errno=0;
	dirent=readdir(dir);
	if (!dirent) break;

	switch (dirent->d_type) {
		case DT_REG:
			if (fstatat(fd,dirent->d_name,&st,0)) {
				syslog(LOG_ERR,"stat error: %s %s",dirent->d_name,strerror(errno));
				GOTOERROR;
			}
			if (addfile_directory_scan(directory,scan,&st,dirent->d_name,NULL)) GOTOERROR;
			break;
		case DT_DIR:
			if (dirent->d_name[0]=='.') {
					if (!dirent->d_name[1]) continue;
					if ((dirent->d_name[1]=='.')&&(!dirent->d_name[2])) continue;
			}
			if (fstatat(fd,dirent->d_name,&st,0)) GOTOERROR;
			{
				struct directory_scan *d;
				if (adddirectory_directory_scan(&d,directory,scan,&st,dirent->d_name,NULL)) GOTOERROR;
				if (addsubdir(curdepth_inout,scan,dir,dirent->d_name,d,options)) GOTOERROR;
			}
			break;
		case DT_LNK: 
			if (addsymlink_directory_scan(directory,scan,dir,dirent->d_name,NULL)) GOTOERROR;
			break;
		case DT_BLK:
			if (fstatat(fd,dirent->d_name,&st,0)) GOTOERROR;
			if (addbdev_directory_scan(directory,scan,&st,dirent->d_name)) GOTOERROR;
			break;
		case DT_CHR:
			if (fstatat(fd,dirent->d_name,&st,0)) GOTOERROR;
			if (addcdev_directory_scan(directory,scan,&st,dirent->d_name)) GOTOERROR;
			break;
		case DT_FIFO:
			if (options->isdebug) {
				syslog(LOG_DEBUG,"Ignoring fifo: %s",dirent->d_name);
			}
			break;
		case DT_SOCK:
			if (options->isdebug) {
				syslog(LOG_DEBUG,"Ignoring socket: %s",dirent->d_name);
			}
			break;
		default:
			if (options->isdebug) {
				syslog(LOG_DEBUG,"Ignoring unknown entry: %s (unknown type: %d)",dirent->d_name,dirent->d_type);
			}
			break;
	}
}
if (errno) GOTOERROR;
return 0;
error:
	return -1;
}

static int fillfakedir(struct scan *scan, struct directory_scan *d, struct directory_scan *parent) {
struct inode_scan *inode;
d->linkcount=2;
d->parent=parent;
d->common.mode=S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
if (register_id_scan(&d->common.uid,scan,0)) GOTOERROR;
if (register_id_scan(&d->common.gid,scan,0)) GOTOERROR;
d->common.mtime=time(NULL);

if (!(inode=pseudo_inode(scan,DIRECTORY_TYPE_SCAN,d))) GOTOERROR;
d->common.inode=inode;
return 0;
error:
	return -1;
}

int setnorootdir_scan(struct scan *scan, struct options *options) {
struct directory_scan *d;

scan->rootdir.path="";

d=&scan->rootdir.directory;
if (fillfakedir(scan,d,NULL)) GOTOERROR;
d->namelens=1; // we may need this if overlay is added (probably)

return 0;
error:
	return -1;
}

int setrootdir_scan(struct scan *scan, char *dirname, struct options *options) {
DIR *dir=NULL;
struct stat st;
int fd=-1;
struct directory_scan *d;
struct inode_scan *inode;
unsigned int curdepth=0;

if (!*dirname) return setnorootdir_scan(scan,options);

if (0>(fd=open(dirname,OPENDIRFLAGS))) GOTOERROR;
if (fstat(fd,&st)) GOTOERROR;
if (!(dir=fdopendir(fd))) GOTOERROR;
fd=-1;
scan->rootdir.path=dirname;
scan->counts.namelens+=strlen(dirname)+1;

d=&scan->rootdir.directory;

d->linkcount=2;
// d->parent=NULL; // already set

d->common.mode=st.st_mode&MODEMASK;
if (register_id_scan(&d->common.uid,scan,st.st_uid)) GOTOERROR;
if (register_id_scan(&d->common.gid,scan,st.st_gid)) GOTOERROR;
d->common.mtime=st.st_mtim.tv_sec;

if (!(inode=pseudo_inode(scan,DIRECTORY_TYPE_SCAN,d))) GOTOERROR;
d->common.inode=inode;

if (add_dir(&curdepth,scan,dir,&scan->rootdir.directory,options)) GOTOERROR;

(ignore)closedir(dir);
return 0;
error:
	if (fd<0) close(fd);
	if (dir) closedir(dir);
	return -1;
}

#if 0
int finalize_scan(struct scan *scan) {
return 0;
}
#endif

int init_scan(struct scan *scan, unsigned int mapsize, unsigned int maxfiles) {
if (init_mapmem(&scan->mapmem,mapsize)) GOTOERROR;
scan->rootdir.directory.linkcount=1; // TODO should this be 1 or 2?
scan->config.maxfiles=maxfiles;
return 0;
error:
	return -1;
}

void deinit_scan(struct scan *scan) {
deinit_mapmem(&scan->mapmem);
}

static struct directory_scan *add_directory(struct scan *scan, struct directory_scan *parent, char *filename) {
struct directory_scan *d;
struct dirent_scan *de;
unsigned int namelen;

namelen=strlen(filename);

if (!(d=S_MAPMEM(&scan->mapmem,struct directory_scan))) GOTOERROR;
clear_directory_scan(d);
if (fillfakedir(scan,d,parent)) GOTOERROR;
parent->linkcount+=1;
scan->counts.subdirs+=1;

if (!(de=S_MAPMEM(&scan->mapmem,struct dirent_scan))) GOTOERROR;
clear_dirent_scan(de);
de->filenamelen=namelen;
if (!(de->filename=strdup3_mapmem(&scan->mapmem,filename,namelen))) GOTOERROR;
de->type=DIRECTORY_TYPE_SCAN;
de->directory=d;
de->treevars.balance=0;
de->treevars.left= de->treevars.right= de->next= NULL;
(void)add_sort_dirent_scan(&parent->entries.top,de);
d->namelens=namelen+1;
return d;
error:
	return NULL;
}

static int find_directory(struct directory_scan **d_out, struct directory_scan *parent, char *name) {
struct dirent_scan *de;
de=find_dirent_scan(parent->entries.top,name);
if (!de) { *d_out=NULL; return 0; }
if (de->type!=DIRECTORY_TYPE_SCAN) return -1;
*d_out=de->directory;
return 0;
}

static struct directory_scan *findmake_directory(struct scan *scan, char *dirpath) {
struct directory_scan *d;
d=&scan->rootdir.directory;
if (!dirpath) return d;
while (1) {
	char *t;
	if (!*dirpath) break;
	t=strchr(dirpath,'/');
	if (t) *t='\0';
	{
		struct directory_scan *d2;
		if (find_directory(&d2,d,dirpath)) {
			syslog(LOG_ERR,"Can't overlay directory %s over existing file\n",dirpath);
			GOTOERROR;
		}
		if (!d2) {
			if (!(d2=add_directory(scan,d,dirpath))) GOTOERROR;
		}
		d=d2;
	}
	if (!t) break;
	*t='/';
	dirpath=t+1;
}
return d;
error:
	return NULL;
}

static int addoverlay(struct scan *scan, struct directory_scan *d, char *realpath, char *fakebase, int israw, struct options *options) {
// overlay points to the full path
struct stat st;
unsigned int curdepth=1;
int r;

if (find_dirent_scan(d->entries.top,fakebase)) { // if d is new, this is unnecessary but quick
	if (options->isverbose) {
		syslog(LOG_INFO,"Ignoring overlapping file %s -> %s",realpath,fakebase);
	}
	return 0;
}

r=(israw)?lstat(realpath,&st):stat(realpath,&st);
if (r) {
	if (errno==ENOENT) { syslog(LOG_INFO,"Ignoring broken overlay %s",realpath); return 0; }
	GOTOERROR;
}
switch (st.st_mode&S_IFMT) {
	case S_IFSOCK:
		if (addsymlink_directory_scan(d,scan,NULL,fakebase,realpath)) GOTOERROR;
		break;
	case S_IFREG:
		if (addfile_directory_scan(d,scan,&st,fakebase,realpath)) GOTOERROR;
		break;
	case S_IFDIR:
		{
			struct directory_scan *d2;
			if (adddirectory_directory_scan(&d2,d,scan,&st,fakebase,realpath)) GOTOERROR;
			if (addsubdir(&curdepth,scan,NULL,realpath,d2,options)) GOTOERROR;
		}
		break;
	case S_IFBLK:
		if (israw) {
			if (addbdev_directory_scan(d,scan,&st,fakebase)) GOTOERROR;
		} else {
			uint64_t u64;
			if (0>getsize_blockdevice(&u64,realpath)) GOTOERROR;
			st.st_size=u64;
			if (addfile_directory_scan(d,scan,&st,fakebase,realpath)) GOTOERROR;
		}
		break;
	case S_IFCHR:
		if (addcdev_directory_scan(d,scan,&st,fakebase)) GOTOERROR;
		break;
	default:
		if (options->isverbose) {
			syslog(LOG_INFO,"Ignoring overlay %s of wrong type",realpath);
		}
		break;
}
return 0;
error:
	return -1;
}

int applyoverlay_scan(struct scan *scan, char *realpath_in, char *fakepath_in, int israw, struct options *options) {
char *realpath;
char *fakedir,*fakebase,*fakepath;
struct directory_scan *d;

if (!(realpath=strdup_mapmem(&scan->mapmem,realpath_in))) GOTOERROR;
if (!(fakepath=strdup_mapmem(&scan->mapmem,fakepath_in))) GOTOERROR;
fakedir=fakepath;
if ((fakebase=strrchr(fakepath,'/'))) {
	*fakebase='\0';
	fakebase++;
} else {
	fakedir=NULL;
	fakebase=fakepath;
}

if (!(d=findmake_directory(scan,fakedir))) GOTOERROR;
if (addoverlay(scan,d,realpath,fakebase,israw,options)) GOTOERROR;

return 0;
error:
	return -1;
}
