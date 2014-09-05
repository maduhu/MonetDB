/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 *                * Copyright August 2008-2014 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * (c)2014 author Martin Kersten
 * Byte-wise delta encoding for SHT,INT,LNG,TIMESTAMP
 */

#include "monetdb_config.h"
#include "mosaic.h"
#include "mosaic_delta.h"

void
MOSadvance_delta(Client cntxt, MOStask task)
{
	MosaicBlk blk = task->blk;
	(void) cntxt;

	task->start += MOSgetCnt(blk);
	switch(task->type){
	//case TYPE_bte: case TYPE_bit: no compression achievable
	case TYPE_sht: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(sht) + MOSgetCnt(blk)-1,sht)); break ;
	case TYPE_int: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(int) + MOSgetCnt(blk)-1,int)); break ;
	case TYPE_oid: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(oid) + MOSgetCnt(blk)-1,oid)); break ;
	case TYPE_wrd: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(wrd) + MOSgetCnt(blk)-1,wrd)); break ;
	case TYPE_lng: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(lng) + MOSgetCnt(blk)-1,lng)); break ;
	//case TYPE_flt: case TYPE_dbl: to be looked into.
#ifdef HAVE_HGE
	case TYPE_hge: task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(hge) + MOSgetCnt(blk)-1,hge)); break ;
#endif
	default:
		if( task->type == TYPE_timestamp)
			task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(timestamp) + MOSgetCnt(blk)-1,timestamp)); 
		if( task->type == TYPE_date)
			task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(date) + MOSgetCnt(blk)-1,date)); 
		if( task->type == TYPE_daytime)
			task->blk = (MosaicBlk)( ((char*) blk) + MosaicBlkSize + wordaligned(sizeof(daytime) + MOSgetCnt(blk)-1,daytime)); 
	}
}


void
MOSdump_delta(Client cntxt, MOStask task)
{
	MosaicBlk blk = task->blk;
	mnstr_printf(cntxt->fdout,"#delta "BUNFMT"\n", MOSgetCnt(blk));
}

void
MOSskip_delta(Client cntxt, MOStask task)
{
	MOSadvance_delta(cntxt, task);
	if ( MOSgetTag(task->blk) == MOSAIC_EOL)
		task->blk = 0; // ENDOFLIST
}

// append a series of values into the non-compressed block
#define Estimate_delta(TYPE, EXPR)\
{	TYPE *w = (TYPE*)task->src, val= *w, delta;\
	for(w++,i =1; i<task->elm; i++,w++){\
		delta = *w -val;\
		if ( EXPR)\
		if ( delta < -127 || delta >127)\
			break;\
		val = *w;\
	}\
	if ( i > MOSlimit() ) i = MOSlimit();\
	factor = ((float) i * sizeof(TYPE))/  (MosaicBlkSize + sizeof(TYPE)+(bte)i-1);\
}

// estimate the compression level 
flt
MOSestimate_delta(Client cntxt, MOStask task)
{	BUN i = 0;
	flt factor = 1.0;
	(void) cntxt;

	switch(ATOMstorage(task->type)){
	//case TYPE_bte: case TYPE_bit: no compression achievable
	case TYPE_sht: Estimate_delta(sht,  (delta < -127 || delta >127)); break;
	case TYPE_oid: Estimate_delta(sht,  (delta < 256)); break;
	case TYPE_wrd: Estimate_delta(wrd,  (delta < -127 || delta >127)); break;
	case TYPE_lng: Estimate_delta(lng,  (delta < -127 || delta >127)); break;
#ifdef HAVE_HGE
	case TYPE_hge: Estimate_delta(hge,  (delta < -127 || delta >127)); break;
#endif
	case TYPE_int:
		{	int *w = (int*)task->src, val= *w, delta;
			for(w++,i =1; i<task->elm; i++,w++){
				delta = *w -val;
				if ( delta < -127 || delta >127)
					break;
				val = *w;
			}
			factor = ((float) i * sizeof(int))/  (MosaicBlkSize + sizeof(int)+(bte)i-1);
		}
		break;
	//case TYPE_flt: case TYPE_dbl: to be looked into.
	}
#ifdef _DEBUG_MOSAIC_
	mnstr_printf(cntxt->fdout,"#estimate delta "BUNFMT" elm %.3f factor\n",i,factor);
#endif
	return factor;
}

