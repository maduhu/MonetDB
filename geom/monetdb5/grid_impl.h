/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2016 MonetDB B.V.
 */

/*
 * @a Kostis Kyzirakos, Foteini Alvanaki
 * @* A grid based index
 */

#define CONCAT2(a,b)   a##_##b
#define CONCAT3(a,b,c) a##_##b##_##c
#define U2(a,b)        CONCAT2(a,b)
#define U3(a,b,c)      CONCAT3(a,b,c)

static BUN
U3(distance,TP1,TP2) (const TP1 *lft, int incr1,
				const TP2 *rgt, int incr2,
				TP3 *restrict dst, TP3 max,
				BUN cnt, BUN start,
				BUN end, const oid *restrict cand,
				const oid *candend, oid candoff,
				int abort_on_error)
{
	BUN i, j, k;
	BUN nils = 0;

	CANDLOOP(dst, k, U2(TP3,nil), 0, start);
	for (i = start * incr1, j = start * incr2, k = start;
		k < end; i += incr1, j += incr2, k++) {
		CHECKCAND(dst, k, candoff, U2(TYPE3,nil));
		if (lft[i] == U2(TP1,nil) || rgt[j] == U2(TP2,nil)) {
			dst[k] = U2(TP3,nil);
			nils++;
		} else {
		ADD_WITH_CHECK(TP1, lft[i],
					   TP2, rgt[j],
					   TP3, dst[k],
					   max,
					   ON_OVERFLOW(TP1, TP2, "+"));
		}
	}
	CANDLOOP(dst, k, U2(TYPE3,nil), end, cnt);
	return nils;
}

#if 0
str
U3(GRIDdistance,TP1,TP2) (bit * res, TP1 * x1, TP1 * y1, TP2 * x2, TP2 * y2, dbl * d)
{
	str r = MAL_SUCCEED;

	if ((res = GDKmalloc(sizeof(bit))) == NULL)
		r = createException(MAL, "grid.distance", MAL_MALLOC_FAIL);
	else {
		*res = ((*x2-*x1)*(*x2-*x1)+(*y2-*y1)*(*y2-*y1))<(*d)*(*d);
	}

	return r;
}
#endif


