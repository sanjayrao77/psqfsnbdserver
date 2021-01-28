/*
 * export.h
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
struct iprange4_export {
	unsigned int ui32_ipv4;
	unsigned int netmask;
	int isonlyiftls:1; // only match if client has already enabled tls
	struct iprange4_export *next;
};
struct iprange6_export {
	uint64_t high_ipv6, low_ipv6;
	uint64_t high_netmask, low_netmask;
	int isonlyiftls:1;
	struct iprange6_export *next;
};

struct overlay_export {
	char *realpath;
	char *fakepath;
	int israw:1; // raw => don't follow symlink

	struct overlay_export *next;
};

struct key_export {
	char *key;
	struct key_export *next;
};

#define DIR_TYPE_CHUNK_EXPORT			1
#define FILE_TYPE_CHUNK_EXPORT		2
#define PADTO4K_TYPE_CHUNK_EXPORT	3
struct chunk_export {
	int type;
	union {
		char *filename;
		char *directoryname;
	};

	struct chunk_export *next;
};

struct one_export {
	int isdisabled:1;
	int isdenydefault:1;
	int ispreload:1;
	int isnodelay:1;
	int iskeepalive:1;
	int islisted:1;
	int iskeyrequired:1;
	int istlsrequired:1;
	int isbuilt:1;
	unsigned int gziplevel:4;
	unsigned int maxfiles;
	uint32_t id; // starts at 1
	char *name;
	uint64_t timestamp; // time of build
	struct overlays_export {
		struct overlay_export *first;
	} overlays;
	struct {
		unsigned int num;
		struct chunk_export *first,*directory;
		struct chunk_export _directory;
	} chunks;
	unsigned int shorttimeout,longtimeout;
	struct keys_export {
		struct key_export *first;
	} keys;
	struct allows_export {
		struct iprange4_export *firstipv4;
		struct iprange6_export *firstipv6;
	} allowedhosts;
	struct {
		unsigned int msec_buildtime;
		unsigned int filecount;
		unsigned int subdircount;
	} stats;
	struct range range;

	struct one_export *next;
};

struct all_export {
	struct {
		int istlsrequired:1; // this effectively is a default
		unsigned int shorttimeout; // this does double-duty as a default
		uid_t uid;
		gid_t gid;
	} config;
	struct {
		unsigned int longtimeout;
		int isdenydefault:1;
		int ispreload:1;
		int isnodelay:1;
		int iskeepalive:1;
		int islisted:1;
		int iskeyrequired:1;
		unsigned int gziplevel:4;
		unsigned int maxfiles;
	} defaults;
	struct {
		unsigned int count;
		struct one_export *first;
	} exports;
	struct overlays_export overlays;
	struct keys_export keys; // this could be expanded with a deny list
	struct allows_export allowedhosts; // this can be copied to individual exports if they don't override
	struct {
		char *certfile;
		char *keyfile;
	} tls;
	struct {
		struct blockmem blockmem;
	} tofree;
};

int init_all_export(struct all_export *all);
int finalize_all_export(struct all_export *all);
void deinit_all_export(struct all_export *all);
int add_export(struct one_export **one_out, struct all_export *all, char *exportname);
int directoryname_set_export(struct all_export *all, struct one_export *one, char *directoryname);
int filename_set_export(struct all_export *all, struct one_export *one, char *filename);
int padto4k_set_export(struct all_export *all, struct one_export *one);
int build_one_export(struct one_export *one, struct options *options);
struct one_export *ipv4_findone_export(int *ismissingket_ouy, struct all_export *all, char *exportname, unsigned char *ipv4, int istls);
struct one_export *ipv6_findone_export(int *ismissingkey_out, struct all_export *all, char *exportname, unsigned char *ipv6, int istls);
struct one_export *ipv4_findany_export(struct all_export *all, unsigned char *ipv4);
struct one_export *ipv6_findany_export(struct all_export *all, unsigned char *ipv6);
int text_allowhost_add_one_export(struct all_export *all, struct one_export *one, char *text, int isonlyiftls);
int text_allowhost_add_export(struct all_export *all, char *text, int isonlyiftls);
int preload_export(struct all_export *exports, struct options *options);
int isallowed_export(struct one_export *one, unsigned char *ip, int isipv4);
int overlay_add_export(struct all_export *exports, char *str, int israw, struct options *options);
int overlay_add_one_export(struct all_export *exports, struct one_export *one, char *str, int israw, struct options *options);
int key_add_export(struct all_export *exports, char *str);
int key_add_one_export(struct all_export *exports, struct one_export *one, char *str);
int setfilename_export(char **filename_out, struct all_export *all, char *filename);
struct one_export *findbyid_one_export(struct all_export *exports, uint32_t id);
int rebuild_one_export(struct one_export *one, struct options *options);