#define DELTAcompress(TYPE,EXPR)\
{	TYPE *w = (TYPE*)task->src, val= *w, delta;\
	BUN limit = task->elm > MOSlimit()? MOSlimit():task->elm;\
	task->dst = ((char*) task->blk) + MosaicBlkSize;\
	*(TYPE*)task->dst = val;\
	task->dst += sizeof(TYPE);\
	for(w++,i =1; i<limit; i++,w++){\
		delta = *w -val;\
		if ( EXPR )\
			break;\
		*(bte*)task->dst++ = (bte) delta;\
		val = *w;\
	}\
	task->src += i * sizeof(TYPE);\
	MOSincCnt(blk,i);\
}

// rather expensive simple value non-compressed store
void
MOScompress_delta(Client cntxt, MOStask task)
{
	MosaicBlk blk = (MosaicBlk) task->blk;
	BUN i = 0;

	(void) cntxt;
	MOSsetTag(blk,MOSAIC_DELTA);

	switch(ATOMstorage(task->type)){
	//case TYPE_bte: case TYPE_bit: no compression achievable
	case TYPE_sht: DELTAcompress(sht,(delta < -127 || delta >127)); break;
	case TYPE_lng: DELTAcompress(lng,(delta < -127 || delta >127)); break;
	case TYPE_oid: DELTAcompress(oid,(delta < 256)); break;
	case TYPE_wrd: DELTAcompress(wrd,(delta < -127 || delta >127)); break;
#ifdef HAVE_HGE
	case TYPE_hge: DELTAcompress(hge,(delta < -127 || delta >127)); break;
#endif
	case TYPE_int:
		{	int *w = (int*)task->src, val= *w, delta;
			BUN limit = task->elm > MOSlimit()? MOSlimit():task->elm;
			task->dst = ((char*) task->blk) + MosaicBlkSize;
			*(int*)task->dst = val;
			task->dst += sizeof(int);
			for(w++,i =1; i<limit; i++,w++){
				delta = *w -val;
				if ( delta < -127 || delta >127)
					break;
				*(bte*)task->dst++ = (bte) delta;
				val = *w;
			}
			task->src += i * sizeof(int);
			MOSincCnt(blk,i);
		}
		break;
	//case TYPE_flt: case TYPE_dbl: to be looked into.
	}
#ifdef _DEBUG_MOSAIC_
	MOSdump_delta(cntxt, task);
#endif
}

// the inverse operator, extend the src
#define DELTAdecompress(TYPE)\
{ 	TYPE val;\
	BUN lim = MOSgetCnt(blk);\
	task->dst = ((char*) task->blk) + MosaicBlkSize;\
	val = *(TYPE*)task->dst ;\
	task->dst += sizeof(TYPE);\
	for(i = 0; i < lim; i++) {\
		((TYPE*)task->src)[i] = val;\
		val += (bte) *task->dst++;\
	}\
	task->src += i * sizeof(TYPE);\
}

