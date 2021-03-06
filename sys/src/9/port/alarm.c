#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

Alarms	alarms;
Rendez	alarmr;
Talarm	talarm;

void
alarmkproc(void*)
{
	Proc *rp;
	ulong now;

	for(;;){
		now = MACHP(0)->ticks;
		qlock(&alarms);
		while((rp = alarms.head) && rp->alarm <= now){
			if(rp->alarm != 0L){
				if(canqlock(&rp->debug)){
					if(!waserror()){
						postnote(rp, 0, "alarm", NUser);
						poperror();
					}
					qunlock(&rp->debug);
					rp->alarm = 0L;
				}else
					break;
			}
			alarms.head = rp->palarm;
		}
		qunlock(&alarms);

		sleep(&alarmr, return0, 0);
	}
}

/*
 *  called every clock tick
 */
void
checkalarms(void)
{
	Proc *p;
	ulong now;

	p = alarms.head;
	now = MACHP(0)->ticks;

	if(p && p->alarm <= now)
		wakeup(&alarmr);

	if(talarm.list == 0 || !canlock(&talarm))
		return;

	for(;;) {
		p = talarm.list;
		if(p == 0)
			break;

		if(p->twhen == 0) {
			talarm.list = p->tlink;
			p->trend = 0;
			continue;
		}
		if(now < p->twhen)
			break;
		wakeup(p->trend);
		talarm.list = p->tlink;
		p->trend = 0;
	}

	unlock(&talarm);
}

ulong
procalarm(ulong time)
{
	Proc **l, *f;
	ulong when, old;

	if(up->alarm)
		old = TK2MS(up->alarm - MACHP(0)->ticks);
	else
		old = 0;
	if(time == 0) {
		up->alarm = 0;
		return old;
	}
	when = ms2tk(time)+MACHP(0)->ticks;

	qlock(&alarms);
	l = &alarms.head;
	for(f = *l; f; f = f->palarm) {
		if(up == f){
			*l = f->palarm;
			break;
		}
		l = &f->palarm;
	}

	up->palarm = 0;
	if(alarms.head) {
		l = &alarms.head;
		for(f = *l; f; f = f->palarm) {
			if(f->alarm > when) {
				up->palarm = f;
				*l = up;
				goto done;
			}
			l = &f->palarm;
		}
		*l = up;
	}
	else
		alarms.head = up;
done:
	up->alarm = when;
	qunlock(&alarms);

	return old;
}
