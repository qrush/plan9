#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"init.h"
#include	"pool.h"
#include	"/sys/src/boot/alphapc/conf.h"
#include	"axp.h"

char argbuf[128];	/* arguments passed to initcode and /boot */

Hwrpb *hwrpb;
Bootconf *bootconf;
Conf	conf;
FPsave	initfp;
	/* setfcr(FPPDBL|FPRNR|FPINVAL|FPZDIV|FPOVFL) */
uvlong initfpcr = (1LL<62)|(1LL<61)|(1LL<60)|(2LL<<58)|(1LL<48);

char bootargs[BOOTARGSLEN];
char *confname[MAXCONF];
char *confval[MAXCONF];
int	nconf;

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	cp = bootargs;
	strncpy(cp, bootconf->bootargs, BOOTARGSLEN);
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

void
main(void)
{
	hwrpb = (Hwrpb*)0x10000000;
	hwrpb = (Hwrpb*)(KZERO|hwrpb->phys);
	arginit();
	machinit();
	options();
	ioinit();
	clockinit();
	confinit();
	archinit();
	xinit();
	memholes();
	mmuinit();
	if(arch->coreinit)
		arch->coreinit();
	trapinit();
	screeninit();
	printinit();
	kbdinit();
	i8250console();
	quotefmtinstall();
	print("\nPlan 9\n");

	cpuidprint();
	if(arch->corehello)
		arch->corehello();

	procinit0();
	initseg();
	timersinit();
	links();
	chandevreset();
	pageinit();
	swapinit();
	savefpregs(&initfp);
initfp.fpstatus = 0x68028000;
	userinit();
	schedinit();
}

/*
 *  initialize a processor's mach structure.  each processor does this
 *  for itself.
 */
void
machinit(void)
{
	int n;
	Hwcpu *cpu;

	icflush();
	n = m->machno;
	memset(m, 0, sizeof(Mach));
	m->machno = n;

	active.exiting = 0;
	active.machs = 1;

	cpu = (Hwcpu*) ((ulong)hwrpb + hwrpb->cpuoff + n*hwrpb->cpulen);
	cpu->state &= ~1;			/* boot in progress - not */
/*	cpu->state |= (4<<16);		/* stay halted */
}

void
init0(void)
{
	int i;
	char tstr[32];

	up->nerrlab = 0;

	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	cnameclose(up->slash->name);
	up->slash->name = newcname("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "alpha", 0);
		sprint(tstr, "alpha %s alphapc", conffile);
		ksetenv("terminal", tstr, 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		for(i = 0; i < nconf; i++)
			if(confname[i]){
				if(confname[i][0] != '*')
					ksetenv(confname[i], confval[i], 0);
				ksetenv(confname[i], confval[i], 1);
			}
		poperror();
	}

	kproc("alarm", alarmkproc, 0);
	touser((uchar*)(USTKTOP - sizeof(argbuf)));
}

void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	char **av;
	Page *pg;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	procsetup(p);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-MAXSYSARG*BY2WD;
	/*
	 * User Stack, pass input arguments to boot process
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	for(av = (char**)argbuf; *av; av++)
		*av += (USTKTOP - sizeof(argbuf)) - (ulong)argbuf;

	memmove((uchar*)VA(k) + BY2PG - sizeof(argbuf), argbuf, sizeof argbuf);
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove((uchar*)VA(k), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpenab(0);
}

void
procsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpenab(0);
		else
			savefpregs(&up->fpsave);
		p->fpstate = FPinactive;
	}

	/*
	 * Switch to the prototype page tables for this processor.
	 * While this processor is in the scheduler, the process could run
	 * on another processor and exit, returning the page tables to
	 * the free list where they could be reallocated and overwritten.
	 * When this processor eventually has to get an entry from the
	 * trashed page tables it will crash.
	 */
	mmupark();
}

/* still to do */
void
reboot(void*, void*, ulong)
{
	exit(0);
}

void
exit(int)
{
	canlock(&active);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	spllo();
	print("cpu %d exiting\n", m->machno);
	do
		delay(100);
	while(consactive());

	splhi();
	delay(1000);	/* give serial fifo time to finish flushing */
	if(arch->coredetach)
		arch->coredetach();
	firmware();
}

