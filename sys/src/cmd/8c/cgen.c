#include "gc.h"

void
cgen(Node *n, Node *nn)
{
	Node *l, *r;
	Prog *p1;
	Node nod, nod1, nod2, nod3, nod4;
	int o, hardleft;
	long v, curs;

	if(debug['g']) {
		prtree(nn, "cgen lhs");
		prtree(n, "cgen");
	}
	if(n == Z || n->type == T)
		return;
	if(typesuv[n->type->etype]) {
		sugen(n, nn, n->type->width);
		return;
	}
	l = n->left;
	r = n->right;
	o = n->op;
	if(n->addable >= INDEXED) {
		if(nn == Z) {
			switch(o) {
			default:
				nullwarn(Z, Z);
				break;
			case OINDEX:
				nullwarn(l, r);
				break;
			}
			return;
		}
		gmove(n, nn);
		return;
	}
	curs = cursafe;

	if(l->complex >= FNX)
	if(r != Z && r->complex >= FNX)
	switch(o) {
	default:
		regret(&nod, r);
		cgen(r, &nod);

		regsalloc(&nod1, r);
		gmove(&nod, &nod1);

		regfree(&nod);
		nod = *n;
		nod.right = &nod1;

		cgen(&nod, nn);
		return;

	case OFUNC:
	case OCOMMA:
	case OANDAND:
	case OOROR:
	case OCOND:
	case ODOT:
		break;
	}

	hardleft = l->addable < INDEXED || l->complex >= FNX;
	switch(o) {
	default:
		diag(n, "unknown op in cgen: %O", o);
		break;

	case OAS:
		if(typefd[n->type->etype]) {
			cgen(r, &fregnode0);
			if(nn != Z)
				gins(AFMOVD, &fregnode0, &fregnode0);
			if(l->addable < INDEXED) {
				reglcgen(&nod, l, Z);
				gmove(&fregnode0, &nod);
				regfree(&nod);
			} else
				gmove(&fregnode0, l);
			if(nn != Z)
				gmove(&fregnode0, nn);
			return;
		}
		if(l->op == OBIT)
			goto bitas;
		if(!hardleft) {
			if(nn != Z || r->addable < INDEXED) {
				if(r->complex >= FNX && nn == Z)
					regret(&nod, r);
				else
					regalloc(&nod, r, nn);
				cgen(r, &nod);
				gmove(&nod, l);
				if(nn != Z)
					gmove(&nod, nn);
				regfree(&nod);
			} else
				gmove(r, l);
			break;
		}
		if(l->complex >= r->complex) {
			if(l->op == OINDEX && r->op == OCONST) {
				gmove(r, l);
				break;
			}
			reglcgen(&nod1, l, Z);
			if(r->addable >= INDEXED) {
				gmove(r, &nod1);
				if(nn != Z)
					gmove(r, nn);
				regfree(&nod1);
				break;
			}
			regalloc(&nod, r, nn);
			cgen(r, &nod);
		} else {
			regalloc(&nod, r, nn);
			cgen(r, &nod);
			reglcgen(&nod1, l, Z);
		}
		gmove(&nod, &nod1);
		regfree(&nod);
		regfree(&nod1);
		break;

	bitas:
		n = l->left;
		regalloc(&nod, r, nn);
		if(l->complex >= r->complex) {
			reglcgen(&nod1, n, Z);
			cgen(r, &nod);
		} else {
			cgen(r, &nod);
			reglcgen(&nod1, n, Z);
		}
		regalloc(&nod2, n, Z);
		gmove(&nod1, &nod2);
		bitstore(l, &nod, &nod1, &nod2, nn);
		break;

	case OBIT:
		if(nn == Z) {
			nullwarn(l, Z);
			break;
		}
		bitload(n, &nod, Z, Z, nn);
		gmove(&nod, nn);
		regfree(&nod);
		break;

	case OLSHR:
	case OASHL:
	case OASHR:
		if(nn == Z) {
			nullwarn(l, r);
			break;
		}
		if(r->op == OCONST) {
			if(r->vconst == 0) {
				cgen(l, nn);
				break;
			}
			regalloc(&nod, l, nn);
			cgen(l, &nod);
			gopcode(o, n->type, r, &nod);
			gmove(&nod, nn);
			regfree(&nod);
			break;
		}

		/*
		 * get nod to be D_CX
		 */
		if(nodreg(&nod, nn, D_CX)) {
			regsalloc(&nod1, n);
			gmove(&nod, &nod1);
			cgen(n, &nod);		/* probably a bug */
			gmove(&nod, nn);
			gmove(&nod1, &nod);
			break;
		}
		reg[D_CX]++;
		if(nn->op == OREGISTER && nn->reg == D_CX)
			regalloc(&nod1, l, Z);
		else
			regalloc(&nod1, l, nn);
		if(r->complex >= l->complex) {
			cgen(r, &nod);
			cgen(l, &nod1);
		} else {
			cgen(l, &nod1);
			cgen(r, &nod);
		}
		gopcode(o, n->type, &nod, &nod1);
		gmove(&nod1, nn);
		regfree(&nod);
		regfree(&nod1);
		break;

	case OADD:
	case OSUB:
	case OOR:
	case OXOR:
	case OAND:
		if(nn == Z) {
			nullwarn(l, r);
			break;
		}
		if(typefd[n->type->etype])
			goto fop;
		if(r->op == OCONST) {
			if(r->vconst == 0 && o != OAND) {
				cgen(l, nn);
				break;
			}
		}
		if(r->addable >= INDEXED) {
			regalloc(&nod, l, nn);
			cgen(l, &nod);
			gopcode(o, n->type, r, &nod);
			gmove(&nod, nn);
			regfree(&nod);
			break;
		}
		if(l->complex >= r->complex) {
			regalloc(&nod, l, nn);
			cgen(l, &nod);
			regalloc(&nod1, r, Z);
			cgen(r, &nod1);
			gopcode(o, n->type, &nod1, &nod);
		} else {
			regalloc(&nod1, r, nn);
			cgen(r, &nod1);
			regalloc(&nod, l, Z);
			cgen(l, &nod);
			gopcode(o, n->type, &nod1, &nod);
		}
		gmove(&nod, nn);
		regfree(&nod);
		regfree(&nod1);
		break;

	case OLMOD:
	case OMOD:
	case OLMUL:
	case OLDIV:
	case OMUL:
	case ODIV:
		if(nn == Z) {
			nullwarn(l, r);
			break;
		}
		if(typefd[n->type->etype])
			goto fop;
		if(o == OMUL && r->op == OCONST) {
			regalloc(&nod, l, nn);
			cgen(l, &nod);
			gopcode(o, n->type, r, &nod);
			gmove(&nod, nn);
			regfree(&nod);
			break;
		}
		/*
		 * get nod to be D_AX
		 * get nod1 to be D_DX
		 */
		if(nodreg(&nod, nn, D_AX)) {
			regsalloc(&nod2, n);
			gmove(&nod, &nod2);
			v = reg[D_AX];
			reg[D_AX] = 0;

			if(isreg(l, D_AX)) {
				nod3 = *n;
				nod3.left = &nod2;
				cgen(&nod3, nn);
			} else
			if(isreg(r, D_AX)) {
				nod3 = *n;
				nod3.right = &nod2;
				cgen(&nod3, nn);
			} else
				cgen(n, nn);

			gmove(&nod2, &nod);
			reg[D_AX] = v;
			break;
		}
		if(nodreg(&nod1, nn, D_DX)) {
			regsalloc(&nod2, n);
			gmove(&nod1, &nod2);
			v = reg[D_DX];
			reg[D_DX] = 0;

			if(isreg(l, D_DX)) {
				nod3 = *n;
				nod3.left = &nod2;
				cgen(&nod3, nn);
			} else
			if(isreg(r, D_DX)) {
				nod3 = *n;
				nod3.right = &nod2;
				cgen(&nod3, nn);
			} else
				cgen(n, nn);

			gmove(&nod2, &nod1);
			reg[D_DX] = v;
			break;
		}
		reg[D_AX]++;

		if(l->complex >= r->complex) {
			cgen(l, &nod);
			reg[D_DX]++;
			if(o == ODIV || o == OMOD)
				gins(ACDQ, Z, Z);
			if(o == OLDIV || o == OLMOD)
				gins(AMOVL, nodconst(0), &nod1);
			if(r->addable < INDEXED || r->op == OCONST) {
				regsalloc(&nod3, r);
				cgen(r, &nod3);
				gopcode(o, n->type, &nod3, Z);
			} else
				gopcode(o, n->type, r, Z);
		} else {
			regsalloc(&nod3, r);
			cgen(r, &nod3);
			cgen(l, &nod);
			reg[D_DX]++;
			if(o == ODIV || o == OMOD)
				gins(ACDQ, Z, Z);
			if(o == OLDIV || o == OLMOD)
				gins(AMOVL, nodconst(0), &nod1);
			gopcode(o, n->type, &nod3, Z);
		}
		if(o == OMOD || o == OLMOD)
			gmove(&nod1, nn);
		else
			gmove(&nod, nn);
		regfree(&nod);
		regfree(&nod1);
		break;

	case OASLSHR:
	case OASASHL:
	case OASASHR:
		if(r->op == OCONST)
			goto asand;
		if(l->op == OBIT)
			goto asbitop;
		if(typefd[n->type->etype])
			goto asfop;

		/*
		 * get nod to be D_CX
		 */
		if(nodreg(&nod, nn, D_CX)) {
			regsalloc(&nod1, n);
			gmove(&nod, &nod1);
			cgen(n, &nod);
			if(nn != Z)
				gmove(&nod, nn);
			gmove(&nod1, &nod);
			break;
		}
		reg[D_CX]++;

		if(r->complex >= l->complex) {
			cgen(r, &nod);
			if(hardleft)
				reglcgen(&nod1, l, Z);
			else
				nod1 = *l;
		} else {
			if(hardleft)
				reglcgen(&nod1, l, Z);
			else
				nod1 = *l;
			cgen(r, &nod);
		}

		gopcode(o, l->type, &nod, &nod1);
		regfree(&nod);
		if(nn != Z)
			gmove(&nod1, nn);
		if(hardleft)
			regfree(&nod1);
		break;

	case OASAND:
	case OASADD:
	case OASSUB:
	case OASXOR:
	case OASOR:
	asand:
		if(l->op == OBIT)
			goto asbitop;
		if(typefd[n->type->etype])
			goto asfop;
		if(l->complex >= r->complex) {
			if(hardleft)
				reglcgen(&nod, l, Z);
			else
				nod = *l;
			if(r->op != OCONST) {
				regalloc(&nod1, r, nn);
				cgen(r, &nod1);
				gopcode(o, l->type, &nod1, &nod);
				regfree(&nod1);
			} else
				gopcode(o, l->type, r, &nod);
		} else {
			regalloc(&nod1, r, nn);
			cgen(r, &nod1);
			if(hardleft)
				reglcgen(&nod, l, Z);
			else
				nod = *l;
			gopcode(o, l->type, &nod1, &nod);
			regfree(&nod1);
		}
		if(nn != Z)
			gmove(&nod, nn);
		if(hardleft)
			regfree(&nod);
		break;

	case OASLMUL:
	case OASLDIV:
	case OASLMOD:
	case OASMUL:
	case OASDIV:
	case OASMOD:
		if(l->op == OBIT)
			goto asbitop;
		if(typefd[n->type->etype])
			goto asfop;
		if(o == OASMUL && r->op == OCONST) {
			if(hardleft)
				reglcgen(&nod2, l, Z);
			else
				nod2 = *l;
			regalloc(&nod, l, nn);
			cgen(&nod2, &nod);
			gopcode(o, l->type, r, &nod);
			gmove(&nod, &nod2);
			if(nn != Z)
				gmove(&nod, nn);
			if(hardleft)
				regfree(&nod2);
			regfree(&nod);
			break;
		}
		/*
		 * get nod to be D_AX
		 * get nod1 to be D_DX
		 */
		if(nodreg(&nod, nn, D_AX)) {
			regsalloc(&nod2, n);
			gmove(&nod, &nod2);
			v = reg[D_AX];
			reg[D_AX] = 0;

			if(isreg(l, D_AX)) {
				nod3 = *n;
				nod3.left = &nod2;
				cgen(&nod3, nn);
			} else
			if(isreg(r, D_AX)) {
				nod3 = *n;
				nod3.right = &nod2;
				cgen(&nod3, nn);
			} else
				cgen(n, nn);

			gmove(&nod2, &nod);
			reg[D_AX] = v;
			break;
		}
		if(nodreg(&nod1, nn, D_DX)) {
			regsalloc(&nod2, n);
			gmove(&nod1, &nod2);
			v = reg[D_DX];
			reg[D_DX] = 0;

			if(isreg(l, D_DX)) {
				nod3 = *n;
				nod3.left = &nod2;
				cgen(&nod3, nn);
			} else
			if(isreg(r, D_DX)) {
				nod3 = *n;
				nod3.right = &nod2;
				cgen(&nod3, nn);
			} else
				cgen(n, nn);

			gmove(&nod2, &nod1);
			reg[D_DX] = v;
			break;
		}
		reg[D_AX]++;
		reg[D_DX]++;

		if(l->complex >= r->complex) {
			if(hardleft)
				reglcgen(&nod2, l, Z);
			else
				nod2 = *l;
			cgen(&nod2, &nod);
			if(o == OASDIV || o == OASMOD)
				gins(ACDQ, Z, Z);
			if(o == OASLDIV || o == OASLMOD)
				gins(AMOVL, nodconst(0), &nod1);
			if(r->addable < INDEXED || r->op == OCONST ||
			   !typeil[r->type->etype]) {
				regalloc(&nod3, r, Z);
				cgen(r, &nod3);
				gopcode(o, l->type, &nod3, Z);
				regfree(&nod3);
			} else
				gopcode(o, n->type, r, Z);
		} else {
			regalloc(&nod3, r, Z);
			cgen(r, &nod3);
			if(hardleft)
				reglcgen(&nod2, l, Z);
			else
				nod2 = *l;
			cgen(&nod2, &nod);
			if(o == OASDIV || o == OASMOD)
				gins(ACDQ, Z, Z);
			if(o == OASLDIV || o == OASLMOD)
				gins(AMOVL, nodconst(0), &nod1);
			gopcode(o, l->type, &nod3, Z);
			regfree(&nod3);
		}
		if(o == OASMOD || o == OASLMOD) {
			gmove(&nod1, &nod2);
			if(nn != Z)
				gmove(&nod1, nn);
		} else {
			gmove(&nod, &nod2);
			if(nn != Z)
				gmove(&nod, nn);
		}
		if(hardleft)
			regfree(&nod2);
		regfree(&nod);
		regfree(&nod1);
		break;

	fop:
		if(l->complex >= r->complex) {
			cgen(l, &fregnode0);
			if(r->addable < INDEXED) {
				cgen(r, &fregnode0);
				fgopcode(o, &fregnode0, &fregnode1, 1, 0);
			} else
				fgopcode(o, r, &fregnode0, 0, 0);
		} else {
			cgen(r, &fregnode0);
			if(l->addable < INDEXED) {
				cgen(l, &fregnode0);
				fgopcode(o, &fregnode0, &fregnode1, 1, 1);
			} else
				fgopcode(o, l, &fregnode0, 0, 1);
		}
		gmove(&fregnode0, nn);
		break;

	asfop:
		if(l->complex >= r->complex) {
			if(hardleft)
				reglcgen(&nod, l, Z);
			else
				nod = *l;
			cgen(r, &fregnode0);
		} else {
			cgen(r, &fregnode0);
			if(hardleft)
				reglcgen(&nod, l, Z);
			else
				nod = *l;
		}
		if(!typefd[l->type->etype]) {
			gmove(&nod, &fregnode0);
			fgopcode(o, &fregnode0, &fregnode1, 1, 1);
		} else
			fgopcode(o, &nod, &fregnode0, 0, 1);
		if(nn != Z)
			gins(AFMOVD, &fregnode0, &fregnode0);
		gmove(&fregnode0, &nod);
		if(nn != Z)
			gmove(&fregnode0, nn);
		if(hardleft)
			regfree(&nod);
		break;

	asbitop:
		regalloc(&nod4, n, nn);
		if(l->complex >= r->complex) {
			bitload(l, &nod, &nod1, &nod2, &nod4);
			regalloc(&nod3, r, Z);
			cgen(r, &nod3);
		} else {
			regalloc(&nod3, r, Z);
			cgen(r, &nod3);
			bitload(l, &nod, &nod1, &nod2, &nod4);
		}
		gmove(&nod, &nod4);

		if(typefd[nod3.type->etype])
			fgopcode(o, &fregnode0, &fregnode1, 1, 1);
		else {
			Node onod;

			/* incredible grot ... */
			onod = nod3;
			onod.op = o;
			onod.complex = 2;
			onod.addable = 0;
			onod.type = tfield;
			onod.left = &nod4;
			onod.right = &nod3;
			cgen(&onod, Z);
		}
		regfree(&nod3);
		gmove(&nod4, &nod);
		regfree(&nod4);
		bitstore(l, &nod, &nod1, &nod2, nn);
		break;

	case OADDR:
		if(nn == Z) {
			nullwarn(l, Z);
			break;
		}
		lcgen(l, nn);
		break;

	case OFUNC:
		if(l->complex >= FNX) {
			if(l->op != OIND)
				diag(n, "bad function call");

			regret(&nod, l->left);
			cgen(l->left, &nod);
			regsalloc(&nod1, l->left);
			gmove(&nod, &nod1);
			regfree(&nod);

			nod = *n;
			nod.left = &nod2;
			nod2 = *l;
			nod2.left = &nod1;
			nod2.complex = 1;
			cgen(&nod, nn);

			return;
		}
		gargs(r, &nod, &nod1);
		if(l->addable < INDEXED) {
			reglcgen(&nod, l, nn);
			nod.op = OREGISTER;
			gopcode(OFUNC, n->type, Z, &nod);
			regfree(&nod);
		} else
			gopcode(OFUNC, n->type, Z, l);
		if(REGARG && reg[REGARG])
			reg[REGARG]--;
		if(nn != Z) {
			regret(&nod, n);
			gmove(&nod, nn);
			regfree(&nod);
		} else
		if(typefd[n->type->etype])
			gins(AFMOVDP, &fregnode0, &fregnode0);
		break;

	case OIND:
		if(nn == Z) {
			nullwarn(l, Z);
			break;
		}
		regialloc(&nod, n, nn);
		r = l;
		while(r->op == OADD)
			r = r->right;
		if(sconst(r)) {
			v = r->vconst;
			r->vconst = 0;
			cgen(l, &nod);
			nod.xoffset += v;
			r->vconst = v;
		} else
			cgen(l, &nod);
		regind(&nod, n);
		gmove(&nod, nn);
		regfree(&nod);
		break;

	case OEQ:
	case ONE:
	case OLE:
	case OLT:
	case OGE:
	case OGT:
	case OLO:
	case OLS:
	case OHI:
	case OHS:
		if(nn == Z) {
			nullwarn(l, r);
			break;
		}
		boolgen(n, 1, nn);
		break;

	case OANDAND:
	case OOROR:
		boolgen(n, 1, nn);
		if(nn == Z)
			patch(p, pc);
		break;

	case ONOT:
		if(nn == Z) {
			nullwarn(l, Z);
			break;
		}
		boolgen(n, 1, nn);
		break;

	case OCOMMA:
		cgen(l, Z);
		cgen(r, nn);
		break;

	case OCAST:
		if(nn == Z) {
			nullwarn(l, Z);
			break;
		}
		/*
		 * convert from types l->n->nn
		 */
		if(nocast(l->type, n->type) && nocast(n->type, nn->type)) {
			/* both null, gen l->nn */
			cgen(l, nn);
			break;
		}
		regalloc(&nod, l, nn);
		cgen(l, &nod);
		regalloc(&nod1, n, &nod);
		gmove(&nod, &nod1);
		gmove(&nod1, nn);
		regfree(&nod1);
		regfree(&nod);
		break;

	case ODOT:
		sugen(l, nodrat, l->type->width);
		if(nn == Z)
			break;
		warn(n, "non-interruptable temporary");
		nod = *nodrat;
		if(!r || r->op != OCONST) {
			diag(n, "DOT and no offset");
			break;
		}
		nod.xoffset += (long)r->vconst;
		nod.type = n->type;
		cgen(&nod, nn);
		break;

	case OCOND:
		bcgen(l, 1);
		p1 = p;
		cgen(r->left, nn);
		gbranch(OGOTO);
		patch(p1, pc);
		p1 = p;
		cgen(r->right, nn);
		patch(p1, pc);
		break;

	case OPOSTINC:
	case OPOSTDEC:
		v = 1;
		if(l->type->etype == TIND)
			v = l->type->link->width;
		if(o == OPOSTDEC)
			v = -v;
		if(l->op == OBIT)
			goto bitinc;
		if(nn == Z)
			goto pre;

		if(hardleft)
			reglcgen(&nod, l, Z);
		else
			nod = *l;

		if(typefd[n->type->etype])
			goto fltinc;
		gmove(&nod, nn);
		gopcode(OADD, n->type, nodconst(v), &nod);
		if(hardleft)
			regfree(&nod);
		break;

	case OPREINC:
	case OPREDEC:
		v = 1;
		if(l->type->etype == TIND)
			v = l->type->link->width;
		if(o == OPREDEC)
			v = -v;
		if(l->op == OBIT)
			goto bitinc;

	pre:
		if(hardleft)
			reglcgen(&nod, l, Z);
		else
			nod = *l;
		if(typefd[n->type->etype])
			goto fltinc;
		gopcode(OADD, n->type, nodconst(v), &nod);
		if(nn != Z)
			gmove(&nod, nn);
		if(hardleft)
			regfree(&nod);
		break;

	fltinc:
		gmove(&nod, &fregnode0);
		if(nn != Z && (o == OPOSTINC || o == OPOSTDEC))
			gins(AFMOVD, &fregnode0, &fregnode0);
		gins(AFLD1, Z, Z);
		if(v < 0)
			fgopcode(OSUB, &fregnode0, &fregnode1, 1, 0);
		else
			fgopcode(OADD, &fregnode0, &fregnode1, 1, 0);
		if(nn != Z && (o == OPREINC || o == OPREDEC))
			gins(AFMOVD, &fregnode0, &fregnode0);
		gmove(&fregnode0, &nod);
		if(hardleft)
			regfree(&nod);
		break;

	bitinc:
		if(nn != Z && (o == OPOSTINC || o == OPOSTDEC)) {
			bitload(l, &nod, &nod1, &nod2, Z);
			gmove(&nod, nn);
			gopcode(OADD, tfield, nodconst(v), &nod);
			bitstore(l, &nod, &nod1, &nod2, Z);
			break;
		}
		bitload(l, &nod, &nod1, &nod2, nn);
		gopcode(OADD, tfield, nodconst(v), &nod);
		bitstore(l, &nod, &nod1, &nod2, nn);
		break;
	}
	cursafe = curs;
}