str
U3(GRIDdistancesubselect,TP1,TP2) (bat * res, bat * x1, bat * y1, bat * cand1, TP2 * x2, TP2 * y2, dbl * d, bit * anti)
{
	size_t minCellx, minCelly, maxCellx, maxCelly, cellx, celly;
	size_t *borderCells, *internalCells;
	size_t borderCellsNum, internalCellsNum, totalCellsNum;
	size_t i, j, bvsize, num;
	uint64_t * bv, * cbv;
	double fxa, fxb, fya, fyb;
	BAT *x1BAT = NULL, *y1BAT = NULL, *cBAT = NULL;
	TP1 * x1Vals = NULL, * y1Vals = NULL;
	oid * resVals = NULL;
	oid seq;
	Grid * g = NULL;
	mbr mbb;
	BAT *r;
	BUN p, q;
	BATiter pi;
	BUN resNum = 0;
	str msg = MAL_SUCCEED;
	assert (*d > 0);

	/* get the X and Y BATs*/
	if((x1BAT = BATdescriptor(*x1)) == NULL) {
		msg = createException(MAL, "grid.distance", "runtime object missing");
		goto distanceselect_fail;
	}
	if((y1BAT = BATdescriptor(*y1)) == NULL) {
		msg = createException(MAL, "grid.distance", "runtime object missing");
		goto distanceselect_fail;
	}
	if((cBAT = BATdescriptor(*cand1)) == NULL) {
		msg = createException(MAL, "grid.distance", "runtime object missing");
		goto distanceselect_fail;
	}

	/* check if the BATs have dense heads and are aligned */
	if (!BAThdense(x1BAT) || !BAThdense(y1BAT)) {
		msg = createException(MAL, "grid.distance", "BATs must have dense heads");
		goto distanceselect_fail;
	}
	if(x1BAT->hseqbase != y1BAT->hseqbase) {
		msg = createException(MAL, "grid.distance", "BATs must be aligned");
		goto distanceselect_fail;
	}
	num = BATcount(x1BAT);
	seq = x1BAT->hseqbase;

	assert( x1BAT->ttype == U2(TYPE,TP1) );
	assert( y1BAT->ttype == U2(TYPE,TP1) );
	x1Vals = (TP1*)Tloc(x1BAT, BUNfirst(x1BAT));
	y1Vals = (TP1*)Tloc(y1BAT, BUNfirst(y1BAT));

	/* initialize the bit vectors */
	bvsize = (num >> SHIFT) + ((num & ONES) > 0);
	if ((bv = GDKmalloc(bvsize * sizeof(uint64_t))) == NULL)
		throw(MAL, "grid.distance", MAL_MALLOC_FAIL);
	for (i = 0; i < bvsize; i++)
		bv[i] = 0;

	if ((cbv = GDKmalloc(bvsize * sizeof(uint64_t))) == NULL) {
		GDKfree(bv);
		throw(MAL, "grid.distance", MAL_MALLOC_FAIL);
	}
	for (i = 0; i < bvsize; i++)
		cbv[i] = 0;

	pi = bat_iterator(cBAT);
	BATloop(cBAT, p, q) {
		oid o = *(oid*)BUNtail(pi, p) - seq;
		size_t blockNum = o >> SHIFT;
		uint64_t bitPos = o & ONES;
		set(cbv[blockNum], bitPos, 1);
	}
	BBPunfix(cBAT->batCacheid);

	/* compute the grid index */
	if((g = U2(grid_create,TP1)(x1BAT, y1BAT)) == NULL) {
		msg = createException(MAL, "grid.distance", "Could not compute the grid index");
		goto distanceselect_fail;
	}

	/* find which cells have to be examined */
	fxa = ((double)g->cellsPerAxis/(double)(g->xmax-g->xmin));
	fxb = (double)g->xmin*fxa; 
	fya = ((double)g->cellsPerAxis/(double)(g->ymax-g->ymin));
	fyb = (double)g->ymin*fya; 
	//mbb = (mbr) {.xmin = 0, .ymin = 0, .xmax = 0, .ymax = 0}
	mbb = (mbr) { .xmin = *x2 - *d, .ymin = *y2 - *d, .xmax = *x2 + *d, .ymax = *y2 + *d};
	if (mbb.xmin > g->xmax || mbb.xmax < g->xmin ||
		mbb.ymin > g->ymax || mbb.ymax < g->ymin) {
		/* no results */
		if ((r = BATnew(TYPE_void, TYPE_oid, 0, TRANSIENT)) == NULL) {
			msg = createException(MAL, "grid.distance", "could not create a BAT for storing the results");
			goto distanceselect_fail;
		}
		*res = r->batCacheid;
		BBPkeepref(*res);
		goto distanceselect_clean;
	}

	mbb.xmin = (mbb.xmin < g->xmin) ? g->xmin : mbb.xmin;
	mbb.xmax = (mbb.xmax > g->xmax) ? g->xmax : mbb.xmax; 
	mbb.ymin = (mbb.ymin < g->ymin) ? g->ymin : mbb.ymin;
	mbb.ymax = (mbb.ymax > g->ymax) ? g->ymax : mbb.ymax; 

	minCellx = (double)(mbb.xmin)*fxa - fxb;
	maxCellx = (double)(mbb.xmax)*fxa - fxb;
	minCelly = (double)(mbb.ymin)*fya - fyb;
	maxCelly = (double)(mbb.ymax)*fya - fyb;

	/* split the cells in border and internal ones */
	totalCellsNum = (maxCellx - minCellx + 1)*(maxCelly - minCelly + 1);
	borderCellsNum = (maxCellx - minCellx + 1) + (maxCelly - minCelly + 1) - 1; /* per axis, remove the corner cell that has been added twice */
	if(maxCellx > minCellx && maxCelly > minCelly)
		borderCellsNum = borderCellsNum*2 - 2; /* subtract the two corner cells that have been added twice */
	internalCellsNum = totalCellsNum - borderCellsNum;

	if((borderCells = (size_t*)GDKmalloc((borderCellsNum + 1) * sizeof(size_t*))) == NULL) {
		msg = createException(MAL, "grid.distance", "malloc failed");
		goto distanceselect_fail;
	}
	if((internalCells = (size_t*)GDKmalloc((internalCellsNum + 1) * sizeof(size_t*))) == NULL) {
		msg = createException(MAL, "grid.distance", "malloc failed");
		goto distanceselect_fail;
	}

	borderCellsNum = 0;
	internalCellsNum = 0;
	for(cellx = minCellx ; cellx <= maxCellx; cellx++) {
		for(celly = minCelly ; celly <= maxCelly ; celly++) {
			size_t cellId = (cellx << g->shift) | celly;
			unsigned short border = (cellx == minCellx) | (cellx == maxCellx) | (celly == minCelly) | (celly == maxCelly);
			borderCells[borderCellsNum] = cellId;
			internalCells[internalCellsNum] = cellId;
			borderCellsNum += border;
			internalCellsNum += 1 - border;
		}
	}

	/* process cells along the border */
	for (i = 0; i < borderCellsNum; i++) {
		size_t cellId = borderCells[i];
		size_t offsetStartIdx = g->dir[cellId];
		size_t offsetEndIdx = g->dir[cellId+1]; /* exclusive */
		for(j = offsetStartIdx; j < offsetEndIdx; j++) {
			size_t o = g->oids[j];
			size_t blockNum = o >> SHIFT;
			uint64_t bitPos = o & ONES;
			size_t mask = (mbb.xmin <= x1Vals[o]) & (mbb.xmax >= x1Vals[o]) & 
				(mbb.ymin <= y1Vals[o]) & (mbb.ymax >= y1Vals[o]);
			set(bv[blockNum], bitPos, mask);
		}
	} 

	GDKfree(borderCells);
	BBPunfix(x1BAT->batCacheid);
	BBPunfix(y1BAT->batCacheid);

	/* process internal cells */
	for (i = 0; i < internalCellsNum; i++) {
		size_t cellId = internalCells[i];
		size_t offsetStartIdx = g->dir[cellId];
		size_t offsetEndIdx = g->dir[cellId+1]; /* exclusive */
		for(j = offsetStartIdx; j < offsetEndIdx; j++) {
			size_t o = g->oids[j];
			size_t blockNum = o >> SHIFT;
			uint64_t bitPos = o & ONES;
			set(bv[blockNum], bitPos, 0x01);
		}
	}
	GDKfree(internalCells);
	GDKfree(g->oids);
	GDKfree(g->dir);
	GDKfree(g);

	/* anti */
	if (*anti) {
		for (i = 0; i < bvsize; i++)
			bv[i] = ~bv[i];
		for (i = num % BITSNUM; i < BITSNUM; i++)
			set(bv[bvsize-1], i, 0);
	}

	/* & the bit vectors*/
	for (i = 0; i < bvsize; i++)
		bv[i] &= cbv[i];
	GDKfree(cbv);

	/* allocate a BAT for the results */
	resNum = countSetBits(bv, bvsize);
	if ((r = BATnew(TYPE_void, TYPE_oid, resNum+1, TRANSIENT)) == NULL) {
		msg = createException(MAL, "grid.distance", "could not create a BAT for storing the results");
		goto distanceselect_fail;
	}

	/* project the bit vector */
	resVals = (oid*)Tloc(r, BUNfirst(r));
	j = 0;
	for(i = 0; i < bvsize; i++) {
		uint64_t b = bv[i];
		oid o = i * BITSNUM + seq;
		for(short l = 0; l < BITSNUM; l++) {
			resVals[j] = o;
			j += b & 0x01;
			b >>= 1;
			o++;
		}
	}

	//BATderiveProps(r, false);
	BATsetcount(r, resNum);
	//BATseqbase(r, 0);
	r->tsorted = true;
	r->trevsorted = false;
	*res = r->batCacheid;
	BBPkeepref(*res);

distanceselect_clean:
	BBPunfix(x1BAT->batCacheid);
	BBPunfix(y1BAT->batCacheid);
	BBPunfix(cBAT->batCacheid);
	if (g) {
		if (g->oids) GDKfree(g->oids);
		if (g->dir) GDKfree(g->dir);
		GDKfree(g);
	}
	if (cbv) GDKfree(cbv);
	if (bv)  GDKfree(bv);
	if (borderCells)   GDKfree(borderCells);
	if (internalCells) GDKfree(internalCells);

	return msg;

distanceselect_fail:
	if ((r = BATnew(TYPE_void, TYPE_oid, 0, TRANSIENT)) == NULL) {
		msg = createException(MAL, "grid.distance", "could not create a BAT for storing the results");
		*res = 0;
	} else {
		*res = r->batCacheid;
		BBPkeepref(*res);
	}
	
	goto distanceselect_clean;
}

#undef U2
#undef U3
#undef CONCAT2
#undef CONCAT3
