/***** spin: mesg.c *****/

/* Copyright (c) 1991-2000 by Lucent Technologies - Bell Laboratories     */
/* All Rights Reserved.  This software is for educational purposes only.  */
/* Permission is given to distribute this code provided that this intro-  */
/* ductory message is not removed and no monies are exchanged.            */
/* No guarantee is expressed or implied by the distribution of this code. */
/* Software written by Gerard J. Holzmann as part of the book:            */
/* `Design and Validation of Computer Protocols,' ISBN 0-13-539925-4,     */
/* Prentice Hall, Englewood Cliffs, NJ, 07632.                            */
/* Send bug-reports and/or questions to: gerard@research.bell-labs.com    */

#include "spin.h"
#ifdef PC
#include "y_tab.h"
#else
#include "y.tab.h"
#endif

#define MAXQ	2500		/* default max # queues  */

extern RunList	*X;
extern Symbol	*Fname;
extern Lextok	*Mtype;
extern int	verbose, TstOnly, s_trail, analyze, columns;
extern int	lineno, depth, xspin, m_loss, jumpsteps;
extern int	nproc, nstop;
extern short	Have_claim;

Queue	*qtab = (Queue *) 0;	/* linked list of queues */
Queue	*ltab[MAXQ];		/* linear list of queues */
int	nqs = 0, firstrow = 1;
char	Buf[4096];

static Lextok	*n_rem = (Lextok *) 0;
static Queue	*q_rem = (Queue  *) 0;

static int	a_rcv(Queue *, Lextok *, int);
static int	a_snd(Queue *, Lextok *);
static int	sa_snd(Queue *, Lextok *);
static int	s_snd(Queue *, Lextok *);
extern void	sr_buf(int, int);
extern void	sr_mesg(FILE *, int, int);
extern void	putarrow(int, int);
static void	sr_talk(Lextok *, int, char *, char *, int, Queue *);

int
cnt_mpars(Lextok *n)
{	Lextok *m;
	int i=0;

	for (m = n; m; m = m->rgt)
		i += Cnt_flds(m);
	return i;
}

int
qmake(Symbol *s)
{	Lextok *m;
	Queue *q;
	int i; extern int analyze;

	if (!s->ini)
		return 0;

	if (nqs >= MAXQ)
	{	lineno = s->ini->ln;
		Fname  = s->ini->fn;
		fatal("too many queues (%s)", s->name);
	}
	if (analyze && nqs >= 255)
	{	fatal("too many channel types", (char *)0);
	}

	if (s->ini->ntyp != CHAN)
		return eval(s->ini);

	q = (Queue *) emalloc(sizeof(Queue));
	q->qid    = ++nqs;
	q->nslots = s->ini->val;
	q->nflds  = cnt_mpars(s->ini->rgt);
	q->setat  = depth;

	i = max(1, q->nslots);	/* 0-slot qs get 1 slot minimum */

	q->contents  = (int *) emalloc(q->nflds*i*sizeof(int));
	q->fld_width = (int *) emalloc(q->nflds*sizeof(int));
	q->stepnr = (int *)   emalloc(i*sizeof(int));

	for (m = s->ini->rgt, i = 0; m; m = m->rgt)
	{	if (m->sym && m->ntyp == STRUCT)
			i = Width_set(q->fld_width, i, getuname(m->sym));
		else
			q->fld_width[i++] = m->ntyp;
	}
	q->nxt = qtab;
	qtab = q;
	ltab[q->qid-1] = q;

	return q->qid;
}

int
qfull(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return (ltab[whichq]->qlen >= ltab[whichq]->nslots);
	return 0;
}

int
qlen(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return ltab[whichq]->qlen;
	return 0;
}

int
q_is_sync(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return (ltab[whichq]->nslots == 0);
	return 0;
}

int
qsend(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq == -1)
	{	printf("Error: sending to an uninitialized chan\n");
		whichq = 0;
		return 0;
	}
	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
	{	ltab[whichq]->setat = depth;
		if (ltab[whichq]->nslots > 0)
			return a_snd(ltab[whichq], n);
		else
			return s_snd(ltab[whichq], n);
	}
	return 0;
}

int
qrecv(Lextok *n, int full)
{	int whichq = eval(n->lft)-1;

	if (whichq == -1)
	{	if (n->sym && !strcmp(n->sym->name, "STDIN"))
		{	Lextok *m;

			if (TstOnly) return 1;

			for (m = n->rgt; m; m = m->rgt)
			if (m->lft->ntyp != CONST && m->lft->ntyp != EVAL)
			{	int c = getchar();
				(void) setval(m->lft, c);
			} else
				fatal("invalid use of STDIN", (char *)0);

			whichq = 0;
			return 1;
		}
		printf("Error: receiving from an uninitialized chan %s\n",
			n->sym?n->sym->name:"");
		whichq = 0;
		return 0;
	}
	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
	{	ltab[whichq]->setat = depth;
		return a_rcv(ltab[whichq], n, full);
	}
	return 0;
}