void
reglcgen(Node *t, Node *n, Node *nn)
{
	Node *r;
	long v;

	regialloc(t, n, nn);
	if(n->op == OIND) {
		r = n->left;
		while(r->op == OADD)
			r = r->right;
		if(sconst(r)) {
			v = r->vconst;
			r->vconst = 0;
			lcgen(n, t);
			t->xoffset += v;
			r->vconst = v;
			regind(t, n);
			return;
		}
	}
	lcgen(n, t);
	regind(t, n);
}

void
lcgen(Node *n, Node *nn)
{
	Prog *p1;
	Node nod;

	if(debug['g']) {
		prtree(nn, "lcgen lhs");
		prtree(n, "lcgen");
	}
	if(n == Z || n->type == T)
		return;
	if(nn == Z) {
		nn = &nod;
		regalloc(&nod, n, Z);
	}
	switch(n->op) {
	default:
		if(n->addable < INDEXED) {
			diag(n, "unknown op in lcgen: %O", n->op);
			break;
		}
		gopcode(OADDR, n->type, n, nn);
		break;

	case OCOMMA:
		cgen(n->left, n->left);
		lcgen(n->right, nn);
		break;

	case OIND:
		cgen(n->left, nn);
		break;

	case OCOND:
		bcgen(n->left, 1);
		p1 = p;
		lcgen(n->right->left, nn);
		gbranch(OGOTO);
		patch(p1, pc);
		p1 = p;
		lcgen(n->right->right, nn);
		patch(p1, pc);
		break;
	}
}

