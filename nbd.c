/*
 * nbd.c - nbd server
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
#include <endian.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <inttypes.h>
#include <netinet/in.h>
#ifdef HAVETLS
#include <gnutls/gnutls.h>
#endif
// #define DEBUG2
#include "common/conventions.h"
#include "common/mmapread.h"
#include "common/blockmem.h"
#include "common/unixaf.h"
#include "common/overwrite_environ.h"
#include "misc.h"
#include "options.h"
#include "range.h"
#include "export.h"
#include "tcpsocket.h"

#include "nbd.h"

#define getu16(a) be16toh(*(uint16_t*)(a))
#define getu32(a)	be32toh(*(uint32_t*)(a))
#define getu64(a) be64toh(*(uint64_t*)(a))
#define setu16(a,b) *(uint16_t*)(a)=htobe16(b)
#define setu32(a,b) *(uint32_t*)(a)=htobe32(b)
#define setu64(a,b) *(uint64_t*)(a)=htobe64(b)


// "NBDMAGIC"
#define NBDMAGIC 0x4e42444d41474943
#define IHAVEOPT 0x49484156454F5054
#define NBD_FLAG_FIXED_NEWSTYLE		(1)
#define NBD_FLAG_NO_ZEROES				(2)
#define NBD_FLAG_C_FIXED_NEWSTYLE	(1)
#define NBD_FLAG_C_NO_ZEROES			(2)
#define NBD_REPLY_MAGIC							(0x3e889045565a9)
#define NBD_OPT_ABORT							(2)
#define NBD_OPT_LIST							(3)
#define NBD_OPT_STARTTLS					(5)
#define NBD_OPT_GO								(7)
#define NBD_INFO_EXPORT						(0)
#define NBD_INFO_NAME							(1)
#define NBD_REQUEST_MAGIC					(0x25609513)
#define NBD_FLAG_HAS_FLAGS					(1<<0)
#define NBD_FLAG_READ_ONLY					(1<<1)
#define NBD_FLAG_SEND_DF						(1<<7)
#define NBD_CMD_READ								(0)
#define NBD_CMD_DISC								(2)
#define NBD_REP_ACK									(1)
#define NBD_REP_SERVER							(2)
#define NBD_REP_INFO								(3)
#define NBD_REP_ERRBIT							(1<<31)
#define NBD_REP_ERR_UNSUP						((1<<31) + 1)
#define NBD_REP_ERR_POLICY					((1<<31) + 2)
#define NBD_REP_ERR_INVALID					((1<<31) + 3)
#define NBD_REP_ERR_PLATFORM				((1<<31) + 4)
#define NBD_REP_ERR_TLS_REQD				((1<<31) + 5)
#define NBD_REP_ERR_UNKNOWN					((1<<31) + 6)
#define NBD_REP_ERR_SHUTDOWN				((1<<31) + 7)
#define NBD_REP_ERR_BLOCK_SIZE_REQD	((1<<31) + 8)
#define NBD_REP_ERR_TOO_BIG					((1<<31) + 9)
#define NBD_SIMPLE_REPLY_MAGIC			(0x67446698)

#define SIZE_EXPORTNAME_NBD 130
struct nbd {
	int fd;
	int isno0s:1;
	int istls:1;
	uint64_t exportsize;
	struct options *options;
	unsigned char exportname[SIZE_EXPORTNAME_NBD];
#ifdef HAVETLS
	gnutls_certificate_credentials_t x509_cred;
	gnutls_session_t	tlssession;
	gnutls_priority_t	priority_cache;
#endif
};

#ifdef HAVETLS
#define xtls_timeout_writen(a,b,c,d) (((a)->istls)?tls_timeout_writen((a)->tlssession,b,c,d):timeout_writen((a)->fd,b,c,d))
#define xtls_timeout_readn(a,b,c,d) (((a)->istls)?tls_timeout_readn((a)->tlssession,b,c,d):timeout_readn((a)->fd,b,c,d))
#define xtls_timeout_write0s(a,b,c) (((a)->istls)?tls_timeout_write0s((a)->tlssession,b,c):timeout_write0s((a)->fd,b,c))
#else
#define xtls_timeout_writen(a,b,c,d) timeout_writen((a)->fd,b,c,d)
#define xtls_timeout_readn(a,b,c,d) timeout_readn((a)->fd,b,c,d)
#define xtls_timeout_write0s(a,b,c) timeout_write0s((a)->fd,b,c)
#endif

static inline void deinit_nbd(struct nbd *n) {
#ifdef HAVETLS
if (n->tlssession) gnutls_deinit(n->tlssession);
if (n->priority_cache) gnutls_priority_deinit(n->priority_cache);
if (n->x509_cred) gnutls_certificate_free_credentials(n->x509_cred);
gnutls_global_deinit(); // not needed
#endif
}

#ifdef HAVETLS
static inline int tls_timeout_readn(gnutls_session_t s, unsigned char *buff, unsigned int n, time_t maxtime) {
int fd;
fd=gnutls_transport_get_int(s);
if (n) while (1) {
	int r;
	r=gnutls_record_check_pending(s);
	if (!r) {
		struct timeval tv;
		fd_set rset;
		time_t t;

		FD_ZERO(&rset);
		FD_SET(fd,&rset);
		t=time(NULL);
		if (t>=maxtime) return -2;
		tv.tv_sec=maxtime-t;
		tv.tv_usec=0;
		switch (select(fd+1,&rset,NULL,NULL,&tv)) { case 0: continue; case -1: if (errno==EINTR) { sleep(1); continue; } GOTOERROR; }
	}
	r=gnutls_record_recv(s,buff,n);
	switch (r) {
		case GNUTLS_E_REHANDSHAKE:
			if (gnutls_alert_send(s,GNUTLS_AL_WARNING,GNUTLS_A_NO_RENEGOTIATION)) GOTOERROR; // no break
		case GNUTLS_E_AGAIN:
		case GNUTLS_E_INTERRUPTED:
			continue;
	}
	if (r<0) GOTOERROR;
	n-=r;
	if (!n) break;
	buff+=r;
}
return 0;
error:
	return -1;
}
static inline int tls_timeout_writen(gnutls_session_t s, unsigned char *buff, unsigned int n, time_t maxtime) {
int fd;
fd=gnutls_transport_get_int(s);
if (n) while (1) {
	int r;
	struct timeval tv;
	fd_set wset;
	time_t t;

	FD_ZERO(&wset);
	FD_SET(fd,&wset);
	t=time(NULL);
	if (t>=maxtime) GOTOERROR;
	tv.tv_sec=maxtime-t;
	tv.tv_usec=0;
	switch (select(fd+1,NULL,&wset,NULL,&tv)) { case 0: continue; case -1: if (errno==EINTR) { sleep(1); continue; } GOTOERROR; }

	r=gnutls_record_send(s,buff,n);
	switch (r) {
		case GNUTLS_E_AGAIN:
		case GNUTLS_E_INTERRUPTED:
			continue;
	}
	if (r<0) GOTOERROR;
	n-=r;
	if (!n) break;
	buff+=r;
}
return 0;
error:
	return -1;
}
static inline int tls_timeout_write0s(gnutls_session_t s, unsigned int n, time_t maxtime) {
int fd;
fd=gnutls_transport_get_int(s);
if (n) while (1) {
	int r;
	struct timeval tv;
	fd_set wset;
	time_t t;

	FD_ZERO(&wset);
	FD_SET(fd,&wset);
	t=time(NULL);
	if (t>=maxtime) GOTOERROR;
	tv.tv_sec=maxtime-t;
	tv.tv_usec=0;
	switch (select(fd+1,NULL,&wset,NULL,&tv)) { case 0: continue; case -1: if (errno==EINTR) { sleep(1); continue; } GOTOERROR; }

	r=gnutls_record_send(s,zero128_misc,_BADMIN(128,n));
	switch (r) {
		case GNUTLS_E_AGAIN:
		case GNUTLS_E_INTERRUPTED:
			continue;
	}
	if (r<0) GOTOERROR;
	n-=r;
	if (!n) break;
}
return 0;
error:
	return -1;
}
#endif

static int dohello(struct nbd *nbd, struct all_export *exports) {
unsigned char buffer[18];
unsigned int client;
setu64(buffer,NBDMAGIC);
setu64(buffer+8,IHAVEOPT);
setu16(buffer+16,NBD_FLAG_FIXED_NEWSTYLE|NBD_FLAG_NO_ZEROES);
if (timeout_writen(nbd->fd,buffer,18,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
if (timeout_readn(nbd->fd,buffer,4,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
client=getu32(buffer);
if (!(client&NBD_FLAG_C_FIXED_NEWSTYLE)) GOTOERROR; // required for TLS, assumed elsewhere also
nbd->isno0s=(client&NBD_FLAG_C_NO_ZEROES)?1:0;
return 0;
error:
	return -1;
}

static int replyerror_doopts(struct nbd *nbd, unsigned int cmd, unsigned int errcode, char *errmsg, unsigned int timeout) {
unsigned char buffer[20];
unsigned int errmsglen;

if (!errmsg) {
	errmsg="Server does not recognize command";
#ifndef HAVETLS
	if (cmd==NBD_OPT_STARTTLS) errmsg="Server built without TLS support";
#endif
}
errmsglen=strlen(errmsg);
setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,cmd);
setu32(buffer+12,NBD_REP_ERRBIT|errcode);
setu32(buffer+16,errmsglen);
if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+timeout)) GOTOERROR;
if (xtls_timeout_writen(nbd,(unsigned char *)errmsg,errmsglen,time(NULL)+timeout)) GOTOERROR;
return 0;
error:
	return -1;
}

static int sendmessage_control(int sock, unsigned char *message, unsigned int len) {
struct unixaf unixaf;
int fd=-1;
(void)voidinit_unixaf(&unixaf,-1,sock);
if (connect_unixaf(&fd,&unixaf)) GOTOERROR;
if (writen(fd,message,len)) GOTOERROR;
close(fd);
return 0;
error:
	ifclose(fd);
	return -1;
}

static int handlerebuild(struct nbd *nbd, struct all_export *exports, struct one_export *one, int controlsock) {
if (controlsock<0) {
	if (rebuild_one_export(one,nbd->options)) GOTOERROR;
} else {
	unsigned char message[4+sizeof(uint32_t)];
	memcpy(message,"RBLD",4);
	memcpy(message+4,&one->id,sizeof(uint32_t));
	if (sendmessage_control(controlsock,message,4+sizeof(uint32_t))) GOTOERROR;
}
return 0;
error:
	return -1;
}

static int go_doopts(struct one_export **one_export_inout, int *isbugout_inout, struct nbd *nbd,
		struct tcpsocket *tcp, struct all_export *exports, unsigned int bytecount, int controlsock) {
struct one_export *one_export=NULL;
unsigned char buffer[22];
unsigned int exportnamelen;
unsigned short client;
unsigned int errflag=0;
char *errmsg;
int ismissingkey=0;
int isrebuild=0;

if (bytecount<4) GOTOERROR; // really 6 is necessary
if (xtls_timeout_readn(nbd,buffer,4,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
exportnamelen=getu32(buffer);
if (bytecount!=exportnamelen+6) GOTOERROR;
if (exportnamelen+2>SIZE_EXPORTNAME_NBD) GOTOERROR;
if (xtls_timeout_readn(nbd,nbd->exportname,exportnamelen+2,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
client=getu16(nbd->exportname+exportnamelen);
if (client) GOTOERROR;

if (exports->config.istlsrequired && (!nbd->istls)) {
		return replyerror_doopts(nbd,NBD_OPT_GO,NBD_REP_ERR_TLS_REQD,"TLS is required for exports",exports->config.shorttimeout);
}

nbd->exportname[exportnamelen]='\0';
{
	char *tail;
	if ((tail=strrchr((char *)nbd->exportname,']'))) {
		if (!strcmp(tail+1,"_rebuild")) {
			*tail='\0';
			isrebuild=1;
		}
	}
}
if (tcp->isipv4) {
	one_export=ipv4_findone_export(&ismissingkey,exports,(char *)nbd->exportname,tcp->ip,nbd->istls);
} else {
	one_export=ipv6_findone_export(&ismissingkey,exports,(char *)nbd->exportname,tcp->ip,nbd->istls);
}

if (!one_export) {
	if (ismissingkey) {
		errflag=NBD_REP_ERR_POLICY;
		errmsg="A ]key is required"; // this is fine to say because everything else matched and listing was allowed
	} else {
		errflag=NBD_REP_ERR_UNKNOWN;
		errmsg="No export matches"; // we don't really want to say if it's because of an IP rejection as that could leak info
	}
} else {
	if (!one_export->isbuilt) {
		if (build_one_export(one_export,nbd->options)) {
			errflag=NBD_REP_ERRBIT;
			errmsg="Export build error";
			syslog(LOG_ERR,"Error building export \"%s\"",one_export->name);
		}
	} else {
		if (isrebuild) {
			if (handlerebuild(nbd,exports,one_export,controlsock)) GOTOERROR;
			if (controlsock>=0) {
				syslog(LOG_INFO,"Client marked for rebuild existing export %s",one_export->name);
				*isbugout_inout=1;
				return 0;
			} else if (one_export->isdisabled) {
				syslog(LOG_ERR,"Client unable to rebuild export %s",one_export->name);
				errflag=NBD_REP_ERRBIT;
				errmsg="Export build error";
			} else if (nbd->options->isverbose) {
				syslog(LOG_INFO,"Client rebuilt existing export %s",one_export->name);
			}
		} else if (nbd->options->isverbose) {
			syslog(LOG_INFO,"Client using existing export %s",one_export->name);
		}
	}
}

if (errflag) {
	return replyerror_doopts(nbd,NBD_OPT_GO,errflag,errmsg,exports->config.shorttimeout);
}

setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,NBD_OPT_GO);
setu32(buffer+12,NBD_REP_INFO);
setu32(buffer+16,12);
if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+one_export->shorttimeout)) GOTOERROR;
nbd->exportsize=one_export->range.entries.nextstart;
setu16(buffer,NBD_INFO_EXPORT);
setu64(buffer+2,nbd->exportsize);
setu16(buffer+10,NBD_FLAG_HAS_FLAGS|NBD_FLAG_READ_ONLY); // |NBD_FLAG_SEND_DF);
if (xtls_timeout_writen(nbd,buffer,12,time(NULL)+one_export->shorttimeout)) GOTOERROR;

{ // send a canonical name, with the build timestamp postfixed, allowing a client to remount and/or request a rebuild
	char buff[22];
	int buffn,namen;
	buffn=sprintf(buff,".%"PRIu64,one_export->timestamp);
	namen=strlen(one_export->name);

	setu64(buffer,NBD_REPLY_MAGIC);
	setu32(buffer+8,NBD_OPT_GO);
	setu32(buffer+12,NBD_REP_INFO);
	setu32(buffer+16,2+namen+buffn);
	setu16(buffer+20,NBD_INFO_NAME);
	if (xtls_timeout_writen(nbd,buffer,22,time(NULL)+one_export->shorttimeout)) GOTOERROR;
	if (xtls_timeout_writen(nbd,(unsigned char *)one_export->name,namen,time(NULL)+one_export->shorttimeout)) GOTOERROR;
	if (xtls_timeout_writen(nbd,(unsigned char *)buff,buffn,time(NULL)+one_export->shorttimeout)) GOTOERROR;
}



setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,NBD_OPT_GO);
setu32(buffer+12,NBD_REP_ACK);
setu32(buffer+16,0);
if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+one_export->shorttimeout)) GOTOERROR;

*one_export_inout=one_export;
return 0;
error:
	return -1;
}

static int list_doopts(struct nbd *nbd, struct tcpsocket *tcp, struct all_export *exports) {
unsigned char buffer[20];
struct one_export *one;
unsigned char *ip;
int isipv4=0;

if (exports->config.istlsrequired && (!nbd->istls))
		return replyerror_doopts(nbd,NBD_OPT_LIST,NBD_REP_ERR_TLS_REQD,"TLS is required for listing",exports->config.shorttimeout);

ip=tcp->ip;
isipv4=tcp->isipv4;

for (one=exports->exports.first;one;one=one->next) {
	unsigned int len;
	if (!one->islisted) continue;
	if (!isallowed_export(one,ip,isipv4)) continue;
	len=strlen(one->name);
	setu64(buffer,NBD_REPLY_MAGIC);
	setu32(buffer+8,NBD_OPT_LIST);
	setu32(buffer+12,NBD_REP_SERVER);
	setu32(buffer+16,len+4); // len+4
	if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
	setu32(buffer,len);
	if (xtls_timeout_writen(nbd,buffer,4,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
	if (xtls_timeout_writen(nbd,(unsigned char *)one->name,len,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
}

setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,NBD_OPT_LIST);
setu32(buffer+12,NBD_REP_ACK);
setu32(buffer+16,0);
if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
return 0;
error:
	return -1;
}

static int abort_doopts(struct nbd *nbd, struct tcpsocket *tcp, struct all_export *exports) {
unsigned char buffer[20];
/*
    Return zero or more `NBD_REP_SERVER` replies, one for each export,
    followed by `NBD_REP_ACK` or an error (such as
*/
setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,NBD_OPT_ABORT);
setu32(buffer+12,NBD_REP_ACK);
setu32(buffer+16,0);
if (xtls_timeout_writen(nbd,buffer,20,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
return 0;
error:
	return -1;
}

#ifdef HAVETLS
static int starttls_doopts(struct nbd *n, struct all_export *exports) {
unsigned char buffer[20];
unsigned int timeout;
timeout=exports->config.shorttimeout;
if (!exports->tls.certfile) {
	syslog(LOG_INFO,"TLS denied because tlscert not set");
	return replyerror_doopts(n,NBD_OPT_STARTTLS,NBD_REP_ERR_UNSUP,"tlscert not defined",timeout);
}
if (!exports->tls.keyfile) {
	syslog(LOG_INFO,"TLS denied because tlskey not set");
	return replyerror_doopts(n,NBD_OPT_STARTTLS,NBD_REP_ERR_UNSUP,"tlskey not defined",timeout);
}
if (n->istls) return replyerror_doopts(n,NBD_OPT_STARTTLS,NBD_REP_ERR_INVALID,"TLS has already been adopted",timeout);

setu64(buffer,NBD_REPLY_MAGIC);
setu32(buffer+8,NBD_OPT_STARTTLS);
setu32(buffer+12,NBD_REP_ACK);
setu32(buffer+16,0);
if (timeout_writen(n->fd,buffer,20,time(NULL)+timeout)) GOTOERROR;

if (0>gnutls_global_init()) GOTOERROR;
if (0>gnutls_certificate_allocate_credentials(&n->x509_cred)) GOTOERROR;
if (0) { // cacertfile
// set verify function if we want to do that here
	if (0>gnutls_certificate_set_x509_trust_file(n->x509_cred,"certs/ca.cert",GNUTLS_X509_FMT_PEM)) GOTOERROR;
}
// if (0>gnutls_certificate_set_x509_crl_file(n->x509_cred,"certs/ca.crl",GNUTLS_X509_FMT_PEM)) GOTOERROR;
// if (0>gnutls_certificate_set_x509_system_trust(n->x509_cred)) GOTOERROR;
if (0>gnutls_certificate_set_x509_key_file(n->x509_cred,exports->tls.certfile,exports->tls.keyfile,GNUTLS_X509_FMT_PEM)) GOTOERROR;
// if (0>gnutls_priority_init(&n->priority_cache,NULL,NULL)) GOTOERROR;
if (0>gnutls_init(&n->tlssession,GNUTLS_SERVER)) GOTOERROR;
// if (0>gnutls_priority_set(n->tlssession,n->priority_cache)) GOTOERROR;
if (0>gnutls_set_default_priority(n->tlssession)) GOTOERROR;
if (0>gnutls_credentials_set(n->tlssession,GNUTLS_CRD_CERTIFICATE,n->x509_cred)) GOTOERROR;
// (void)gnutls_session_set_verify_cert(n->tlssession,NULL,0); // noop
(void)gnutls_certificate_server_set_request(n->tlssession,GNUTLS_CERT_IGNORE);
(void)gnutls_transport_set_int(n->tlssession,n->fd);
(void)gnutls_handshake_set_timeout(n->tlssession,GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

while (1) {
	int r;
	r=gnutls_handshake(n->tlssession);
	if (r==GNUTLS_E_SUCCESS) break;
	if (r==GNUTLS_E_AGAIN) continue;
	if (r==GNUTLS_E_INTERRUPTED) continue;
	syslog(LOG_ERR,"%s:%d gnutls handshake failed: %s", __FILE__,__LINE__,gnutls_strerror(r));
	GOTOERROR;
}
#ifdef DEBUG
{ // copied from docs
	char *desc;
	desc = gnutls_session_get_desc(n->tlssession);
	if (n->options->isverbose) syslog(LOG_INFO,"gnutls session info: %s", desc);
	gnutls_free(desc);
}
#endif
n->istls=1;
return 0;
error:
	return -1;
}
#endif

static int doopts(struct one_export **one_export_out, struct nbd *nbd, struct tcpsocket *tcp,
		struct all_export *exports, int controlsock) {
unsigned char buffer[16];
unsigned int bytecount;
struct one_export *one=NULL;
int isbugout=0;

while (1) {
	if (xtls_timeout_readn(nbd,buffer,16,time(NULL)+exports->config.shorttimeout)) GOTOERROR;
	if (getu64(buffer)!=IHAVEOPT) GOTOERROR;
	bytecount=getu32(buffer+12);
#ifdef DEBUG
//	fprintf(stderr,"%s:%d Command %u\n",__FILE__,__LINE__,getu32(buffer+8));
#endif
	switch (getu32(buffer+8)) {
		case NBD_OPT_GO:
			if (go_doopts(&one,&isbugout,nbd,tcp,exports,bytecount,controlsock)) GOTOERROR;
			if (one || isbugout) { *one_export_out=one; return 0; }
			break;
		case NBD_OPT_LIST:
			if (bytecount) GOTOERROR;
			if (list_doopts(nbd,tcp,exports)) GOTOERROR;
			break;
		case NBD_OPT_ABORT:
			if (abort_doopts(nbd,tcp,exports)) GOTOERROR;
			*one_export_out=NULL;
			return 0;
#ifdef HAVETLS
		case NBD_OPT_STARTTLS:
			if (starttls_doopts(nbd,exports)) GOTOERROR;
			break;
#endif
		default:
			if (replyerror_doopts(nbd,getu32(buffer+8),NBD_REP_ERR_UNSUP,NULL,exports->config.shorttimeout)) GOTOERROR;
			syslog(LOG_ERR,"Unhandled option %u",getu32(buffer+8));
			GOTOERROR;
	}
}
return 0;
error:
	return -1;
}

#define NBD_EPERM (1)
#define NBD_EIO (5)
#define NBD_ENOMEM (12)
#define NBD_EINVAL (22)
#define NBD_ENOSPC (28)
#define NBD_EOVERFLOW (75)
#define NBD_ENOTSUP (95)
#define NBD_ESHUTDOWN (108)

// SICLEARFUNC(match_range);
static int nbd_cmd_read(struct nbd *nbd, struct one_export *one, unsigned char *cmd28) {
struct range *range=&one->range;
unsigned char buffer[16];
uint64_t offset;
uint32_t count;
unsigned int errorvalue=0;
int headersent=0;

offset=getu64(cmd28+16);
count=getu32(cmd28+24);

if (!count) {
	errorvalue=NBD_EINVAL;
	GOTOERROR;
}

#ifdef DEBUG2
#if 0
fprintf(stderr,"nbd_cmd_read, offset:%"PRIu64" count:%u\n",offset,count);
#endif
#endif

while (1) {
	struct match_range *m;
	unsigned int bytecount;
	m=finddata_range(range,offset,nbd->options);
	if (!m) {
		errorvalue=NBD_EINVAL;
		GOTOERROR; // bad request, offset out of range
	}
	if (m->iserror) {
		errorvalue=NBD_EIO;
		GOTOERROR; // io error or file not found
	}
	if (!m->len) {
		errorvalue=NBD_EOVERFLOW; // really, it's an underflow but whatevs
		GOTOERROR; // could happen if file gets truncated on us
	}

	if (!headersent) {
		headersent=1;
		setu32(buffer,NBD_SIMPLE_REPLY_MAGIC);
		setu32(buffer+4,0);
		memcpy(buffer+8,cmd28+8,8); // handle
		if (xtls_timeout_writen(nbd,buffer,16,time(NULL)+one->shorttimeout)) GOTOERROR;
	}

	bytecount=_BADMIN(m->len,count);
	if (m->data) {
		if (xtls_timeout_writen(nbd,m->data,bytecount,time(NULL)+one->shorttimeout)) GOTOERROR;
	} else {
		if (xtls_timeout_write0s(nbd,bytecount,time(NULL)+one->shorttimeout)) GOTOERROR;
	}

	count-=bytecount;
	if (!count) break;
	offset+=bytecount;
}
return 0;
error:
	if (!headersent) {
		setu32(buffer,NBD_SIMPLE_REPLY_MAGIC);
		setu32(buffer+4,errorvalue);
		memcpy(buffer+8,cmd28+8,8); // handle
		if (xtls_timeout_writen(nbd,buffer,16,time(NULL)+one->shorttimeout)) GOTOERROR;
	}
	return -1;
}

static int mainloop(struct nbd *nbd, struct one_export *one) {
unsigned char buffer[28];

while (1) {
	int r;
	r=xtls_timeout_readn(nbd,buffer,28,time(NULL)+one->longtimeout);
	if (r) {
		if (r==-2) {
			syslog(LOG_INFO,"Client timed out from %s",one->name);
			return 0;
		}
		syslog(LOG_INFO,"Client connection broken from %s",one->name);
		return 0;
	}
	if (getu32(buffer)!=NBD_REQUEST_MAGIC) GOTOERROR;
	switch (getu16(buffer+6)) {
		case NBD_CMD_READ:
			if (nbd_cmd_read(nbd,one,buffer)) GOTOERROR;
			break;
		case NBD_CMD_DISC:
			if (nbd->options->isverbose) {
				syslog(LOG_INFO,"Client disconnected from %s",one->name);
			}
			goto doublebreak;
			break;
		default: // TODO send an error reply
			GOTOERROR;
			break;
	}
}
doublebreak:

return 0;
error:
	return -1;
}

static inline char *startswith(char *str, char *sub) {
while (1) {
	if (!*sub) return str;
	if (*str!=*sub) return NULL;
	str++;
	sub++;
}
}
SICLEARFUNC(overwrite_environ);
static int setenviron(struct one_export *one, char *iptext, struct nbd *nbd) {
struct overwrite_environ oe;
clear_overwrite_environ(&oe);
(void)voidinit_overwrite_environ(&oe);
if (setenv_overwrite_environ(&oe,"NBD_CLIENTIP",iptext)) GOTOERROR;
if (setenv_overwrite_environ(&oe,"NBD_EXPORTNAME",one->name)) GOTOERROR;
#ifdef HAVETLS
if (nbd->istls) {
	if (setenv2_overwrite_environ(&oe,"NBD_TLSMODE=1")) GOTOERROR;
}
{
	char *tail;
	tail=startswith((char *)nbd->exportname,one->name);
	if (tail && *tail) {
		if (setenv_overwrite_environ(&oe,"NBD_KEY",tail)) GOTOERROR;
	}
}
#endif
return 0;
error:
	return -1;
}

SICLEARFUNC(nbd);
int handleclient_nbd(struct tcpsocket *client, struct all_export *exports, struct options *options, int controlsock) {
struct one_export *one_export;
struct nbd nbd;
unsigned char *ip;

clear_nbd(&nbd);

if (isipv4_tcpsocket(&ip,client)) {
	if (!ipv4_findany_export(exports,ip)) {
		if (options->isverbose) syslog(LOG_DEBUG,"No exports matched request (ipv4)");
		return 0;
	}
} else if (isipv6_tcpsocket(&ip,client)) {
	if (!ipv6_findany_export(exports,ip)) {
		if (options->isverbose) syslog(LOG_DEBUG,"No exports matched request (ipv6)");
		return 0;
	}
} else GOTOERROR;

nbd.fd=client->fd;
nbd.isno0s=0;
nbd.exportsize=0;
nbd.options=options;
nbd.istls=0; // TODO set this on tls

if (dohello(&nbd,exports)) GOTOERROR;
if (doopts(&one_export,&nbd,client,exports,controlsock)) GOTOERROR;
if (!one_export) goto done;

if (one_export->isnodelay) (ignore)nodelay_tcpsocket(nbd.fd);
if (one_export->iskeepalive) (ignore)keepalive_tcpsocket(nbd.fd);
if (options->issetenv) (ignore)setenviron(one_export,client->iptext,&nbd);

if (mainloop(&nbd,one_export)) GOTOERROR;
done:
deinit_nbd(&nbd);
return 0;
error:
	deinit_nbd(&nbd);
	return -1;
}
