#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

static Ether *etherxx[MaxEther];

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int
etherconfig(int on, char *spec, DevConf *cf)
{
	Ether *ether;
	int n, ctlrno;
	char name[32], buf[128];
	char *p, *e;

	/* can't unconfigure yet */
	if(on == 0)
		return -1;

	ctlrno = atoi(spec);
	if(etherxx[ctlrno] != nil)
		return -1;

	ether = malloc(sizeof(Ether));
	if(ether == nil)
		panic("etherconfig");
	memset(ether, 0, sizeof(Ether));
	*(DevConf*)ether = *cf;

	for(n = 0; cards[n].type; n++){
		if(strcmp(cards[n].type, ether->type) != 0)
			continue;
		if(cards[n].reset(ether))
			break;
		sprint(name, "ether%d", ctlrno);

		if(ether->mbps >= 100){
			netifinit(ether, name, Ntypes, 256*1024);
			if(ether->oq == 0)
				ether->oq = qopen(256*1024, 1, 0, 0);
		}
		else{
			netifinit(ether, name, Ntypes, 65*1024);
			if(ether->oq == 0)
				ether->oq = qopen(65*1024, 1, 0, 0);
		}
		if(ether->oq == 0)
			panic("etherreset %s", name);
		ether->alen = Eaddrlen;
		memmove(ether->addr, ether->ea, Eaddrlen);
		memset(ether->bcast, 0xFF, Eaddrlen);
		ether->mbps = 10;
		ether->minmtu = ETHERMINTU;
		ether->maxmtu = ETHERMAXTU;

		if(ether->interrupt != nil)
			intrenable(cf->itype, cf->interrupt, ether->interrupt, ether, name);

		p = buf;
		e = buf+sizeof(buf);
		p = seprint(p, e, "#l%d: %s: %dMbps port 0x%luX",
			ctlrno, ether->type, ether->mbps, ether->port);
		if(ether->mem)
			p = seprint(p, e, " addr 0x%luX", PADDR(ether->mem));
		if(ether->size)
			p = seprint(p, e, " size 0x%X", ether->size);
		p = seprint(p, e, ": %2.2uX%2.2uX%2.2uX%2.2uX%2.2uX%2.2uX",
			ether->ea[0], ether->ea[1], ether->ea[2],
			ether->ea[3], ether->ea[4], ether->ea[5]);
		seprint(p, e, "\n");
		pprint(buf);

		etherxx[ctlrno] = ether;
		return 0;
	}

	if (ether->type)
		free(ether->type);
	free(ether);
	return -1;
}



Chan*
etherattach(char* spec)
{
	ulong ctlrno;
	char *p;
	Chan *chan;

	ctlrno = 0;
	if(spec && *spec){
		ctlrno = strtoul(spec, &p, 0);
		if((ctlrno == 0 && p == spec) || *p || (ctlrno >= MaxEther))
			error(Ebadarg);
	}
	if(etherxx[ctlrno] == 0)
		error(Enodev);

	chan = devattach('l', spec);
	chan->dev = ctlrno;
	if(etherxx[ctlrno]->attach)
		etherxx[ctlrno]->attach(etherxx[ctlrno]);
	return chan;
}

static Walkqid*
etherwalk(Chan* chan, Chan* nchan, char** name, int nname)
{
	return netifwalk(etherxx[chan->dev], chan, nchan, name, nname);
}

static int
etherstat(Chan* chan, uchar* dp, int n)
{
	return netifstat(etherxx[chan->dev], chan, dp, n);
}

static Chan*
etheropen(Chan* chan, int omode)
{
	return netifopen(etherxx[chan->dev], chan, omode);
}

static void
ethercreate(Chan*, char*, int, ulong)
{
}

static void
etherclose(Chan* chan)
{
	netifclose(etherxx[chan->dev], chan);
}