static int
sa_snd(Queue *q, Lextok *n)	/* sorted asynchronous */
{	Lextok *m;
	int i, j, k;
	int New, Old;

	for (i = 0; i < q->qlen; i++)
	for (j = 0, m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	New = cast_val(q->fld_width[j], eval(m->lft), 0);
		Old = q->contents[i*q->nflds+j];
		if (New == Old) continue;
		if (New >  Old) break;			/* inner loop */
		if (New <  Old) goto found;
	}
found:
	for (j = q->qlen-1; j >= i; j--)
	for (k = 0; k < q->nflds; k++)
	{	q->contents[(j+1)*q->nflds+k] =
			q->contents[j*q->nflds+k];	/* shift up */
		if (k == 0)
			q->stepnr[j+1] = q->stepnr[j];
	}
	return i*q->nflds;				/* new q offset */
}

void
typ_ck(int ft, int at, char *s)
{
	if ((verbose&32) && ft != at
	&& (ft == CHAN || at == CHAN))
	{	char buf[128], tag1[64], tag2[64];
		(void) sputtype(tag1, ft);
		(void) sputtype(tag2, at);
		sprintf(buf, "type-clash in %s, (%s<-> %s)", s, tag1, tag2);
		non_fatal("%s", buf);
	}
}

static int
a_snd(Queue *q, Lextok *n)
{	Lextok *m;
	int i = q->qlen*q->nflds;	/* q offset */
	int j = 0;			/* q field# */

	if (q->nslots > 0 && q->qlen >= q->nslots)
		return m_loss;	/* q is full */

	if (TstOnly) return 1;

	if (n->val) i = sa_snd(q, n);	/* sorted insert */

	q->stepnr[i/q->nflds] = depth;

	for (m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	int New = eval(m->lft);
		q->contents[i+j] = cast_val(q->fld_width[j], New, 0);
		if ((verbose&16) && depth >= jumpsteps)
			sr_talk(n, New, "Send ", "->", j, q);
		typ_ck(q->fld_width[j], Sym_typ(m->lft), "send");
	}
	if ((verbose&16) && depth >= jumpsteps)
	{	for (i = j; i < q->nflds; i++)
			sr_talk(n, 0, "Send ", "->", i, q);
		if (j < q->nflds)
			printf("%3d: warning: missing params in send\n",
				depth);
		if (m)
			printf("%3d: warning: too many params in send\n",
				depth);
	}
	q->qlen++;
	return 1;
}

static int
a_rcv(Queue *q, Lextok *n, int full)
{	Lextok *m;
	int i=0, oi, j, k;
	extern int Rvous;

	if (q->qlen == 0)
		return 0;	/* q is empty */
try_slot:
	/* test executability */
	for (m = n->rgt, j=0; m && j < q->nflds; m = m->rgt, j++)
		if ((m->lft->ntyp == CONST
		   && q->contents[i*q->nflds+j] != m->lft->val)
		||  (m->lft->ntyp == EVAL
		   && q->contents[i*q->nflds+j] != eval(m->lft->lft)))
		{	if (n->val == 0		/* fifo recv */
			||  n->val == 2		/* fifo poll */
			|| ++i >= q->qlen)	/* last slot */
				return 0;	/* no match  */
			goto try_slot;
		}
	if (TstOnly) return 1;

	if (verbose&8)
	{	if (j < q->nflds)
			printf("%3d: warning: missing params in next recv\n",
				depth);
		else if (m)
			printf("%3d: warning: too many params in next recv\n",
				depth);
	}

	/* set the fields */
	if (Rvous)
	{	n_rem = n;
		q_rem = q;
	}

	oi = q->stepnr[i];
	for (m = n->rgt, j = 0; m && j < q->nflds; m = m->rgt, j++)
	{	if (columns && !full)	/* was columns == 1 */
			continue;
		if ((verbose&8) && !Rvous && depth >= jumpsteps)
		{	sr_talk(n, q->contents[i*q->nflds+j],
			(full && n->val < 2)?"Recv ":"[Recv] ", "<-", j, q);
		}
		if (!full)
			continue;	/* test */
		if (m && m->lft->ntyp != CONST && m->lft->ntyp != EVAL)
		{	(void) setval(m->lft, q->contents[i*q->nflds+j]);
			typ_ck(q->fld_width[j], Sym_typ(m->lft), "recv");
		}
		if (n->val < 2)		/* not a poll */
		for (k = i; k < q->qlen-1; k++)
		{	q->contents[k*q->nflds+j] =
			  q->contents[(k+1)*q->nflds+j];
			if (j == 0)
			  q->stepnr[k] = q->stepnr[k+1];
		}
	}

	if ((!columns || full)
	&& (verbose&8) && !Rvous && depth >= jumpsteps)
	for (i = j; i < q->nflds; i++)
	{	sr_talk(n, 0,
		(full && n->val < 2)?"Recv ":"[Recv] ", "<-", i, q);
	}
	if (columns == 2 && full && !Rvous && depth >= jumpsteps)
		putarrow(oi, depth);

	if (full && n->val < 2)
		q->qlen--;
	return 1;
}

