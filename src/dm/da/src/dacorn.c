#ifndef lint
static char vcid[] = "$Id: dacorn.c,v 1.1 1996/01/30 04:27:58 bsmith Exp bsmith $";
#endif
 
/*
  Code for manipulating distributed regular arrays in parallel.
*/

#include "daimpl.h"    /*I   "da.h"   I*/

/*@
   DAGetCorners - Returns the global (x,y,z) indices of the lower left
   corner of the local region, excluding ghost points.

   Input Parameter:
.  da - the distributed array

   Output Parameters:
.  x,y,z - the corner indices. y and z are optional.
.  m,n,p - widths in the corresponding directions. n and p are optional.

.keywords: distributed array, get, corners, nodes, local indices

.seealso: DAGetGhostCorners()
@*/
int DAGetCorners(DA da,int *x,int *y,int *z,int *m, int *n, int *p)
{
  int w;

  PETSCVALIDHEADERSPECIFIC(da,DA_COOKIE);
  /* since the xs, xe ... have all been multiplied by the number of degrees 
     of freedom per cell, w = da->w, we divide that out before returning.*/
  w = da->w;  
  *x = da->xs/w; *m = (da->xe - da->xs)/w;
  /* the y and z have NOT been multiplied by w */
  if (y) *y = da->ys; if (n) *n = (da->ye - da->ys);
  if (z) *z = da->zs; if (p) *p = (da->ze - da->zs); 
  return 0;
} 

