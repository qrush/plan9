#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>

#include "cformat.h"
#include "lru.h"
#include "bcache.h"
#include "disk.h"
#include "inode.h"
#include "file.h"
#include "stats.h"

enum
{
	Nfid=		10240,
};

/* maximum length of a file */
#define MAXLEN 0x7fffffffffffffffLL

typedef struct Mfile Mfile;
typedef struct Ram Ram;
typedef struct P9fs P9fs;
typedef struct Clunkitem Clunkitem;

struct Mfile
{
	Qid	qid;
	char	busy;
};

Mfile	mfile[Nfid];
Icache	ic;
int	debug, statson;

struct P9fs
{
	int	fd[2];
	Fcall	rhdr;
	Fcall	thdr;
	long	len;
	char	*name;
};

/*
 * We do clunks asynchronously (we don't wait for the rclunk from
 * the server).  The fid, however, may not be reused until the Rcklunk
 * is in.  We therefore remember outstanding Rclunks in a linked
 * list of Clunkitems.  It is hard to imagine having more than one or two
 * outstanding.  We pretend clunks always succeed.
 */
struct Clunkitem
{
	u32int	fid;
	ushort	tag;
	Clunkitem	*next;
};
Clunkitem	*clunklist;


P9fs	c;	/* client conversation */
P9fs	s;	/* server conversation */

struct Cfsstat  cfsstat, cfsprev;
char	statbuf[2048];
int	statlen;

#define	MAXFDATA	8192	/* i/o size for read/write */

int		messagesize = MAXFDATA+IOHDRSZ;

uchar	datasnd[MAXFDATA + IOHDRSZ];
uchar	datarcv[MAXFDATA + IOHDRSZ];

Qid	rootqid;
Qid	ctlqid = {0x5555555555555555LL, 0, 0};

Clunkitem *getclunk(u32int, ushort);
void	rversion(void);
void	rauth(Mfile*);
void	rflush(void);
void	rattach(Mfile*);
void	rwalk(Mfile*);
void	ropen(Mfile*);
void	rcreate(Mfile*);
void	rread(Mfile*);
void	rwrite(Mfile*);
void	rclunk(Mfile*);
void	rremove(Mfile*);
void	rstat(Mfile*);
void	rwstat(Mfile*);
void	error(char*, ...);
void	warning(char*);
void	mountinit(char*, char*);
Clunkitem *notclunked(u32int, ushort);
void	io(void);
void putclunk(u32int, ushort);
void	sendreply(char*);
void	sendmsg(P9fs*, Fcall*);
u32int rcvclunk(P9fs*, Fcall*);
void	rcvmsg(P9fs*, Fcall*);
int	delegate(void);
int	askserver(void);
void	cachesetup(int, char*);
int	ctltest(Mfile*);
void	genstats(void);

char *mname[]={
	[Tversion]		"Tversion",
	[Tauth]	"Tauth",
	[Tflush]	"Tflush",
	[Tattach]	"Tattach",
	[Twalk]		"Twalk",
	[Topen]		"Topen",
	[Tcreate]	"Tcreate",
	[Tclunk]	"Tclunk",
	[Tread]		"Tread",
	[Twrite]	"Twrite",
	[Tremove]	"Tremove",
	[Tstat]		"Tstat",
	[Twstat]	"Twstat",
	[Rversion]	"Rversion",
	[Rauth]	"Rauth",
	[Rerror]	"Rerror",
	[Rflush]	"Rflush",
	[Rattach]	"Rattach",
	[Rwalk]		"Rwalk",
	[Ropen]		"Ropen",
	[Rcreate]	"Rcreate",
	[Rclunk]	"Rclunk",
	[Rread]		"Rread",
	[Rwrite]	"Rwrite",
	[Rremove]	"Rremove",
	[Rstat]		"Rstat",
	[Rwstat]	"Rwstat",
			0,
};