static int
s_snd(Queue *q, Lextok *n)
{	Lextok *m;
	RunList *rX, *sX = X;	/* rX=recvr, sX=sendr */
	int i, j = 0;	/* q field# */

	for (m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	q->contents[j] = cast_val(q->fld_width[j], eval(m->lft), 0);
		typ_ck(q->fld_width[j], Sym_typ(m->lft), "rv-send");
	}
	q->qlen = 1;
	if (!complete_rendez())
	{	q->qlen = 0;
		return 0;
	}
	if (TstOnly)
	{	q->qlen = 0;
		return 1;
	}
	q->stepnr[0] = depth;
	if ((verbose&16) && depth >= jumpsteps)
	{	m = n->rgt;
		rX = X; X = sX;
		for (j = 0; m && j < q->nflds; m = m->rgt, j++)
			sr_talk(n, eval(m->lft), "Sent ", "->", j, q);
		for (i = j; i < q->nflds; i++)
			sr_talk(n, 0, "Sent ", "->", i, q);
		if (j < q->nflds)
			  printf("%3d: warning: missing params in rv-send\n",
				depth);
		else if (m)
			  printf("%3d: warning: too many params in rv-send\n",
				depth);
		X = rX;	/* restore receiver's context */
		if (!s_trail)
		{	if (!n_rem || !q_rem)
				fatal("cannot happen, s_snd", (char *) 0);
			m = n_rem->rgt;
			for (j = 0; m && j < q->nflds; m = m->rgt, j++)
			{	if (m->lft->ntyp != NAME
				||  strcmp(m->lft->sym->name, "_") != 0)
					i = eval(m->lft);
				else	i = 0;
				sr_talk(n_rem,i,"Recv ","<-",j,q_rem);
			}
			for (i = j; i < q->nflds; i++)
			sr_talk(n_rem, 0, "Recv ", "<-", j, q_rem);
			if (columns == 2)
				putarrow(depth, depth);
		}
		n_rem = (Lextok *) 0;
		q_rem = (Queue *) 0;
	}
	return 1;
}

void
channm(Lextok *n)
{	char lbuf[256];

	if (n->sym->type == CHAN)
		strcat(Buf, n->sym->name);
	else if (n->sym->type == NAME)
		strcat(Buf, lookup(n->sym->name)->name);
	else if (n->sym->type == STRUCT)
	{	Symbol *r = n->sym;
		if (r->context)
			r = findloc(r);
		ini_struct(r);
		printf("%s", r->name);
		struct_name(n->lft, r, 1, lbuf);
		strcat(Buf, lbuf);
	} else
		strcat(Buf, "-");
	if (n->lft->lft)
	{	sprintf(lbuf, "[%d]", eval(n->lft->lft));
		strcat(Buf, lbuf);
	}
}

static void
difcolumns(Lextok *n, char *tr, int v, int j, Queue *q)
{	int cnt = 1; extern int pno;
	Lextok *nn = ZN;

	if (j == 0)
	{	Buf[0] = '\0';
		channm(n);
		strcat(Buf, (strncmp(tr, "Sen", 3))?"?":"!");
	} else
		strcat(Buf, ",");
	if (tr[0] == '[') strcat(Buf, "[");
	sr_buf(v, q->fld_width[j] == MTYPE);
	if (j == q->nflds - 1)
	{	int cnr;
		if (s_trail) cnr = pno; else cnr = X?X->pid - Have_claim:0;
		if (tr[0] == '[') strcat(Buf, "]");
		pstext(cnr, Buf);
	}
}

