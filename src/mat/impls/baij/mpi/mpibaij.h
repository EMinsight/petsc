/* $Id: mpibaij.h,v 1.19 1999/03/18 00:37:02 balay Exp bsmith $ */

#include "src/mat/impls/baij/seq/baij.h"
#include "src/sys/ctable.h"

#if !defined(__MPIBAIJ_H)
#define __MPIBAIJ_H

typedef struct {
  int           *rowners,*cowners;      /* ranges owned by each processor */
  int           *rowners_bs;            /* rowners*bs */
  int           m, n;                   /* local rows and columns */
  int           M, N;                   /* global rows and columns */
  int           rstart, rend;           /* starting and ending owned rows */
  int           cstart, cend;           /* starting and ending owned columns */
  Mat           A, B;                   /* local submatrices: A (diag part),
                                           B (off-diag part) */
  int           size;                   /* size of communicator */
  int           rank;                   /* rank of proc in communicator */ 
  int           bs, bs2;                /* block size, bs2 = bs*bs */
  int           Mbs, Nbs;               /* number block rows/cols in matrix; M/bs, N/bs */
  int           mbs, nbs;               /* number block rows/cols on processor; m/bs, n/bs */

  /* The following variables are used for matrix assembly */

  int           donotstash;             /* if 1, off processor entries dropped */
  MPI_Request   *send_waits;            /* array of send requests */
  MPI_Request   *recv_waits;            /* array of receive requests */
  int           nsends, nrecvs;         /* numbers of sends and receives */
  Scalar        *svalues, *rvalues;     /* sending and receiving data */
  int           rmax;                   /* maximum message length */
#if defined (PETSC_USE_CTABLE)
  Table         colmap;
#else
  int           *colmap;                /* local col number of off-diag col */
#endif
  int           *garray;                /* work array */

  /* The following variable is used by blocked matrix assembly */
  Scalar        *barray;                /* Block array of size bs2 */

  /* The following variables are used for matrix-vector products */

  Vec           lvec;              /* local vector */
  VecScatter    Mvctx;             /* scatter context for vector */
  int           roworiented;       /* if true, row-oriented input, default true */

  /* The following variables are for MatGetRow() */

  int           *rowindices;       /* column indices for row */
  Scalar        *rowvalues;        /* nonzero values in row */
  PetscTruth    getrowactive;      /* indicates MatGetRow(), not restored */

  /* Some variables to make MatSetValues and others more efficient */
  int           rstart_bs, rend_bs; 
  int           cstart_bs, cend_bs;
  int           *ht;                      /* Hash table to speed up matrix assembly */
  Scalar        **hd;                     /* Hash table data */
  int           ht_size;
  int           ht_total_ct,ht_insert_ct; /* Hash table statistics */
  int           ht_flag;                  /* Flag to indicate if hash tables are used */
  double        ht_fact;                  /* Factor to determine the HT size */
} Mat_MPIBAIJ;


#endif
