#ifndef FGRDIFFUSION_HH
#define FGRDIFFUSION_HH

#include "Diffusion.hh"
#include "LocalGrid.hh"
#include "Array3d.hh"
#include "ibmIntrinsics.hh"

class Anatomy;
class Vector;
class SymmetricTensor;
class CoreGroup;
class L2_Barrier_t;
class L2_BarrierHandle_t;

struct FGRDiffusionParms
{
   double diffusionScale_;
};

struct DiffWeight
{
   double A[19];
};

class FGRDiffusion : public Diffusion
{
 public:
   FGRDiffusion(
      const FGRDiffusionParms& parms,
      const Anatomy& anatomy,
      const CoreGroup& threadInfo);
   
   void calc(const std::vector<double>& Vm, std::vector<double>& dVm, double *recv_buf, int nLocal);
   void calc_simd(const std::vector<double>& Vm, std::vector<double>& dVm, double *recv_buf_, int nLocal);
   void FGRDiff_simd(const uint32_t start,const int32_t chunk_size, Array3d<double>* VmTmp, double* out);
   void FGRDiff_simd_thread(const uint32_t start,const int32_t chunk_size, Array3d<double>* VmTmp, double* out);

 private:
   void buildTupleArray(const Anatomy& anatomy);
   void buildBlockIndex(const Anatomy& anatomy);
   void precomputeCoefficients(const Anatomy& anatomy);
   void reorder_Coeff();

   void updateVoltageBlock(const std::vector<double>& Vm, double *recv_buf, int nLocal);
   void mkTissueArray(const Array3d<int>& tissueBlk, int ib, int* tissue);
   Vector f1(int ib, int iFace, const Vector& h,
             const Array3d<SymmetricTensor>& sigmaBlk);
   void printAllWeights(const Array3d<int>& tissue);

   int                             offset_[19];
   int                             faceNbrOffset_[6];
   LocalGrid                       localGrid_;
   double                          diffusionScale_;
   const CoreGroup&                threadInfo_;
   L2_Barrier_t*                   fgrBarrier_;
   std::vector<L2_BarrierHandle_t> barrierHandle_;
   std::vector<int>                threadOffset_;
   std::vector<unsigned>           blockIndex_; // for local and remote cells
   std::vector<Tuple>              localTuple_; // only for local cells
   Array3d<DiffWeight>             weight_;
   Array3d<double>                 VmBlock_;
   Array3d<double>                 diffCoefT2_;
   Array3d<double>                 tmp_dVm;
   
   
};

#endif
