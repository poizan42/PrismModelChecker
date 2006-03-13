//==============================================================================
//	
//	Copyright (c) 2002-2004, Dave Parker
//	
//	This file is part of PRISM.
//	
//	PRISM is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//	
//	PRISM is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//	
//	You should have received a copy of the GNU General Public License
//	along with PRISM; if not, write to the Free Software Foundation,
//	Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//	
//==============================================================================

// includes
#include "PrismHybrid.h"
#include <math.h>
#include <util.h>
#include <cudd.h>
#include <dd.h>
#include <odd.h>
#include <dv.h>
#include "sparse.h"
#include "hybrid.h"
#include "PrismHybridGlob.h"

// local prototypes
static void power_rec(HDDNode *hdd, int level, int row_offset, int col_offset, bool transpose);
static void power_rm(RMSparseMatrix *rmsm, int row_offset, int col_offset);
static void power_cmsr(CMSRSparseMatrix *cmsrsm, int row_offset, int col_offset);

// globals (used by local functions)
static HDDNode *zero;
static int num_levels;
static bool compact_sm;
static double *sm_dist;
static int sm_dist_shift;
static int sm_dist_mask;
static double *soln, *soln2;

//------------------------------------------------------------------------------

// solve the linear equation system Ax=x with the Power method
// in addition, solutions may be provided for additional states in the vector b
// these states are assumed not to have non-zero rows in the matrix A