void
MOSdecompress_delta(Client cntxt, MOStask task)
{
	MosaicBlk blk = (MosaicBlk) task->blk;
	BUN i;
	(void) cntxt;

	switch(ATOMstorage(task->type)){
	//case TYPE_bte: case TYPE_bit: no compression achievable
	case TYPE_sht: DELTAdecompress(sht); break;
	case TYPE_lng: DELTAdecompress(lng); break;
	case TYPE_oid: DELTAdecompress(oid); break;
	case TYPE_wrd: DELTAdecompress(wrd); break;
#ifdef HAVE_HGE
	case TYPE_hge: DELTAdecompress(hge); break;
#endif
	case TYPE_int:
	{ 	int val;
		BUN lim = MOSgetCnt(blk);
		task->dst = ((char*) task->blk) + MosaicBlkSize;
		val = *(int*)task->dst ;
		task->dst += sizeof(int);
		for(i = 0; i < lim; i++) {
			((int*)task->src)[i] = val;
			val += *(bte*) task->dst++;
		}
		task->src += i * sizeof(int);
	}
	}
}

// The remainder should provide the minimal algebraic framework
//  to apply the operator to a DELTA compressed chunk

	
#define subselect_delta(TPE) {\
		TPE val= * (TPE*) (((char*) task->blk) + MosaicBlkSize);\
		task->dst = ((char*)task->blk)+ MosaicBlkSize + sizeof(TYPE);\
		if( !*anti){\
			if( *(TPE*) low == TPE##_nil && *(TPE*) hgh == TPE##_nil){\
				for( ; first < last; first++){\
					MOSskipit();\
					*o++ = (oid) first;\
				}\
			} else\
			if( *(TPE*) low == TPE##_nil ){\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*hi && val <= * (TPE*)hgh ) || (!*hi && val < *(TPE*)hgh ));\
					if (cmp )\
						*o++ = (oid) first;\
				}\
			} else\
			if( *(TPE*) hgh == TPE##_nil ){\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*li && val >= * (TPE*)low ) || (!*li && val > *(TPE*)low ));\
					if (cmp )\
						*o++ = (oid) first;\
				}\
			} else{\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*hi && val <= * (TPE*)hgh ) || (!*hi && val < *(TPE*)hgh )) &&\
							((*li && val >= * (TPE*)low ) || (!*li && val > *(TPE*)low ));\
					if (cmp )\
						*o++ = (oid) first;\
				}\
			}\
		} else {\
			if( *(TPE*) low == TPE##_nil && *(TPE*) hgh == TPE##_nil){\
				/* nothing is matching */\
			} else\
			if( *(TPE*) low == TPE##_nil ){\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*hi && val <= * (TPE*)hgh ) || (!*hi && val < *(TPE*)hgh ));\
					if ( !cmp )\
						*o++ = (oid) first;\
				}\
			} else\
			if( *(TPE*) hgh == TPE##_nil ){\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*li && val >= * (TPE*)low ) || (!*li && val > *(TPE*)low ));\
					if ( !cmp )\
						*o++ = (oid) first;\
				}\
			} else{\
				for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){\
					MOSskipit();\
					cmp  =  ((*hi && val <= * (TPE*)hgh ) || (!*hi && val < *(TPE*)hgh )) &&\
							((*li && val >= * (TPE*)low ) || (!*li && val > *(TPE*)low ));\
					if ( !cmp )\
						*o++ = (oid) first;\
				}\
			}\
		}\
	}

