#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: zsys.c,v 1.48 1998/03/31 23:42:18 balay Exp balay $";
#endif

#include "src/fortran/custom/zpetsc.h"
#include "sys.h"
#include "vec.h"
#include "pinclude/petscfix.h"

#ifdef HAVE_FORTRAN_CAPS
#define petscattachdebugger_       PETSCATTACHDEBUGGER
#define petscobjectsetname_        PETSCOBJECTSETNAME
#define petscobjectdestroy_        PETSCOBJECTDESTROY
#define petscobjectgetcomm_        PETSCOBJECTGETCOMM
#define petscobjectgetname_        PETSCOBJECTGETNAME
#define petscgetflops_             PETSCGETFLOPS
#define petscerror_                PETSCERROR
#define petscrandomcreate_         PETSCRANDOMCREATE
#define petscrandomdestroy_        PETSCRANDOMDESTROY
#define petscrandomgetvalue_       PETSCRANDOMGETVALUE
#define vecsetrandom_              VECSETRANDOM
#define petsctrvalid_              PETSCTRVALID
#define petscdoubleview_           PETSCDOUBLEVIEW
#define petscintview_              PETSCINTVIEW
#define petscsequentialphasebegin_ PETSCSEQUENTIALPHASEBEGIN
#define petscsequentialphaseend_   PETSCSEQUENTIALPHASEEND
#define petsctrlog_                PETSCTRLOG
#define petscmemcpy_               PETSCMEMCPY
#define petsctrdump_               PETSCTRDUMP
#define petscmemzero_              PETSCMEMZERO
#define petscbinaryopen_           PETSCBINARYOPEN
#define petscbinaryread_           PETSCBINARYREAD
#define petscbinarywrite_          PETSCBINARYWRITE
#define petscbinaryclose_          PETSCBINARYCLOSE
#define petscbinaryseek_           PETSCBINARYSEEK
#define petscfixfilename_          PETSCFIXFILENAME
#define petscreleasepointer_       PETSCRELEASEPOINTER
#define petscstrncpy_              PETSCSTRNCPY
#elif !defined(HAVE_FORTRAN_UNDERSCORE)
#define petscstrncpy_              petscstrncpy
#define petscreleasepointer_       petscreleasepointer
#define petscfixfilename_          petscfixfilename
#define petsctrlog_                petsctrlog
#define petscattachdebugger_       petscattachdebugger
#define petscobjectsetname_        petscobjectsetname
#define petscobjectdestroy_        petscobjectdestroy
#define petscobjectgetcomm_        petscobjectgetcomm
#define petscobjectgetname_        petscobjectgetname
#define petscgetflops_             petscgetflops 
#define petscerror_                petscerror
#define petscrandomcreate_         petscrandomcreate
#define petscrandomdestroy_        petscrandomdestroy
#define petscrandomgetvalue_       petscrandomgetvalue
#define vecsetrandom_              vecsetrandom
#define petsctrvalid_              petsctrvalid
#define petscdoubleview_           petscdoubleview
#define petscintview_              petscintview
#define petscsequentialphasebegin_ petscsequentialphasebegin
#define petscsequentialphaseend_   petscsequentialphaseend
#define petscmemcpy_               petscmemcpy
#define petsctrdump_               petsctrdump
#define petscmemzero_              petscmemzero
#define petscbinaryopen_           petscbinaryopen
#define petscbinaryread_           petscbinaryread
#define petscbinarywrite_          petscbinarywrite
#define petscbinaryclose_          petscbinaryclose
#define petscbinaryseek_           petscbinaryseek
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void petscstrncpy_(CHAR s1, CHAR s2, int *n,int len1, int len2)
{
  char *t1,*t2;
  int  m;

#if defined(PARCH_t3d)
  t1 = _fcdtocp(s1); 
  t2 = _fcdtocp(s2); 
  m = *n; if (_fcdlen(s1) < m) m = _fcdlen(s1); if (_fcdlen(s2) < m) m = _fcdlen(s2);
#else
  t1 = s1;
  t2 = s2;
  m = *n; if (len1 < m) m = len1; if (len2 < m) m = len2;
#endif
  PetscStrncpy(t1,t2,m);
}

void petscfixfilename_(CHAR file, int *__ierr,int len )
{
  int  i,n;
  char *b;

#if defined(PARCH_t3d)
  b = _fcdtocp(file); 
  n = _fcdlen (file); 
#else
  b = file;
  n = len;
#endif

  for (i=0; i<n; i++) {
#if defined(PARCH_nt)
    if (b[i] == '/') b[i] = '\\';
#else
    if (b[i] == '\\') b[i] = '/';
#endif
  }
}

