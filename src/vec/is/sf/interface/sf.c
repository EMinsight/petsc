#include <petsc/private/sfimpl.h> /*I "petscsf.h" I*/
#include <petsc/private/hashseti.h>
#include <petsc/private/viewerimpl.h>
#include <petscctable.h>

#if defined(PETSC_HAVE_CUDA)
  #include <cuda_runtime.h>
#endif

#if defined(PETSC_HAVE_HIP)
  #include <hip/hip_runtime.h>
#endif

#if defined(PETSC_CLANG_STATIC_ANALYZER)
void PetscSFCheckGraphSet(PetscSF,int);
#else
#if defined(PETSC_USE_DEBUG)
#  define PetscSFCheckGraphSet(sf,arg) do {                          \
    if (PetscUnlikely(!(sf)->graphset))                              \
      SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Must call PetscSFSetGraph() or PetscSFSetGraphWithPattern() on argument %d \"%s\" before %s()",(arg),#sf,PETSC_FUNCTION_NAME); \
  } while (0)
#else
#  define PetscSFCheckGraphSet(sf,arg) do {} while (0)
#endif
#endif

const char *const PetscSFDuplicateOptions[] = {"CONFONLY","RANKS","GRAPH","PetscSFDuplicateOption","PETSCSF_DUPLICATE_",NULL};

/*@
   PetscSFCreate - create a star forest communication context

   Collective

   Input Parameter:
.  comm - communicator on which the star forest will operate

   Output Parameter:
.  sf - new star forest context

   Options Database Keys:
+  -sf_type basic     -Use MPI persistent Isend/Irecv for communication (Default)
.  -sf_type window    -Use MPI-3 one-sided window for communication
-  -sf_type neighbor  -Use MPI-3 neighborhood collectives for communication

   Level: intermediate

   Notes:
   When one knows the communication graph is one of the predefined graph, such as MPI_Alltoall, MPI_Allgatherv,
   MPI_Gatherv, one can create a PetscSF and then set its graph with PetscSFSetGraphWithPattern(). These special
   SFs are optimized and they have better performance than general SFs.

.seealso: PetscSFSetGraph(), PetscSFSetGraphWithPattern(), PetscSFDestroy()
@*/
PetscErrorCode PetscSFCreate(MPI_Comm comm,PetscSF *sf)
{
  PetscSF        b;

  PetscFunctionBegin;
  PetscValidPointer(sf,2);
  CHKERRQ(PetscSFInitializePackage());

  CHKERRQ(PetscHeaderCreate(b,PETSCSF_CLASSID,"PetscSF","Star Forest","PetscSF",comm,PetscSFDestroy,PetscSFView));

  b->nroots    = -1;
  b->nleaves   = -1;
  b->minleaf   = PETSC_MAX_INT;
  b->maxleaf   = PETSC_MIN_INT;
  b->nranks    = -1;
  b->rankorder = PETSC_TRUE;
  b->ingroup   = MPI_GROUP_NULL;
  b->outgroup  = MPI_GROUP_NULL;
  b->graphset  = PETSC_FALSE;
#if defined(PETSC_HAVE_DEVICE)
  b->use_gpu_aware_mpi    = use_gpu_aware_mpi;
  b->use_stream_aware_mpi = PETSC_FALSE;
  b->unknown_input_stream= PETSC_FALSE;
  #if defined(PETSC_HAVE_KOKKOS) /* Prefer kokkos over cuda*/
    b->backend = PETSCSF_BACKEND_KOKKOS;
  #elif defined(PETSC_HAVE_CUDA)
    b->backend = PETSCSF_BACKEND_CUDA;
  #elif defined(PETSC_HAVE_HIP)
    b->backend = PETSCSF_BACKEND_HIP;
  #endif

  #if defined(PETSC_HAVE_NVSHMEM)
    b->use_nvshmem     = PETSC_FALSE; /* Default is not to try NVSHMEM */
    b->use_nvshmem_get = PETSC_FALSE; /* Default is to use nvshmem_put based protocol */
    CHKERRQ(PetscOptionsGetBool(NULL,NULL,"-use_nvshmem",&b->use_nvshmem,NULL));
    CHKERRQ(PetscOptionsGetBool(NULL,NULL,"-use_nvshmem_get",&b->use_nvshmem_get,NULL));
  #endif
#endif
  b->vscat.from_n = -1;
  b->vscat.to_n   = -1;
  b->vscat.unit   = MPIU_SCALAR;
 *sf = b;
  PetscFunctionReturn(0);
}

/*@
   PetscSFReset - Reset a star forest so that different sizes or neighbors can be used

   Collective

   Input Parameter:
.  sf - star forest

   Level: advanced

.seealso: PetscSFCreate(), PetscSFSetGraph(), PetscSFDestroy()
@*/
PetscErrorCode PetscSFReset(PetscSF sf)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (sf->ops->Reset) CHKERRQ((*sf->ops->Reset)(sf));
  sf->nroots   = -1;
  sf->nleaves  = -1;
  sf->minleaf  = PETSC_MAX_INT;
  sf->maxleaf  = PETSC_MIN_INT;
  sf->mine     = NULL;
  sf->remote   = NULL;
  sf->graphset = PETSC_FALSE;
  CHKERRQ(PetscFree(sf->mine_alloc));
  CHKERRQ(PetscFree(sf->remote_alloc));
  sf->nranks = -1;
  CHKERRQ(PetscFree4(sf->ranks,sf->roffset,sf->rmine,sf->rremote));
  sf->degreeknown = PETSC_FALSE;
  CHKERRQ(PetscFree(sf->degree));
  if (sf->ingroup  != MPI_GROUP_NULL) CHKERRMPI(MPI_Group_free(&sf->ingroup));
  if (sf->outgroup != MPI_GROUP_NULL) CHKERRMPI(MPI_Group_free(&sf->outgroup));
  if (sf->multi) sf->multi->multi = NULL;
  CHKERRQ(PetscSFDestroy(&sf->multi));
  CHKERRQ(PetscLayoutDestroy(&sf->map));

 #if defined(PETSC_HAVE_DEVICE)
  for (PetscInt i=0; i<2; i++) CHKERRQ(PetscSFFree(sf,PETSC_MEMTYPE_DEVICE,sf->rmine_d[i]));
 #endif

  sf->setupcalled = PETSC_FALSE;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFSetType - Set the PetscSF communication implementation

   Collective on PetscSF

   Input Parameters:
+  sf - the PetscSF context
-  type - a known method

   Options Database Key:
.  -sf_type <type> - Sets the method; use -help for a list
   of available methods (for instance, window, basic, neighbor)

   Notes:
   See "include/petscsf.h" for available methods (for instance)
+    PETSCSFWINDOW - MPI-2/3 one-sided
-    PETSCSFBASIC - basic implementation using MPI-1 two-sided

  Level: intermediate

.seealso: PetscSFType, PetscSFCreate()
@*/
PetscErrorCode PetscSFSetType(PetscSF sf,PetscSFType type)
{
  PetscBool      match;
  PetscErrorCode (*r)(PetscSF);

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscValidCharPointer(type,2);

  CHKERRQ(PetscObjectTypeCompare((PetscObject)sf,type,&match));
  if (match) PetscFunctionReturn(0);

  CHKERRQ(PetscFunctionListFind(PetscSFList,type,&r));
  PetscCheck(r,PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"Unable to find requested PetscSF type %s",type);
  /* Destroy the previous PetscSF implementation context */
  if (sf->ops->Destroy) CHKERRQ((*(sf)->ops->Destroy)(sf));
  CHKERRQ(PetscMemzero(sf->ops,sizeof(*sf->ops)));
  CHKERRQ(PetscObjectChangeTypeName((PetscObject)sf,type));
  CHKERRQ((*r)(sf));
  PetscFunctionReturn(0);
}

/*@C
  PetscSFGetType - Get the PetscSF communication implementation

  Not Collective

  Input Parameter:
. sf  - the PetscSF context

  Output Parameter:
. type - the PetscSF type name

  Level: intermediate

.seealso: PetscSFSetType(), PetscSFCreate()
@*/
PetscErrorCode PetscSFGetType(PetscSF sf, PetscSFType *type)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf, PETSCSF_CLASSID,1);
  PetscValidPointer(type,2);
  *type = ((PetscObject)sf)->type_name;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFDestroy - destroy star forest

   Collective

   Input Parameter:
.  sf - address of star forest

   Level: intermediate