str
MOSsubselect_delta(Client cntxt,  MOStask task, void *low, void *hgh, bit *li, bit *hi, bit *anti)
{
	oid *o;
	BUN first,last;
	int cmp;
	(void) cntxt;

	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	if (task->cl && *task->cl > last){
		MOSskip_delta(cntxt,task);
		return MAL_SUCCEED;
	}
	o = task->lb;

	switch(task->type){
	case TYPE_sht: subselect_delta(sht); break;
	case TYPE_lng: subselect_delta(lng); break;
	case TYPE_oid: subselect_delta(oid); break;
	case TYPE_wrd: subselect_delta(wrd); break;
#ifdef HAVE_HGE
	case TYPE_hge: subselect_delta(hge); break;
#endif
	case TYPE_int:
	// Expanded MOSselect_delta for debugging
		{ 	int val= *(int*) (((char*) task->blk) + MosaicBlkSize);
			task->dst = ((char *)task->blk) +MosaicBlkSize + sizeof(int);

			if( !*anti){
				if( *(int*) low == int_nil && *(int*) hgh == int_nil){
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						*o++ = (oid) first;
					}
				} else
				if( *(int*) low == int_nil ){
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*hi && val <= * (int*)hgh ) || (!*hi && val < *(int*)hgh ));
						if (cmp )
							*o++ = (oid) first;
					}
				} else
				if( *(int*) hgh == int_nil ){
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*li && val >= * (int*)low ) || (!*li && val > *(int*)low ));
						if (cmp )
							*o++ = (oid) first;
					}
				} else{
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*hi && val <= * (int*)hgh ) || (!*hi && val < *(int*)hgh )) &&
								((*li && val >= * (int*)low ) || (!*li && val > *(int*)low ));
						if (cmp )
							*o++ = (oid) first;
					}
				}
			} else {
				if( *(int*) low == int_nil && *(int*) hgh == int_nil){
					/* nothing is matching */
				} else
				if( *(int*) low == int_nil ){
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*hi && val <= * (int*)hgh ) || (!*hi && val < *(int*)hgh ));
						if ( !cmp )
							*o++ = (oid) first;
					}
				} else
				if( *(int*) hgh == int_nil ){
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*li && val >= * (int*)low ) || (!*li && val > *(int*)low ));
						if ( !cmp )
							*o++ = (oid) first;
					}
				} else{
					for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
						MOSskipit();
						cmp  =  ((*hi && val <= * (int*)hgh ) || (!*hi && val < *(int*)hgh )) &&
								((*li && val >= * (int*)low ) || (!*li && val > *(int*)low ));
						if ( !cmp )
							*o++ = (oid) first;
					}
				}
			}
		}
			break;
		default:
			if( task->type == TYPE_daytime)
				subselect_delta(daytime);
			if( task->type == TYPE_date)
				subselect_delta(date);
			if( task->type == TYPE_timestamp)
				{ 	lng val= *(lng*) (((char*) task->blk) + MosaicBlkSize);
					int lownil = timestamp_isnil(*(timestamp*)low);
					int hghnil = timestamp_isnil(*(timestamp*)hgh);

					if( !*anti){
						if( lownil && hghnil){
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								*o++ = (oid) first;
							}
						} else
						if( lownil){
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*hi && val <= * (lng*)hgh ) || (!*hi && val < *(lng*)hgh ));
								if (cmp )
									*o++ = (oid) first;
							}
						} else
						if( hghnil){
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*li && val >= * (lng*)low ) || (!*li && val > *(lng*)low ));
								if (cmp )
									*o++ = (oid) first;
							}
						} else{
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*hi && val <= * (lng*)hgh ) || (!*hi && val < *(lng*)hgh )) &&
										((*li && val >= * (lng*)low ) || (!*li && val > *(lng*)low ));
								if (cmp )
									*o++ = (oid) first;
							}
						}
					} else {
						if( lownil && hghnil){
							/* nothing is matching */
						} else
						if( lownil){
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*hi && val <= * (lng*)hgh ) || (!*hi && val < *(lng*)hgh ));
								if ( !cmp )
									*o++ = (oid) first;
							}
						} else
						if( hghnil){
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*li && val >= * (lng*)low ) || (!*li && val > *(lng*)low ));
								if ( !cmp )
									*o++ = (oid) first;
							}
						} else{
							for( ; first < last; first++, val+= *(bte*)task->dst, task->dst++){
								MOSskipit();
								cmp  =  ((*hi && val <= * (lng*)hgh ) || (!*hi && val < *(lng*)hgh )) &&
										((*li && val >= * (lng*)low ) || (!*li && val > *(lng*)low ));
								if ( !cmp )
									*o++ = (oid) first;
							}
						}
					}
				}
	}
	MOSskip_delta(cntxt,task);
	task->lb = o;
	return MAL_SUCCEED;
}