void
bcgen(Node *n, int true)
{

	if(n->type == T)
		gbranch(OGOTO);
	else
		boolgen(n, true, Z);
}

void
boolgen(Node *n, int true, Node *nn)
{
	int o;
	Prog *p1, *p2;
	Node *l, *r, nod, nod1;
	long curs;

	if(debug['g']) {
		prtree(nn, "boolgen lhs");
		prtree(n, "boolgen");
	}
	curs = cursafe;
	l = n->left;
	r = n->right;
	switch(n->op) {

	default:
		o = ONE;
		if(true)
			o = OEQ;
		if(typefd[n->type->etype]) {
			if(n->addable < INDEXED) {
				cgen(n, &fregnode0);
				gins(AFLDZ, Z, Z);
				fgopcode(o, &fregnode0, &fregnode1, 1, 1);
			} else {
				gins(AFLDZ, Z, Z);
				fgopcode(o, n, &fregnode0, 0, 1);
			}
			goto com;
		}
		/* bad, 13 is address of external that becomes constant */
		if(n->addable >= INDEXED && n->addable != 13) {
			gopcode(o, n->type, n, nodconst(0));
			goto com;
		}
		regalloc(&nod, n, nn);
		cgen(n, &nod);
		gopcode(o, n->type, &nod, nodconst(0));
		regfree(&nod);
		goto com;

	case OCONST:
		o = vconst(n);
		if(!true)
			o = !o;
		gbranch(OGOTO);
		if(o) {
			p1 = p;
			gbranch(OGOTO);
			patch(p1, pc);
		}
		goto com;

	case OCOMMA:
		cgen(l, Z);
		boolgen(r, true, nn);
		break;

	case ONOT:
		boolgen(l, !true, nn);
		break;

	case OCOND:
		bcgen(l, 1);
		p1 = p;
		bcgen(r->left, true);
		p2 = p;
		gbranch(OGOTO);
		patch(p1, pc);
		p1 = p;
		bcgen(r->right, !true);
		patch(p2, pc);
		p2 = p;
		gbranch(OGOTO);
		patch(p1, pc);
		patch(p2, pc);
		goto com;

	case OANDAND:
		if(!true)
			goto caseor;

	caseand:
		bcgen(l, true);
		p1 = p;
		bcgen(r, !true);
		p2 = p;
		patch(p1, pc);
		gbranch(OGOTO);
		patch(p2, pc);
		goto com;

	case OOROR:
		if(!true)
			goto caseand;

	caseor:
		bcgen(l, !true);
		p1 = p;
		bcgen(r, !true);
		p2 = p;
		gbranch(OGOTO);
		patch(p1, pc);
		patch(p2, pc);
		goto com;

	case OEQ:
	case ONE:
	case OLE:
	case OLT:
	case OGE:
	case OGT:
	case OHI:
	case OHS:
	case OLO:
	case OLS:
		o = n->op;
		if(true)
			o = comrel[relindex(o)];
		if(l->complex >= FNX && r->complex >= FNX) {
			regret(&nod, r);
			cgen(r, &nod);
			regsalloc(&nod1, r);
			gmove(&nod, &nod1);
			regfree(&nod);
			nod = *n;
			nod.right = &nod1;
			boolgen(&nod, true, nn);
			break;
		}
		if(typefd[l->type->etype]) {
			if(l->complex >= r->complex) {
				cgen(l, &fregnode0);
				if(r->addable < INDEXED) {
					cgen(r, &fregnode0);
					o = invrel[relindex(o)];
					fgopcode(o, &fregnode0, &fregnode1, 1, 1);
				} else
					fgopcode(o, r, &fregnode0, 0, 1);
			} else {
				o = invrel[relindex(o)];
				cgen(r, &fregnode0);
				if(l->addable < INDEXED) {
					cgen(l, &fregnode0);
					o = invrel[relindex(o)];
					fgopcode(o, &fregnode0, &fregnode1, 1, 1);
				} else
					fgopcode(o, l, &fregnode0, 0, 1);
			}
			goto com;
		}
		if(l->op == OCONST) {
			o = invrel[relindex(o)];
			/* bad, 13 is address of external that becomes constant */
			if(r->addable < INDEXED || r->addable == 13) {
				regalloc(&nod, r, nn);
				cgen(r, &nod);
				gopcode(o, l->type, &nod, l);
				regfree(&nod);
			} else
				gopcode(o, l->type, r, l);
			goto com;
		}
		if(l->complex >= r->complex) {
			regalloc(&nod, l, nn);
			cgen(l, &nod);
			if(r->addable < INDEXED) {
				regalloc(&nod1, r, Z);
				cgen(r, &nod1);
				gopcode(o, l->type, &nod, &nod1);
				regfree(&nod1);
			} else
				gopcode(o, l->type, &nod, r);
			regfree(&nod);
			goto com;
		}
		regalloc(&nod, r, nn);
		cgen(r, &nod);
		if(l->addable < INDEXED || l->addable == 13) {
			regalloc(&nod1, l, Z);
			cgen(l, &nod1);
			gopcode(o, l->type, &nod1, &nod);
			regfree(&nod1);
		} else
			gopcode(o, l->type, l, &nod);
		regfree(&nod);

	com:
		if(nn != Z) {
			p1 = p;
			gmove(nodconst(1L), nn);
			gbranch(OGOTO);
			p2 = p;
			patch(p1, pc);
			gmove(nodconst(0L), nn);
			patch(p2, pc);
		}
		break;
	}
	cursafe = curs;
}

