#include "Simulate.hh"

#include <cmath>
#include <mpi.h>
#include "Diffusion.hh"

using std::isnan;
using std::vector;


/** */
void Simulate::checkRanges(const vector<double>& dVmReaction,
                           const vector<double>& dVmDiffusion)
{
   const double vMax = 60;
   const double vMin = -110;
   unsigned nLocal = anatomy_.nLocal();
   for (unsigned ii=0; ii<nLocal; ++ii)
   {
      if (VmArray_[ii] > vMax || VmArray_[ii] < vMin || isnan(VmArray_[ii]) )
         outOfRange(ii, dVmReaction[ii], dVmDiffusion[ii]);
   }
}

void Simulate::outOfRange(unsigned index, double dVmr, double dVmd)
{
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank); 
   double Vm = VmArray_[index];

   /** This is awful.  Some diffusion classes don't store the results in
    *  array form, but keep them in an internal matrix.  We have to go
    *  fetch them. */
   if (diffusion_->VmBlock() != 0)
   {
      unsigned bi = diffusion_->blockIndex()[index];
      dVmd += diffusion_->VmBlock()[bi] * diffusion_->diffusionScale();
   }
   
   printf("WARNING: Voltage out of range: rank %d, index %d\n"
          "         loop = %d, V = %e, dVmd = %e, dVmr = %e\n",
          myRank, index, loop_, Vm, dVmd, dVmr);
   fflush(stdout);
}