void
confinit(void)
{
	ulong ktop, kpages;
	Bank *b, *eb;
	extern void _main(void);
	int userpcnt;
	char *p;

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	/*
	 * The console firmware divides memory into 1 or more banks.
	 * FInd the bank with the kernel in it.
	 */
	b = bootconf->bank;
	eb = b+bootconf->nbank;
	ktop = PGROUND((ulong)end);
	ktop = PADDR(ktop);
	while(b < eb) {
		if(b->min < ktop && ktop < b->max)
			break;
		b++;
	}
	if(b == eb)
		panic("confinit");

	/*
	 * Split the bank of memory into 2 banks to fool the allocator into
	 * allocating low memory pages from bank 0 for any peripherals
	 * which only have a 24bit address counter.
	 */
	conf.npage0 = (8*1024*1024)/BY2PG;
	conf.base0 = 0;

	conf.npage1 = (b->max-8*1024*1024)/BY2PG;
	conf.base1 = 8*1024*1024;

	conf.npage = conf.npage0+conf.npage1;
	conf.upages = (conf.npage*70)/100;

	conf.npage0 -= ktop/BY2PG;
	conf.base0 += ktop;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/*
	 * Fix up the bank we found to be the remnant, below the kernel.
	 * This, and the other banks, will be passed to xhole() later.
	 * BUG: conf.upages needs to be adjusted, but how?  In practice,
	 * we only have 1 bank, and the remnant is small.
	 */
	b->max = (uvlong)_main & ~(BY2PG-1);

	conf.nmach = 1;
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nimage = 200;
	conf.nswap = conf.nproc*80;
	conf.nswppo = 4096;
	conf.copymode = 0;			/* copy on write */

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Hack for the big boys. Only good while physmem < 4GB.
		 * Give the kernel a max. of 16MB + enough to allocate the
		 * page pool.
		 * This is an overestimate as conf.upages < conf.npages.
		 * The patch of nimage is a band-aid, scanning the whole
		 * page list in imagereclaim just takes too long.
		 */
		if(kpages > (16*MB + conf.npage*sizeof(Page))/BY2PG){
			kpages = (16*MB + conf.npage*sizeof(Page))/BY2PG;
			conf.nimage = 2000;
			kpages += (conf.nproc*KSTACK)/BY2PG;
		}
	} else {
		if(userpcnt < 10) {
			if(conf.npage*BY2PG < 16*MB)
				userpcnt = 40;
			else
				userpcnt = 60;
		}
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Make sure terminals with low memory get at least
		 * 4MB on the first Image chunk allocation.
		 */
		if(conf.npage*BY2PG < 16*MB)
			imagmem->minarena = 4*1024*1024;
	}
	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page);
	mainmem->maxsize = kpages;
	if(!cpuserver){
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;
	}

//	conf.monitor = 1;	/* BUG */
}

void
memholes(void)
{
	Bank *b, *eb;
	
	b = bootconf->bank;
	eb = b+bootconf->nbank;
	while(b < eb) {
		if(b->min < (1LL<<32) && b->max < (1LL<<32))
			xhole(b->min, b->max-b->min);
		b++;
	}
}

char *sp;

char *
pusharg(char *p)
{
	int n;

	n = strlen(p)+1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}

void
arginit(void)
{
	char **av;

	av = (char**)argbuf;
	sp = argbuf + sizeof(argbuf);
	*av++ = pusharg("boot");
	*av = 0;
}

char *
getconf(char *name)
{
	int n;

	for(n = 0; n < nconf; n++){
		if(cistrcmp(confname[n], name) == 0)
			return confval[n];
	}
	return 0;
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p;
	int i, n;

	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	for(n = 0; n < nconf; n++){
		if(cistrcmp(confname[n], cc) != 0)
			continue;
		isa->nopt = tokenize(confval[n], isa->opt, NISAOPT);
		for(i = 0; i < isa->nopt; i++){
			p = isa->opt[i];
			if(cistrncmp(p, "type=", 5) == 0)
				isa->type = p + 5;
			else if(cistrncmp(p, "port=", 5) == 0)
				isa->port = strtoul(p+5, &p, 0);
			else if(cistrncmp(p, "irq=", 4) == 0)
				isa->irq = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "dma=", 4) == 0)
				isa->dma = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "mem=", 4) == 0)
				isa->mem = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "size=", 5) == 0)
				isa->size = strtoul(p+5, &p, 0);
			else if(cistrncmp(p, "freq=", 5) == 0)
				isa->freq = strtoul(p+5, &p, 0);
		}
		return 1;
	}
	return 0;
}

int
cistrcmp(char *a, char *b)
{
	int ac, bc;

	for(;;){
		ac = *a++;
		bc = *b++;
	
		if(ac >= 'A' && ac <= 'Z')
			ac = 'a' + (ac - 'A');
		if(bc >= 'A' && bc <= 'Z')
			bc = 'a' + (bc - 'A');
		ac -= bc;
		if(ac)
			return ac;
		if(bc == 0)
			break;
	}
	return 0;
}

int
cistrncmp(char *a, char *b, int n)
{
	unsigned ac, bc;

	while(n > 0){
		ac = *a++;
		bc = *b++;
		n--;

		if(ac >= 'A' && ac <= 'Z')
			ac = 'a' + (ac - 'A');
		if(bc >= 'A' && bc <= 'Z')
			bc = 'a' + (bc - 'A');

		ac -= bc;
		if(ac)
			return ac;
		if(bc == 0)
			break;
	}

	return 0;
}

int
getcfields(char* lp, char** fields, int n, char* sep)
{
	int i;

	for(i = 0; lp && *lp && i < n; i++){
		while(*lp && strchr(sep, *lp) != 0)
			*lp++ = 0;
		if(*lp == 0)
			break;
		fields[i] = lp;
		while(*lp && strchr(sep, *lp) == 0){
			if(*lp == '\\' && *(lp+1) == '\n')
				*lp++ = ' ';
			lp++;
		}
	}

	return i;
}