void petscbinaryopen_(CHAR name,int *type,int *fd,int *__ierr,int len)
{
  int  ierr;
  char *c1;

  FIXCHAR(name,len,c1);
  ierr = PetscBinaryOpen(c1,*type,fd);
  FREECHAR(name,c1);
  *__ierr = ierr;
}

void petscbinarywrite_(int *fd,void *p,int *n,PetscDataType *type,int *istemp,int *__ierr)
{
  *__ierr = PetscBinaryWrite(*fd,p,*n,*type,*istemp);
}

void petscbinaryread_(int *fd,void *p,int *n,PetscDataType *type,int *__ierr)
{
  *__ierr = PetscBinaryRead(*fd,p,*n,*type);
}

void petscbinaryseek_(int *fd,int *size,PetscBinarySeekType *whence,int *__ierr)
{
  *__ierr = PetscBinarySeek(*fd,*size,*whence);
}

void petscbinaryclose_(int *fd,int *__ierr)
{
  *__ierr = PetscBinaryClose(*fd);
}

/* ---------------------------------------------------------------------------------*/
void petscmemzero_(void *a,int *n) 
{
  PetscMemzero(a,*n);
}

void petsctrdump_(int *__ierr)
{
  *__ierr = PetscTrDump(stdout);
}

void petscmemcpy_(int *out,int *in,int *length)
{
  PetscMemcpy(out,in,*length);
}

void petsctrlog_(int *__ierr)
{
  *__ierr = PetscTrLog();
}

void petsctrvalid_(int *__ierr)
{
  *__ierr = PetscTrValid(0,"Unknown Fortran",0,0);
}

void petscrandomgetvalue_(PetscRandom r,Scalar *val, int *__ierr )
{
  *__ierr = PetscRandomGetValue((PetscRandom)PetscToPointer(r),val);
}

void vecsetrandom_(PetscRandom r,Vec x, int *__ierr )
{
  *__ierr = VecSetRandom((PetscRandom)PetscToPointer(r),
                         (Vec)PetscToPointer(x));
}

void petscobjectgetname(PetscObject obj, CHAR name, int *__ierr, int len)
{
  char *tmp;
  *__ierr = PetscObjectGetName((PetscObject)PetscToPointer(obj),&tmp);
#if defined(USES_CPTOFCD)
  {
  char *t = _fcdtocp(name);
  int  len1 = _fcdlen(name);
  PetscStrncpy(t,tmp,len1);
  }
#else
  PetscStrncpy(name,tmp,len);
#endif
}

void petscobjectdestroy_(PetscObject obj, int *__ierr )
{
  *__ierr = PetscObjectDestroy((PetscObject)PetscToPointer(obj));
  PetscRmPointer(obj);
}

void petscobjectgetcomm_(PetscObject obj,int *comm, int *__ierr )
{
  MPI_Comm c;
  *__ierr = PetscObjectGetComm((PetscObject)PetscToPointer(obj),&c);
  *(int*)comm = PetscFromPointerComm(c);
}

void petscattachdebugger_(int *__ierr)
{
  *__ierr = PetscAttachDebugger();
}

/*
      This bleeds memory, but no easy way to get around it
*/
void petscobjectsetname_(PetscObject obj,CHAR name,int *__ierr,int len)
{
  char *t1;

  FIXCHAR(name,len,t1);
  *__ierr = PetscObjectSetName((PetscObject)PetscToPointer(obj),t1);
}

void petscerror_(int *number,int *p,CHAR message,int *__ierr,int len)
{
  char *t1;
  FIXCHAR(message,len,t1);
  *__ierr = PetscError(-1,0,"fortran_interface_unknown_file",0,*number,*p,t1);
}

void petscgetflops_(PLogDouble *d,int *__ierr)
{
#if defined(USE_PETSC_LOG)
  *__ierr = PetscGetFlops(d);
#else
  __ierr = 0;
  *d     = 0.0;
#endif
}

void petscrandomcreate_(MPI_Comm *comm,PetscRandomType *type,PetscRandom *r,int *__ierr )
{
  PetscRandom rr;
  *__ierr = PetscRandomCreate((MPI_Comm)PetscToPointerComm( *comm ),*type,&rr);
  *(PetscFortranAddr*)r = PetscFromPointer(rr);
}

void petscrandomdestroy_(PetscRandom r, int *__ierr )
{
  *__ierr = PetscRandomDestroy((PetscRandom )PetscToPointer(r));
   PetscRmPointer(r); 
}

void petscdoubleview_(int *n,double *d,int *viwer,int *__ierr)
{
  *__ierr = PetscDoubleView(*n,d,0);
}

void petscintview_(int *n,int *d,int *viwer,int *__ierr)
{
  *__ierr = PetscIntView(*n,d,0);
}