static void
docolumns(Lextok *n, char *tr, int v, int j, Queue *q)
{	int i;

	if (firstrow)
	{	printf("q\\p");
		for (i = 0; i < nproc-nstop - Have_claim; i++)
			printf(" %3d", i);
		printf("\n");
		firstrow = 0;
	}
	if (j == 0)
	{	printf("%3d", q->qid);
		if (X)
		for (i = 0; i < X->pid - Have_claim; i++)
			printf("   .");
		printf("   ");
		Buf[0] = '\0';
		channm(n);
		printf("%s%c", Buf, (strncmp(tr, "Sen", 3))?'?':'!');
	} else
		printf(",");
	if (tr[0] == '[') printf("[");
	sr_mesg(stdout, v, q->fld_width[j] == MTYPE);
	if (j == q->nflds - 1)
	{	if (tr[0] == '[') printf("]");
		printf("\n");
	}
}

typedef struct QH {
	int	n;
	struct	QH *nxt;
} QH;
QH *qh;

void
qhide(int q)
{	QH *p = (QH *) emalloc(sizeof(QH));
	p->n = q;
	p->nxt = qh;
	qh = p;
}

int
qishidden(int q)
{	QH *p;
	for (p = qh; p; p = p->nxt)
		if (p->n == q)
			return 1;
	return 0;
}

static void
sr_talk(Lextok *n, int v, char *tr, char *a, int j, Queue *q)
{	char s[64];

	if (qishidden(eval(n->lft)))
		return;

	if (columns)
	{	if (columns == 2)
			difcolumns(n, tr, v, j, q);
		else
			docolumns(n, tr, v, j, q);
		return;
	}
	if (xspin)
	{	if ((verbose&4) && tr[0] != '[')
		sprintf(s, "(state -)\t[values: %d",
			eval(n->lft));
		else
		sprintf(s, "(state -)\t[%d", eval(n->lft));
		if (strncmp(tr, "Sen", 3) == 0)
			strcat(s, "!");
		else
			strcat(s, "?");
	} else
	{	strcpy(s, tr);
	}

	if (j == 0)
	{	whoruns(1);
		printf("line %3d %s %s",
			n->ln, n->fn->name, s);
	} else
		printf(",");
	sr_mesg(stdout, v, q->fld_width[j] == MTYPE);

	if (j == q->nflds - 1)
	{	if (xspin)
		{	printf("]\n");
			if (!(verbose&4)) printf("\n");
			return;
		}
		printf("\t%s queue %d (", a, eval(n->lft));
		Buf[0] = '\0';
		channm(n);
		printf("%s)\n", Buf);
	}
	fflush(stdout);
}

void
sr_buf(int v, int j)
{	int cnt = 1; Lextok *n;
	char lbuf[128];

	for (n = Mtype; n && j; n = n->rgt, cnt++)
		if (cnt == v)
		{	sprintf(lbuf, "%s", n->lft->sym->name);
			strcat(Buf, lbuf);
			return;
		}
	sprintf(lbuf, "%d", v);
	strcat(Buf, lbuf);
}

void
sr_mesg(FILE *fd, int v, int j)
{	Buf[0] ='\0';
	sr_buf(v, j);
	fprintf(fd, Buf);
}

void
doq(Symbol *s, int n, RunList *r)
{	Queue *q;
	int j, k;

	if (!s->val)	/* uninitialized queue */
		return;
	for (q = qtab; q; q = q->nxt)
	if (q->qid == s->val[n])
	{	if (xspin > 0
		&& (verbose&4)
		&& q->setat < depth)
			continue;
		if (q->nslots == 0)
			continue; /* rv q always empty */
		printf("\t\tqueue %d (", q->qid);
		if (r)
		printf("%s(%d):", r->n->name, r->pid - Have_claim);
		if (s->nel != 1)
		  printf("%s[%d]): ", s->name, n);
		else
		  printf("%s): ", s->name);
		for (k = 0; k < q->qlen; k++)
		{	printf("[");
			for (j = 0; j < q->nflds; j++)
			{	if (j > 0) printf(",");
				sr_mesg(stdout, q->contents[k*q->nflds+j],
					q->fld_width[j] == MTYPE);
			}
			printf("]");
		}
		printf("\n");
		break;
	}
}

void
nochan_manip(Lextok *p, Lextok *n, int d)
{	int e = 1;

	if (d == 0 && p->sym && p->sym->type == CHAN)
		setaccess(p->sym, ZS, 0, 'L');

	if (!n || n->ntyp == LEN || n->ntyp == RUN)
		return;

	if (n->sym && n->sym->type == CHAN)
	{	if (d == 1)
			fatal("invalid use of chan name", (char *) 0);
		else
			setaccess(n->sym, ZS, 0, 'V');	
	}

	if (n->ntyp == NAME
	||  n->ntyp == '.')
		e = 0;	/* array index or struct element */

	nochan_manip(p, n->lft, e);
	nochan_manip(p, n->rgt, 1);
}
