#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: mal.c,v 1.41 1999/03/17 23:21:40 bsmith Exp bsmith $";
#endif
/*
    Code that allows a user to dictate what malloc() PETSc uses.
*/
#include "petsc.h"             /*I   "petsc.h"   I*/
#if defined(PETSC_HAVE_STDLIB_H)
#include <stdlib.h>
#endif
#if defined(PETSC_HAVE_MALLOC_H) && !defined(__cplusplus)
#include <malloc.h>
#endif
#include "pinclude/petscfix.h"


/*
        We want to make sure that all mallocs of double or complex numbers are complex aligned.
    1) on systems with memalign() we call that routine to get an aligned memory location
    2) on systems without memalign() we 
       - allocate one sizeof(Scalar) extra space
       - we shift the pointer up slightly if needed to get Scalar aligned
       - if shifted we store at ptr[-1] the amount of shift (plus a cookie)
*/
#define SHIFT_COOKIE 456123

void *PetscMallocAlign(int mem)
{
#if defined(PETSC_HAVE_DOUBLE_ALIGN_MALLOC) && !defined(PETSC_USE_COMPLEX)
  return malloc(mem);
#elif defined(PETSC_HAVE_MEMALIGN)
  return memalign(sizeof(Scalar),mem);
#else
  {
    int *ptr,shift;
    /*
      malloc space for two extra Scalar and shift ptr 1 + enough to get it Scalar aligned
    */
    ptr = (int *) malloc(mem + 2*sizeof(Scalar));
    if (!ptr) return 0;
    shift = (int)(((unsigned long) ptr) % sizeof(Scalar));
    shift = (2*sizeof(Scalar) - shift)/sizeof(int);
    ptr     += shift;
    ptr[-1]  = shift + SHIFT_COOKIE ;
    return (void *) ptr;
  }
#endif
}

int PetscFreeAlign(void *ptr)
{
#if defined(PETSC_HAVE_DOUBLE_ALIGN_MALLOC) && !defined(PETSC_USE_COMPLEX)
  free(ptr);
  return 0;
#elif defined(PETSC_HAVE_MEMALIGN)
  free(ptr);
  return 0;
#else
  int shift;
  /*
       Previous int tells us how many ints the pointer has been shifted from
    the original address provided by the system malloc().
  */
  shift = ((int *)ptr)[-1] - SHIFT_COOKIE;   
  if (shift > 15) SETERRQ(1,1,"Likely memory corruption in heap");
  ptr   = (void *) (((int *) ptr) - shift);
  free(ptr);
  return 0;
#endif
}

/*
    Set the default malloc and free to be the usual system versions unless using complex
*/
#if defined(PETSC_USE_COMPLEX)
void *(*PetscTrMalloc)(int,int,char*,char*,char*) = 
     (void*(*)(int,int,char*,char*,char*)) PetscMallocAlign;
int  (*PetscTrFree)(void *,int,char*,char *,char*)         = 
     (int (*)(void*,int,char*,char*,char*)) PetscFreeAlign;
#else
void *(*PetscTrMalloc)(int,int,char*,char*,char*) = 
     (void*(*)(int,int,char*,char*,char*))malloc;
int  (*PetscTrFree)(void *,int,char*,char *,char*)         = 
     (int (*)(void*,int,char*,char*,char*))free;
#endif


static int petscsetmallocvisited = 0;

#undef __FUNC__  
#define __FUNC__ "PetscSetMalloc"
/*@C
   PetscSetMalloc - Sets the routines used to do mallocs and frees.
   This routine MUST be called before PetscInitialize() and may be
   called only once.

   Not Collective

   Input Parameters:
+  malloc - the malloc routine
-  free - the free routine

   Level: developer

.keywords: Petsc, set, malloc, free, memory allocation
@*/
int PetscSetMalloc(void *(*imalloc)(int,int,char*,char*,char*),
                   int (*ifree)(void*,int,char*,char*,char*))
{
  PetscFunctionBegin;
  if (petscsetmallocvisited) SETERRQ(PETSC_ERR_SUP,0,"cannot call multiple times");
  PetscTrMalloc               = imalloc;
  PetscTrFree                 = ifree;
  petscsetmallocvisited       = 1;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "PetscClearMalloc"
/*@C
   PetscClearMalloc - Resets the routines used to do mallocs and frees to the 
        defaults.

   Not Collective

   Level: developer

   Notes:
    In general one should never run a PETSc program with different malloc() and 
    free() settings for different parts; this is because one NEVER wants to 
    free() and address that was malloced by a different memory management system

.keywords: Petsc, set, malloc, free, memory allocation
@*/
int PetscClearMalloc(void)
{
  PetscFunctionBegin;
#if defined(PETSC_HAVE_MEMALIGN) && defined(PETSC_USE_COMPLEX)
  PetscTrMalloc               = (void*(*)(int,int,char*,char*,char*))PetscMallocAlign;
  PetscTrFree                 = (int (*)(void*,int,char*,char*,char*))PetscFreeAlign;
#else
  PetscTrMalloc               = (void*(*)(int,int,char*,char*,char*))malloc;
  PetscTrFree                 = (int (*)(void*,int,char*,char*,char*))free;
#endif
  petscsetmallocvisited       = 0;
  PetscFunctionReturn(0);
}