#define thetasubselect_delta(TPE)\
{ 	TPE low,hgh, v;\
	low= hgh = TPE##_nil;\
	if ( strcmp(oper,"<") == 0){\
		hgh= *(TPE*) val;\
		hgh = PREVVALUE##TPE(hgh);\
	} else\
	if ( strcmp(oper,"<=") == 0){\
		hgh= *(TPE*) val;\
	} else\
	if ( strcmp(oper,">") == 0){\
		low = *(TPE*) val;\
		low = NEXTVALUE##TPE(low);\
	} else\
	if ( strcmp(oper,">=") == 0){\
		low = *(TPE*) val;\
	} else\
	if ( strcmp(oper,"!=") == 0){\
		hgh= low= *(TPE*) val;\
		anti++;\
	} else\
	if ( strcmp(oper,"==") == 0){\
		hgh= low= *(TPE*) val;\
	} \
	v= *(TPE*) (((char*) task->blk) + MosaicBlkSize);\
	task->dst = ((char *)task->blk) +MosaicBlkSize + sizeof(int);\
	for( ; first < last; first++, v+= *(bte*)task->dst, task->dst++){\
		if( (low == TPE##_nil || v >= low) && (v <= hgh || hgh == TPE##_nil) ){\
			if ( !anti) {\
				MOSskipit();\
				*o++ = (oid) first;\
			}\
		} else\
		if( anti){\
			MOSskipit();\
			*o++ = (oid) first;\
		}\
	}\
} 

str
MOSthetasubselect_delta(Client cntxt,  MOStask task, void *val, str oper)
{
	oid *o;
	int anti=0;
	BUN first,last;
	(void) cntxt;
	
	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	if (task->cl && *task->cl > last){
		MOSskip_delta(cntxt,task);
		return MAL_SUCCEED;
	}
	o = task->lb;

	switch(task->type){
	case TYPE_sht: thetasubselect_delta(sht); break;
	case TYPE_lng: thetasubselect_delta(lng); break;
	case TYPE_oid: thetasubselect_delta(oid); break;
	case TYPE_wrd: thetasubselect_delta(wrd); break;
#ifdef HAVE_HGE
	case TYPE_hge: thetasubselect_delta(hge); break;
#endif
	case TYPE_int:
		{ 	int low,hgh, v;
			low= hgh = int_nil;
			if ( strcmp(oper,"<") == 0){
				hgh= *(int*) val;
				hgh = PREVVALUEint(hgh);
			} else
			if ( strcmp(oper,"<=") == 0){
				hgh= *(int*) val;
			} else
			if ( strcmp(oper,">") == 0){
				low = *(int*) val;
				low = NEXTVALUEint(low);
			} else
			if ( strcmp(oper,">=") == 0){
				low = *(int*) val;
			} else
			if ( strcmp(oper,"!=") == 0){
				hgh= low= *(int*) val;
				anti++;
			} else
			if ( strcmp(oper,"==") == 0){
				hgh= low= *(int*) val;
			} 
		 	v= *(int*) (((char*) task->blk) + MosaicBlkSize);
			task->dst = ((char *)task->blk) +MosaicBlkSize + sizeof(int);
			for( ; first < last; first++, v+= *(bte*)task->dst, task->dst++){
				if( (low == int_nil || v >= low) && (v <= hgh || hgh == int_nil) ){
					if ( !anti) {
						MOSskipit();
						*o++ = (oid) first;
					}
				} else
				if( anti){
					MOSskipit();
					*o++ = (oid) first;
				}
			}
		} 
		break;
	default:
			if( task->type == TYPE_daytime)
				thetasubselect_delta(daytime); 
			if( task->type == TYPE_date)
				thetasubselect_delta(date); 
			if( task->type == TYPE_timestamp)
				thetasubselect_delta(lng); 
	}
	MOSskip_delta(cntxt,task);
	task->lb =o;
	return MAL_SUCCEED;
}

#define leftfetchjoin_delta(TPE)\
{	TPE val, *v;\
	bte *delta;\
	v= (TPE*) task->src;\
	delta = (bte*) (((char*)task->blk + MosaicBlkSize) + sizeof(TPE));\
	for(; first < last; first++, delta++, val+= *delta){\
		MOSskipit();\
		*v++ = val;\
		task->n--;\
	}\
	task->src = (char*) v;\
}

str
MOSleftfetchjoin_delta(Client cntxt,  MOStask task)
{
	BUN first, last;
	(void) cntxt;

	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	switch(task->type){
		case TYPE_sht: leftfetchjoin_delta(sht); break;
		case TYPE_lng: leftfetchjoin_delta(lng); break;
		case TYPE_oid: leftfetchjoin_delta(oid); break;
		case TYPE_wrd: leftfetchjoin_delta(wrd); break;
#ifdef HAVE_HGE
		case TYPE_hge: leftfetchjoin_delta(hge); break;
#endif
		case TYPE_int:
		{	int val, *v;
			bte *delta;
			v= (int*) task->src;
			val = *(int*) (((char*) task->blk) + MosaicBlkSize);
			delta = (bte*) (((char*)task->blk + MosaicBlkSize) + sizeof(int));
			for(; first < last; first++, val+= *delta, delta++){
				MOSskipit();
				*v++ = val;
				task->n--;
			}
			task->src = (char*) v;
		}
		break;
		default:
			if (task->type == TYPE_daytime)
				leftfetchjoin_delta(daytime); 
			if (task->type == TYPE_date)
				leftfetchjoin_delta(date); 
			if (task->type == TYPE_timestamp)
				leftfetchjoin_delta(lng); 
	}
	MOSskip_delta(cntxt,task);
	return MAL_SUCCEED;
}

#define join_delta(TPE)\
{	TPE *v, *w;\
	v = (TPE*) (((char*) task->blk) + MosaicBlkSize);\
	for(oo= (oid) first; first < last; first++, v++, oo++){\
		w = (TPE*) task->src;\
		for(n = task->elm, o = 0; n -- > 0; w++,o++)\
		if ( *w == *v){\
			BUNappend(task->lbat, &oo, FALSE);\
			BUNappend(task->rbat, &o, FALSE);\
		}\
	}\
}

str
MOSjoin_delta(Client cntxt,  MOStask task)
{
	BUN n, first, last;
	oid o, oo;
	(void) cntxt;

	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	switch(task->type){
		case TYPE_sht: join_delta(sht); break;
		case TYPE_lng: join_delta(lng); break;
		case TYPE_oid: join_delta(oid); break;
		case TYPE_wrd: join_delta(wrd); break;
#ifdef HAVE_HGE
		case TYPE_hge: join_delta(hge); break;
#endif
		case TYPE_int:
		{	int *w,base;
			bte *v;
			base = *(int*) (((char*) task->blk) + MosaicBlkSize);
			v = (bte*) (((char*) task->blk) + MosaicBlkSize + sizeof(int));
			for(oo= (oid) first; first < last; first++, base += *v,v++, oo++){
				w = (int*) task->src;
				for(n = task->elm, o = 0; n -- > 0; w++,o++)
				if ( *w == base){
					BUNappend(task->lbat, &oo, FALSE);
					BUNappend(task->rbat, &o, FALSE);
				}
			}
		}
		break;
		default:
			if (task->type == TYPE_daytime)
				join_delta(daytime); 
			if (task->type == TYPE_date)
				join_delta(date); 
			if (task->type == TYPE_timestamp)
				join_delta(lng); 
	}
	MOSskip_delta(cntxt,task);
	return MAL_SUCCEED;
}