jint JNICALL Java_hybrid_PrismHybrid_PH_1Power
(
JNIEnv *env,
jclass cls,
jint _odd,			// odd
jint rv,			// row vars
jint num_rvars,
jint cv,			// col vars
jint num_cvars,
jint _a,			// matrix A
jint _b,			// vector b (if null, assume all zero)
jint _init,			// init soln
jboolean transpose	// transpose A? (i.e. solve xA=x not Ax=x?)
)
{
	// cast function parameters
	ODDNode *odd = (ODDNode *)_odd;		// odd
	DdNode **rvars = (DdNode **)rv; 	// row vars
	DdNode **cvars = (DdNode **)cv; 	// col vars
	DdNode *a = (DdNode *)_a;			// matrix A
	DdNode *b = (DdNode *)_b;			// vector b
	DdNode *init = (DdNode *)_init;		// init soln
	// model stats
	int n;
	long nnz;
	// flags
	bool compact_b;
	// matrix mtbdd
	HDDMatrix *hddm;
	HDDNode *hdd;
	// vectors
	double *b_vec, *tmpsoln;
	DistVector *b_dist;
	// timing stuff
	long start1, start2, start3, stop;
	double time_taken, time_for_setup, time_for_iters;
	// misc
	int i, j, l, h, iters;
	double d, kb, kbt;
	bool done;
	
	// start clocks
	start1 = start2 = util_cpu_time();
	
	// get number of states
	n = odd->eoff + odd->toff;
	
	// make local copy of a
	Cudd_Ref(a);
	
	// build hdd for matrix
	PH_PrintToMainLog(env, "\nBuilding hybrid MTBDD matrix... ");
	hddm = build_hdd_matrix(a, rvars, cvars, num_rvars, odd, true, transpose);
	hdd = hddm->top;
	zero = hddm->zero;
	num_levels = hddm->num_levels;
	kb = hddm->mem_nodes;
	kbt = kb;
	PH_PrintToMainLog(env, "[levels=%d, nodes=%d] [%.1f KB]\n", hddm->num_levels, hddm->num_nodes, kb);
	
	// add sparse matrices
	PH_PrintToMainLog(env, "Adding explicit sparse matrices... ");
	add_sparse_matrices(hddm, compact, false, transpose);
	compact_sm = hddm->compact_sm;
	if (compact_sm) {
		sm_dist = hddm->dist;
		sm_dist_shift = hddm->dist_shift;
		sm_dist_mask = hddm->dist_mask;
	}
	kb = hddm->mem_sm;
	kbt += kb;
	PH_PrintToMainLog(env, "[levels=%d, num=%d%s] [%.1f KB]\n", hddm->l_sm, hddm->num_sm, compact_sm?", compact":"", kb);
	
	// build b vector (if present)
	if (b != NULL) {
		PH_PrintToMainLog(env, "Creating vector for RHS... ");
		b_vec = mtbdd_to_double_vector(ddman, b, rvars, num_rvars, odd);
		// try and convert to compact form if required
		compact_b = false;
		if (compact) {
			if (b_dist = double_vector_to_dist(b_vec, n)) {
				compact_b = true;
				free(b_vec);
			}
		}
		kb = (!compact_b) ? n*8.0/1024.0 : (b_dist->num_dist*8.0+n*2.0)/1024.0;
		kbt += kb;
		if (!compact_b) PH_PrintToMainLog(env, "[%.1f KB]\n", kb);
		else PH_PrintToMainLog(env, "[dist=%d, compact] [%.1f KB]\n", b_dist->num_dist, kb);
	}
	
	// create solution/iteration vectors
	PH_PrintToMainLog(env, "Allocating iteration vectors... ");
	soln = mtbdd_to_double_vector(ddman, init, rvars, num_rvars, odd);
	soln2 = new double[n];
	kb = n*8.0/1024.0;
	kbt += 2*kb;
	PH_PrintToMainLog(env, "[2 x %.1f KB]\n", kb);
	
	// print total memory usage
	PH_PrintToMainLog(env, "TOTAL: [%.1f KB]\n", kbt);
	
	// get setup time
	stop = util_cpu_time();
	time_for_setup = (double)(stop - start2)/1000;
	start2 = stop;
	
	// start iterations
	iters = 0;
	done = false;
	PH_PrintToMainLog(env, "\nStarting iterations...\n");
	
	while (!done && iters < max_iters) {
		
		iters++;
		
//		PH_PrintToMainLog(env, "Iteration %d: ", iters);
//		start3 = util_cpu_time();
		
		// matrix multiply
		
		// initialise vector
		if (b == NULL) {
			for (i = 0; i < n; i++) { soln2[i] = 0.0; }
		} else if (!compact_b) {
			for (i = 0; i < n; i++) { soln2[i] = b_vec[i]; }
		} else {
			for (i = 0; i < n; i++) { soln2[i] = b_dist->dist[b_dist->ptrs[i]]; }
		}
		
		// do matrix vector multiply bit
		power_rec(hdd, 0, 0, 0, transpose);
		
		// check convergence
		// (note: doing outside loop means may not need to check all elements)
		switch (term_crit) {
		case TERM_CRIT_ABSOLUTE:
			done = true;
			for (i = 0; i < n; i++) {
				if (fabs(soln2[i] - soln[i]) > term_crit_param) {
					done = false;
					break;
				}
				
			}
			break;
		case TERM_CRIT_RELATIVE:
			done = true;
			for (i = 0; i < n; i++) {
				if (fabs(soln2[i] - soln[i])/soln2[i] > term_crit_param) {
					done = false;
					break;
				}
				
			}
			break;
		}
		
		// prepare for next iteration
		tmpsoln = soln;
		soln = soln2;
		soln2 = tmpsoln;
		
//		PH_PrintToMainLog(env, "%.2f %.2f sec\n", ((double)(util_cpu_time() - start3)/1000), ((double)(util_cpu_time() - start2)/1000)/iters);
	}
	
	// stop clocks
	stop = util_cpu_time();
	time_for_iters = (double)(stop - start2)/1000;
	time_taken = (double)(stop - start1)/1000;
	
	// print iters/timing info
	PH_PrintToMainLog(env, "\nPower method: %d iterations in %.2f seconds (average %.6f, setup %.2f)\n", iters, time_taken, time_for_iters/iters, time_for_setup);
	
	// free memory
	Cudd_RecursiveDeref(ddman, a);
	free_hdd_matrix(hddm);
	if (b != NULL) if (compact_b) free_dist_vector(b_dist); else free(b_vec);
	delete soln2;
	
	// if the iterative method didn't terminate, this is an error
	if (!done) { delete soln; PH_SetErrorMessage("Iterative method did not converge within %d iterations.\nConsider using a different numerical method or increase the maximum number of iterations", iters); return 0; }
	
	return (int)soln;
}

//------------------------------------------------------------------------------