static long
etherread(Chan* chan, void* buf, long n, vlong off)
{
	Ether *ether;
	ulong offset = off;

	ether = etherxx[chan->dev];
	if((chan->qid.type & QTDIR) == 0 && ether->ifstat){
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if(NETTYPE(chan->qid.path) == Nifstatqid)
			return ether->ifstat(ether, buf, n, offset);
		else if(NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}

	return netifread(ether, chan, buf, n, offset);
}

static Block*
etherbread(Chan* chan, long n, ulong offset)
{
	return netifbread(etherxx[chan->dev], chan, n, offset);
}

static int
etherwstat(Chan* chan, uchar* dp, int n)
{
	return netifwstat(etherxx[chan->dev], chan, dp, n);
}

static void
etherrtrace(Netfile* f, Etherpkt* pkt, int len)
{
	int i, n;
	Block *bp;

	if(qwindow(f->in) <= 0)
		return;
	if(len > 58)
		n = 58;
	else
		n = len;
	bp = iallocb(64);
	if(bp == nil)
		return;
	memmove(bp->wp, pkt->d, n);
	i = TK2MS(MACHP(0)->ticks);
	bp->wp[58] = len>>8;
	bp->wp[59] = len;
	bp->wp[60] = i>>24;
	bp->wp[61] = i>>16;
	bp->wp[62] = i>>8;
	bp->wp[63] = i;
	bp->wp += 64;
	qpass(f->in, bp);
}

Block*
etheriq(Ether* ether, Block* bp, int fromwire)
{
	Etherpkt *pkt;
	ushort type;
	int len, multi, tome, fromme;
	Netfile **ep, *f, **fp, *fx;
	Block *xbp;

	ether->inpackets++;

	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0]<<8)|pkt->type[1];
	fx = 0;
	ep = &ether->f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multcast addresses */
	if(multi && memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) && ether->prom == 0){
		if(!activemulti(ether, pkt->d, sizeof(pkt->d))){
			if(fromwire){
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for(fp = ether->f; fp < ep; fp++){
		if(f = *fp)
		if(f->type == type || f->type < 0)
		if(tome || multi || f->prom){
			/* Don't want to hear bridged packets */
			if(f->bridge && !fromwire && !fromme)
				continue;
			if(!f->headersonly){
				if(fromwire && fx == 0)
					fx = f;
				else if(xbp = iallocb(len)){
					memmove(xbp->wp, pkt, len);
					xbp->wp += len;
					qpass(f->in, xbp);
				}
				else
					ether->soverflows++;
			}
			else
				etherrtrace(f, pkt, len);
		}
	}

	if(fx){
		if(qpass(fx->in, bp) < 0)
			ether->soverflows++;
		return 0;
	}
	if(fromwire){
		freeb(bp);
		return 0;
	}

	return bp;
}

static int
etheroq(Ether* ether, Block* bp)
{
	int len, loopback, s;
	Etherpkt *pkt;

	ether->outpackets++;

	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	loopback = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if(loopback || memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) == 0 || ether->prom){
		s = splhi();
		etheriq(ether, bp, 0);
		splx(s);
	}

	if(!loopback){
		qbwrite(ether->oq, bp);
		ether->transmit(ether);
	} else
		freeb(bp);

	return len;
}

static long
etherwrite(Chan* chan, void* buf, long n, vlong)
{
	Ether *ether;
	Block *bp;
	int nn;

	ether = etherxx[chan->dev];
	if(NETTYPE(chan->qid.path) != Ndataqid) {
		nn = netifwrite(ether, chan, buf, n);
		if(nn >= 0)
			return nn;

		if(ether->ctl!=nil)
			return ether->ctl(ether,buf,n);
			
		error(Ebadctl);
	}

	if(n > ether->maxmtu)
		error(Etoobig);
	if(n < ether->minmtu)
		error(Etoosmall);

	bp = allocb(n);
	memmove(bp->rp, buf, n);
	memmove(bp->rp+Eaddrlen, ether->ea, Eaddrlen);
	bp->wp += n;

	return etheroq(ether, bp);
}

static long
etherbwrite(Chan* chan, Block* bp, ulong)
{
	Ether *ether;
	long n;

	n = BLEN(bp);
	if(NETTYPE(chan->qid.path) != Ndataqid){
		if(waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = etherxx[chan->dev];

	if(n > ether->maxmtu){
		freeb(bp);
		error(Etoobig);
	}
	if(n < ether->minmtu){
		freeb(bp);
		error(Etoosmall);
	}

	return etheroq(ether, bp);
}

int
parseether(uchar *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for(i = 0; i < 6; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}

static void
etherreset(void)
{
}

#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
ulong
ethercrc(uchar *p, int len)
{
	int i, j;
	ulong crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

Dev etherdevtab = {
	'l',
	"ether",

	etherreset,
	devinit,
	devshutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	ethercreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
	nil,		/* no power management */
	etherconfig,
};