.seealso: PetscSFCreate(), PetscSFReset()
@*/
PetscErrorCode PetscSFDestroy(PetscSF *sf)
{
  PetscFunctionBegin;
  if (!*sf) PetscFunctionReturn(0);
  PetscValidHeaderSpecific((*sf),PETSCSF_CLASSID,1);
  if (--((PetscObject)(*sf))->refct > 0) {*sf = NULL; PetscFunctionReturn(0);}
  CHKERRQ(PetscSFReset(*sf));
  if ((*sf)->ops->Destroy) CHKERRQ((*(*sf)->ops->Destroy)(*sf));
  CHKERRQ(PetscSFDestroy(&(*sf)->vscat.lsf));
  if ((*sf)->vscat.bs > 1) CHKERRMPI(MPI_Type_free(&(*sf)->vscat.unit));
  CHKERRQ(PetscHeaderDestroy(sf));
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFCheckGraphValid_Private(PetscSF sf)
{
  PetscInt           i, nleaves;
  PetscMPIInt        size;
  const PetscInt    *ilocal;
  const PetscSFNode *iremote;

  PetscFunctionBegin;
  if (!sf->graphset || !PetscDefined(USE_DEBUG)) PetscFunctionReturn(0);
  CHKERRQ(PetscSFGetGraph(sf,NULL,&nleaves,&ilocal,&iremote));
  CHKERRMPI(MPI_Comm_size(PetscObjectComm((PetscObject)sf),&size));
  for (i = 0; i < nleaves; i++) {
    const PetscInt rank = iremote[i].rank;
    const PetscInt remote = iremote[i].index;
    const PetscInt leaf = ilocal ? ilocal[i] : i;
    PetscCheckFalse(rank < 0 || rank >= size,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Provided rank (%" PetscInt_FMT ") for remote %" PetscInt_FMT " is invalid, should be in [0, %d)",rank,i,size);
    PetscCheckFalse(remote < 0,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Provided index (%" PetscInt_FMT ") for remote %" PetscInt_FMT " is invalid, should be >= 0",remote,i);
    PetscCheckFalse(leaf < 0,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Provided location (%" PetscInt_FMT ") for leaf %" PetscInt_FMT " is invalid, should be >= 0",leaf,i);
  }
  PetscFunctionReturn(0);
}

/*@
   PetscSFSetUp - set up communication structures

   Collective

   Input Parameter:
.  sf - star forest communication object

   Level: beginner

.seealso: PetscSFSetFromOptions(), PetscSFSetType()
@*/
PetscErrorCode PetscSFSetUp(PetscSF sf)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  if (sf->setupcalled) PetscFunctionReturn(0);
  CHKERRQ(PetscLogEventBegin(PETSCSF_SetUp,sf,0,0,0));
  CHKERRQ(PetscSFCheckGraphValid_Private(sf));
  if (!((PetscObject)sf)->type_name) CHKERRQ(PetscSFSetType(sf,PETSCSFBASIC)); /* Zero all sf->ops */
  if (sf->ops->SetUp) CHKERRQ((*sf->ops->SetUp)(sf));
#if defined(PETSC_HAVE_CUDA)
  if (sf->backend == PETSCSF_BACKEND_CUDA) {
    sf->ops->Malloc = PetscSFMalloc_CUDA;
    sf->ops->Free   = PetscSFFree_CUDA;
  }
#endif
#if defined(PETSC_HAVE_HIP)
  if (sf->backend == PETSCSF_BACKEND_HIP) {
    sf->ops->Malloc = PetscSFMalloc_HIP;
    sf->ops->Free   = PetscSFFree_HIP;
  }
#endif

#
#if defined(PETSC_HAVE_KOKKOS)
  if (sf->backend == PETSCSF_BACKEND_KOKKOS) {
    sf->ops->Malloc = PetscSFMalloc_Kokkos;
    sf->ops->Free   = PetscSFFree_Kokkos;
  }
#endif
  CHKERRQ(PetscLogEventEnd(PETSCSF_SetUp,sf,0,0,0));
  sf->setupcalled = PETSC_TRUE;
  PetscFunctionReturn(0);
}

/*@
   PetscSFSetFromOptions - set PetscSF options using the options database

   Logically Collective

   Input Parameter:
.  sf - star forest

   Options Database Keys:
+  -sf_type               - implementation type, see PetscSFSetType()
.  -sf_rank_order         - sort composite points for gathers and scatters in rank order, gathers are non-deterministic otherwise
.  -sf_use_default_stream - Assume callers of SF computed the input root/leafdata with the default cuda stream. SF will also
                            use the default stream to process data. Therefore, no stream synchronization is needed between SF and its caller (default: true).
                            If true, this option only works with -use_gpu_aware_mpi 1.
.  -sf_use_stream_aware_mpi  - Assume the underlying MPI is cuda-stream aware and SF won't sync streams for send/recv buffers passed to MPI (default: false).
                               If true, this option only works with -use_gpu_aware_mpi 1.

-  -sf_backend cuda | hip | kokkos -Select the device backend SF uses. Currently SF has these backends: cuda, hip and Kokkos.
                              On CUDA (HIP) devices, one can choose cuda (hip) or kokkos with the default being kokkos. On other devices,
                              the only available is kokkos.

   Level: intermediate
@*/
PetscErrorCode PetscSFSetFromOptions(PetscSF sf)
{
  PetscSFType    deft;
  char           type[256];
  PetscErrorCode ierr;
  PetscBool      flg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  ierr = PetscObjectOptionsBegin((PetscObject)sf);CHKERRQ(ierr);
  deft = ((PetscObject)sf)->type_name ? ((PetscObject)sf)->type_name : PETSCSFBASIC;
  CHKERRQ(PetscOptionsFList("-sf_type","PetscSF implementation type","PetscSFSetType",PetscSFList,deft,type,sizeof(type),&flg));
  CHKERRQ(PetscSFSetType(sf,flg ? type : deft));
  CHKERRQ(PetscOptionsBool("-sf_rank_order","sort composite points for gathers and scatters in rank order, gathers are non-deterministic otherwise","PetscSFSetRankOrder",sf->rankorder,&sf->rankorder,NULL));
 #if defined(PETSC_HAVE_DEVICE)
  {
    char        backendstr[32] = {0};
    PetscBool   isCuda = PETSC_FALSE,isHip = PETSC_FALSE,isKokkos = PETSC_FALSE,set;
    /* Change the defaults set in PetscSFCreate() with command line options */
    CHKERRQ(PetscOptionsBool("-sf_unknown_input_stream","SF root/leafdata is computed on arbitary streams unknown to SF","PetscSFSetFromOptions",sf->unknown_input_stream,&sf->unknown_input_stream,NULL));
    CHKERRQ(PetscOptionsBool("-sf_use_stream_aware_mpi","Assume the underlying MPI is cuda-stream aware","PetscSFSetFromOptions",sf->use_stream_aware_mpi,&sf->use_stream_aware_mpi,NULL));
    CHKERRQ(PetscOptionsString("-sf_backend","Select the device backend SF uses","PetscSFSetFromOptions",NULL,backendstr,sizeof(backendstr),&set));
    CHKERRQ(PetscStrcasecmp("cuda",backendstr,&isCuda));
    CHKERRQ(PetscStrcasecmp("kokkos",backendstr,&isKokkos));
    CHKERRQ(PetscStrcasecmp("hip",backendstr,&isHip));
  #if defined(PETSC_HAVE_CUDA) || defined(PETSC_HAVE_HIP)
    if (isCuda) sf->backend = PETSCSF_BACKEND_CUDA;
    else if (isKokkos) sf->backend = PETSCSF_BACKEND_KOKKOS;
    else if (isHip) sf->backend = PETSCSF_BACKEND_HIP;
    else PetscCheck(!set,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"-sf_backend %s is not supported. You may choose cuda, hip or kokkos (if installed)", backendstr);
  #elif defined(PETSC_HAVE_KOKKOS)
    PetscCheckFalse(set && !isKokkos,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"-sf_backend %s is not supported. You can only choose kokkos", backendstr);
   #endif
  }
 #endif
  if (sf->ops->SetFromOptions) CHKERRQ((*sf->ops->SetFromOptions)(PetscOptionsObject,sf));
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@
   PetscSFSetRankOrder - sort multi-points for gathers and scatters by rank order

   Logically Collective

   Input Parameters:
+  sf - star forest
-  flg - PETSC_TRUE to sort, PETSC_FALSE to skip sorting (lower setup cost, but non-deterministic)

   Level: advanced

.seealso: PetscSFGatherBegin(), PetscSFScatterBegin()
@*/
PetscErrorCode PetscSFSetRankOrder(PetscSF sf,PetscBool flg)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscValidLogicalCollectiveBool(sf,flg,2);
  PetscCheck(!sf->multi,PetscObjectComm((PetscObject)sf),PETSC_ERR_ARG_WRONGSTATE,"Rank ordering must be set before first call to PetscSFGatherBegin() or PetscSFScatterBegin()");
  sf->rankorder = flg;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFSetGraph - Set a parallel star forest

   Collective

   Input Parameters:
+  sf - star forest
.  nroots - number of root vertices on the current process (these are possible targets for other process to attach leaves)
.  nleaves - number of leaf vertices on the current process, each of these references a root on any process
.  ilocal - locations of leaves in leafdata buffers, pass NULL for contiguous storage (locations must be >= 0, enforced
during setup in debug mode)
.  localmode - copy mode for ilocal
.  iremote - remote locations of root vertices for each leaf on the current process (locations must be >= 0, enforced
during setup in debug mode)
-  remotemode - copy mode for iremote

   Level: intermediate

   Notes:
   Leaf indices in ilocal must be unique, otherwise an error occurs.

   Input arrays ilocal and iremote follow the PetscCopyMode semantics.
   In particular, if localmode/remotemode is PETSC_OWN_POINTER or PETSC_USE_POINTER,
   PETSc might modify the respective array;
   if PETSC_USE_POINTER, the user must delete the array after PetscSFDestroy().
   Only if PETSC_COPY_VALUES is used, the respective array is guaranteed to stay intact and a const array can be passed (but a cast to non-const is needed).

   Fortran Notes:
   In Fortran you must use PETSC_COPY_VALUES for localmode and remotemode.

   Developer Notes:
   We sort leaves to check for duplicates and contiguousness and to find minleaf/maxleaf.
   This also allows to compare leaf sets of two SFs easily.

.seealso: PetscSFCreate(), PetscSFView(), PetscSFGetGraph()
@*/
PetscErrorCode PetscSFSetGraph(PetscSF sf,PetscInt nroots,PetscInt nleaves,PetscInt *ilocal,PetscCopyMode localmode,PetscSFNode *iremote,PetscCopyMode remotemode)
{
  PetscBool       unique, contiguous;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (nleaves > 0 && ilocal) PetscValidIntPointer(ilocal,4);
  if (nleaves > 0) PetscValidPointer(iremote,6);
  PetscCheckFalse(nroots  < 0,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"nroots %" PetscInt_FMT ", cannot be negative",nroots);
  PetscCheckFalse(nleaves < 0,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"nleaves %" PetscInt_FMT ", cannot be negative",nleaves);
  PetscCheck(localmode  >= PETSC_COPY_VALUES && localmode  <= PETSC_USE_POINTER,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Wrong localmode %d",localmode);
  PetscCheck(remotemode >= PETSC_COPY_VALUES && remotemode <= PETSC_USE_POINTER,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Wrong remotemode %d",remotemode);

  if (sf->nroots >= 0) { /* Reset only if graph already set */
    CHKERRQ(PetscSFReset(sf));
  }

  CHKERRQ(PetscLogEventBegin(PETSCSF_SetGraph,sf,0,0,0));

  sf->nroots  = nroots;
  sf->nleaves = nleaves;

  if (localmode == PETSC_COPY_VALUES && ilocal) {
    PetscInt *tlocal = NULL;

    CHKERRQ(PetscMalloc1(nleaves,&tlocal));
    CHKERRQ(PetscArraycpy(tlocal,ilocal,nleaves));
    ilocal = tlocal;
  }
  if (remotemode == PETSC_COPY_VALUES) {
    PetscSFNode *tremote = NULL;

    CHKERRQ(PetscMalloc1(nleaves,&tremote));
    CHKERRQ(PetscArraycpy(tremote,iremote,nleaves));
    iremote = tremote;
  }

  if (nleaves && ilocal) {
    PetscSFNode   work;

    CHKERRQ(PetscSortIntWithDataArray(nleaves, ilocal, iremote, sizeof(PetscSFNode), &work));
    CHKERRQ(PetscSortedCheckDupsInt(nleaves, ilocal, &unique));
    unique = PetscNot(unique);
    PetscCheck(sf->allow_multi_leaves || unique,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Input ilocal has duplicate entries which is not allowed for this PetscSF");
    sf->minleaf = ilocal[0];
    sf->maxleaf = ilocal[nleaves-1];
    contiguous = (PetscBool) (unique && ilocal[0] == 0 && ilocal[nleaves-1] == nleaves-1);
  } else {
    sf->minleaf = 0;
    sf->maxleaf = nleaves - 1;
    unique      = PETSC_TRUE;
    contiguous  = PETSC_TRUE;
  }

  if (contiguous) {
    if (localmode == PETSC_USE_POINTER) {
      ilocal = NULL;
    } else {
      CHKERRQ(PetscFree(ilocal));
    }
  }
  sf->mine            = ilocal;
  if (localmode == PETSC_USE_POINTER) {
    sf->mine_alloc    = NULL;
  } else {
    sf->mine_alloc    = ilocal;
  }
  sf->remote          = iremote;
  if (remotemode == PETSC_USE_POINTER) {
    sf->remote_alloc  = NULL;
  } else {
    sf->remote_alloc  = iremote;
  }
  CHKERRQ(PetscLogEventEnd(PETSCSF_SetGraph,sf,0,0,0));
  sf->graphset = PETSC_TRUE;
  PetscFunctionReturn(0);
}

/*@
  PetscSFSetGraphWithPattern - Sets the graph of an SF with a specific pattern

  Collective

  Input Parameters:
+ sf      - The PetscSF
. map     - Layout of roots over all processes (insignificant when pattern is PETSCSF_PATTERN_ALLTOALL)
- pattern - One of PETSCSF_PATTERN_ALLGATHER, PETSCSF_PATTERN_GATHER, PETSCSF_PATTERN_ALLTOALL

  Notes:
  It is easier to explain PetscSFPattern using vectors. Suppose we have an MPI vector x and its layout is map.
  n and N are local and global sizes of x respectively.

  With PETSCSF_PATTERN_ALLGATHER, the routine creates a graph that if one does Bcast on it, it will copy x to
  sequential vectors y on all ranks.

  With PETSCSF_PATTERN_GATHER, the routine creates a graph that if one does Bcast on it, it will copy x to a
  sequential vector y on rank 0.

  In above cases, entries of x are roots and entries of y are leaves.

  With PETSCSF_PATTERN_ALLTOALL, map is insignificant. Suppose NP is size of sf's communicator. The routine
  creates a graph that every rank has NP leaves and NP roots. On rank i, its leaf j is connected to root i
  of rank j. Here 0 <=i,j<NP. It is a kind of MPI_Alltoall with sendcount/recvcount being 1. Note that it does
  not mean one can not send multiple items. One just needs to create a new MPI datatype for the mulptiple data
  items with MPI_Type_contiguous() and use that as the <unit> argument in SF routines.

  In this case, roots and leaves are symmetric.

  Level: intermediate
 @*/
PetscErrorCode PetscSFSetGraphWithPattern(PetscSF sf,PetscLayout map,PetscSFPattern pattern)
{
  MPI_Comm       comm;
  PetscInt       n,N,res[2];
  PetscMPIInt    rank,size;
  PetscSFType    type;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (pattern != PETSCSF_PATTERN_ALLTOALL) PetscValidPointer(map,2);
  CHKERRQ(PetscObjectGetComm((PetscObject)sf, &comm));
  PetscCheck(pattern >= PETSCSF_PATTERN_ALLGATHER && pattern <= PETSCSF_PATTERN_ALLTOALL,comm,PETSC_ERR_ARG_OUTOFRANGE,"Unsupported PetscSFPattern %d",pattern);
  CHKERRMPI(MPI_Comm_rank(comm,&rank));
  CHKERRMPI(MPI_Comm_size(comm,&size));

  if (pattern == PETSCSF_PATTERN_ALLTOALL) {
    type = PETSCSFALLTOALL;
    CHKERRQ(PetscLayoutCreate(comm,&sf->map));
    CHKERRQ(PetscLayoutSetLocalSize(sf->map,size));
    CHKERRQ(PetscLayoutSetSize(sf->map,((PetscInt)size)*size));
    CHKERRQ(PetscLayoutSetUp(sf->map));
  } else {
    CHKERRQ(PetscLayoutGetLocalSize(map,&n));
    CHKERRQ(PetscLayoutGetSize(map,&N));
    res[0] = n;
    res[1] = -n;
    /* Check if n are same over all ranks so that we can optimize it */
    CHKERRMPI(MPIU_Allreduce(MPI_IN_PLACE,res,2,MPIU_INT,MPI_MAX,comm));
    if (res[0] == -res[1]) { /* same n */
      type = (pattern == PETSCSF_PATTERN_ALLGATHER) ? PETSCSFALLGATHER  : PETSCSFGATHER;
    } else {
      type = (pattern == PETSCSF_PATTERN_ALLGATHER) ? PETSCSFALLGATHERV : PETSCSFGATHERV;
    }
    CHKERRQ(PetscLayoutReference(map,&sf->map));
  }
  CHKERRQ(PetscSFSetType(sf,type));

  sf->pattern = pattern;
  sf->mine    = NULL; /* Contiguous */

  /* Set nleaves, nroots here in case user calls PetscSFGetGraph, which is legal to call even before PetscSFSetUp is called.
     Also set other easy stuff.
   */
  if (pattern == PETSCSF_PATTERN_ALLGATHER) {
    sf->nleaves      = N;
    sf->nroots       = n;
    sf->nranks       = size;
    sf->minleaf      = 0;
    sf->maxleaf      = N - 1;
  } else if (pattern == PETSCSF_PATTERN_GATHER) {
    sf->nleaves      = rank ? 0 : N;
    sf->nroots       = n;
    sf->nranks       = rank ? 0 : size;
    sf->minleaf      = 0;
    sf->maxleaf      = rank ? -1 : N - 1;
  } else if (pattern == PETSCSF_PATTERN_ALLTOALL) {
    sf->nleaves      = size;
    sf->nroots       = size;
    sf->nranks       = size;
    sf->minleaf      = 0;
    sf->maxleaf      = size - 1;
  }
  sf->ndranks  = 0; /* We do not need to separate out distinguished ranks for patterned graphs to improve communication performance */
  sf->graphset = PETSC_TRUE;
  PetscFunctionReturn(0);
}

/*@
   PetscSFCreateInverseSF - given a PetscSF in which all vertices have degree 1, creates the inverse map

   Collective

   Input Parameter:
.  sf - star forest to invert

   Output Parameter:
.  isf - inverse of sf

   Level: advanced

   Notes:
   All roots must have degree 1.

   The local space may be a permutation, but cannot be sparse.

.seealso: PetscSFSetGraph()
@*/
PetscErrorCode PetscSFCreateInverseSF(PetscSF sf,PetscSF *isf)
{
  PetscMPIInt    rank;
  PetscInt       i,nroots,nleaves,maxlocal,count,*newilocal;
  const PetscInt *ilocal;
  PetscSFNode    *roots,*leaves;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  PetscValidPointer(isf,2);

  CHKERRQ(PetscSFGetGraph(sf,&nroots,&nleaves,&ilocal,NULL));
  maxlocal = sf->maxleaf+1; /* TODO: We should use PetscSFGetLeafRange() */

  CHKERRMPI(MPI_Comm_rank(PetscObjectComm((PetscObject)sf),&rank));
  CHKERRQ(PetscMalloc2(nroots,&roots,maxlocal,&leaves));
  for (i=0; i<maxlocal; i++) {
    leaves[i].rank  = rank;
    leaves[i].index = i;
  }
  for (i=0; i <nroots; i++) {
    roots[i].rank  = -1;
    roots[i].index = -1;
  }
  CHKERRQ(PetscSFReduceBegin(sf,MPIU_2INT,leaves,roots,MPI_REPLACE));
  CHKERRQ(PetscSFReduceEnd(sf,MPIU_2INT,leaves,roots,MPI_REPLACE));

  /* Check whether our leaves are sparse */
  for (i=0,count=0; i<nroots; i++) if (roots[i].rank >= 0) count++;
  if (count == nroots) newilocal = NULL;
  else {                        /* Index for sparse leaves and compact "roots" array (which is to become our leaves). */
    CHKERRQ(PetscMalloc1(count,&newilocal));
    for (i=0,count=0; i<nroots; i++) {
      if (roots[i].rank >= 0) {
        newilocal[count]   = i;
        roots[count].rank  = roots[i].rank;
        roots[count].index = roots[i].index;
        count++;
      }
    }
  }

  CHKERRQ(PetscSFDuplicate(sf,PETSCSF_DUPLICATE_CONFONLY,isf));
  CHKERRQ(PetscSFSetGraph(*isf,maxlocal,count,newilocal,PETSC_OWN_POINTER,roots,PETSC_COPY_VALUES));
  CHKERRQ(PetscFree2(roots,leaves));
  PetscFunctionReturn(0);
}

/*@
   PetscSFDuplicate - duplicate a PetscSF, optionally preserving rank connectivity and graph

   Collective

   Input Parameters:
+  sf - communication object to duplicate
-  opt - PETSCSF_DUPLICATE_CONFONLY, PETSCSF_DUPLICATE_RANKS, or PETSCSF_DUPLICATE_GRAPH (see PetscSFDuplicateOption)

   Output Parameter:
.  newsf - new communication object

   Level: beginner

.seealso: PetscSFCreate(), PetscSFSetType(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFDuplicate(PetscSF sf,PetscSFDuplicateOption opt,PetscSF *newsf)
{
  PetscSFType    type;
  MPI_Datatype   dtype=MPIU_SCALAR;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscValidLogicalCollectiveEnum(sf,opt,2);
  PetscValidPointer(newsf,3);
  CHKERRQ(PetscSFCreate(PetscObjectComm((PetscObject)sf),newsf));
  CHKERRQ(PetscSFGetType(sf,&type));
  if (type) CHKERRQ(PetscSFSetType(*newsf,type));
  if (opt == PETSCSF_DUPLICATE_GRAPH) {
    PetscSFCheckGraphSet(sf,1);
    if (sf->pattern == PETSCSF_PATTERN_GENERAL) {
      PetscInt          nroots,nleaves;
      const PetscInt    *ilocal;
      const PetscSFNode *iremote;
      CHKERRQ(PetscSFGetGraph(sf,&nroots,&nleaves,&ilocal,&iremote));
      CHKERRQ(PetscSFSetGraph(*newsf,nroots,nleaves,(PetscInt*)ilocal,PETSC_COPY_VALUES,(PetscSFNode*)iremote,PETSC_COPY_VALUES));
    } else {
      CHKERRQ(PetscSFSetGraphWithPattern(*newsf,sf->map,sf->pattern));
    }
  }
  /* Since oldtype is committed, so is newtype, according to MPI */
  if (sf->vscat.bs > 1) CHKERRMPI(MPI_Type_dup(sf->vscat.unit,&dtype));
  (*newsf)->vscat.bs     = sf->vscat.bs;
  (*newsf)->vscat.unit   = dtype;
  (*newsf)->vscat.to_n   = sf->vscat.to_n;
  (*newsf)->vscat.from_n = sf->vscat.from_n;
  /* Do not copy lsf. Build it on demand since it is rarely used */

#if defined(PETSC_HAVE_DEVICE)
  (*newsf)->backend              = sf->backend;
  (*newsf)->unknown_input_stream= sf->unknown_input_stream;
  (*newsf)->use_gpu_aware_mpi    = sf->use_gpu_aware_mpi;
  (*newsf)->use_stream_aware_mpi = sf->use_stream_aware_mpi;
#endif
  if (sf->ops->Duplicate) CHKERRQ((*sf->ops->Duplicate)(sf,opt,*newsf));
  /* Don't do PetscSFSetUp() since the new sf's graph might have not been set. */
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGetGraph - Get the graph specifying a parallel star forest

   Not Collective

   Input Parameter:
.  sf - star forest

   Output Parameters:
+  nroots - number of root vertices on the current process (these are possible targets for other process to attach leaves)
.  nleaves - number of leaf vertices on the current process, each of these references a root on any process
.  ilocal - locations of leaves in leafdata buffers (if returned value is NULL, it means leaves are in contiguous storage)
-  iremote - remote locations of root vertices for each leaf on the current process

   Notes:
     We are not currently requiring that the graph is set, thus returning nroots=-1 if it has not been set yet

     The returned ilocal/iremote might contain values in different order than the input ones in PetscSFSetGraph(),
     see its manpage for details.

   Fortran Notes:
     The returned iremote array is a copy and must be deallocated after use. Consequently, if you
     want to update the graph, you must call PetscSFSetGraph() after modifying the iremote array.

     To check for a NULL ilocal use
$      if (loc(ilocal) == loc(PETSC_NULL_INTEGER)) then

   Level: intermediate

.seealso: PetscSFCreate(), PetscSFView(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFGetGraph(PetscSF sf,PetscInt *nroots,PetscInt *nleaves,const PetscInt **ilocal,const PetscSFNode **iremote)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (sf->ops->GetGraph) {
    CHKERRQ((sf->ops->GetGraph)(sf,nroots,nleaves,ilocal,iremote));
  } else {
    if (nroots) *nroots = sf->nroots;
    if (nleaves) *nleaves = sf->nleaves;
    if (ilocal) *ilocal = sf->mine;
    if (iremote) *iremote = sf->remote;
  }
  PetscFunctionReturn(0);
}

/*@
   PetscSFGetLeafRange - Get the active leaf ranges

   Not Collective

   Input Parameter:
.  sf - star forest

   Output Parameters:
+  minleaf - minimum active leaf on this process. Return 0 if there are no leaves.
-  maxleaf - maximum active leaf on this process. Return -1 if there are no leaves.

   Level: developer

.seealso: PetscSFCreate(), PetscSFView(), PetscSFSetGraph(), PetscSFGetGraph()
@*/
PetscErrorCode PetscSFGetLeafRange(PetscSF sf,PetscInt *minleaf,PetscInt *maxleaf)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  if (minleaf) *minleaf = sf->minleaf;
  if (maxleaf) *maxleaf = sf->maxleaf;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFViewFromOptions - View from Options

   Collective on PetscSF

   Input Parameters:
+  A - the star forest
.  obj - Optional object
-  name - command line option

   Level: intermediate
.seealso:  PetscSF, PetscSFView, PetscObjectViewFromOptions(), PetscSFCreate()
@*/
PetscErrorCode  PetscSFViewFromOptions(PetscSF A,PetscObject obj,const char name[])
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(A,PETSCSF_CLASSID,1);
  CHKERRQ(PetscObjectViewFromOptions((PetscObject)A,obj,name));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFView - view a star forest

   Collective

   Input Parameters:
+  sf - star forest
-  viewer - viewer to display graph, for example PETSC_VIEWER_STDOUT_WORLD

   Level: beginner

.seealso: PetscSFCreate(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFView(PetscSF sf,PetscViewer viewer)
{
  PetscBool         iascii;
  PetscViewerFormat format;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (!viewer) CHKERRQ(PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)sf),&viewer));
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(sf,1,viewer,2);
  if (sf->graphset) CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii));
  if (iascii && viewer->format != PETSC_VIEWER_ASCII_MATLAB) {
    PetscMPIInt rank;
    PetscInt    ii,i,j;

    CHKERRQ(PetscObjectPrintClassNamePrefixType((PetscObject)sf,viewer));
    CHKERRQ(PetscViewerASCIIPushTab(viewer));
    if (sf->pattern == PETSCSF_PATTERN_GENERAL) {
      if (!sf->graphset) {
        CHKERRQ(PetscViewerASCIIPrintf(viewer,"PetscSFSetGraph() has not been called yet\n"));
        CHKERRQ(PetscViewerASCIIPopTab(viewer));
        PetscFunctionReturn(0);
      }
      CHKERRMPI(MPI_Comm_rank(PetscObjectComm((PetscObject)sf),&rank));
      CHKERRQ(PetscViewerASCIIPushSynchronized(viewer));
      CHKERRQ(PetscViewerASCIISynchronizedPrintf(viewer,"[%d] Number of roots=%" PetscInt_FMT ", leaves=%" PetscInt_FMT ", remote ranks=%" PetscInt_FMT "\n",rank,sf->nroots,sf->nleaves,sf->nranks));
      for (i=0; i<sf->nleaves; i++) {
        CHKERRQ(PetscViewerASCIISynchronizedPrintf(viewer,"[%d] %" PetscInt_FMT " <- (%" PetscInt_FMT ",%" PetscInt_FMT ")\n",rank,sf->mine ? sf->mine[i] : i,sf->remote[i].rank,sf->remote[i].index));
      }
      CHKERRQ(PetscViewerFlush(viewer));
      CHKERRQ(PetscViewerGetFormat(viewer,&format));
      if (format == PETSC_VIEWER_ASCII_INFO_DETAIL) {
        PetscMPIInt *tmpranks,*perm;
        CHKERRQ(PetscMalloc2(sf->nranks,&tmpranks,sf->nranks,&perm));
        CHKERRQ(PetscArraycpy(tmpranks,sf->ranks,sf->nranks));
        for (i=0; i<sf->nranks; i++) perm[i] = i;
        CHKERRQ(PetscSortMPIIntWithArray(sf->nranks,tmpranks,perm));
        CHKERRQ(PetscViewerASCIISynchronizedPrintf(viewer,"[%d] Roots referenced by my leaves, by rank\n",rank));
        for (ii=0; ii<sf->nranks; ii++) {
          i = perm[ii];
          CHKERRQ(PetscViewerASCIISynchronizedPrintf(viewer,"[%d] %d: %" PetscInt_FMT " edges\n",rank,sf->ranks[i],sf->roffset[i+1]-sf->roffset[i]));
          for (j=sf->roffset[i]; j<sf->roffset[i+1]; j++) {
            CHKERRQ(PetscViewerASCIISynchronizedPrintf(viewer,"[%d]    %" PetscInt_FMT " <- %" PetscInt_FMT "\n",rank,sf->rmine[j],sf->rremote[j]));
          }
        }
        CHKERRQ(PetscFree2(tmpranks,perm));
      }
      CHKERRQ(PetscViewerFlush(viewer));
      CHKERRQ(PetscViewerASCIIPopSynchronized(viewer));
    }
    CHKERRQ(PetscViewerASCIIPopTab(viewer));
  }
  if (sf->ops->View) CHKERRQ((*sf->ops->View)(sf,viewer));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGetRootRanks - Get root ranks and number of vertices referenced by leaves on this process

   Not Collective

   Input Parameter:
.  sf - star forest

   Output Parameters:
+  nranks - number of ranks referenced by local part
.  ranks - array of ranks
.  roffset - offset in rmine/rremote for each rank (length nranks+1)
.  rmine - concatenated array holding local indices referencing each remote rank
-  rremote - concatenated array holding remote indices referenced for each remote rank

   Level: developer

.seealso: PetscSFGetLeafRanks()
@*/
PetscErrorCode PetscSFGetRootRanks(PetscSF sf,PetscInt *nranks,const PetscMPIInt **ranks,const PetscInt **roffset,const PetscInt **rmine,const PetscInt **rremote)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscCheck(sf->setupcalled,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Must call PetscSFSetUp() before obtaining ranks");
  if (sf->ops->GetRootRanks) {
    CHKERRQ((sf->ops->GetRootRanks)(sf,nranks,ranks,roffset,rmine,rremote));
  } else {
    /* The generic implementation */
    if (nranks)  *nranks  = sf->nranks;
    if (ranks)   *ranks   = sf->ranks;
    if (roffset) *roffset = sf->roffset;
    if (rmine)   *rmine   = sf->rmine;
    if (rremote) *rremote = sf->rremote;
  }
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGetLeafRanks - Get leaf ranks referencing roots on this process

   Not Collective

   Input Parameter:
.  sf - star forest

   Output Parameters:
+  niranks - number of leaf ranks referencing roots on this process
.  iranks - array of ranks
.  ioffset - offset in irootloc for each rank (length niranks+1)
-  irootloc - concatenated array holding local indices of roots referenced by each leaf rank

   Level: developer

.seealso: PetscSFGetRootRanks()
@*/
PetscErrorCode PetscSFGetLeafRanks(PetscSF sf,PetscInt *niranks,const PetscMPIInt **iranks,const PetscInt **ioffset,const PetscInt **irootloc)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscCheck(sf->setupcalled,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Must call PetscSFSetUp() before obtaining ranks");
  if (sf->ops->GetLeafRanks) {
    CHKERRQ((sf->ops->GetLeafRanks)(sf,niranks,iranks,ioffset,irootloc));
  } else {
    PetscSFType type;
    CHKERRQ(PetscSFGetType(sf,&type));
    SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"PetscSFGetLeafRanks() is not supported on this StarForest type: %s", type);
  }
  PetscFunctionReturn(0);
}

static PetscBool InList(PetscMPIInt needle,PetscMPIInt n,const PetscMPIInt *list) {
  PetscInt i;
  for (i=0; i<n; i++) {
    if (needle == list[i]) return PETSC_TRUE;
  }
  return PETSC_FALSE;
}

/*@C
   PetscSFSetUpRanks - Set up data structures associated with ranks; this is for internal use by PetscSF implementations.

   Collective

   Input Parameters:
+  sf - PetscSF to set up; PetscSFSetGraph() must have been called
-  dgroup - MPI_Group of ranks to be distinguished (e.g., for self or shared memory exchange)

   Level: developer

.seealso: PetscSFGetRootRanks()
@*/
PetscErrorCode PetscSFSetUpRanks(PetscSF sf,MPI_Group dgroup)
{
  PetscTable         table;
  PetscTablePosition pos;
  PetscMPIInt        size,groupsize,*groupranks;
  PetscInt           *rcount,*ranks;
  PetscInt           i, irank = -1,orank = -1;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  CHKERRMPI(MPI_Comm_size(PetscObjectComm((PetscObject)sf),&size));
  CHKERRQ(PetscTableCreate(10,size,&table));
  for (i=0; i<sf->nleaves; i++) {
    /* Log 1-based rank */
    CHKERRQ(PetscTableAdd(table,sf->remote[i].rank+1,1,ADD_VALUES));
  }
  CHKERRQ(PetscTableGetCount(table,&sf->nranks));
  CHKERRQ(PetscMalloc4(sf->nranks,&sf->ranks,sf->nranks+1,&sf->roffset,sf->nleaves,&sf->rmine,sf->nleaves,&sf->rremote));
  CHKERRQ(PetscMalloc2(sf->nranks,&rcount,sf->nranks,&ranks));
  CHKERRQ(PetscTableGetHeadPosition(table,&pos));
  for (i=0; i<sf->nranks; i++) {
    CHKERRQ(PetscTableGetNext(table,&pos,&ranks[i],&rcount[i]));
    ranks[i]--;             /* Convert back to 0-based */
  }
  CHKERRQ(PetscTableDestroy(&table));

  /* We expect that dgroup is reliably "small" while nranks could be large */
  {
    MPI_Group group = MPI_GROUP_NULL;
    PetscMPIInt *dgroupranks;
    CHKERRMPI(MPI_Comm_group(PetscObjectComm((PetscObject)sf),&group));
    CHKERRMPI(MPI_Group_size(dgroup,&groupsize));
    CHKERRQ(PetscMalloc1(groupsize,&dgroupranks));
    CHKERRQ(PetscMalloc1(groupsize,&groupranks));
    for (i=0; i<groupsize; i++) dgroupranks[i] = i;
    if (groupsize) CHKERRMPI(MPI_Group_translate_ranks(dgroup,groupsize,dgroupranks,group,groupranks));
    CHKERRMPI(MPI_Group_free(&group));
    CHKERRQ(PetscFree(dgroupranks));
  }

  /* Partition ranks[] into distinguished (first sf->ndranks) followed by non-distinguished */
  for (sf->ndranks=0,i=sf->nranks; sf->ndranks<i;) {
    for (i--; sf->ndranks<i; i--) { /* Scan i backward looking for distinguished rank */
      if (InList(ranks[i],groupsize,groupranks)) break;
    }
    for (; sf->ndranks<=i; sf->ndranks++) { /* Scan sf->ndranks forward looking for non-distinguished rank */
      if (!InList(ranks[sf->ndranks],groupsize,groupranks)) break;
    }
    if (sf->ndranks < i) {                         /* Swap ranks[sf->ndranks] with ranks[i] */
      PetscInt tmprank,tmpcount;

      tmprank             = ranks[i];
      tmpcount            = rcount[i];
      ranks[i]            = ranks[sf->ndranks];
      rcount[i]           = rcount[sf->ndranks];
      ranks[sf->ndranks]  = tmprank;
      rcount[sf->ndranks] = tmpcount;
      sf->ndranks++;
    }
  }
  CHKERRQ(PetscFree(groupranks));
  CHKERRQ(PetscSortIntWithArray(sf->ndranks,ranks,rcount));
  CHKERRQ(PetscSortIntWithArray(sf->nranks-sf->ndranks,ranks+sf->ndranks,rcount+sf->ndranks));
  sf->roffset[0] = 0;
  for (i=0; i<sf->nranks; i++) {
    CHKERRQ(PetscMPIIntCast(ranks[i],sf->ranks+i));
    sf->roffset[i+1] = sf->roffset[i] + rcount[i];
    rcount[i]        = 0;
  }
  for (i=0, irank = -1, orank = -1; i<sf->nleaves; i++) {
    /* short circuit */
    if (orank != sf->remote[i].rank) {
      /* Search for index of iremote[i].rank in sf->ranks */
      CHKERRQ(PetscFindMPIInt(sf->remote[i].rank,sf->ndranks,sf->ranks,&irank));
      if (irank < 0) {
        CHKERRQ(PetscFindMPIInt(sf->remote[i].rank,sf->nranks-sf->ndranks,sf->ranks+sf->ndranks,&irank));
        if (irank >= 0) irank += sf->ndranks;
      }
      orank = sf->remote[i].rank;
    }
    PetscCheckFalse(irank < 0,PETSC_COMM_SELF,PETSC_ERR_PLIB,"Could not find rank %" PetscInt_FMT " in array",sf->remote[i].rank);
    sf->rmine[sf->roffset[irank] + rcount[irank]]   = sf->mine ? sf->mine[i] : i;
    sf->rremote[sf->roffset[irank] + rcount[irank]] = sf->remote[i].index;
    rcount[irank]++;
  }
  CHKERRQ(PetscFree2(rcount,ranks));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGetGroups - gets incoming and outgoing process groups

   Collective

   Input Parameter:
.  sf - star forest

   Output Parameters:
+  incoming - group of origin processes for incoming edges (leaves that reference my roots)
-  outgoing - group of destination processes for outgoing edges (roots that I reference)

   Level: developer

.seealso: PetscSFGetWindow(), PetscSFRestoreWindow()
@*/
PetscErrorCode PetscSFGetGroups(PetscSF sf,MPI_Group *incoming,MPI_Group *outgoing)
{
  MPI_Group      group = MPI_GROUP_NULL;

  PetscFunctionBegin;
  PetscCheckFalse(sf->nranks < 0,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"Must call PetscSFSetUpRanks() before obtaining groups");
  if (sf->ingroup == MPI_GROUP_NULL) {
    PetscInt       i;
    const PetscInt *indegree;
    PetscMPIInt    rank,*outranks,*inranks;
    PetscSFNode    *remote;
    PetscSF        bgcount;

    /* Compute the number of incoming ranks */
    CHKERRQ(PetscMalloc1(sf->nranks,&remote));
    for (i=0; i<sf->nranks; i++) {
      remote[i].rank  = sf->ranks[i];
      remote[i].index = 0;
    }
    CHKERRQ(PetscSFDuplicate(sf,PETSCSF_DUPLICATE_CONFONLY,&bgcount));
    CHKERRQ(PetscSFSetGraph(bgcount,1,sf->nranks,NULL,PETSC_COPY_VALUES,remote,PETSC_OWN_POINTER));
    CHKERRQ(PetscSFComputeDegreeBegin(bgcount,&indegree));
    CHKERRQ(PetscSFComputeDegreeEnd(bgcount,&indegree));
    /* Enumerate the incoming ranks */
    CHKERRQ(PetscMalloc2(indegree[0],&inranks,sf->nranks,&outranks));
    CHKERRMPI(MPI_Comm_rank(PetscObjectComm((PetscObject)sf),&rank));
    for (i=0; i<sf->nranks; i++) outranks[i] = rank;
    CHKERRQ(PetscSFGatherBegin(bgcount,MPI_INT,outranks,inranks));
    CHKERRQ(PetscSFGatherEnd(bgcount,MPI_INT,outranks,inranks));
    CHKERRMPI(MPI_Comm_group(PetscObjectComm((PetscObject)sf),&group));
    CHKERRMPI(MPI_Group_incl(group,indegree[0],inranks,&sf->ingroup));
    CHKERRMPI(MPI_Group_free(&group));
    CHKERRQ(PetscFree2(inranks,outranks));
    CHKERRQ(PetscSFDestroy(&bgcount));
  }
  *incoming = sf->ingroup;

  if (sf->outgroup == MPI_GROUP_NULL) {
    CHKERRMPI(MPI_Comm_group(PetscObjectComm((PetscObject)sf),&group));
    CHKERRMPI(MPI_Group_incl(group,sf->nranks,sf->ranks,&sf->outgroup));
    CHKERRMPI(MPI_Group_free(&group));
  }
  *outgoing = sf->outgroup;
  PetscFunctionReturn(0);
}

/*@
   PetscSFGetMultiSF - gets the inner SF implementing gathers and scatters

   Collective

   Input Parameter:
.  sf - star forest that may contain roots with 0 or with more than 1 vertex

   Output Parameter:
.  multi - star forest with split roots, such that each root has degree exactly 1

   Level: developer

   Notes:

   In most cases, users should use PetscSFGatherBegin() and PetscSFScatterBegin() instead of manipulating multi
   directly. Since multi satisfies the stronger condition that each entry in the global space has exactly one incoming
   edge, it is a candidate for future optimization that might involve its removal.

.seealso: PetscSFSetGraph(), PetscSFGatherBegin(), PetscSFScatterBegin(), PetscSFComputeMultiRootOriginalNumbering()
@*/
PetscErrorCode PetscSFGetMultiSF(PetscSF sf,PetscSF *multi)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscValidPointer(multi,2);
  if (sf->nroots < 0) {         /* Graph has not been set yet; why do we need this? */
    CHKERRQ(PetscSFDuplicate(sf,PETSCSF_DUPLICATE_RANKS,&sf->multi));
    *multi = sf->multi;
    sf->multi->multi = sf->multi;
    PetscFunctionReturn(0);
  }
  if (!sf->multi) {
    const PetscInt *indegree;
    PetscInt       i,*inoffset,*outones,*outoffset,maxlocal;
    PetscSFNode    *remote;
    maxlocal = sf->maxleaf+1; /* TODO: We should use PetscSFGetLeafRange() */
    CHKERRQ(PetscSFComputeDegreeBegin(sf,&indegree));
    CHKERRQ(PetscSFComputeDegreeEnd(sf,&indegree));
    CHKERRQ(PetscMalloc3(sf->nroots+1,&inoffset,maxlocal,&outones,maxlocal,&outoffset));
    inoffset[0] = 0;
    for (i=0; i<sf->nroots; i++) inoffset[i+1] = inoffset[i] + indegree[i];
    for (i=0; i<maxlocal; i++) outones[i] = 1;
    CHKERRQ(PetscSFFetchAndOpBegin(sf,MPIU_INT,inoffset,outones,outoffset,MPI_SUM));
    CHKERRQ(PetscSFFetchAndOpEnd(sf,MPIU_INT,inoffset,outones,outoffset,MPI_SUM));
    for (i=0; i<sf->nroots; i++) inoffset[i] -= indegree[i]; /* Undo the increment */
    if (PetscDefined(USE_DEBUG)) { /* Check that the expected number of increments occurred */
      for (i=0; i<sf->nroots; i++) {
        PetscCheckFalse(inoffset[i] + indegree[i] != inoffset[i+1],PETSC_COMM_SELF,PETSC_ERR_PLIB,"Incorrect result after PetscSFFetchAndOp");
      }
    }
    CHKERRQ(PetscMalloc1(sf->nleaves,&remote));
    for (i=0; i<sf->nleaves; i++) {
      remote[i].rank  = sf->remote[i].rank;
      remote[i].index = outoffset[sf->mine ? sf->mine[i] : i];
    }
    CHKERRQ(PetscSFDuplicate(sf,PETSCSF_DUPLICATE_RANKS,&sf->multi));
    sf->multi->multi = sf->multi;
    CHKERRQ(PetscSFSetGraph(sf->multi,inoffset[sf->nroots],sf->nleaves,sf->mine,PETSC_COPY_VALUES,remote,PETSC_OWN_POINTER));
    if (sf->rankorder) {        /* Sort the ranks */
      PetscMPIInt rank;
      PetscInt    *inranks,*newoffset,*outranks,*newoutoffset,*tmpoffset,maxdegree;
      PetscSFNode *newremote;
      CHKERRMPI(MPI_Comm_rank(PetscObjectComm((PetscObject)sf),&rank));
      for (i=0,maxdegree=0; i<sf->nroots; i++) maxdegree = PetscMax(maxdegree,indegree[i]);
      CHKERRQ(PetscMalloc5(sf->multi->nroots,&inranks,sf->multi->nroots,&newoffset,maxlocal,&outranks,maxlocal,&newoutoffset,maxdegree,&tmpoffset));
      for (i=0; i<maxlocal; i++) outranks[i] = rank;
      CHKERRQ(PetscSFReduceBegin(sf->multi,MPIU_INT,outranks,inranks,MPI_REPLACE));
      CHKERRQ(PetscSFReduceEnd(sf->multi,MPIU_INT,outranks,inranks,MPI_REPLACE));
      /* Sort the incoming ranks at each vertex, build the inverse map */
      for (i=0; i<sf->nroots; i++) {
        PetscInt j;
        for (j=0; j<indegree[i]; j++) tmpoffset[j] = j;
        CHKERRQ(PetscSortIntWithArray(indegree[i],inranks+inoffset[i],tmpoffset));
        for (j=0; j<indegree[i]; j++) newoffset[inoffset[i] + tmpoffset[j]] = inoffset[i] + j;
      }
      CHKERRQ(PetscSFBcastBegin(sf->multi,MPIU_INT,newoffset,newoutoffset,MPI_REPLACE));
      CHKERRQ(PetscSFBcastEnd(sf->multi,MPIU_INT,newoffset,newoutoffset,MPI_REPLACE));
      CHKERRQ(PetscMalloc1(sf->nleaves,&newremote));
      for (i=0; i<sf->nleaves; i++) {
        newremote[i].rank  = sf->remote[i].rank;
        newremote[i].index = newoutoffset[sf->mine ? sf->mine[i] : i];
      }
      CHKERRQ(PetscSFSetGraph(sf->multi,inoffset[sf->nroots],sf->nleaves,sf->mine,PETSC_COPY_VALUES,newremote,PETSC_OWN_POINTER));
      CHKERRQ(PetscFree5(inranks,newoffset,outranks,newoutoffset,tmpoffset));
    }
    CHKERRQ(PetscFree3(inoffset,outones,outoffset));
  }
  *multi = sf->multi;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFCreateEmbeddedRootSF - removes edges from all but the selected roots, does not remap indices

   Collective

   Input Parameters:
+  sf - original star forest
.  nselected  - number of selected roots on this process
-  selected   - indices of the selected roots on this process

   Output Parameter:
.  esf - new star forest

   Level: advanced

   Note:
   To use the new PetscSF, it may be necessary to know the indices of the leaves that are still participating. This can
   be done by calling PetscSFGetGraph().

.seealso: PetscSFSetGraph(), PetscSFGetGraph()
@*/
PetscErrorCode PetscSFCreateEmbeddedRootSF(PetscSF sf,PetscInt nselected,const PetscInt *selected,PetscSF *esf)
{
  PetscInt          i,j,n,nroots,nleaves,esf_nleaves,*new_ilocal,minleaf,maxleaf,maxlocal;
  const PetscInt    *ilocal;
  signed char       *rootdata,*leafdata,*leafmem;
  const PetscSFNode *iremote;
  PetscSFNode       *new_iremote;
  MPI_Comm          comm;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  if (nselected) PetscValidIntPointer(selected,3);
  PetscValidPointer(esf,4);

  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscLogEventBegin(PETSCSF_EmbedSF,sf,0,0,0));
  CHKERRQ(PetscObjectGetComm((PetscObject)sf,&comm));
  CHKERRQ(PetscSFGetGraph(sf,&nroots,&nleaves,&ilocal,&iremote));

  if (PetscDefined(USE_DEBUG)) {  /* Error out if selected[] has dups or  out of range indices */
    PetscBool dups;
    CHKERRQ(PetscCheckDupsInt(nselected,selected,&dups));
    PetscCheck(!dups,comm,PETSC_ERR_ARG_WRONG,"selected[] has dups");
    for (i=0; i<nselected; i++)
      PetscCheckFalse(selected[i] < 0 || selected[i] >= nroots,comm,PETSC_ERR_ARG_OUTOFRANGE,"selected root indice %" PetscInt_FMT " is out of [0,%" PetscInt_FMT ")",selected[i],nroots);
  }

  if (sf->ops->CreateEmbeddedRootSF) {
    CHKERRQ((*sf->ops->CreateEmbeddedRootSF)(sf,nselected,selected,esf));
  } else {
    /* A generic version of creating embedded sf */
    CHKERRQ(PetscSFGetLeafRange(sf,&minleaf,&maxleaf));
    maxlocal = maxleaf - minleaf + 1;
    CHKERRQ(PetscCalloc2(nroots,&rootdata,maxlocal,&leafmem));
    leafdata = leafmem - minleaf;
    /* Tag selected roots and bcast to leaves */
    for (i=0; i<nselected; i++) rootdata[selected[i]] = 1;
    CHKERRQ(PetscSFBcastBegin(sf,MPI_SIGNED_CHAR,rootdata,leafdata,MPI_REPLACE));
    CHKERRQ(PetscSFBcastEnd(sf,MPI_SIGNED_CHAR,rootdata,leafdata,MPI_REPLACE));

    /* Build esf with leaves that are still connected */
    esf_nleaves = 0;
    for (i=0; i<nleaves; i++) {
      j = ilocal ? ilocal[i] : i;
      /* esf_nleaves += leafdata[j] should work in theory, but failed with SFWindow bugs
         with PetscSFBcast. See https://gitlab.com/petsc/petsc/issues/555
      */
      esf_nleaves += (leafdata[j] ? 1 : 0);
    }
    CHKERRQ(PetscMalloc1(esf_nleaves,&new_ilocal));
    CHKERRQ(PetscMalloc1(esf_nleaves,&new_iremote));
    for (i=n=0; i<nleaves; i++) {
      j = ilocal ? ilocal[i] : i;
      if (leafdata[j]) {
        new_ilocal[n]        = j;
        new_iremote[n].rank  = iremote[i].rank;
        new_iremote[n].index = iremote[i].index;
        ++n;
      }
    }
    CHKERRQ(PetscSFCreate(comm,esf));
    CHKERRQ(PetscSFSetFromOptions(*esf));
    CHKERRQ(PetscSFSetGraph(*esf,nroots,esf_nleaves,new_ilocal,PETSC_OWN_POINTER,new_iremote,PETSC_OWN_POINTER));
    CHKERRQ(PetscFree2(rootdata,leafmem));
  }
  CHKERRQ(PetscLogEventEnd(PETSCSF_EmbedSF,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
  PetscSFCreateEmbeddedLeafSF - removes edges from all but the selected leaves, does not remap indices

  Collective

  Input Parameters:
+ sf - original star forest
. nselected  - number of selected leaves on this process
- selected   - indices of the selected leaves on this process

  Output Parameter:
.  newsf - new star forest

  Level: advanced

.seealso: PetscSFCreateEmbeddedRootSF(), PetscSFSetGraph(), PetscSFGetGraph()
@*/
PetscErrorCode PetscSFCreateEmbeddedLeafSF(PetscSF sf,PetscInt nselected,const PetscInt *selected,PetscSF *newsf)
{
  const PetscSFNode *iremote;
  PetscSFNode       *new_iremote;
  const PetscInt    *ilocal;
  PetscInt          i,nroots,*leaves,*new_ilocal;
  MPI_Comm          comm;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  if (nselected) PetscValidIntPointer(selected,3);
  PetscValidPointer(newsf,4);

  /* Uniq selected[] and put results in leaves[] */
  CHKERRQ(PetscObjectGetComm((PetscObject)sf,&comm));
  CHKERRQ(PetscMalloc1(nselected,&leaves));
  CHKERRQ(PetscArraycpy(leaves,selected,nselected));
  CHKERRQ(PetscSortedRemoveDupsInt(&nselected,leaves));
  PetscCheckFalse(nselected && (leaves[0] < 0 || leaves[nselected-1] >= sf->nleaves),comm,PETSC_ERR_ARG_OUTOFRANGE,"Min/Max leaf indices %" PetscInt_FMT "/%" PetscInt_FMT " are not in [0,%" PetscInt_FMT ")",leaves[0],leaves[nselected-1],sf->nleaves);

  /* Optimize the routine only when sf is setup and hence we can reuse sf's communication pattern */
  if (sf->setupcalled && sf->ops->CreateEmbeddedLeafSF) {
    CHKERRQ((*sf->ops->CreateEmbeddedLeafSF)(sf,nselected,leaves,newsf));
  } else {
    CHKERRQ(PetscSFGetGraph(sf,&nroots,NULL,&ilocal,&iremote));
    CHKERRQ(PetscMalloc1(nselected,&new_ilocal));
    CHKERRQ(PetscMalloc1(nselected,&new_iremote));
    for (i=0; i<nselected; ++i) {
      const PetscInt l     = leaves[i];
      new_ilocal[i]        = ilocal ? ilocal[l] : l;
      new_iremote[i].rank  = iremote[l].rank;
      new_iremote[i].index = iremote[l].index;
    }
    CHKERRQ(PetscSFDuplicate(sf,PETSCSF_DUPLICATE_CONFONLY,newsf));
    CHKERRQ(PetscSFSetGraph(*newsf,nroots,nselected,new_ilocal,PETSC_OWN_POINTER,new_iremote,PETSC_OWN_POINTER));
  }
  CHKERRQ(PetscFree(leaves));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFBcastBegin - begin pointwise broadcast with root value being reduced to leaf value, to be concluded with call to PetscSFBcastEnd()

   Collective on PetscSF

   Input Parameters:
+  sf - star forest on which to communicate
.  unit - data type associated with each node
.  rootdata - buffer to broadcast
-  op - operation to use for reduction

   Output Parameter:
.  leafdata - buffer to be reduced with values from each leaf's respective root

   Level: intermediate

   Notes:
    When petsc is configured with device support, it will use its own mechanism to figure out whether the given data pointers
    are host pointers or device pointers, which may incur a noticable cost. If you already knew the info, you should
    use PetscSFBcastWithMemTypeBegin() instead.
.seealso: PetscSFBcastEnd(), PetscSFBcastWithMemTypeBegin()
@*/
PetscErrorCode PetscSFBcastBegin(PetscSF sf,MPI_Datatype unit,const void *rootdata,void *leafdata,MPI_Op op)
{
  PetscMemType   rootmtype,leafmtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_BcastBegin,sf,0,0,0));
  CHKERRQ(PetscGetMemType(rootdata,&rootmtype));
  CHKERRQ(PetscGetMemType(leafdata,&leafmtype));
  CHKERRQ((*sf->ops->BcastBegin)(sf,unit,rootmtype,rootdata,leafmtype,leafdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_BcastBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFBcastWithMemTypeBegin - begin pointwise broadcast with root value being reduced to leaf value with explicit memory types, to be concluded with call to PetscSFBcastEnd()

   Collective on PetscSF

   Input Parameters:
+  sf - star forest on which to communicate
.  unit - data type associated with each node
.  rootmtype - memory type of rootdata
.  rootdata - buffer to broadcast
.  leafmtype - memory type of leafdata
-  op - operation to use for reduction

   Output Parameter:
.  leafdata - buffer to be reduced with values from each leaf's respective root

   Level: intermediate

.seealso: PetscSFBcastEnd(), PetscSFBcastBegin()
@*/
PetscErrorCode PetscSFBcastWithMemTypeBegin(PetscSF sf,MPI_Datatype unit,PetscMemType rootmtype,const void *rootdata,PetscMemType leafmtype,void *leafdata,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_BcastBegin,sf,0,0,0));
  CHKERRQ((*sf->ops->BcastBegin)(sf,unit,rootmtype,rootdata,leafmtype,leafdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_BcastBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFBcastEnd - end a broadcast & reduce operation started with PetscSFBcastBegin()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  rootdata - buffer to broadcast
-  op - operation to use for reduction

   Output Parameter:
.  leafdata - buffer to be reduced with values from each leaf's respective root

   Level: intermediate

.seealso: PetscSFSetGraph(), PetscSFReduceEnd()
@*/
PetscErrorCode PetscSFBcastEnd(PetscSF sf,MPI_Datatype unit,const void *rootdata,void *leafdata,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_BcastEnd,sf,0,0,0));
  CHKERRQ((*sf->ops->BcastEnd)(sf,unit,rootdata,leafdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_BcastEnd,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFReduceBegin - begin reduction of leafdata into rootdata, to be completed with call to PetscSFReduceEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  leafdata - values to reduce
-  op - reduction operation

   Output Parameter:
.  rootdata - result of reduction of values from all leaves of each root

   Level: intermediate

   Notes:
    When petsc is configured with device support, it will use its own mechanism to figure out whether the given data pointers
    are host pointers or device pointers, which may incur a noticable cost. If you already knew the info, you should
    use PetscSFReduceWithMemTypeBegin() instead.

.seealso: PetscSFBcastBegin(), PetscSFReduceWithMemTypeBegin()
@*/
PetscErrorCode PetscSFReduceBegin(PetscSF sf,MPI_Datatype unit,const void *leafdata,void *rootdata,MPI_Op op)
{
  PetscMemType   rootmtype,leafmtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_ReduceBegin,sf,0,0,0));
  CHKERRQ(PetscGetMemType(rootdata,&rootmtype));
  CHKERRQ(PetscGetMemType(leafdata,&leafmtype));
  CHKERRQ((sf->ops->ReduceBegin)(sf,unit,leafmtype,leafdata,rootmtype,rootdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_ReduceBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFReduceWithMemTypeBegin - begin reduction of leafdata into rootdata with explicit memory types, to be completed with call to PetscSFReduceEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  leafmtype - memory type of leafdata
.  leafdata - values to reduce
.  rootmtype - memory type of rootdata
-  op - reduction operation

   Output Parameter:
.  rootdata - result of reduction of values from all leaves of each root

   Level: intermediate

.seealso: PetscSFBcastBegin(), PetscSFReduceBegin()
@*/
PetscErrorCode PetscSFReduceWithMemTypeBegin(PetscSF sf,MPI_Datatype unit,PetscMemType leafmtype,const void *leafdata,PetscMemType rootmtype,void *rootdata,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_ReduceBegin,sf,0,0,0));
  CHKERRQ((sf->ops->ReduceBegin)(sf,unit,leafmtype,leafdata,rootmtype,rootdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_ReduceBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFReduceEnd - end a reduction operation started with PetscSFReduceBegin()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  leafdata - values to reduce
-  op - reduction operation

   Output Parameter:
.  rootdata - result of reduction of values from all leaves of each root

   Level: intermediate

.seealso: PetscSFSetGraph(), PetscSFBcastEnd()
@*/
PetscErrorCode PetscSFReduceEnd(PetscSF sf,MPI_Datatype unit,const void *leafdata,void *rootdata,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventBegin(PETSCSF_ReduceEnd,sf,0,0,0));
  CHKERRQ((*sf->ops->ReduceEnd)(sf,unit,leafdata,rootdata,op));
  if (!sf->vscat.logging) CHKERRQ(PetscLogEventEnd(PETSCSF_ReduceEnd,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFFetchAndOpBegin - begin operation that fetches values from root and updates atomically by applying operation using my leaf value, to be completed with PetscSFFetchAndOpEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  leafdata - leaf values to use in reduction
-  op - operation to use for reduction

   Output Parameters:
+  rootdata - root values to be updated, input state is seen by first process to perform an update
-  leafupdate - state at each leaf's respective root immediately prior to my atomic update

   Level: advanced

   Note:
   The update is only atomic at the granularity provided by the hardware. Different roots referenced by the same process
   might be updated in a different order. Furthermore, if a composite type is used for the unit datatype, atomicity is
   not guaranteed across the whole vertex. Therefore, this function is mostly only used with primitive types such as
   integers.

.seealso: PetscSFComputeDegreeBegin(), PetscSFReduceBegin(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFFetchAndOpBegin(PetscSF sf,MPI_Datatype unit,void *rootdata,const void *leafdata,void *leafupdate,MPI_Op op)
{
  PetscMemType   rootmtype,leafmtype,leafupdatemtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscLogEventBegin(PETSCSF_FetchAndOpBegin,sf,0,0,0));
  CHKERRQ(PetscGetMemType(rootdata,&rootmtype));
  CHKERRQ(PetscGetMemType(leafdata,&leafmtype));
  CHKERRQ(PetscGetMemType(leafupdate,&leafupdatemtype));
  PetscCheckFalse(leafmtype != leafupdatemtype,PETSC_COMM_SELF,PETSC_ERR_SUP,"No support for leafdata and leafupdate in different memory types");
  CHKERRQ((*sf->ops->FetchAndOpBegin)(sf,unit,rootmtype,rootdata,leafmtype,leafdata,leafupdate,op));
  CHKERRQ(PetscLogEventEnd(PETSCSF_FetchAndOpBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFFetchAndOpWithMemTypeBegin - begin operation with explicit memory types that fetches values from root and updates atomically by applying operation using my leaf value, to be completed with PetscSFFetchAndOpEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  rootmtype - memory type of rootdata
.  leafmtype - memory type of leafdata
.  leafdata - leaf values to use in reduction
.  leafupdatemtype - memory type of leafupdate
-  op - operation to use for reduction

   Output Parameters:
+  rootdata - root values to be updated, input state is seen by first process to perform an update
-  leafupdate - state at each leaf's respective root immediately prior to my atomic update

   Level: advanced

   Note: See PetscSFFetchAndOpBegin() for more details.

.seealso: PetscSFFetchAndOpBegin(),PetscSFComputeDegreeBegin(), PetscSFReduceBegin(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFFetchAndOpWithMemTypeBegin(PetscSF sf,MPI_Datatype unit,PetscMemType rootmtype,void *rootdata,PetscMemType leafmtype,const void *leafdata,PetscMemType leafupdatemtype,void *leafupdate,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscLogEventBegin(PETSCSF_FetchAndOpBegin,sf,0,0,0));
  PetscCheckFalse(leafmtype != leafupdatemtype,PETSC_COMM_SELF,PETSC_ERR_SUP,"No support for leafdata and leafupdate in different memory types");
  CHKERRQ((*sf->ops->FetchAndOpBegin)(sf,unit,rootmtype,rootdata,leafmtype,leafdata,leafupdate,op));
  CHKERRQ(PetscLogEventEnd(PETSCSF_FetchAndOpBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFFetchAndOpEnd - end operation started in matching call to PetscSFFetchAndOpBegin() to fetch values from roots and update atomically by applying operation using my leaf value

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
.  leafdata - leaf values to use in reduction
-  op - operation to use for reduction

   Output Parameters:
+  rootdata - root values to be updated, input state is seen by first process to perform an update
-  leafupdate - state at each leaf's respective root immediately prior to my atomic update

   Level: advanced

.seealso: PetscSFComputeDegreeEnd(), PetscSFReduceEnd(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFFetchAndOpEnd(PetscSF sf,MPI_Datatype unit,void *rootdata,const void *leafdata,void *leafupdate,MPI_Op op)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscLogEventBegin(PETSCSF_FetchAndOpEnd,sf,0,0,0));
  CHKERRQ((*sf->ops->FetchAndOpEnd)(sf,unit,rootdata,leafdata,leafupdate,op));
  CHKERRQ(PetscLogEventEnd(PETSCSF_FetchAndOpEnd,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFComputeDegreeBegin - begin computation of degree for each root vertex, to be completed with PetscSFComputeDegreeEnd()

   Collective

   Input Parameter:
.  sf - star forest

   Output Parameter:
.  degree - degree of each root vertex

   Level: advanced

   Notes:
   The returned array is owned by PetscSF and automatically freed by PetscSFDestroy(). Hence no need to call PetscFree() on it.

.seealso: PetscSFGatherBegin()
@*/
PetscErrorCode PetscSFComputeDegreeBegin(PetscSF sf,const PetscInt **degree)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  PetscValidPointer(degree,2);
  if (!sf->degreeknown) {
    PetscInt i, nroots = sf->nroots, maxlocal;
    PetscCheck(!sf->degree,PETSC_COMM_SELF, PETSC_ERR_ARG_WRONGSTATE, "Calls to PetscSFComputeDegreeBegin() cannot be nested.");
    maxlocal = sf->maxleaf-sf->minleaf+1;
    CHKERRQ(PetscMalloc1(nroots,&sf->degree));
    CHKERRQ(PetscMalloc1(PetscMax(maxlocal,1),&sf->degreetmp)); /* allocate at least one entry, see check in PetscSFComputeDegreeEnd() */
    for (i=0; i<nroots; i++) sf->degree[i] = 0;
    for (i=0; i<maxlocal; i++) sf->degreetmp[i] = 1;
    CHKERRQ(PetscSFReduceBegin(sf,MPIU_INT,sf->degreetmp-sf->minleaf,sf->degree,MPI_SUM));
  }
  *degree = NULL;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFComputeDegreeEnd - complete computation of degree for each root vertex, started with PetscSFComputeDegreeBegin()

   Collective

   Input Parameter:
.  sf - star forest

   Output Parameter:
.  degree - degree of each root vertex

   Level: developer

   Notes:
   The returned array is owned by PetscSF and automatically freed by PetscSFDestroy(). Hence no need to call PetscFree() on it.

.seealso:
@*/
PetscErrorCode PetscSFComputeDegreeEnd(PetscSF sf,const PetscInt **degree)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sf,1);
  PetscValidPointer(degree,2);
  if (!sf->degreeknown) {
    PetscCheck(sf->degreetmp,PETSC_COMM_SELF, PETSC_ERR_ARG_WRONGSTATE, "Must call PetscSFComputeDegreeBegin() before PetscSFComputeDegreeEnd()");
    CHKERRQ(PetscSFReduceEnd(sf,MPIU_INT,sf->degreetmp-sf->minleaf,sf->degree,MPI_SUM));
    CHKERRQ(PetscFree(sf->degreetmp));
    sf->degreeknown = PETSC_TRUE;
  }
  *degree = sf->degree;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFComputeMultiRootOriginalNumbering - Returns original numbering of multi-roots (roots of multi-SF returned by PetscSFGetMultiSF()).
   Each multi-root is assigned index of the corresponding original root.

   Collective

   Input Parameters:
+  sf - star forest
-  degree - degree of each root vertex, computed with PetscSFComputeDegreeBegin()/PetscSFComputeDegreeEnd()

   Output Parameters:
+  nMultiRoots - (optional) number of multi-roots (roots of multi-SF)
-  multiRootsOrigNumbering - original indices of multi-roots; length of this array is nMultiRoots

   Level: developer

   Notes:
   The returned array multiRootsOrigNumbering is newly allocated and should be destroyed with PetscFree() when no longer needed.

.seealso: PetscSFComputeDegreeBegin(), PetscSFComputeDegreeEnd(), PetscSFGetMultiSF()
@*/
PetscErrorCode PetscSFComputeMultiRootOriginalNumbering(PetscSF sf, const PetscInt degree[], PetscInt *nMultiRoots, PetscInt *multiRootsOrigNumbering[])
{
  PetscSF             msf;
  PetscInt            i, j, k, nroots, nmroots;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFGetGraph(sf, &nroots, NULL, NULL, NULL));
  if (nroots) PetscValidIntPointer(degree,2);
  if (nMultiRoots) PetscValidIntPointer(nMultiRoots,3);
  PetscValidPointer(multiRootsOrigNumbering,4);
  CHKERRQ(PetscSFGetMultiSF(sf,&msf));
  CHKERRQ(PetscSFGetGraph(msf, &nmroots, NULL, NULL, NULL));
  CHKERRQ(PetscMalloc1(nmroots, multiRootsOrigNumbering));
  for (i=0,j=0,k=0; i<nroots; i++) {
    if (!degree[i]) continue;
    for (j=0; j<degree[i]; j++,k++) {
      (*multiRootsOrigNumbering)[k] = i;
    }
  }
  PetscCheckFalse(k != nmroots,PETSC_COMM_SELF,PETSC_ERR_PLIB,"sanity check fail");
  if (nMultiRoots) *nMultiRoots = nmroots;
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGatherBegin - begin pointwise gather of all leaves into multi-roots, to be completed with PetscSFGatherEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
-  leafdata - leaf data to gather to roots

   Output Parameter:
.  multirootdata - root buffer to gather into, amount of space per root is equal to its degree

   Level: intermediate

.seealso: PetscSFComputeDegreeBegin(), PetscSFScatterBegin()
@*/
PetscErrorCode PetscSFGatherBegin(PetscSF sf,MPI_Datatype unit,const void *leafdata,void *multirootdata)
{
  PetscSF        multi = NULL;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscSFGetMultiSF(sf,&multi));
  CHKERRQ(PetscSFReduceBegin(multi,unit,leafdata,multirootdata,MPI_REPLACE));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFGatherEnd - ends pointwise gather operation that was started with PetscSFGatherBegin()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
-  leafdata - leaf data to gather to roots

   Output Parameter:
.  multirootdata - root buffer to gather into, amount of space per root is equal to its degree

   Level: intermediate

.seealso: PetscSFComputeDegreeEnd(), PetscSFScatterEnd()
@*/
PetscErrorCode PetscSFGatherEnd(PetscSF sf,MPI_Datatype unit,const void *leafdata,void *multirootdata)
{
  PetscSF        multi = NULL;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFGetMultiSF(sf,&multi));
  CHKERRQ(PetscSFReduceEnd(multi,unit,leafdata,multirootdata,MPI_REPLACE));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFScatterBegin - begin pointwise scatter operation from multi-roots to leaves, to be completed with PetscSFScatterEnd()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
-  multirootdata - root buffer to send to each leaf, one unit of data per leaf

   Output Parameter:
.  leafdata - leaf data to be update with personal data from each respective root

   Level: intermediate

.seealso: PetscSFComputeDegreeBegin(), PetscSFScatterBegin()
@*/
PetscErrorCode PetscSFScatterBegin(PetscSF sf,MPI_Datatype unit,const void *multirootdata,void *leafdata)
{
  PetscSF        multi = NULL;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscSFGetMultiSF(sf,&multi));
  CHKERRQ(PetscSFBcastBegin(multi,unit,multirootdata,leafdata,MPI_REPLACE));
  PetscFunctionReturn(0);
}

/*@C
   PetscSFScatterEnd - ends pointwise scatter operation that was started with PetscSFScatterBegin()

   Collective

   Input Parameters:
+  sf - star forest
.  unit - data type
-  multirootdata - root buffer to send to each leaf, one unit of data per leaf

   Output Parameter:
.  leafdata - leaf data to be update with personal data from each respective root

   Level: intermediate

.seealso: PetscSFComputeDegreeEnd(), PetscSFScatterEnd()
@*/
PetscErrorCode PetscSFScatterEnd(PetscSF sf,MPI_Datatype unit,const void *multirootdata,void *leafdata)
{
  PetscSF        multi = NULL;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFGetMultiSF(sf,&multi));
  CHKERRQ(PetscSFBcastEnd(multi,unit,multirootdata,leafdata,MPI_REPLACE));
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFCheckLeavesUnique_Private(PetscSF sf)
{
  PetscInt        i, n, nleaves;
  const PetscInt *ilocal = NULL;
  PetscHSetI      seen;

  PetscFunctionBegin;
  if (PetscDefined(USE_DEBUG)) {
    CHKERRQ(PetscSFGetGraph(sf,NULL,&nleaves,&ilocal,NULL));
    CHKERRQ(PetscHSetICreate(&seen));
    for (i = 0; i < nleaves; i++) {
      const PetscInt leaf = ilocal ? ilocal[i] : i;
      CHKERRQ(PetscHSetIAdd(seen,leaf));
    }
    CHKERRQ(PetscHSetIGetSize(seen,&n));
    PetscCheckFalse(n != nleaves,PETSC_COMM_SELF,PETSC_ERR_ARG_OUTOFRANGE,"Provided leaves have repeated values: all leaves must be unique");
    CHKERRQ(PetscHSetIDestroy(&seen));
  }
  PetscFunctionReturn(0);
}

/*@
  PetscSFCompose - Compose a new PetscSF by putting the second SF under the first one in a top (roots) down (leaves) view

  Input Parameters:
+ sfA - The first PetscSF
- sfB - The second PetscSF

  Output Parameters:
. sfBA - The composite SF

  Level: developer

  Notes:
  Currently, the two SFs must be defined on congruent communicators and they must be true star
  forests, i.e. the same leaf is not connected with different roots.

  sfA's leaf space and sfB's root space might be partially overlapped. The composition builds
  a graph with sfA's roots and sfB's leaves only when there is a path between them. Unconnected
  nodes (roots or leaves) are not in sfBA. Doing a Bcast on the new SF is equivalent to doing a
  Bcast on sfA, then a Bcast on sfB, on connected nodes.

.seealso: PetscSF, PetscSFComposeInverse(), PetscSFGetGraph(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFCompose(PetscSF sfA,PetscSF sfB,PetscSF *sfBA)
{
  const PetscSFNode *remotePointsA,*remotePointsB;
  PetscSFNode       *remotePointsBA=NULL,*reorderedRemotePointsA = NULL,*leafdataB;
  const PetscInt    *localPointsA,*localPointsB;
  PetscInt          *localPointsBA;
  PetscInt          i,numRootsA,numLeavesA,numRootsB,numLeavesB,minleaf,maxleaf,numLeavesBA;
  PetscBool         denseB;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sfA,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sfA,1);
  PetscValidHeaderSpecific(sfB,PETSCSF_CLASSID,2);
  PetscSFCheckGraphSet(sfB,2);
  PetscCheckSameComm(sfA,1,sfB,2);
  PetscValidPointer(sfBA,3);
  CHKERRQ(PetscSFCheckLeavesUnique_Private(sfA));
  CHKERRQ(PetscSFCheckLeavesUnique_Private(sfB));

  CHKERRQ(PetscSFGetGraph(sfA,&numRootsA,&numLeavesA,&localPointsA,&remotePointsA));
  CHKERRQ(PetscSFGetGraph(sfB,&numRootsB,&numLeavesB,&localPointsB,&remotePointsB));
  if (localPointsA) {
    CHKERRQ(PetscMalloc1(numRootsB,&reorderedRemotePointsA));
    for (i=0; i<numRootsB; i++) {
      reorderedRemotePointsA[i].rank = -1;
      reorderedRemotePointsA[i].index = -1;
    }
    for (i=0; i<numLeavesA; i++) {
      if (localPointsA[i] >= numRootsB) continue;
      reorderedRemotePointsA[localPointsA[i]] = remotePointsA[i];
    }
    remotePointsA = reorderedRemotePointsA;
  }
  CHKERRQ(PetscSFGetLeafRange(sfB,&minleaf,&maxleaf));
  CHKERRQ(PetscMalloc1(maxleaf-minleaf+1,&leafdataB));
  CHKERRQ(PetscSFBcastBegin(sfB,MPIU_2INT,remotePointsA,leafdataB-minleaf,MPI_REPLACE));
  CHKERRQ(PetscSFBcastEnd(sfB,MPIU_2INT,remotePointsA,leafdataB-minleaf,MPI_REPLACE));
  CHKERRQ(PetscFree(reorderedRemotePointsA));

  denseB = (PetscBool)!localPointsB;
  for (i=0,numLeavesBA=0; i<numLeavesB; i++) {
    if (leafdataB[localPointsB ? localPointsB[i]-minleaf : i].rank == -1) denseB = PETSC_FALSE;
    else numLeavesBA++;
  }
  if (denseB) {
    localPointsBA  = NULL;
    remotePointsBA = leafdataB;
  } else {
    CHKERRQ(PetscMalloc1(numLeavesBA,&localPointsBA));
    CHKERRQ(PetscMalloc1(numLeavesBA,&remotePointsBA));
    for (i=0,numLeavesBA=0; i<numLeavesB; i++) {
      const PetscInt l = localPointsB ? localPointsB[i] : i;

      if (leafdataB[l-minleaf].rank == -1) continue;
      remotePointsBA[numLeavesBA] = leafdataB[l-minleaf];
      localPointsBA[numLeavesBA] = l;
      numLeavesBA++;
    }
    CHKERRQ(PetscFree(leafdataB));
  }
  CHKERRQ(PetscSFCreate(PetscObjectComm((PetscObject)sfA),sfBA));
  CHKERRQ(PetscSFSetFromOptions(*sfBA));
  CHKERRQ(PetscSFSetGraph(*sfBA,numRootsA,numLeavesBA,localPointsBA,PETSC_OWN_POINTER,remotePointsBA,PETSC_OWN_POINTER));
  PetscFunctionReturn(0);
}

/*@
  PetscSFComposeInverse - Compose a new PetscSF by putting the inverse of the second SF under the first one

  Input Parameters:
+ sfA - The first PetscSF
- sfB - The second PetscSF

  Output Parameters:
. sfBA - The composite SF.

  Level: developer

  Notes:
  Currently, the two SFs must be defined on congruent communicators and they must be true star
  forests, i.e. the same leaf is not connected with different roots. Even more, all roots of the
  second SF must have a degree of 1, i.e., no roots have more than one leaf connected.

  sfA's leaf space and sfB's leaf space might be partially overlapped. The composition builds
  a graph with sfA's roots and sfB's roots only when there is a path between them. Unconnected
  roots are not in sfBA. Doing a Bcast on the new SF is equivalent to doing a Bcast on sfA, then
  a Reduce on sfB, on connected roots.

.seealso: PetscSF, PetscSFCompose(), PetscSFGetGraph(), PetscSFSetGraph(), PetscSFCreateInverseSF()
@*/
PetscErrorCode PetscSFComposeInverse(PetscSF sfA,PetscSF sfB,PetscSF *sfBA)
{
  const PetscSFNode *remotePointsA,*remotePointsB;
  PetscSFNode       *remotePointsBA;
  const PetscInt    *localPointsA,*localPointsB;
  PetscSFNode       *reorderedRemotePointsA = NULL;
  PetscInt          i,numRootsA,numLeavesA,numLeavesBA,numRootsB,numLeavesB,minleaf,maxleaf,*localPointsBA;
  MPI_Op            op;
#if defined(PETSC_USE_64BIT_INDICES)
  PetscBool         iswin;
#endif

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sfA,PETSCSF_CLASSID,1);
  PetscSFCheckGraphSet(sfA,1);
  PetscValidHeaderSpecific(sfB,PETSCSF_CLASSID,2);
  PetscSFCheckGraphSet(sfB,2);
  PetscCheckSameComm(sfA,1,sfB,2);
  PetscValidPointer(sfBA,3);
  CHKERRQ(PetscSFCheckLeavesUnique_Private(sfA));
  CHKERRQ(PetscSFCheckLeavesUnique_Private(sfB));

  CHKERRQ(PetscSFGetGraph(sfA, &numRootsA, &numLeavesA, &localPointsA, &remotePointsA));
  CHKERRQ(PetscSFGetGraph(sfB, &numRootsB, &numLeavesB, &localPointsB, &remotePointsB));

  /* TODO: Check roots of sfB have degree of 1 */
  /* Once we implement it, we can replace the MPI_MAXLOC
     with MPI_REPLACE. In that case, MPI_MAXLOC and MPI_REPLACE have the same effect.
     We use MPI_MAXLOC only to have a deterministic output from this routine if
     the root condition is not meet.
   */
  op = MPI_MAXLOC;
#if defined(PETSC_USE_64BIT_INDICES)
  /* we accept a non-deterministic output (if any) with PETSCSFWINDOW, since MPI_MAXLOC cannot operate on MPIU_2INT with MPI_Accumulate */
  CHKERRQ(PetscObjectTypeCompare((PetscObject)sfB,PETSCSFWINDOW,&iswin));
  if (iswin) op = MPI_REPLACE;
#endif

  CHKERRQ(PetscSFGetLeafRange(sfB, &minleaf, &maxleaf));
  CHKERRQ(PetscMalloc1(maxleaf - minleaf + 1,&reorderedRemotePointsA));
  for (i=0; i<maxleaf - minleaf + 1; i++) {
    reorderedRemotePointsA[i].rank = -1;
    reorderedRemotePointsA[i].index = -1;
  }
  if (localPointsA) {
    for (i=0; i<numLeavesA; i++) {
      if (localPointsA[i] > maxleaf || localPointsA[i] < minleaf) continue;
      reorderedRemotePointsA[localPointsA[i] - minleaf] = remotePointsA[i];
    }
  } else {
    for (i=0; i<numLeavesA; i++) {
      if (i > maxleaf || i < minleaf) continue;
      reorderedRemotePointsA[i - minleaf] = remotePointsA[i];
    }
  }

  CHKERRQ(PetscMalloc1(numRootsB,&localPointsBA));
  CHKERRQ(PetscMalloc1(numRootsB,&remotePointsBA));
  for (i=0; i<numRootsB; i++) {
    remotePointsBA[i].rank = -1;
    remotePointsBA[i].index = -1;
  }

  CHKERRQ(PetscSFReduceBegin(sfB,MPIU_2INT,reorderedRemotePointsA-minleaf,remotePointsBA,op));
  CHKERRQ(PetscSFReduceEnd(sfB,MPIU_2INT,reorderedRemotePointsA-minleaf,remotePointsBA,op));
  CHKERRQ(PetscFree(reorderedRemotePointsA));
  for (i=0,numLeavesBA=0; i<numRootsB; i++) {
    if (remotePointsBA[i].rank == -1) continue;
    remotePointsBA[numLeavesBA].rank = remotePointsBA[i].rank;
    remotePointsBA[numLeavesBA].index = remotePointsBA[i].index;
    localPointsBA[numLeavesBA] = i;
    numLeavesBA++;
  }
  CHKERRQ(PetscSFCreate(PetscObjectComm((PetscObject)sfA),sfBA));
  CHKERRQ(PetscSFSetFromOptions(*sfBA));
  CHKERRQ(PetscSFSetGraph(*sfBA,numRootsA,numLeavesBA,localPointsBA,PETSC_OWN_POINTER,remotePointsBA,PETSC_OWN_POINTER));
  PetscFunctionReturn(0);
}

/*
  PetscSFCreateLocalSF_Private - Creates a local PetscSF that only has intra-process edges of the global PetscSF

  Input Parameters:
. sf - The global PetscSF

  Output Parameters:
. out - The local PetscSF
 */
PetscErrorCode PetscSFCreateLocalSF_Private(PetscSF sf,PetscSF *out)
{
  MPI_Comm           comm;
  PetscMPIInt        myrank;
  const PetscInt     *ilocal;
  const PetscSFNode  *iremote;
  PetscInt           i,j,nroots,nleaves,lnleaves,*lilocal;
  PetscSFNode        *liremote;
  PetscSF            lsf;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  if (sf->ops->CreateLocalSF) {
    CHKERRQ((*sf->ops->CreateLocalSF)(sf,out));
  } else {
    /* Could use PetscSFCreateEmbeddedLeafSF, but since we know the comm is PETSC_COMM_SELF, we can make it fast */
    CHKERRQ(PetscObjectGetComm((PetscObject)sf,&comm));
    CHKERRMPI(MPI_Comm_rank(comm,&myrank));

    /* Find out local edges and build a local SF */
    CHKERRQ(PetscSFGetGraph(sf,&nroots,&nleaves,&ilocal,&iremote));
    for (i=lnleaves=0; i<nleaves; i++) {if (iremote[i].rank == (PetscInt)myrank) lnleaves++;}
    CHKERRQ(PetscMalloc1(lnleaves,&lilocal));
    CHKERRQ(PetscMalloc1(lnleaves,&liremote));

    for (i=j=0; i<nleaves; i++) {
      if (iremote[i].rank == (PetscInt)myrank) {
        lilocal[j]        = ilocal? ilocal[i] : i; /* ilocal=NULL for contiguous storage */
        liremote[j].rank  = 0; /* rank in PETSC_COMM_SELF */
        liremote[j].index = iremote[i].index;
        j++;
      }
    }
    CHKERRQ(PetscSFCreate(PETSC_COMM_SELF,&lsf));
    CHKERRQ(PetscSFSetFromOptions(lsf));
    CHKERRQ(PetscSFSetGraph(lsf,nroots,lnleaves,lilocal,PETSC_OWN_POINTER,liremote,PETSC_OWN_POINTER));
    CHKERRQ(PetscSFSetUp(lsf));
    *out = lsf;
  }
  PetscFunctionReturn(0);
}

/* Similar to PetscSFBcast, but only Bcast to leaves on rank 0 */
PetscErrorCode PetscSFBcastToZero_Private(PetscSF sf,MPI_Datatype unit,const void *rootdata,void *leafdata)
{
  PetscMemType       rootmtype,leafmtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(sf,PETSCSF_CLASSID,1);
  CHKERRQ(PetscSFSetUp(sf));
  CHKERRQ(PetscLogEventBegin(PETSCSF_BcastBegin,sf,0,0,0));
  CHKERRQ(PetscGetMemType(rootdata,&rootmtype));
  CHKERRQ(PetscGetMemType(leafdata,&leafmtype));
  if (sf->ops->BcastToZero) {
    CHKERRQ((*sf->ops->BcastToZero)(sf,unit,rootmtype,rootdata,leafmtype,leafdata));
  } else SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SUP,"PetscSFBcastToZero_Private is not supported on this SF type");
  CHKERRQ(PetscLogEventEnd(PETSCSF_BcastBegin,sf,0,0,0));
  PetscFunctionReturn(0);
}

/*@
  PetscSFConcatenate - concatenate multiple SFs into one

  Input Parameters:
+ comm - the communicator
. nsfs - the number of input PetscSF
. sfs  - the array of input PetscSF
. shareRoots - the flag whether roots of input PetscSFs are taken as shared (PETSC_TRUE), or separate and concatenated (PETSC_FALSE)
- leafOffsets - the array of local leaf offsets, one for each input PetscSF, or NULL for contiguous storage

  Output Parameters:
. newsf - The resulting PetscSF

  Level: developer

  Notes:
  The communicator of all SFs in sfs must be comm.

  The offsets in leafOffsets are added to the original leaf indices.

  If all input SFs use contiguous leaf storage (ilocal = NULL), leafOffsets can be passed as NULL as well.
  In this case, NULL is also passed as ilocal to the resulting SF.

  If any input SF has non-null ilocal, leafOffsets is needed to distinguish leaves from different input SFs.
  In this case, user is responsible to provide correct offsets so that the resulting leaves are unique (otherwise an error occurs).

.seealso: PetscSF, PetscSFCompose(), PetscSFGetGraph(), PetscSFSetGraph()
@*/
PetscErrorCode PetscSFConcatenate(MPI_Comm comm, PetscInt nsfs, PetscSF sfs[], PetscBool shareRoots, PetscInt leafOffsets[], PetscSF *newsf)
{
  PetscInt            i, s, nLeaves, nRoots;
  PetscInt           *leafArrayOffsets;
  PetscInt           *ilocal_new;
  PetscSFNode        *iremote_new;
  PetscInt           *rootOffsets;
  PetscBool           all_ilocal_null = PETSC_FALSE;
  PetscMPIInt         rank;

  PetscFunctionBegin;
  {
    PetscSF dummy; /* just to have a PetscObject on comm for input validation */

    CHKERRQ(PetscSFCreate(comm,&dummy));
    PetscValidLogicalCollectiveInt(dummy,nsfs,2);
    PetscValidPointer(sfs,3);
    for (i=0; i<nsfs; i++) {
      PetscValidHeaderSpecific(sfs[i],PETSCSF_CLASSID,3);
      PetscCheckSameComm(dummy,1,sfs[i],3);
    }
    PetscValidLogicalCollectiveBool(dummy,shareRoots,4);
    if (leafOffsets) PetscValidIntPointer(leafOffsets,5);
    PetscValidPointer(newsf,6);
    CHKERRQ(PetscSFDestroy(&dummy));
  }
  if (!nsfs) {
    CHKERRQ(PetscSFCreate(comm, newsf));
    CHKERRQ(PetscSFSetGraph(*newsf, 0, 0, NULL, PETSC_OWN_POINTER, NULL, PETSC_OWN_POINTER));
    PetscFunctionReturn(0);
  }
  CHKERRMPI(MPI_Comm_rank(comm, &rank));

  CHKERRQ(PetscCalloc1(nsfs+1, &rootOffsets));
  if (shareRoots) {
    CHKERRQ(PetscSFGetGraph(sfs[0], &nRoots, NULL, NULL, NULL));
    if (PetscDefined(USE_DEBUG)) {
      for (s=1; s<nsfs; s++) {
        PetscInt nr;

        CHKERRQ(PetscSFGetGraph(sfs[s], &nr, NULL, NULL, NULL));
        PetscCheck(nr == nRoots, comm, PETSC_ERR_ARG_SIZ, "shareRoots = PETSC_TRUE but sfs[%" PetscInt_FMT "] has a different number of roots (%" PetscInt_FMT ") than sfs[0] (%" PetscInt_FMT ")", s, nr, nRoots);
      }
    }
  } else {
    for (s=0; s<nsfs; s++) {
      PetscInt nr;

      CHKERRQ(PetscSFGetGraph(sfs[s], &nr, NULL, NULL, NULL));
      rootOffsets[s+1] = rootOffsets[s] + nr;
    }
    nRoots = rootOffsets[nsfs];
  }

  /* Calculate leaf array offsets and automatic root offsets */
  CHKERRQ(PetscMalloc1(nsfs+1,&leafArrayOffsets));
  leafArrayOffsets[0] = 0;
  for (s=0; s<nsfs; s++) {
    PetscInt        nl;

    CHKERRQ(PetscSFGetGraph(sfs[s], NULL, &nl, NULL, NULL));
    leafArrayOffsets[s+1] = leafArrayOffsets[s] + nl;
  }
  nLeaves = leafArrayOffsets[nsfs];

  if (!leafOffsets) {
    all_ilocal_null = PETSC_TRUE;
    for (s=0; s<nsfs; s++) {
      const PetscInt *ilocal;

      CHKERRQ(PetscSFGetGraph(sfs[s], NULL, NULL, &ilocal, NULL));
      if (ilocal) {
        all_ilocal_null = PETSC_FALSE;
        break;
      }
    }
    PetscCheck(all_ilocal_null, PETSC_COMM_SELF, PETSC_ERR_ARG_NULL, "leafOffsets can be passed as NULL only if all SFs have ilocal = NULL");
  }

  /* Renumber and concatenate local leaves */
  ilocal_new = NULL;
  if (!all_ilocal_null) {
    CHKERRQ(PetscMalloc1(nLeaves, &ilocal_new));
    for (i = 0; i<nLeaves; i++) ilocal_new[i] = -1;
    for (s = 0; s<nsfs; s++) {
      const PetscInt   *ilocal;
      PetscInt         *ilocal_l = &ilocal_new[leafArrayOffsets[s]];
      PetscInt          i, nleaves_l;

      CHKERRQ(PetscSFGetGraph(sfs[s], NULL, &nleaves_l, &ilocal, NULL));
      for (i=0; i<nleaves_l; i++) ilocal_l[i] = (ilocal ? ilocal[i] : i) + leafOffsets[s];
    }
  }

  /* Renumber and concatenate remote roots */
  CHKERRQ(PetscMalloc1(nLeaves, &iremote_new));
  for (i = 0; i < nLeaves; i++) {
    iremote_new[i].rank   = -1;
    iremote_new[i].index  = -1;
  }
  for (s = 0; s<nsfs; s++) {
    PetscInt            i, nl, nr;
    PetscSF             tmp_sf;
    const PetscSFNode  *iremote;
    PetscSFNode        *tmp_rootdata;
    PetscSFNode        *tmp_leafdata = &iremote_new[leafArrayOffsets[s]];

    CHKERRQ(PetscSFGetGraph(sfs[s], &nr, &nl, NULL, &iremote));
    CHKERRQ(PetscSFCreate(comm, &tmp_sf));
    /* create helper SF with contiguous leaves */
    CHKERRQ(PetscSFSetGraph(tmp_sf, nr, nl, NULL, PETSC_USE_POINTER, (PetscSFNode*) iremote, PETSC_COPY_VALUES));
    CHKERRQ(PetscSFSetUp(tmp_sf));
    CHKERRQ(PetscMalloc1(nr, &tmp_rootdata));
    for (i = 0; i < nr; i++) {
      tmp_rootdata[i].index = i + rootOffsets[s];
      tmp_rootdata[i].rank  = (PetscInt) rank;
    }
    CHKERRQ(PetscSFBcastBegin(tmp_sf, MPIU_2INT, tmp_rootdata, tmp_leafdata, MPI_REPLACE));
    CHKERRQ(PetscSFBcastEnd(  tmp_sf, MPIU_2INT, tmp_rootdata, tmp_leafdata, MPI_REPLACE));
    CHKERRQ(PetscSFDestroy(&tmp_sf));
    CHKERRQ(PetscFree(tmp_rootdata));
  }

  /* Build the new SF */
  CHKERRQ(PetscSFCreate(comm, newsf));
  CHKERRQ(PetscSFSetGraph(*newsf, nRoots, nLeaves, ilocal_new, PETSC_OWN_POINTER, iremote_new, PETSC_OWN_POINTER));
  CHKERRQ(PetscSFSetUp(*newsf));
  CHKERRQ(PetscFree(rootOffsets));
  CHKERRQ(PetscFree(leafArrayOffsets));
  PetscFunctionReturn(0);
}