void power_rec(HDDNode *hdd, int level, int row_offset, int col_offset, bool transpose)
{
	HDDNode *e, *t;
	
	// if it's the zero node
	if (hdd == zero) {
		return;
	}
	// or if we've reached a submatrix
	// (check for non-null ptr but, equivalently, we could just check if level==l_sm)
	else if (hdd->sm) {
		if (!compact_sm) {
			power_rm((RMSparseMatrix *)hdd->sm, row_offset, col_offset);
		} else {
			power_cmsr((CMSRSparseMatrix *)hdd->sm, row_offset, col_offset);
		}
		return;
	}
	// or if we've reached the bottom
	else if (level == num_levels) {
		//printf("(%d,%d)=%f\n", row_offset, col_offset, hdd->type.val);
		soln2[row_offset] += soln[col_offset] * hdd->type.val;
		return;
	}
	// otherwise recurse
	e = hdd->type.kids.e;
	if (e != zero) {
		if (!transpose) {
			power_rec(e->type.kids.e, level+1, row_offset, col_offset, transpose);
			power_rec(e->type.kids.t, level+1, row_offset, col_offset+e->off, transpose);
		} else {
			power_rec(e->type.kids.e, level+1, row_offset, col_offset, transpose);
			power_rec(e->type.kids.t, level+1, row_offset+e->off, col_offset, transpose);
		}
	}
	t = hdd->type.kids.t;
	if (t != zero) {
		if (!transpose) {
			power_rec(t->type.kids.e, level+1, row_offset+hdd->off, col_offset, transpose);
			power_rec(t->type.kids.t, level+1, row_offset+hdd->off, col_offset+t->off, transpose);
		} else {
			power_rec(t->type.kids.e, level+1, row_offset, col_offset+hdd->off, transpose);
			power_rec(t->type.kids.t, level+1, row_offset+t->off, col_offset+hdd->off, transpose);
		}
	}
}

//-----------------------------------------------------------------------------------

void power_rm(RMSparseMatrix *rmsm, int row_offset, int col_offset)
{
	int i2, j2, l2, h2;
	int sm_n = rmsm->n;
	int sm_nnz = rmsm->nnz;
	double *sm_non_zeros = rmsm->non_zeros;
	unsigned char *sm_row_counts = rmsm->row_counts;
	int *sm_row_starts = (int *)rmsm->row_counts;
	bool sm_use_counts = rmsm->use_counts;
	unsigned int *sm_cols = rmsm->cols;
	
	// loop through rows of submatrix
	l2 = sm_nnz; h2 = 0;
	for (i2 = 0; i2 < sm_n; i2++) {
		
		// loop through entries in this row
		if (!sm_use_counts) { l2 = sm_row_starts[i2]; h2 = sm_row_starts[i2+1]; }
		else { l2 = h2; h2 += sm_row_counts[i2]; }
		for (j2 = l2; j2 < h2; j2++) {
			soln2[row_offset + i2] += soln[col_offset + sm_cols[j2]] * sm_non_zeros[j2];
			//printf("(%d,%d)=%f\n", row_offset + i2, col_offset + sm_cols[j2], sm_non_zeros[j2]);
		}
	}
}

//-----------------------------------------------------------------------------------

void power_cmsr(CMSRSparseMatrix *cmsrsm, int row_offset, int col_offset)
{
	int i2, j2, l2, h2;
	int sm_n = cmsrsm->n;
	int sm_nnz = cmsrsm->nnz;
	unsigned char *sm_row_counts = cmsrsm->row_counts;
	int *sm_row_starts = (int *)cmsrsm->row_counts;
	bool sm_use_counts = cmsrsm->use_counts;
	unsigned int *sm_cols = cmsrsm->cols;
	
	// loop through rows of submatrix
	l2 = sm_nnz; h2 = 0;
	for (i2 = 0; i2 < sm_n; i2++) {
		
		// loop through entries in this row
		if (!sm_use_counts) { l2 = sm_row_starts[i2]; h2 = sm_row_starts[i2+1]; }
		else { l2 = h2; h2 += sm_row_counts[i2]; }
		for (j2 = l2; j2 < h2; j2++) {
			soln2[row_offset + i2] += soln[col_offset + (int)(sm_cols[j2] >> sm_dist_shift)] * sm_dist[(int)(sm_cols[j2] & sm_dist_mask)];
			//printf("(%d,%d)=%f\n", row_offset + i2, col_offset + (int)(sm_cols[j2] >> sm_dist_shift), sm_dist[(int)(sm_cols[j2] & sm_dist_mask)]);
		}
	}
}

//------------------------------------------------------------------------------