void
usage(void)
{
	fprint(2, "usage:\tcfs -s [-rd] [-f partition]");
	fprint(2, "\tcfs [-rd] [-f partition] [-a netaddr] [mt-pt]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int std;
	int format;
	char *part;
	char *server;
	char *mtpt;

	std = 0;
	format = 0;
	part = "/dev/sdC0/cache";
	server = "il!emelie";
	mtpt = "/tmp";

	ARGBEGIN{
	case 'a':
		server = ARGF();
		if(server == 0)
			usage();
		break;
	case 'S':
		statson = 1;
		break;
	case 's':
		std = 1;
		break;
	case 'r':
		format = 1;
		break;
	case 'f':
		part = ARGF();
		if(part == 0)
			usage();
		break;
	case 'd':
		debug = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc && *argv)
		mtpt = *argv;

	if(debug)
		fmtinstall('F', fcallfmt);

	cachesetup(format, part);

	c.name = "client";
	s.name = "server";
	if(std){
		c.fd[0] = c.fd[1] = 1;
		s.fd[0] = s.fd[1] = 0;
	}else
		mountinit(server, mtpt);

	switch(fork()){
	case 0:
		io();
		exits("");
	case -1:
		error("fork");
	default:
		exits("");
	}
}

void
cachesetup(int format, char *partition)
{
	int f;
	int secsize;
	int inodes;
	int blocksize;

	secsize = 512;
	inodes = 1024;
	blocksize = 4*1024;

	f = open(partition, ORDWR);
	if(f < 0)
		error("opening partition");

	if(format || iinit(&ic, f, secsize)<0){
		if(iformat(&ic, f, inodes, "bootes", blocksize, secsize) < 0)
			error("formatting failed");
	}
}

void
mountinit(char *server, char *mountpoint)
{
	int p[2];

	/*
	 *  grab a channel and call up the file server
	 */
	s.fd[0] = s.fd[1] = dial(netmkaddr(server, 0, "9fs"), 0, 0, 0);
	if(s.fd[0] < 0)
		error("opening data");

	/*
 	 *  mount onto name space
	 */
	if(pipe(p) < 0)
		error("pipe failed");
	switch(fork()){
	case 0:
		break;
	default:
		if(amount(p[1], mountpoint, MREPL|MCREATE, "") < 0)
			error("mount failed");
		exits(0);
	case -1:
		error("fork failed\n");
/*BUG: no wait!*/
	}
	c.fd[0] = c.fd[1] = p[0];
}

void
io(void)
{
	int type;
	Mfile *mf;
    loop:
	rcvmsg(&c, &c.thdr);

	type = c.thdr.type;

	if(statson){
		cfsstat.cm[type].n++;
		cfsstat.cm[type].s = nsec();
	}

	mf = &mfile[c.thdr.fid];
	switch(type){
	default:
		error("type");
		break;
	case Tversion:
		rversion();
		break;
	case Tauth:
		mf = &mfile[c.thdr.afid];
		rauth(mf);
		break;
	case Tflush:
		rflush();
		break;
	case Tattach:
		rattach(mf);
		break;
	case Twalk:
		rwalk(mf);
		break;
	case Topen:
		ropen(mf);
		break;
	case Tcreate:
		rcreate(mf);
		break;
	case Tread:
		rread(mf);
		break;
	case Twrite:
		rwrite(mf);
		break;
	case Tclunk:
		rclunk(mf);
		break;
	case Tremove:
		rremove(mf);
		break;
	case Tstat:
		rstat(mf);
		break;
	case Twstat:
		rwstat(mf);
		break;
	}
	if(statson){
		cfsstat.cm[type].t += nsec() -cfsstat.cm[type].s;
	}
	goto loop;
}

void
rversion(void)
{
	if(messagesize > c.thdr.msize)
		messagesize = c.thdr.msize;
	c.thdr.msize = messagesize;	/* set downstream size */
	delegate();
}

void
rauth(Mfile *mf)
{
	Mfile *amf;

	if(delegate() == 0){
		if (c.thdr.afid != NOFID){
			amf = &mfile[c.thdr.afid];
			if(amf->busy)
				error("rauth afid on used channel");
			amf->qid = s.rhdr.aqid;
			amf->busy = 1;
		}
		mf->qid = s.rhdr.qid;
		mf->busy = 1;
	}
}

void
rflush(void)		/* synchronous so easy */
{
	sendreply(0);
}

void
rattach(Mfile *mf)
{
	if(delegate() == 0){
		mf->qid = s.rhdr.qid;
		mf->busy = 1;
		if (statson == 1){
			statson++;
			rootqid = mf->qid;
		}
	}
}

void
rwalk(Mfile *mf)
{
	Mfile *nmf;
	u32int fid;

	nmf = nil;
	if(statson
	  && mf->qid.type == rootqid.type && mf->qid.path == rootqid.path
	  && c.thdr.nwname == 1 && strcmp(c.thdr.wname[0], "cfsctl") == 0){
		/* This is the ctl file */
		nmf = &mfile[c.thdr.newfid];
		if(c.thdr.newfid != c.thdr.fid && nmf->busy)
			error("clone to used channel");
		nmf = &mfile[c.thdr.newfid];
		nmf->qid = ctlqid;
		nmf->busy = 1;
		c.rhdr.nwqid = 1;
		c.rhdr.wqid[0] = ctlqid;
		sendreply(0);
		return;
	}
	if(c.thdr.newfid != c.thdr.fid){
		if(c.thdr.newfid<0 || Nfid<=c.thdr.newfid)
			error("clone nfid out of range");
		if(notclunked(c.thdr.newfid, NOTAG)){
			/* wait for any outstanding clunks on this new fid */
			do{
				fid = rcvclunk(&s, &s.rhdr);
			}while(fid != c.thdr.newfid);
		}
		nmf = &mfile[c.thdr.newfid];
		if(nmf->busy)
			error("clone to used channel");
		nmf = &mfile[c.thdr.newfid];
		nmf->qid = mf->qid;
		nmf->busy = 1;
		mf = nmf; /* Walk mf */
	}

	if(delegate() < 0){	/* complete failure */
		if(nmf)
			nmf->busy = 0;
		return;
	}

	if(s.rhdr.nwqid == c.thdr.nwname){	/* complete success */
		if(s.rhdr.nwqid > 0)
			mf->qid = s.rhdr.wqid[s.rhdr.nwqid-1];
		return;
	}

	/* partial success; release fid */
	if(nmf)
		nmf->busy = 0;
}

void
ropen(Mfile *mf)
{
	if(statson && ctltest(mf)){
		/* Opening ctl file */
		if(c.thdr.mode != OREAD){
			sendreply("does not exist");
			return;
		}
		c.rhdr.qid = ctlqid;
		c.rhdr.iounit = 0;
		sendreply(0);
		genstats();
		return;
	}
	if(delegate() == 0){
		mf->qid = s.rhdr.qid;
		if(c.thdr.mode & OTRUNC)
			iget(&ic, mf->qid);
	}
}

void
rcreate(Mfile *mf)
{
	if(statson && ctltest(mf)){
		sendreply("exists");
		return;
	}
	if(delegate() == 0){
		mf->qid = s.rhdr.qid;
		mf->qid.vers++;
	}
}

void
rclunk(Mfile *mf)
{
	if(!mf->busy){
		sendreply(0);
		return;
	}
	mf->busy = 0;
	if(statson && ctltest(mf)){
		sendreply(0);
		return;
	}
	if(statson){
		cfsstat.sm[Tclunk].n++;
		cfsstat.sm[Tclunk].s = nsec();
	}
	/* forward clunk to server */
	sendmsg(&s, &c.thdr);
	/* record outstanding rclunk from server */
	putclunk(c.thdr.fid, c.thdr.tag);
	/* reply immediately */
	sendreply(0);
}

void
rremove(Mfile *mf)
{
	if(statson && ctltest(mf)){
		sendreply("not removed");
		return;
	}
	mf->busy = 0;
	delegate();
}

void
rread(Mfile *mf)
{
	int cnt;
	long off, first;
	char *cp;
	Ibuf *b;
	long n;
	char data[MAXFDATA];
	int done;

	first = off = c.thdr.offset;
	cnt = c.thdr.count;

	if(statson && ctltest(mf)){
		if(cnt > statlen-off)
			c.rhdr.count = statlen-off;
		else
			c.rhdr.count = cnt;
		if(c.rhdr.count < 0){
			sendreply("eof");
			return;
		}
		c.rhdr.data = statbuf + off;
		sendreply(0);
		return;
	}
	if(mf->qid.type & (QTDIR|QTAUTH)){
		delegate();
		if (statson) {
			cfsstat.ndirread++;
			if(c.rhdr.count > 0){
				cfsstat.bytesread += c.rhdr.count;
				cfsstat.bytesfromdirs += c.rhdr.count;
			}
		}
		return;
	}

	b = iget(&ic, mf->qid);
	if(b == 0){
		DPRINT(2, "delegating read\n");
		delegate();
		if (statson){
			cfsstat.ndelegateread++;
			if(c.rhdr.count > 0){
				cfsstat.bytesread += c.rhdr.count;
				cfsstat.bytesfromserver += c.rhdr.count;
			}
		}
		return;
	}

	cp = data;
	done = 0;
	while(cnt>0 && !done){
		if(off >= b->inode.length){
			DPRINT(2, "offset %ld greater than length %lld\n", off, b->inode.length);
			break;
		}
		n = fread(&ic, b, cp, off, cnt);
		if(n <= 0){
			n = -n;
			if(n==0 || n>cnt)
				n = cnt;
			DPRINT(2, "fetch %ld bytes of data from server at offset %ld\n", n, off);
			s.thdr.type = c.thdr.type;
			s.thdr.fid = c.thdr.fid;
			s.thdr.tag = c.thdr.tag;
			s.thdr.offset = off;
			s.thdr.count = n;
			if(statson){
				cfsstat.ndelegateread++;
			}
			if(askserver() < 0){
				sendreply(s.rhdr.ename);
				return;
			}
			if(s.rhdr.count != n)
				done = 1;
			n = s.rhdr.count;
			if(n == 0){
				/* end of file */
				if(b->inode.length > off){
					DPRINT(2, "file %llud.%ld, length %ld\n",
						b->inode.qid.path, b->inode.qid.vers, off);
					b->inode.length = off;
				}
				break;
			}
			memmove(cp, s.rhdr.data, n);
			fwrite(&ic, b, cp, off, n);
			if (statson){
				cfsstat.bytestocache += n;
				cfsstat.bytesfromserver += n;
			}
		}else{
			DPRINT(2, "fetched %ld bytes from cache\n", n);
			if(statson){
				cfsstat.bytesfromcache += n;
			}
		}
		cnt -= n;
		off += n;
		cp += n;
	}
	c.rhdr.data = data;
	c.rhdr.count = off - first;
	if(statson){
		cfsstat.bytesread += c.rhdr.count;
	}
	sendreply(0);
}

void
rwrite(Mfile *mf)
{
	Ibuf *b;
	char buf[MAXFDATA];

	if(statson && ctltest(mf)){
		sendreply("read only");
		return;
	}
	if(mf->qid.type & (QTDIR|QTAUTH)){
		delegate();
		if(statson && c.rhdr.count > 0)
			cfsstat.byteswritten += c.rhdr.count;
		return;
	}

	memmove(buf, c.thdr.data, c.thdr.count);
	if(delegate() < 0)
		return;

	if(s.rhdr.count > 0)
		cfsstat.byteswritten += s.rhdr.count;
	if(mf->qid.type & QTAPPEND)	/* don't modify our cache for append-only data; always read from server*/
		return;
	b = iget(&ic, mf->qid);
	if(b == 0)
		return;
	if (b->inode.length < c.thdr.offset + s.rhdr.count)
		b->inode.length = c.thdr.offset + s.rhdr.count;
	mf->qid.vers++;
	if (s.rhdr.count != c.thdr.count)
		syslog(0, "cfslog", "rhdr.count %ud, thdr.count %ud\n",
			s.rhdr.count, c.thdr.count);
	if(fwrite(&ic, b, buf, c.thdr.offset, s.rhdr.count) == s.rhdr.count){
		iinc(&ic, b);
		if(statson)
			cfsstat.bytestocache += s.rhdr.count;
	}
}

void
rstat(Mfile *mf)
{
	Dir d;
	
	if(statson && ctltest(mf)){
		genstats();
		d.qid = ctlqid;
		d.mode = 0444;
		d.length = statlen;	/* would be nice to do better */
		d.name = "cfsctl";
		d.uid = "none";
		d.gid = "none";
		d.muid = "none";
		d.atime = time(nil);
		d.mtime = d.atime;
		c.rhdr.nstat = convD2M(&d, c.rhdr.stat, sizeof(c.rhdr) - (c.rhdr.stat - (uchar*)&c.rhdr));
		sendreply(0);
		return;
	}
	if(delegate() == 0){
		Ibuf *b;

		convM2D(s.rhdr.stat, s.rhdr.nstat , &d, nil);
		mf->qid = d.qid;
		b = iget(&ic, mf->qid);
		if(b)
			b->inode.length = d.length;
	}
}

void
rwstat(Mfile *mf)
{
	Ibuf *b;

	if(statson && ctltest(mf)){
		sendreply("read only");
		return;
	}
	delegate();
	if(b = iget(&ic, mf->qid))
		b->inode.length = MAXLEN;
}

void
error(char *fmt, ...) {
	va_list arg;
	static char buf[2048];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	fprint(2, "%s: %s\n", argv0, buf);
	exits("error");
}

void
warning(char *s)
{
	fprint(2, "cfs: %s: %r\n", s);
}

/*
 *  send a reply to the client
 */
void
sendreply(char *err)
{

	if(err){
		c.rhdr.type = Rerror;
		c.rhdr.ename = err;
	}else{
		c.rhdr.type = c.thdr.type+1;
		c.rhdr.fid = c.thdr.fid;
	}
	c.rhdr.tag = c.thdr.tag;
	sendmsg(&c, &c.rhdr);
}

/*
 *  send a request to the server, get the reply, and send that to
 *  the client
 */
int
delegate(void)
{
	int type;

	type = c.thdr.type;
	if(statson){
		cfsstat.sm[type].n++;
		cfsstat.sm[type].s = nsec();
	}

	sendmsg(&s, &c.thdr);
	rcvmsg(&s, &s.rhdr);

	if(statson){
		cfsstat.sm[type].t += nsec() - cfsstat.sm[type].s;
	}

	sendmsg(&c, &s.rhdr);
	return c.thdr.type+1 == s.rhdr.type ? 0 : -1;
}

/*
 *  send a request to the server and get a reply
 */
int
askserver(void)
{
	int type;

	s.thdr.tag = c.thdr.tag;

	type = s.thdr.type;
	if(statson){
		cfsstat.sm[type].n++;
		cfsstat.sm[type].s = nsec();
	}

	sendmsg(&s, &s.thdr);
	rcvmsg(&s, &s.rhdr);

	if(statson){
		cfsstat.sm[type].t += nsec() - cfsstat.sm[type].s;
	}

	return s.thdr.type+1 == s.rhdr.type ? 0 : -1;
}

/*
 *  send/receive messages with logging
 */
void
sendmsg(P9fs *p, Fcall *f)
{
	DPRINT(2, "->%s: %F\n", p->name, f);

	p->len = convS2M(f, datasnd, messagesize);
	if(p->len <= 0)
		error("convS2M");
	if(write(p->fd[1], datasnd, p->len)!=p->len)
		error("sendmsg");
}

void
dump(uchar *p, int len)
{
	fprint(2, "%d bytes", len);
	while(len > 0){
		fprint(2, " %.2ux", *p++);
		len--;
	}
	fprint(2, "\n");
}

u32int
rcvclunk(P9fs *p, Fcall *f)
{
	Clunkitem *ci;
	int rlen;
	char buf[128];
	u32int fid;

	p->len = read9pmsg(p->fd[0], datarcv, sizeof(datarcv));
	if(p->len <= 0){
		sprint(buf, "read9pmsg(%d)->%ld: %r", p->fd[0], p->len);
		error(buf);
	}

	if((rlen = convM2S(datarcv, p->len, f)) != p->len)
		error("rcvmsg format error, expected length %d, got %d", rlen, p->len);
	if(f->type != Rclunk && f->type != Rerror)
		sysfatal("rcvclunk: unexpected message, type %d", f->type);
	if((ci = getclunk(NOFID, f->tag)) == nil)
		sysfatal("rcvclunk: unexpected clunk, %d", f->tag);
	fid = ci->fid;
	free(ci);
	return fid;
}

void
rcvmsg(P9fs *p, Fcall *f)
{
	Clunkitem *ci;
	int olen, rlen;
	char buf[128];

	olen = p->len;
again:
	p->len = read9pmsg(p->fd[0], datarcv, sizeof(datarcv));
	if(p->len <= 0){
		sprint(buf, "read9pmsg(%d)->%ld: %r", p->fd[0], p->len);
		error(buf);
	}

	if((rlen = convM2S(datarcv, p->len, f)) != p->len)
		error("rcvmsg format error, expected length %d, got %d", rlen, p->len);
	if(f->type == Rclunk){
		if(ci = getclunk(NOFID, f->tag))
			free(ci);
		goto again;
	}
	if(f->type == Rerror && (ci = getclunk(NOFID, f->tag))){
		free(ci);
		goto again;
	}
	if(f->fid<0 || Nfid<=f->fid){
		fprint(2, "<-%s: %d %s on %d\n", p->name, f->type,
			mname[f->type]? mname[f->type] : "mystery",
			f->fid);
		dump((uchar*)datasnd, olen);
		dump((uchar*)datarcv, p->len);
		error("rcvmsg fid out of range");
	}
	DPRINT(2, "<-%s: %F\n", p->name, f);
}

int
ctltest(Mfile *mf)
{
	return mf->busy && mf->qid.type == ctlqid.type && mf->qid.path == ctlqid.path;
}

void
genstats(void)
{
	int i;
	char *p;

	p = statbuf;

	p += snprint(p, sizeof(statbuf)+statbuf-p, "        Client                          Server\n");
	p += snprint(p, sizeof(statbuf)+statbuf-p, "   #calls     ??  ms/call    ??      #calls     ??  ms/call    ??\n");
	for (i = 0; i < nelem(cfsstat.cm); i++)
		if(cfsstat.cm[i].n || cfsstat.sm[i].n) {
			p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ",
				cfsstat.cm[i].n, cfsstat.cm[i].n - cfsprev.cm[i].n);
			if (cfsstat.cm[i].n)
				p += snprint(p, sizeof(statbuf)+statbuf-p, "%7.3f ",
					0.000001*cfsstat.cm[i].t/cfsstat.cm[i].n);
			else
				p += snprint(p, sizeof(statbuf)+statbuf-p, "        ");
			if(cfsstat.cm[i].n - cfsprev.cm[i].n)
				p += snprint(p, sizeof(statbuf)+statbuf-p, "%7.3f ",
					0.000001*(cfsstat.cm[i].t - cfsprev.cm[i].t)/(cfsstat.cm[i].n - cfsprev.cm[i].n));
			else
				p += snprint(p, sizeof(statbuf)+statbuf-p, "        ");
			p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ",
				cfsstat.sm[i].n, cfsstat.sm[i].n - cfsprev.sm[i].n);
			if (cfsstat.sm[i].n)
				p += snprint(p, sizeof(statbuf)+statbuf-p, "%7.3f ",
					0.000001*cfsstat.sm[i].t/cfsstat.sm[i].n);
			else
				p += snprint(p, sizeof(statbuf)+statbuf-p, "        ");
			if(cfsstat.sm[i].n - cfsprev.sm[i].n)
				p += snprint(p, sizeof(statbuf)+statbuf-p, "%7.3f ",
					0.000001*(cfsstat.sm[i].t - cfsprev.sm[i].t)/(cfsstat.sm[i].n - cfsprev.sm[i].n));
			else
				p += snprint(p, sizeof(statbuf)+statbuf-p, "        ");
			p += snprint(p, sizeof(statbuf)+statbuf-p, "%s\n", mname[i]);
		}
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ndirread\n",
		cfsstat.ndirread, cfsstat.ndirread - cfsprev.ndirread);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ndelegateread\n",
		cfsstat.ndelegateread, cfsstat.ndelegateread - cfsprev.ndelegateread);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ninsert\n",
		cfsstat.ninsert, cfsstat.ninsert - cfsprev.ninsert);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud ndelete\n",
		cfsstat.ndelete, cfsstat.ndelete - cfsprev.ndelete);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud nupdate\n",
		cfsstat.nupdate, cfsstat.nupdate - cfsprev.nupdate);

	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud bytesread\n",
		cfsstat.bytesread, cfsstat.bytesread - cfsprev.bytesread);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud byteswritten\n",
		cfsstat.byteswritten, cfsstat.byteswritten - cfsprev.byteswritten);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud bytesfromserver\n",
		cfsstat.bytesfromserver, cfsstat.bytesfromserver - cfsprev.bytesfromserver);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud bytesfromdirs\n",
		cfsstat.bytesfromdirs, cfsstat.bytesfromdirs - cfsprev.bytesfromdirs);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud bytesfromcache\n",
		cfsstat.bytesfromcache, cfsstat.bytesfromcache - cfsprev.bytesfromcache);
	p += snprint(p, sizeof(statbuf)+statbuf-p, "%7lud %7lud bytestocache\n",
		cfsstat.bytestocache, cfsstat.bytestocache - cfsprev.bytestocache);
	statlen = p - statbuf;
	cfsprev = cfsstat;
}

void
putclunk(u32int fid, ushort tag)
{
	Clunkitem *c;

	c = malloc(sizeof(Clunkitem));
	if(c == nil)
		sysfatal("putclunk: malloc: %r");
	c->fid = fid;
	c->tag = tag;
	c->next = clunklist;
	clunklist = c;
}

Clunkitem *
getclunk(u32int fid, ushort tag)
{
	Clunkitem **p, *c;

	for(p = &clunklist; c = *p; p = &c->next){
		if(fid != c->fid && tag != c->tag)
			continue;
		*p = c->next;
		return c;
	}
	return nil;		
}

Clunkitem *
notclunked(u32int fid, ushort tag)
{
	Clunkitem *c;

	for(c = clunklist; c != nil; c = c->next)
		if(fid == c->fid || tag == c->tag)
			return c;
	return nil;		
}
