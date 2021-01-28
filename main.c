/*
 * main.c - entry and main loop
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
#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/select.h>
#include <syslog.h>
#include <zlib.h>
#include <errno.h>
#include "common/conventions.h"
#include "common/mmapread.h"
#include "common/mapmem.h"
#include "common/blockmem.h"
#include "common/unixaf.h"
#include "misc.h"
#include "options.h"
#include "scan.h"
#include "mkfs.h"
#include "range.h"
#include "assemble.h"
#include "tcpsocket.h"
#include "export.h"
#include "nbd.h"
#include "runninglist.h"

static void processcmdline(struct options *options, int argc, char **argv) {
int i;
for (i=1;i<argc;i++) {
	if (!strcmp(argv[i],"-v")) options->isverbose=1;
	else if (!strcmp(argv[i],"-d")) options->isverbose=options->isdebug=options->isnofork=1;
	else if (!strcmp(argv[i],"-l")) options->islist=1;
	else if (!strcmp(argv[i],"-h")) options->ishelp=1;
	else if (!strcmp(argv[i],".")) {
		options->isverbose=options->isdebug=options->isnofork=1;
		options->tcpport=3000;
		options->portsearch=100;
		options->configfile=".";
	} else options->configfile=argv[i];
}
}

static void copyexportname(char *dest, char *src) {
while (1) {
	if (*src==']') break;
	if (!*src) break;
	*dest=*src;
	dest++;
	src++;
}
*dest='\0';
}

static inline void chomp(char *oneline) {
while (1) {
	switch (*oneline) {
		case '\r':
		case '\n':
			*oneline='\0';
			return;
		case 0: return;
	}
	oneline++;
}
}

static int isyes(char *str) {
if (!strncasecmp(str,"yes",3)) return 1;
if (tolower(*str)=='y') return 1;
if (!strncasecmp(str,"true",4)) return 1;
if (*str=='1') return 1;
return 0;
}

static int loadconfigfile3(struct all_export *exports, struct options *options, struct one_export *one,
		char *start, char *end, int lineno) {
char *tart=start+1;
int f=0;
if (one) {
	switch (*start) {
		case '4': if (!strncmp(tart,"kpad",4)) {f=1;if(isyes(end)&&padto4k_set_export(exports,one)) GOTOERROR;} break;
		case 'a':
			if (!strncmp(tart,"llownet",7)){f=1;if(text_allowhost_add_one_export(exports,one,end,0))GOTOERROR;}
			else if (!strncmp(tart,"llowtlsnet",10)){f=1;if(text_allowhost_add_one_export(exports,one,end,1))GOTOERROR;}
			break;
		case 'd':
			if (!strncmp(tart,"enyall",6)) { f=1; one->isdenydefault=isyes(end); }
			else if (!strncmp(tart,"irectory",8)) {f=1;if (directoryname_set_export(exports,one,end)) GOTOERROR; }
			break;
		case 'f': if (!strncmp(tart,"ilename_ro",10)) {f=1;if(filename_set_export(exports,one,end)) GOTOERROR; } break;
		case 'g': if (!strncmp(tart,"ziplevel",8)) { f=1; one->gziplevel=atoi(end) % 10; } break;
		case 'k':
			if (!strncmp(tart,"eypermit",8)) { f=1; if (key_add_one_export(exports,one,end)) GOTOERROR; }
			else if (!strncmp(tart,"eepalive",8)) { f=1; one->iskeepalive=isyes(end); }
			else if (!strncmp(tart,"eyrequired",10)) { f=1; one->iskeyrequired=isyes(end); }
			break;
		case 'l':
			if (!strncmp(tart,"ongtimeout",10)) { f=1; one->longtimeout=atoi(end); }
			else if (!strncmp(tart,"isted",5)) { f=1; one->islisted=isyes(end); }
			break;
		case 'm': if (!strncmp(tart,"axfiles",7)) { f=1; one->maxfiles=atoi(end); } break;
		case 'n': if (!strncmp(tart,"odelay",6)) { f=1; one->isnodelay=isyes(end); } break;
		case 's': if (!strncmp(tart,"horttimeout",11)) { f=1; one->shorttimeout=atoi(end); } break;
		case 't': if (!strncmp(tart,"lsrequired",10)) { f=1; one->istlsrequired=isyes(end); } break;
		case 'o':
			if (!strncmp(tart,"verlayraw",9)){f=1;if(overlay_add_one_export(exports,one,end,1,options))GOTOERROR;}
			else if (!strncmp(tart,"verlay",6)){f=1;if(overlay_add_one_export(exports,one,end,0,options))GOTOERROR;}
			break;
		case 'p': if (!strncmp(tart,"reload",6)) { f=1; one->ispreload=isyes(end); } break;
	}
} else { // global
	switch (*start) {
		case 'a':
			if (!strncmp(tart,"llownet",7)) { f=1; if (text_allowhost_add_export(exports,end,0)) GOTOERROR; }
			else if (!strncmp(tart,"llowtlsnet",10)) { f=1; if (text_allowhost_add_export(exports,end,1)) GOTOERROR; }
			else if (!strncmp(tart,"llowreset",9)) { f=1; if (isyes(end) && text_allowhost_add_export(exports,NULL,0)) GOTOERROR; }
			break;
		case 'b': if (!strncmp(tart,"ackground",9)) { f=1; options->isnofork=(isyes(end))?0:1; } break;
		case 'c': if (!strncmp(tart,"lientmax",8)) { f=1; options->maxchildren=atoi(end); } break;
		case 'd': // note that (isdebug=>syslog to stderr) only happens with cmdline "-d" and not with "debug=yes"
			if (!strncmp(tart,"ebug",4)) { f=1; options->isdebug=isyes(end); }
			else if (!strncmp(tart,"enyall",6)) { f=1; exports->defaults.isdenydefault=isyes(end); }
			break;
		case 'g':
			if (!strncmp(tart,"roup",4)) { f=1; if (getgid_misc(&exports->config.gid,end)) GOTOERROR; }
			else if (!strncmp(tart,"ziplevel",8)) { f=1; exports->defaults.gziplevel=atoi(end) % 10; }
			break;
		case 'k':
			if (!strncmp(tart,"eypermit",8)) { f=1; if (key_add_export(exports,end)) GOTOERROR; }
			else if (!strncmp(tart,"eyreset",7)) { f=1; if (isyes(end) && key_add_export(exports,NULL)) GOTOERROR; }
			else if (!strncmp(tart,"eyrequired",10)) { f=1; exports->defaults.iskeyrequired=isyes(end); }
			else if (!strncmp(tart,"eepalive",8)) { f=1; exports->defaults.iskeepalive=isyes(end); }
			break;
		case 'l':
			if (!strncmp(tart,"ongtimeout",10)) { f=1; exports->defaults.longtimeout=atoi(end); }
			else if (!strncmp(tart,"isted",5)) { f=1; exports->defaults.islisted=isyes(end); }
			break;
		case 'm':if (!strncmp(tart,"axfiles",7)) { f=1; exports->defaults.maxfiles=atoi(end); } break;
		case 'n': if (!strncmp(tart,"odelay",6)) { f=1; exports->defaults.isnodelay=isyes(end); } break;
		case 'o': 
			if (!strncmp(tart,"verlayreset",11)) { f=1; if (isyes(end) && overlay_add_export(exports,NULL,0,options)) GOTOERROR; }
			else if (!strncmp(tart,"verlayraw",9)) { f=1; if (overlay_add_export(exports,end,1,options)) GOTOERROR; }
			else if (!strncmp(tart,"verlay",6)) { f=1; if (overlay_add_export(exports,end,0,options)) GOTOERROR; }
			break;
		case 'p':
			if (!strncmp(tart,"ortsearch",9)) { f=1; options->portsearch=atoi(end); }
			else if (!strncmp(tart,"ortwait",7)) { f=1; options->portwait=atoi(end); }
			else if (!strncmp(tart,"ort",3)) { f=1; options->tcpport=atoi(end); }
			else if (!strncmp(tart,"reload",6)) { f=1; exports->defaults.ispreload=isyes(end); }
			break;
		case 's': if (!strncmp(tart,"horttimeout",11)) { f=1; exports->config.shorttimeout=atoi(end); } break;
		case 't':
			if (!strncmp(tart,"rackclients",11)) { f=1; options->issetenv=isyes(end); }
			else if (!strncmp(tart,"lsrequired",10)) { f=1; exports->config.istlsrequired=isyes(end); }
			else if (!strncmp(tart,"lscert",6)) { f=1; if (setfilename_export(&exports->tls.certfile,exports,end)) GOTOERROR; }
			else if (!strncmp(tart,"lskey",5)) { f=1; if (setfilename_export(&exports->tls.keyfile,exports,end)) GOTOERROR; }
			break;
		case 'u': if (!strncmp(tart,"ser",3)) { f=1; if (getuid_misc(&exports->config.uid,end)) GOTOERROR; } break;
		case 'v': if (!strncmp(tart,"erbose",6)) { f=1; options->isverbose=isyes(end); } break;
	}
}
if (!f) {
	if (options->isverbose) {
		syslog(LOG_INFO,"Ignored line %d in conf file, \"%s\"",lineno,start);
	}
}
return 0;
error:
	return -1;
}

static int loadconfigfile2(struct all_export *exports, struct options *options, FILE *fin) {
char oneline[128],exportname[128];
struct one_export *one=NULL;
int line=0;

oneline[0]='\0';

while (1) {
	char *start,*end,*eq;
	if (!fgets(oneline,128,fin)) break;
	chomp(oneline);
	line++;
	start=oneline; while (isspace(*start)) start++;
	switch (*start) {
		case '[':
			if ((!strncmp(oneline,"[generic]",9))||(!strncmp(oneline,"[global]",8))) {
				one=NULL;
			} else {
				(void)copyexportname(exportname,oneline+1);
				if (options->isdebug) { syslog(LOG_INFO,"Adding export \"%s\"",exportname); }
				if (add_export(&one,exports,exportname)) GOTOERROR;
			}
		case 0: 
		case '#':
			continue;
	}
	eq=strchr(start,'=');
	if (!eq) GOTOERROR;
	*eq='\0';
	end=eq+1;
	while (isspace(*end)) end++;
	if (loadconfigfile3(exports,options,one,start,end,line)) GOTOERROR;
}
return 0;
error:
	syslog(LOG_ERR,"Error loading config file on line %d, line is: \"%s\"",line,oneline);
	return -1;
}

static int samedir_loadconfigfile(struct all_export *exports, struct options *options) {
struct one_export *one;
exports->defaults.ispreload=1;
if (add_export(&one,exports,".")) GOTOERROR;
if (directoryname_set_export(exports,one,".")) GOTOERROR;
return 0;
error:
	return -1;
}

static int loadconfigfile(struct all_export *exports, struct options *options, char *configfile) {
char *rootnames[]={"/etc/nbd-server/psqfs-config",NULL};
char *homenames[]={".psqfsnbd",".config/psqfs-nbd-server/config",NULL};
int fd=-1,dirfd=-1;
FILE *fin=NULL;

if (configfile) {
	if (!strcmp(configfile,".")) return samedir_loadconfigfile(exports,options);

	fd=open(configfile,O_RDONLY);
	if (fd<0) {
		syslog(LOG_ERR,"Unable to open given config file: \"%s\"",configfile);
		GOTOERROR;
	}
	if (options->isverbose) syslog(LOG_INFO,"Using config file \"%s\"",configfile);
} else {
	char **list;
	for (list=rootnames;*list;list++) {
		fd=open(*list,O_RDONLY);
		if (fd>=0) {
			if (options->isverbose) syslog(LOG_INFO,"Skipping config file \"%s\"",*list);
			break;
		}
	}
	if (fd==-1) {
		char *homedir;
		homedir=getenv("HOME");
		if (homedir) {
			dirfd=open(homedir,O_RDONLY|O_DIRECTORY);
			if (dirfd>=0) for (list=homenames;*list;list++) {
				fd=openat(dirfd,*list,O_RDONLY);
				if (fd>=0) {
					if (options->isverbose) syslog(LOG_INFO,"Using config file \"%s\"",*list);
					break;
				}
			}
		}
	}
	if (fd==-1) {
		syslog(LOG_ERR,"No config file found");
		GOTOERROR;
	}
}

if (!(fin=fdopen(fd,"rb"))) GOTOERROR;
fd=-1;
if (loadconfigfile2(exports,options,fin)) GOTOERROR;

fclose(fin);
ignore_ifclose(dirfd);
return 0;
error:
	ignore_ifclose(fd);
	ignore_ifclose(dirfd);
	ignore_iffclose(fin);
	return -1;
}

static int print_help(void) {
fprintf(stdout,"Usage: psqfs-nbd-server [-v] [-d] [-l] [.] [CONFIGFILE]\n"\
"This exports a pseudo squashfs filesystem over NBD.\n\n"\
"You'll want a config file to run it seriously but for\n"\
"a quick export of the current directory, you can run:\n"\
"Quick start: $ psqfs-nbd-server .\n"\
"\n"\
"-v : verbose mode; print more status messages\n"\
"-d : debug mode; don't fork and print to stderr\n"\
"-l : list connected clients of a running server\n"\
" . : quick start, sets -v and -d\n"\
"See help file for information on config files\n");
return 0;
}

static int numchildren_global;

void sigchld_handler(int ign) {
	numchildren_global-=1;
}

static int handlecontrolrequest(int sock, struct all_export *exports, struct options *options) {
unsigned char message[4+sizeof(uint32_t)];
uint32_t u32;
int fd=-1;
pid_t pid;
struct one_export *one;

if (recvfd_unixaf(&fd,&pid,sock)) GOTOERROR;
if (readn(fd,message,4+sizeof(uint32_t))) GOTOERROR;
switch (message[0]) {
	case 'R':
		memcpy(&u32,message+4,sizeof(uint32_t));
		one=findbyid_one_export(exports,u32);
		if (one) {
			if (rebuild_one_export(one,options)) GOTOERROR;
			if (one->isdisabled) syslog(LOG_ERR,"Error rebuilding export %s",one->name);
		}
		break;
}
close(fd);
return 0;
error:
	ifclose(fd);
	return -1;
}

static inline CLEARFUNC(all_export);
static inline CLEARFUNC(options);
int main(int argc, char **argv) {
struct all_export all_export;
struct tcpsocket tcpsocket;
struct options options;
int controlsockets[2]={-1,-1};
int maxfdp1;

clear_all_export(&all_export);
clear_tcpsocket(&tcpsocket);
clear_options(&options);

// options.isverbose=0;
// options.isnofork=0;
// options.isdebug=0;
// options.issetenv=0;
// options.islist=0;
options.maxchildren=5;
options.tcpport=10809;
options.configfile=NULL;
(void)processcmdline(&options,argc,argv);

if (options.ishelp) return print_help();
if (options.islist) return print_runninglist();

(void)openlog(NULL,(options.isdebug)?LOG_PERROR|LOG_PID:LOG_PID,(options.isnofork)?LOG_USER:LOG_DAEMON);

signal(SIGPIPE,SIG_IGN);
signal(SIGCHLD,sigchld_handler);
// we are read-only now so, might as well just die on KILL

if (init_all_export(&all_export)) GOTOERROR;
if (loadconfigfile(&all_export,&options,options.configfile)) {
	syslog(LOG_ERR,"Error loading config file");
	GOTOERROR;
}
if (finalize_all_export(&all_export)) {
	syslog(LOG_ERR,"Error finalizing exports");
	GOTOERROR;
}

if (!all_export.exports.first) {
	syslog(LOG_INFO,"No exports configured, exiting");
} else {
	if (init_tcpsocket(&tcpsocket,options.tcpport,options.portsearch,options.portwait)) {
		syslog(LOG_ERR,"Error binding to socket");
		GOTOERROR;
	}

	if (all_export.config.gid) {
		if (setgid(all_export.config.gid)) {
			syslog(LOG_ERR,"Error setting gid %d (%s)",all_export.config.gid,strerror(errno));
			GOTOERROR;
		}
	}
	if (all_export.config.uid) {
		if (setuid(all_export.config.uid)) {
			syslog(LOG_ERR,"Error setting uid %d (%s)",all_export.config.uid,strerror(errno));
			GOTOERROR;
		}
	}

	if (preload_export(&all_export,&options)) { // we could do this before setuid but that just delays any problem
		syslog(LOG_ERR,"Error building preloads");
		GOTOERROR;
	}
	syslog(LOG_INFO,"Waiting on port %u",tcpsocket.port);

	maxfdp1=tcpsocket.fd;
	if (!options.isnofork) {
		if (daemon(0,0)) GOTOERROR;
		signal(SIGHUP,SIG_IGN);
		if (socketpair(AF_UNIX,SOCK_STREAM,0,controlsockets)) GOTOERROR;
		if (controlsockets[1]	> maxfdp1) maxfdp1=controlsockets[1];
	}
	maxfdp1+=1;
	while (1) {
		struct tcpsocket client;
		socklen_t ssa;
		fd_set rset;

		while (0<waitpid(-1,NULL,WNOHANG));

		FD_ZERO(&rset);
		FD_SET(tcpsocket.fd,&rset);
		if (controlsockets[0]>=0) FD_SET(controlsockets[0],&rset);
		switch (select(maxfdp1,&rset,NULL,NULL,NULL)) {
			case 2: case 1:
				if ((controlsockets[0]>=0) && FD_ISSET(controlsockets[0],&rset)) {
					(ignore)handlecontrolrequest(controlsockets[0],&all_export,&options);
				}
				if (!FD_ISSET(tcpsocket.fd,&rset)) continue;
				break;
			default:
				if (errno!=EINTR) GOTOERROR; // no break, EINTR pretty much means a SIGCHLD
			case 0: continue;
		}

		if (numchildren_global>=options.maxchildren) {
			if (controlsockets[0]>=0) do {
					struct timeval tv;
					FD_ZERO(&rset);
					FD_SET(controlsockets[0],&rset);
					tv.tv_sec=1;
					tv.tv_usec=0;
					switch (select(controlsockets[0]+1,&rset,NULL,NULL,&tv)) {
						case 1: (ignore)handlecontrolrequest(controlsockets[0],&all_export,&options); break;
						case -1: if (errno!=EINTR) GOTOERROR; break;
					}
			} while (numchildren_global>=options.maxchildren);
			else do sleep(1); while (numchildren_global>=options.maxchildren);
		}

		ssa=sizeof(client.sa6);
		client.port=tcpsocket.port;
		if (0>(client.fd=accept(tcpsocket.fd,(struct sockaddr*)&client.sa6,&ssa))) continue;

		if (!options.isnofork) {
			pid_t pid;
			pid=fork();
			if (pid) { numchildren_global+=1; close(client.fd); if (pid<0) sleep(1); continue; }
			(void)closelog();
			(void)openlog(NULL,(options.isdebug)?LOG_PERROR|LOG_PID:LOG_PID,LOG_DAEMON);
			(ignore)close(controlsockets[0]);
		}

		(void)setupip_tcpsocket(&client);
		syslog(LOG_INFO,"Connection from %s",client.iptext);

		(ignore)handleclient_nbd(&client,&all_export,&options,controlsockets[1]);

		if (!options.isnofork) _exit(0);
		(ignore)close(client.fd);
	}
}

deinit_tcpsocket(&tcpsocket);
deinit_all_export(&all_export);
return 0;
error:
	syslog(LOG_ERR,"Error in main()");
	deinit_tcpsocket(&tcpsocket);
	deinit_all_export(&all_export);
	return -1;
}
