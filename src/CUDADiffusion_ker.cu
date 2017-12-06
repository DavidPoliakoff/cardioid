//#include "CUDADiffusion.hh"
//#include "DiffusionUtils.hh"
//#include "SymmetricTensor.hh"
//#include <vector>
//#include <map>

//#include "options.h"
//#include "cudautil.h"
#include <stdio.h>

#define XTILE 20
typedef double Real;


__global__ void diff_6face_v1(const Real* d_psi, Real* d_npsi, const Real* d_sigmaX, const Real* d_sigmaY, const Real* d_sigmaZ,int Lii, int Ljj, int Lkk)
{

  //map z dir to threads
  //z is the fastest varying direction

  //2d decomposition
  //32x32 in y z direction
  __shared__ Real sm_psi[4][32][32]; //32 KB

  #define V0(y,z) sm_psi[pii][y][z]
  #define V1(y,z) sm_psi[cii][y][z]
  #define V2(y,z) sm_psi[nii][y][z]

  #define sigmaX(x,y,z,dir) d_sigmaX[ z + (Lkk-1) * ( y + (Ljj-1) * ( x + (Lii-1) * dir ) ) ]
  #define sigmaY(x,y,z,dir) d_sigmaY[ z + (Lkk-1) * ( y + (Ljj-1) * ( x + (Lii-1) * dir ) ) ]
  #define sigmaZ(x,y,z,dir) d_sigmaZ[ z + (Lkk-1) * ( y + (Ljj-1) * ( x + (Lii-1) * dir ) ) ]

  #define psi(x,y,z) d_psi[ z + Lkk * ( (y) + Ljj * (x) ) ]
  #define npsi(x,y,z) d_npsi[ z + Lkk * ( (y) + Ljj * (x) ) ]

  int tjj = threadIdx.y;
  int tkk = threadIdx.x;

  //shift for each tile
//  d_psi    += 30 * blockIdx.x + Lkk * ( 30 * blockIdx.y );
//  d_npsi   += 30 * blockIdx.x + Lkk * ( 30 * blockIdx.y );
  d_psi    = &(psi(XTILE*blockIdx.x, 30*blockIdx.y, 30*blockIdx.z));
  d_npsi   = &(npsi(XTILE*blockIdx.x, 30*blockIdx.y, 30*blockIdx.z));

  d_sigmaX  = &(sigmaX(XTILE*blockIdx.x-1, 30*blockIdx.y-1, 30*blockIdx.z-1, 0));
  d_sigmaY  = &(sigmaY(XTILE*blockIdx.x-1, 30*blockIdx.y-1, 30*blockIdx.z-1, 0));
  d_sigmaZ  = &(sigmaZ(XTILE*blockIdx.x-1, 30*blockIdx.y-1, 30*blockIdx.z-1, 0));

  int Last_x=XTILE+1; int nLast_y=31; int nLast_z=31;
  if (blockIdx.x == gridDim.x-1) Last_x = Lii-2 - XTILE * blockIdx.x + 1;
  if (blockIdx.y == gridDim.y-1) nLast_y = Ljj-2 - 30 * blockIdx.y + 1;
  if (blockIdx.z == gridDim.z-1) nLast_z = Lkk-2 - 30 * blockIdx.z + 1;

//  if (blockIdx.x==0 && blockIdx.y==0 && blockIdx.z==0) printf("b(%d,%d,%d) t(%d,%d,%d) LastX:%d nLast_y:%d nLast_z:%d %p %p %p %p %p\n",blockIdx.x,blockIdx.y,blockIdx.z,threadIdx.x,threadIdx.y,threadIdx.z,Last_x,nLast_y,nLast_z,&(psi(0,tjj,tkk)),&(npsi(0,tjj,tkk)),&(sigmaX(0,tjj,tkk,0)),&(sigmaY(0,tjj,tkk,0)),&(sigmaZ(0,tjj,tkk,0)));

  if(tjj>nLast_y) return;
  if(tkk>nLast_z) return;

//  d_sigmaX += 30 * blockIdx.x + (Lkk-2) * ( 31 * blockIdx.y );
//  d_sigmaY += 30 * blockIdx.x + (Lkk-2) * ( 31 * blockIdx.y );
//  d_sigmaZ += 31 * blockIdx.x + (Lkk-1) * ( 31 * blockIdx.y );

//  printf("tjj tkk bx by = %d %d %d %d\n",tjj,tkk,blockIdx.x,blockIdx.y);


  int pii,cii,nii,tii;
  pii=0; cii=1; nii=2;

  sm_psi[cii][tkk][tjj] = psi(0,tjj,tkk);
  sm_psi[nii][tkk][tjj] = psi(1,tjj,tkk);
  Real xcharge,ycharge,zcharge,dV;

  __syncthreads();
  //initial
  if ((tkk>0) && (tkk<nLast_z) && (tjj>0) && (tjj<nLast_y))
  {
    Real xd=-V1(tjj,tkk) + V2(tjj,tkk);
    Real yd=(-V1(-1 + tjj,tkk) + V1(1 + tjj,tkk) - V2(-1 + tjj,tkk) + V2(1 + tjj,tkk))/4.;
    Real zd=(-V1(tjj,-1 + tkk) + V1(tjj,1 + tkk) - V2(tjj,-1 + tkk) + V2(tjj,1 + tkk))/4.;

    dV = sigmaX(0,tjj,tkk,0) * xd + sigmaX(0,tjj,tkk,1) * yd + sigmaX(0,tjj,tkk,2) * zd ; 
  }

  tii=pii; pii=cii; cii=nii; nii=tii;

  for(int ii=1;ii<Last_x;ii++)
  {
    sm_psi[nii][tkk][tjj] = psi(ii+1,tjj,tkk);
    __syncthreads();

    // contribution to (ii-1)
    // use link loaded previous
    // y face current
    // tjj=0 calc face at 0-1 and tjj=30 calc face at 30-31
  
    if ((tkk>0) && (tkk<nLast_z) && (tjj<nLast_y))
    {
      Real xd=(-V0(tjj,tkk) - V0(1 + tjj,tkk) + V2(tjj,tkk) + V2(1 + tjj,tkk))/4.;
      Real yd=-V1(tjj,tkk) + V1(1 + tjj,tkk);
      Real zd=(-V1(tjj,-1 + tkk) + V1(tjj,1 + tkk) - V1(1 + tjj,-1 + tkk) + V1(1 + tjj,1 + tkk))/4.;

      ycharge = sigmaY(ii,tjj,tkk,0) * xd + sigmaY(ii,tjj,tkk,1) * yd + sigmaY(ii,tjj,tkk,2) * zd ; 
      dV += ycharge;
      sm_psi[3][tjj][tkk]=ycharge;
    }
    __syncthreads();

    if ((tkk>0) && (tkk<nLast_z) && (tjj>0) && (tjj<nLast_y))
      dV -= sm_psi[3][tjj-1][tkk];  //bring from left

    __syncthreads();

    // z face current
    // tkk=0 calc face at 0-1 and tkk=30 calc face at 30-31
    if ((tkk<nLast_z) && (tjj>0) && (tjj<nLast_y))
    {

      Real xd=(-V0(tjj,tkk) - V0(tjj,1 + tkk) + V2(tjj,tkk) + V2(tjj,1 + tkk))/4.;
      Real yd=(-V1(-1 + tjj,tkk) - V1(-1 + tjj,1 + tkk) + V1(1 + tjj,tkk) + V1(1 + tjj,1 + tkk))/4.;
      Real zd=-V1(tjj,tkk) + V1(tjj,1 + tkk);

      zcharge = sigmaZ(ii,tjj,tkk,0) * xd + sigmaZ(ii,tjj,tkk,1) * yd + sigmaZ(ii,tjj,tkk,2) * zd ; 
      dV += zcharge;
      sm_psi[3][tjj][tkk]=zcharge;
    }

    __syncthreads();

    if ((tkk>0) && (tkk<nLast_z) && (tjj>0) && (tjj<nLast_y))
      dV -= sm_psi[3][tjj][tkk-1];

    //__syncthreads();

    // x face current
    if ((tkk>0) && (tkk<nLast_z) && (tjj>0) && (tjj<nLast_y))
    {
      Real xd=-V1(tjj,tkk) + V2(tjj,tkk);
      Real yd=(-V1(-1 + tjj,tkk) + V1(1 + tjj,tkk) - V2(-1 + tjj,tkk) + V2(1 + tjj,tkk))/4.;
      Real zd=(-V1(tjj,-1 + tkk) + V1(tjj,1 + tkk) - V2(tjj,-1 + tkk) + V2(tjj,1 + tkk))/4.;
 
      xcharge = sigmaX(ii,tjj,tkk,0) * xd + sigmaX(ii,tjj,tkk,1) * yd + sigmaX(ii,tjj,tkk,2) * zd ; 
      dV += xcharge;
      //store dV
      npsi(ii,tjj,tkk) = dV;

      dV = -xcharge; //pass to the next cell in x-dir
    }
    tii=pii; pii=cii; cii=nii; nii=tii;
  }
//  #undef V0(y,z)
//  #undef V1(y,z)
//  #undef V2(y,z)
//  #undef sigmaX(x,y,z,dir) 
//  #undef sigmaY(x,y,z,dir) 
//  #undef sigmaZ(x,y,z,dir) 
//  #undef psi(x,y,z) 
//  #undef npsi(x,y,z) 
}