void petscsequentialphasebegin_(MPI_Comm *comm,int *ng, int *__ierr ){
*__ierr = PetscSequentialPhaseBegin(
	(MPI_Comm)PetscToPointerComm( *comm ),*ng);
}
void petscsequentialphaseend_(MPI_Comm *comm,int *ng, int *__ierr ){
*__ierr = PetscSequentialPhaseEnd(
	(MPI_Comm)PetscToPointerComm( *comm ),*ng);
}

void petscreleasepointer_(int *index,int *__ierr) 
{
   PetscRmPointer(index);
   *__ierr = 0;
}

#if defined(__cplusplus)
}
#endif

/*
    PETSc objects require routines provided here to do the conversion 
    between C pointers and Fortran integers.
*/
#if defined(USE_POINTER_CONVERSION)

/* 
   This file contains routines to convert to and from C pointers to Fortran integers
*/

typedef struct _PtrToIdx {
    int              idx;
    void             *ptr;
    struct _PtrToIdx *next;
} PtrToIdx;

#define MAX_PTRS 10000

static PtrToIdx PtrArray[MAX_PTRS];
static PtrToIdx *avail=0;
static int      DoInit = 1;

static void PetscInitPointer(void)
{
  int  i;

  for (i=0; i<MAX_PTRS-1; i++) {
    PtrArray[i].next = PtrArray + i + 1;
    PtrArray[i].idx  = i;
  }
  PtrArray[MAX_PTRS-1].next = 0;
  avail   = PtrArray + 1;
}

void *PetscToPointer(void *vidx )
{
  int idx = *(int*)(vidx);

  if (DoInit) {
    DoInit = 0;
    PetscInitPointer();
  }
  if (idx < 0 || idx >= MAX_PTRS) {
    int rank;
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    (*PetscErrorPrintf)( "[%d]PETSC ERROR: Could not convert index %d into a pointer\n",rank, idx );
    (*PetscErrorPrintf)( "[%d]PETSC ERROR: The index may be an incorrect argument.\n\
PETSC ERROR:Possible sources of this problem are a missing include file,\n\
PETSC ERROR:a misspelled PETSC object (e.g., VIEWER_STOUT_WORLD instead of VIEWER_STDOUT_WORLD)\n\
PETSC ERROR:or a misspelled user variable for an PETSc object (e.g., \n\
PETSC ERROR:com instead of comm).\n",rank );
    MPI_Abort(PETSC_COMM_WORLD,1);
  }
  return PtrArray[idx].ptr;
}

int PetscFromPointer(void *ptr )
{
  int      idx,rank;
  PtrToIdx *newl;

  if (DoInit) {
    DoInit = 0;
    PetscInitPointer();
  }
  if (!ptr) return 0;
  if (avail) {
    newl	      = avail;
    avail	      = avail->next;
    newl->next	      = 0;
    idx		      = newl->idx;
    PtrArray[idx].ptr = ptr;
    return idx;
  }
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  /* This isn't the right thing to do, but it isn't too bad */
  (*PetscErrorPrintf)( "[%d]PETSC ERROR:Pointer conversions exhausted\n",rank );
  (*PetscErrorPrintf)( "[%d]PETSC ERROR:Too many PETSc objects may have been passed to/from Fortran\n\
  without being freed\n",rank );
  return MPI_Abort(PETSC_COMM_WORLD,1);
}

void PetscRmPointer(void *vidx )
{
  int idx = *(int*)vidx;

  if (DoInit) {
    DoInit = 0;
    PetscInitPointer();
  }
  if (idx < 0 || idx >= MAX_PTRS) {
    int rank;
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    (*PetscErrorPrintf)( "[%d]PETSC ERROR:Could not convert index %d into a pointer\n",rank, idx );
    (*PetscErrorPrintf)( "[%d]PETSC ERROR:The index may be an incorrect argument.\n\
PETSC ERROR:Possible sources of this problem are a missing include file,\n\
PETSC ERROR:a misspelled PETSC object (e.g., VIEWER_STOUT_WORLD instead of VIEWER_STDOUT_WORLD)\n\
PETSC ERROR:or a misspelled user variable for an PETSc object (e.g., \n\
PETSC ERROR:com instead of comm).\n",rank );
    MPI_Abort(PETSC_COMM_WORLD,1);
  }
  if (idx == 0) return;
  if (PtrArray[idx].next) {
    int rank;
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    (*PetscErrorPrintf)("[%d] Error in recovering Fortran pointer; already freed\n",rank);
    MPI_Abort(PETSC_COMM_WORLD,1);
  }
  PtrArray[idx].next = avail;
  PtrArray[idx].ptr  = 0;
  avail              = PtrArray + idx;
}

#endif