void
sugen(Node *n, Node *nn, long w)
{
	Prog *p1;
	Node nod0, nod1, nod2, nod3, nod4, *l, *r;
	Type *t;
	int c;

	if(n == Z || n->type == T)
		return;
	if(debug['g']) {
		prtree(nn, "sugen lhs");
		prtree(n, "sugen");
	}
	if(nn == nodrat)
		if(w > nrathole)
			nrathole = w;
	switch(n->op) {
	case OIND:
		if(nn == Z) {
			nullwarn(n->left, Z);
			break;
		}

	default:
		goto copy;

	case OCONST:
		if(n->type && typev[n->type->etype]) {
			if(nn == Z) {
				nullwarn(n->left, Z);
				break;
			}

			switch(nn->op) {
			default:
				t = nn->type;
				nn->type = types[TLONG];
				reglcgen(&nod1, nn, Z);
				nn->type = t;

				if(align(0, types[TCHAR], Aarg1))	/* isbigendian */
					gmove(nodconst((long)(n->vconst>>32)), &nod1);
				else
					gmove(nodconst((long)(n->vconst)), &nod1);
				nod1.xoffset += SZ_LONG;
				if(align(0, types[TCHAR], Aarg1))	/* isbigendian */
					gmove(nodconst((long)(n->vconst)), &nod1);
				else
					gmove(nodconst((long)(n->vconst>>32)), &nod1);

				regfree(&nod1);
				break;

			case ONAME:
			case OINDREG:
				if(align(0, types[TCHAR], Aarg1))	/* isbigendian */
					gins(AMOVL, nodconst((long)(n->vconst>>32)), nn);
				else
					gins(AMOVL, nodconst((long)(n->vconst)), nn);
				nn->xoffset += SZ_LONG;
				if(align(0, types[TCHAR], Aarg1))	/* isbigendian */
					gins(AMOVL, nodconst((long)(n->vconst)), nn);
				else
					gins(AMOVL, nodconst((long)(n->vconst>>32)), nn);
				nn->xoffset -= SZ_LONG;
				break;
			}
			break;
		}
		goto copy;

	case ODOT:
		l = n->left;
		sugen(l, nodrat, l->type->width);
		if(nn == Z)
			break;
		warn(n, "non-interruptable temporary");
		nod1 = *nodrat;
		r = n->right;
		if(!r || r->op != OCONST) {
			diag(n, "DOT and no offset");
			break;
		}
		nod1.xoffset += (long)r->vconst;
		nod1.type = n->type;
		sugen(&nod1, nn, w);
		break;

	case OSTRUCT:
		/*
		 * rewrite so lhs has no fn call
		 */
		if(nn != Z && side(nn)) {
			nod1 = *n;
			nod1.type = typ(TIND, n->type);
			regret(&nod2, &nod1);
			lcgen(nn, &nod2);
			regsalloc(&nod0, &nod1);
			cgen(&nod2, &nod0);
			regfree(&nod2);

			nod1 = *n;
			nod1.op = OIND;
			nod1.left = &nod0;
			nod1.right = Z;
			nod1.complex = 1;

			sugen(n, &nod1, w);
			return;
		}

		r = n->left;
		for(t = n->type->link; t != T; t = t->down) {
			l = r;
			if(r->op == OLIST) {
				l = r->left;
				r = r->right;
			}
			if(nn == Z) {
				cgen(l, nn);
				continue;
			}
			/*
			 * hand craft *(&nn + o) = l
			 */
			nod0 = znode;
			nod0.op = OAS;
			nod0.type = t;
			nod0.left = &nod1;
			nod0.right = nil;

			nod1 = znode;
			nod1.op = OIND;
			nod1.type = t;
			nod1.left = &nod2;

			nod2 = znode;
			nod2.op = OADD;
			nod2.type = typ(TIND, t);
			nod2.left = &nod3;
			nod2.right = &nod4;

			nod3 = znode;
			nod3.op = OADDR;
			nod3.type = nod2.type;
			nod3.left = nn;

			nod4 = znode;
			nod4.op = OCONST;
			nod4.type = nod2.type;
			nod4.vconst = t->offset;

			ccom(&nod0);
			acom(&nod0);
			xcom(&nod0);
			nod0.addable = 0;
			nod0.right = l;

			/* prtree(&nod0, "hand craft"); /* */
			cgen(&nod0, Z);
		}
		break;

	case OAS:
		if(nn == Z) {
			if(n->addable < INDEXED)
				sugen(n->right, n->left, w);
			break;
		}

		sugen(n->right, nodrat, w);
		warn(n, "non-interruptable temporary");
		sugen(nodrat, n->left, w);
		sugen(nodrat, nn, w);
		break;

	case OFUNC:
		if(nn == Z) {
			sugen(n, nodrat, w);
			break;
		}
		if(nn->op != OIND) {
			nn = new1(OADDR, nn, Z);
			nn->type = types[TIND];
			nn->addable = 0;
		} else
			nn = nn->left;
		n = new(OFUNC, n->left, new(OLIST, nn, n->right));
		n->type = types[TVOID];
		n->left->type = types[TVOID];
		cgen(n, Z);
		break;

	case OCOND:
		bcgen(n->left, 1);
		p1 = p;
		sugen(n->right->left, nn, w);
		gbranch(OGOTO);
		patch(p1, pc);
		p1 = p;
		sugen(n->right->right, nn, w);
		patch(p1, pc);
		break;

	case OCOMMA:
		cgen(n->left, Z);
		sugen(n->right, nn, w);
		break;
	}
	return;

copy:
	if(nn == Z)
		return;
	if(n->complex >= FNX && nn->complex >= FNX) {
		t = nn->type;
		nn->type = types[TLONG];
		regialloc(&nod1, nn, Z);
		lcgen(nn, &nod1);
		regsalloc(&nod2, nn);
		nn->type = t;

		gins(AMOVL, &nod1, &nod2);
		regfree(&nod1);

		nod2.type = typ(TIND, t);

		nod1 = nod2;
		nod1.op = OIND;
		nod1.left = &nod2;
		nod1.right = Z;
		nod1.complex = 1;
		nod1.type = t;

		sugen(n, &nod1, w);
		return;
	}

	/* most common case of vlong variables */
	if((n->op == ONAME || n->op == OINDREG) &&
	   (nn->op == ONAME || nn->op == OINDREG) && w == 8) {
		n->type = types[TLONG];
		regalloc(&nod1, n, Z);
		gins(AMOVL, n, &nod1);
		gins(AMOVL, &nod1, nn);
		n->xoffset += SZ_LONG;
		nn->xoffset += SZ_LONG;
		gins(AMOVL, n, &nod1);
		gins(AMOVL, &nod1, nn);
		n->xoffset -= SZ_LONG;
		nn->xoffset -= SZ_LONG;
		regfree(&nod1);
		return;
	}

	/* botch, need to save in .safe */
	c = 0;
	if(n->complex > nn->complex) {
		t = n->type;
		n->type = types[TLONG];
		nodreg(&nod1, n, D_SI);
		if(reg[D_SI]) {
			gins(APUSHL, &nod1, Z);
			c |= 1;
			reg[D_SI]++;
		}
		lcgen(n, &nod1);
		n->type = t;

		t = nn->type;
		nn->type = types[TLONG];
		nodreg(&nod2, nn, D_DI);
		if(reg[D_DI]) {
			gins(APUSHL, &nod2, Z);
			c |= 2;
			reg[D_DI]++;
		}
		lcgen(nn, &nod2);
		nn->type = t;
	} else {
		t = nn->type;
		nn->type = types[TLONG];
		nodreg(&nod1, nn, D_DI);
		if(reg[D_DI]) {
			gins(APUSHL, &nod1, Z);
			c |= 1;
			reg[D_DI]++;
		}
		lcgen(nn, &nod1);
		nn->type = t;

		t = n->type;
		n->type = types[TLONG];
		nodreg(&nod2, n, D_SI);
		if(reg[D_SI]) {
			gins(APUSHL, &nod2, Z);
			c |= 2;
			reg[D_SI]++;
		}
		lcgen(n, &nod2);
		n->type = t;
	}
	nodreg(&nod3, n, D_CX);
	if(reg[D_CX]) {
		gins(APUSHL, &nod3, Z);
		c |= 4;
		reg[D_CX]++;
	}
	gins(AMOVL, nodconst(w/SZ_LONG), &nod3);
	gins(ACLD, Z, Z);
	gins(AREP, Z, Z);
	gins(AMOVSL, Z, Z);
	if(c & 4) {
		gins(APOPL, Z, &nod3);
		reg[D_CX]--;
	}
	if(c & 2) {
		gins(APOPL, Z, &nod2);
		reg[nod2.reg]--;
	}
	if(c & 1) {
		gins(APOPL, Z, &nod1);
		reg[nod1.reg]--;
	}
}