__global__ void map_dVm(double * dVmT, double* dVm, const int *remap,int nCells)
{
  int idx0 = threadIdx.x + blockDim.x*blockIdx.x;
  int stride = blockDim.x * gridDim.x;
  for(int idx = idx0 ; idx<nCells ; idx+=stride)
      dVmT[idx] = dVm[remap[idx]];
}

//__global__ void map_V(double * VT, double* V, const int *remap,int nCells)
//{
//  int idx0 = threadIdx.x + blockDim.x*blockIdx.x;
//  int stride = blockDim.x * gridDim.x;
//  for(int idx = idx0 ; idx<nCells ; idx+=stride)
//      VT[remap[idx]] = V[idx];
//}

extern "C"
{
void call_cuda_kernels(const Real *VmRaw, Real *dVmRaw, const Real *sigmaRaw, int nx, int ny, int nz, Real *dVmOut, const int *lookup,int nCells)
{
   //determine block dim
   //1. blockdim.z and blockdim.y are determined in a simple way.
   int bdimz = (int)((nz-2)/30) + ((nz-2)%30==0?0:1);
   int bdimy = (int)((ny-2)/30) + ((ny-2)%30==0?0:1);
   int bdimx = (int)((nx-2)/XTILE) + ((nx-2)%XTILE==0?0:1);
   
//   printf("Vm=%p dVm=%p sigma=%p \n",VmRaw,dVmRaw,sigmaRaw);
//   printf("call_cuda_kernels %d,%d,%d (%d,%d,%d)\n",nx,ny,nz,bdimx,bdimy,bdimz);
#ifdef GPU_SM_70
   cudaFuncSetAttribute(diff_6face_v1, cudaFuncAttributePreferredSharedMemoryCarveout, 50);
#endif

   //map_V<<<112,512>>>(VmBlockRaw,VmRaw,lookup,nCells);
   diff_6face_v1<<<dim3(bdimx,bdimy,bdimz),dim3(32,32,1)>>>(VmRaw,dVmRaw,sigmaRaw,sigmaRaw+3*(nx-1)*(ny-1)*(nz-1),sigmaRaw+6*(nx-1)*(ny-1)*(nz-1),nx,ny,nz);
   map_dVm<<<112,512>>>(dVmRaw,dVmOut,lookup,nCells);
}
}
