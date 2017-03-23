#include "simulationLoop.hh"

#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <omp.h>

#include "Simulate.hh"
#include "Diffusion.hh"
#include "Reaction.hh"
#include "Stimulus.hh"
#include "Sensor.hh"
#include "HaloExchange.hh"
#include "ioUtils.h"
#include "writeCells.hh"
#include "checkpointIO.hh"
#include "PerformanceTimers.hh"
#include "fastBarrier.hh"
#include "mpiUtils.h"

/*
   This follwing header, fastBarrier_nosync.hh, contains barrier code
   stolen from fastBarrierBGQ.hh, but with memory synchronization
   turned off. It is acceptable to use that when a barrier executes
   entirely within one core. It is necessary that a squad runs within
   one core.

   fastBarrier_nosync also sets the PER_SQUAD_BARRIER macro. By not
   including it, the original code with a barrier accross all reaction
   threads is used.
 */
#include "slow_fix.hh"
#ifndef LEGACY_NG_WORKPARTITION
#include "fastBarrier_nosync.hh"
#endif

#include "object_cc.hh"
#include "clooper.h"
#include "ThreadUtils.hh"

//#define timebase(x) asm volatile ("mftb %0" : "=r"(x) : )

using namespace std;
using namespace PerformanceTimers;

namespace
{

void threadBarrier(TimerHandle tHandle, L2_Barrier_t* bPtr, L2_BarrierHandle_t* bHandlePtr, int nThreads)
{
   startTimer(tHandle);
   L2_BarrierWithSync_Barrier(bPtr, bHandlePtr, nThreads);
   stopTimer(tHandle);
}
}

namespace
{

void printData(const Simulate& sim)
{
   startTimer(printDataTimer);
   int pi = sim.printIndex_;
   if (pi >= 0)
   {
      const VectorDouble32 & VmArray(sim.vdata_.VmTransport_.readOnHost());
      const VectorDouble32 & dVmReaction(sim.vdata_.dVmReactionTransport_.readOnHost());
      const VectorDouble32 & dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.readOnHost());
      const unsigned* const blockIndex = sim.diffusion_->blockIndex();
      double dVd = dVmDiffusion[pi];
      if (blockIndex)
      {
         const double* const dVdMatrix = sim.diffusion_->dVmBlock();
         const double scale = sim.diffusion_->diffusionScale();
         const int index = blockIndex[pi];
         dVd += scale * dVdMatrix[index];
      }
      printf("%8d %8.3f %12lu %21.15f %21.15f %21.15f\n",
             sim.loop_, sim.time_, sim.anatomy_.gid(pi),
             VmArray[pi], dVmReaction[pi], dVd);
      fprintf(sim.printFile_,
              "%8d %8.3f %12lu %21.15f %21.15f %21.15f\n",
              sim.loop_, sim.time_, sim.anatomy_.gid(pi),
              VmArray[pi], dVmReaction[pi], dVd);
      fflush(sim.printFile_);
   }
   stopTimer(printDataTimer);
}
}

void simulationProlog(Simulate& sim)
{
   // initialize membrane voltage with default value from the reaction
   // model.  Initialize with zero for remote cells
   sim.vdata_.setup(sim.anatomy_);
   VectorDouble32 & VmArray(sim.vdata_.VmTransport_.modifyOnHost());
   initializeMembraneState(sim.reaction_, sim.reactionName_, VmArray);
   for (unsigned ii = sim.anatomy_.nLocal(); ii < sim.anatomy_.size(); ++ii)
   {
      VmArray[ii] = 0;
   }

   // Load state file, assign corresponding values to membrane voltage
   // and cell model.  This may overwrite the initialization we just
   // did.
   for (unsigned ii = 0; ii < sim.stateFilename_.size(); ++ii)
   {
      readCheckpoint(sim.stateFilename_[ii], sim, MPI_COMM_WORLD);
   }
}

void loopIO(const Simulate& sim, int firstCall)
{
   if (firstCall) { return; }
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
   const Anatomy& anatomy = sim.anatomy_;

   int loop = sim.loop_;

   const VectorDouble32 & VmArray(sim.vdata_.VmTransport_.readOnHost());
   const VectorDouble32 & dVmR(sim.vdata_.dVmReactionTransport_.readOnHost());
   const VectorDouble32 & dVmD(sim.vdata_.dVmDiffusionTransport_.readOnHost());

   // SENSORS
   #pragma omp critical
   {
      startTimer(sensorTimer);
      for (unsigned ii = 0; ii < sim.sensor_.size(); ++ii)
      {
         sim.sensor_[ii]->run(sim.time_, loop);
      }
      stopTimer(sensorTimer);

      startTimer(loopIOTimer);
      if (sim.loop_ > 0 && sim.checkpointRate_ > 0 && sim.loop_ % sim.checkpointRate_ == 0)
      {
         writeCheckpoint(sim, MPI_COMM_WORLD);
      }

   }// critical section
   firstCall = 0;
   stopTimer(loopIOTimer);
}

void simulationLoop(Simulate& sim)
{
   vector<double> iStim(sim.anatomy_.nLocal(), 0.0);
   simulationProlog(sim);
   HaloExchange<double, AlignedAllocator<double> > voltageExchange(sim.sendMap_, (sim.commTable_));

   PotentialData& vdata = sim.vdata_;

#if defined(SPI) && defined(TRACESPI)
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
   cout << "Rank[" << myRank << "]: numOfNeighborToSend=" << sim.commTable_->_sendTask.size() << " numOfNeighborToRecv=" << sim.commTable_->_recvTask.size() << " numOfBytesToSend=" << sim.commTable_->_sendOffset[sim.commTable_->_sendTask.size()] * sizeof (double) << " numOfBytesToRecv=" << sim.commTable_->_recvOffset[sim.commTable_->_recvTask.size()] * sizeof (double) << endl;
#endif

   printData(sim);
   loopIO(sim, 1);
   profileStart(simulationLoopTimer);
   while (sim.loop_ < sim.maxLoop_)
   {
      int nLocal = sim.anatomy_.nLocal();

      if (sim.drugRescale_.size() > 0) { sim.reaction_->scaleCurrents(sim.drugRescale_); } /*   call scaleCurrents here */
      startTimer(imbalanceTimer);
      voltageExchange.barrier();
      stopTimer(imbalanceTimer);

      startTimer(haloTimer);
      {
         const VectorDouble32& vmarray(vdata.VmTransport_.readOnHost());
         voltageExchange.fillSendBuffer(vmarray);
         voltageExchange.startComm();
         voltageExchange.wait();
      }
      stopTimer(haloTimer);

      // DIFFUSION
      startTimer(diffusionCalcTimer);
      {
         const VectorDouble32& vmarray(vdata.VmTransport_.readOnDevice());
         VectorDouble32& dVmDiffusion(vdata.dVmDiffusionTransport_.modifyOnDevice());
         sim.diffusion_->updateLocalVoltage(&(vmarray[0]));
         #pragma omp target update from(voltageExchange.recvBuf_[0:voltageExchange.width_])
         sim.diffusion_->updateRemoteVoltage(voltageExchange.getRecvBuf());
         sim.diffusion_->calc(dVmDiffusion);
      }
      stopTimer(diffusionCalcTimer);

      startTimer(stimulusTimer);
      {
         VectorDouble32& dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.modifyOnDevice());
         // add stimulus to dVmDiffusion
         #pragma omp target teams distribute parallel for
         for (unsigned ii = 0; ii < sim.stimulus_.size(); ++ii)
         {
            sim.stimulus_[ii]->stim(sim.time_, dVmDiffusion);
         }

         {
            double* iStimRaw=&iStim[0];
            double* dVmDiffusionRaw=&dVmDiffusion[0];
            #pragma omp target teams distribute parallel for
            for (unsigned ii = 0; ii < nLocal; ++ii)
            {
               iStimRaw[ii] = -(dVmDiffusionRaw[ii]);
            }
         }
      }
      stopTimer(stimulusTimer);

      // REACTION
      startTimer(reactionTimer);
      {
         const VectorDouble32& vmarray(vdata.VmTransport_.readOnDevice());
         VectorDouble32& dVmReaction(vdata.dVmReactionTransport_.modifyOnDevice());

         sim.reaction_->calc(sim.dt_, vmarray, iStim, dVmReaction);
      }
      stopTimer(reactionTimer);

      startTimer(integratorTimer);
      if (sim.checkRange_.on)
      {
         const VectorDouble32 & vmarray(vdata.VmTransport_.readOnHost());
         const VectorDouble32 & dVmDiffusion(vdata.dVmDiffusionTransport_.readOnHost());
         const VectorDouble32 & dVmReaction(vdata.dVmReactionTransport_.readOnHost());
         if (sim.checkRange_.on)
         {
            sim.checkRanges(vmarray, dVmReaction, dVmDiffusion);
         }
      }
      // no special BGQ integrator is this loop.  More bang for buck
      // from OMP threading.
      {
         VectorDouble32 & vmarray(vdata.VmTransport_.modifyOnDevice());
         const VectorDouble32 & dVmDiffusion(vdata.dVmDiffusionTransport_.readOnDevice());
         const VectorDouble32 & dVmReaction(vdata.dVmReactionTransport_.readOnDevice());
         #pragma omp target teams distribute parallel for
         for (int ii = 0; ii < nLocal; ++ii)
         {
            double dVm = dVmReaction[ii] + dVmDiffusion[ii];
            vmarray[ii] += sim.dt_*dVm;
            if (sim.findVrest_) { vmarray[ii] = sim.Vrest_; }
         }
      }

      sim.time_ += sim.dt_;
      ++sim.loop_;
      stopTimer(integratorTimer);

      if (sim.checkIO()) { sim.bufferReactionData(); }

      if (sim.loop_ % sim.printRate_ == 0)
      {
         printData(sim);
      }
      loopIO(sim, 0);
   }
   profileStop(simulationLoopTimer);
}

/** One stop shopping for all of the data that we would rather create
 *  before the parallel section, either because we want the data to
 *  be shared across threads, or because it is just easier to create
 *  it in a single thread environment.  This data is all gathered
 *  into a struct so that it can be easily passed to the diffusion
 *  and reaction loops without giant argument lists.
 *
 *  You might argue that all of this data could be just as well
 *  stored in the Simulation class.  We choose not to that because
 *  - It would turn the Simulation class into quite a kitchen sink
 *  - It would make it even harder to construct the Simulation class
 *    properly
 *  - The data is only needed in the simulation loop.
 */
struct SimLoopData
{

   SimLoopData(const Simulate& sim)
      : voltageExchange(sim.sendMap_, (sim.commTable_))
   {
      stimIsNonZero = 1;
      diffMiscBarrier = L2_BarrierWithSync_InitShared();
      reactionBarrier = L2_BarrierWithSync_InitShared();
      diffusionBarrier = L2_BarrierWithSync_InitShared();
      reactionWaitOnNonGateBarrier = L2_BarrierWithSync_InitShared();
      timingBarrier = L2_BarrierWithSync_InitShared();

#ifdef PER_SQUAD_BARRIER
      {
         const int
         nsq = sim.reactionThreads_.nSquads(),
         sqsz = sim.reactionThreads_.nThreads() / nsq /*sim.reactionThreads_.squadSize()*/;
         core_barrier = (L2_Barrier_t **) malloc(sizeof (L2_Barrier_t *) * nsq);
         for (int i = 0; i < nsq; i++)
         {
            core_barrier[i] = L2_BarrierWithSync_InitShared();
         }
         integratorOffset_psb.resize(sim.reactionThreads_.nThreads() + 1);

         /*
            This is an ugly import of the workBundle() function from
            TT06Dev_Reaction.cc
          */
         int workBundle(int index, int nItems, int nGroups, int mod, int& offset);
         integratorOffset_psb[0] = 0;
         for (int i = 0; i < nsq; i++)
         {
            const int vlen = 4;
            int ic, nc, nv, nv_div, nv_mod;

            nc = workBundle(i, sim.anatomy_.nLocal(), nsq, vlen, ic);
            nv = (nc + 3) / 4;
            nv_div = nv / sqsz;
            nv_mod = nv % sqsz;

            if (integratorOffset_psb[i * sqsz] != ic)
            {
               printf("%s:%d: Offset error, nsq=%d i=%d sqsz=%d nc=%d iOff[]=%d ic=%d\n",
                      __FILE__, __LINE__, nsq, i, sqsz, nc, integratorOffset_psb[i * sqsz], ic);
               exit(1);
            }

            for (int j = 0; j < sqsz; j++)
            {
               integratorOffset_psb[i * sqsz + j + 1] =
                  integratorOffset_psb[i * sqsz + j] + 4 * (nv_div + (j < nv_mod));
               if (integratorOffset_psb[i * sqsz + j + 1] > integratorOffset_psb[i * sqsz] + nc)
               {
                  integratorOffset_psb[i * sqsz + j + 1] = integratorOffset_psb[i * sqsz] + nc;
               }
            }
         }
      }
#endif

      int nLocal = sim.anatomy_.nLocal();
      const vector<int>& haloSendMap = voltageExchange.getSendMap();
#ifndef PER_SQUAD_BARRIER
      mkOffsets(integratorOffset, sim.anatomy_.nLocal(), sim.reactionThreads_);
#endif
      mkOffsets(fillSendBufOffset, haloSendMap.size(), sim.reactionThreads_);
   }

   ~SimLoopData()
   {
      free(reactionWaitOnNonGateBarrier);
      free(diffusionBarrier);
      free(reactionBarrier);
      free(timingBarrier);
   }


   int stimIsNonZero;
   L2_Barrier_t* diffMiscBarrier;
   L2_Barrier_t* reactionBarrier;
   L2_Barrier_t* diffusionBarrier;
   L2_Barrier_t* reactionWaitOnNonGateBarrier;
   // The timing barrier syncs the top of the reaction loop with the
   // top of the diffusion loop.  This is done only to make it easier
   // to compare and understand timings.  This barrier can be removed
   // to slightly improve performance.
   L2_Barrier_t* timingBarrier;

#ifdef PER_SQUAD_BARRIER
   L2_Barrier_t **core_barrier;
   vector<int> integratorOffset_psb;
#endif

#ifndef PER_SQUAD_BARRIER
   vector<int> integratorOffset;
#endif
   vector<pair<unsigned, unsigned> > sendBufIndirect;
   vector<int> fillSendBufOffset;
   HaloExchange<double, AlignedAllocator<double> > voltageExchange;
};

void diffusionLoop(Simulate& sim,
                   SimLoopData& loopData,
                   L2_BarrierHandle_t& reactionHandle,
                   L2_BarrierHandle_t& diffusionHandle)
{
   profileStart(diffusionLoopTimer);
   int tid = sim.diffusionThreads_.teamRank();
   int nThreads = sim.diffusionThreads_.nThreads();
   L2_BarrierHandle_t diffMiscBarrierHandle;
   L2_BarrierHandle_t timingHandle;
   L2_BarrierWithSync_InitInThread(loopData.diffMiscBarrier, &diffMiscBarrierHandle);
   L2_BarrierWithSync_InitInThread(loopData.timingBarrier, &timingHandle);
   int nTotalThreads = sim.reactionThreads_.nThreads() + sim.diffusionThreads_.nThreads();

   vector<int> zeroDiffusionOffset(nThreads + 1);
   {
      const VectorDouble32 & dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.readOnHost());
      mkOffsets(zeroDiffusionOffset, dVmDiffusion.size(), sim.diffusionThreads_);
   }
   int zdoBegin = zeroDiffusionOffset[tid];
   int zdoEnd = zeroDiffusionOffset[tid + 1];


   //sim.diffusion_->test();

   uint64_t loopLocal = sim.loop_;
   int globalSyncRate = sim.globalSyncRate_;
   //  if globalSyncCnt == -1 never sync
   //  if globalSyncCnt ==  0  sync only once at start of loop
   //  if globalSyncCnt >   0  sync at start of loop and then every globalSyncCnt timestep;
   int globalSyncCnt = 1;
   if (globalSyncRate == -1) { globalSyncCnt = -1; }
   while (loopLocal < sim.maxLoop_)
   {
      if (globalSyncCnt >= 0) { globalSyncCnt--; }
      if (globalSyncCnt == 0)
      {
         globalSyncCnt = globalSyncRate;
         if (tid == 0)
         {
            startTimer(diffusionImbalanceTimer);
            loopData.voltageExchange.barrier();
            stopTimer(diffusionImbalanceTimer);
         }
      }
      threadBarrier(timingBarrierTimer, loopData.timingBarrier, &timingHandle, nTotalThreads);

      // Halo Exchange
      if (tid == 0)
      {
         //         startTimer(diffusionImbalanceTimer);
         //         loopData.voltageExchange.barrier();
         //         stopTimer(diffusionImbalanceTimer);

         startTimer(haloTimer);

         startTimer(haloLaunchTimer);
         loopData.voltageExchange.startComm();
         stopTimer(haloLaunchTimer);
      }

      // stimulus
      startTimer(initializeDVmDTimer);
      {
         VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.modifyOnHost());
         if (loopData.stimIsNonZero > 0)
         {
            // we has a non-zero stimulus in the last time step so we
            // need to zero out the storage.
            for (unsigned ii = zdoBegin; ii < zdoEnd; ++ii)
            {
               dVmDiffusion[ii] = 0;
            }
            L2_BarrierWithSync_Barrier(loopData.diffMiscBarrier, &diffMiscBarrierHandle,
                                       sim.diffusionThreads_.nThreads());
            if (tid == 0)
            {
               loopData.stimIsNonZero = 0;
            }
         }
      }
      stopTimer(initializeDVmDTimer);
      {
         VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.readOnDevice());
         if (sim.stimulus_.size() > 0)
         {
            if (tid == 0)
            {
               startTimer(stimulusTimer);
               for (unsigned ii = 0; ii < sim.stimulus_.size(); ++ii)
               {
                  loopData.stimIsNonZero += sim.stimulus_[ii]->stim(sim.time_, dVmDiffusion);
               }
               stopTimer(stimulusTimer);
            }
         }
      }


      //L2_BarrierWithSync_Barrier(loopData.diffMiscBarrier, &diffMiscBarrierHandle, sim.diffusionThreads_.nThreads());
      startTimer(stencilOverlapTimer);
      {
         VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.readOnDevice());
         sim.diffusion_->calc_overlap(dVmDiffusion); //DelayTimeBase(160000);
      }
      stopTimer(stencilOverlapTimer);

      if (tid == 0)
      {
         startTimer(haloWaitTimer);
         loopData.voltageExchange.wait();
         stopTimer(haloWaitTimer);

         stopTimer(haloTimer);
      }

      // Need a barrier for the completion of the halo exchange.
      startTimer(diffusionL2BarrierHalo1Timer);
      L2_BarrierWithSync_Barrier(loopData.diffMiscBarrier, &diffMiscBarrierHandle,
                                 sim.diffusionThreads_.nThreads());
      stopTimer(diffusionL2BarrierHalo1Timer);

      startTimer(diffusionCalcTimer);
      // copy remote
      sim.diffusion_->updateRemoteVoltage(loopData.voltageExchange.getRecvBuf());
      // temporary barrier
      L2_BarrierWithSync_Barrier(loopData.diffMiscBarrier, &diffMiscBarrierHandle,
                                 sim.diffusionThreads_.nThreads());
      //stencil
      {
         VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.modifyOnDevice());
         sim.diffusion_->calc(dVmDiffusion);
      }
      stopTimer(diffusionCalcTimer);

      //barrier
      startTimer(diffusionL2BarrierHalo2Timer);
      L2_BarrierWithSync_Barrier(loopData.diffMiscBarrier, &diffMiscBarrierHandle,
                                 sim.diffusionThreads_.nThreads());
      stopTimer(diffusionL2BarrierHalo2Timer);


      // announce that diffusion derivatives are ready.
      L2_BarrierWithSync_Arrive(loopData.diffusionBarrier, &diffusionHandle,
                                sim.diffusionThreads_.nThreads());
      L2_BarrierWithSync_Reset(loopData.diffusionBarrier,
                               &diffusionHandle,
                               sim.diffusionThreads_.nThreads());


      // wait for reaction (integration) to finish
      startTimer(diffusionWaitTimer);
      L2_BarrierWithSync_WaitAndReset(loopData.reactionBarrier,
                                      &reactionHandle,
                                      sim.reactionThreads_.nThreads());
      stopTimer(diffusionWaitTimer);
      ++loopLocal;
      //      startTimer(dummyTimer);
      //      stopTimer(dummyTimer);

   }
   profileStop(diffusionLoopTimer);
}

void reactionLoop(Simulate& sim, SimLoopData& loopData, L2_BarrierHandle_t& reactionHandle, L2_BarrierHandle_t& diffusionHandle,
                  void (*integrate)(const int, const int, const double, double*, double*, unsigned*, double*, double*k, double*, double))
{
   profileStart(reactionLoopTimer);
   int tid = sim.reactionThreads_.teamRank();
   L2_BarrierHandle_t reactionWaitOnNonGateHandle;
   L2_BarrierHandle_t timingHandle;
   L2_BarrierWithSync_InitInThread(loopData.reactionWaitOnNonGateBarrier, &reactionWaitOnNonGateHandle);
   L2_BarrierWithSync_InitInThread(loopData.timingBarrier, &timingHandle);
   int nTotalThreads = sim.reactionThreads_.nThreads() + sim.diffusionThreads_.nThreads();

#ifdef PER_SQUAD_BARRIER
   const int sqsz = sim.reactionThreads_.squadSize();
   const int b_id = sim.reactionThreads_.rankInfo().coreRank_;
   L2_BarrierHandle_t core_barrier_h;
   L2_Barrier_t *cb_ptr = loopData.core_barrier[b_id];
   L2_BarrierWithSync_InitInThread(cb_ptr, &core_barrier_h);
#endif


   if (tid == 0)
   {
      printData(sim);
      loopIO(sim, 1);
   }
   uint64_t loopLocal = sim.loop_;
   while (loopLocal < sim.maxLoop_)
   {
      /*   call scaleCurrents here  Need to make sure all nonGate calculation are done*/
      if (tid == 0 && sim.drugRescale_.size() > 0)
      {
         sim.reaction_->scaleCurrents(sim.drugRescale_);
      }
      threadBarrier(timingBarrierTimer, loopData.timingBarrier, &timingHandle, nTotalThreads);

      startTimer(reactionTimer);
      startTimer(nonGateRLTimer);

      {
         VectorDouble32 VmArray(sim.vdata_.VmTransport_.modifyOnDevice());
         VectorDouble32 dVmReaction(sim.vdata_.dVmReactionTransport_.modifyOnDevice());
         sim.reaction_->updateNonGate(sim.dt_, VmArray, dVmReaction);
      }
      stopTimer(nonGateRLTimer);

      startTimer(GateNonGateTimer);
#ifdef PER_SQUAD_BARRIER
      {
         L2_Barrier_nosync_Arrive(cb_ptr, &core_barrier_h, sqsz);
         L2_Barrier_nosync_WaitAndReset(cb_ptr, &core_barrier_h, sqsz);
      }
#else
      L2_BarrierWithSync_Barrier(loopData.reactionWaitOnNonGateBarrier,
                                 &reactionWaitOnNonGateHandle, sim.reactionThreads_.nThreads());
#endif
      stopTimer(GateNonGateTimer);
      startTimer(gateRLTimer);
      {
         const VectorDouble32 & VmArray(sim.vdata_.VmTransport_.modifyOnDevice());
         sim.reaction_->updateGate(sim.dt_, VmArray);
      }
      stopTimer(gateRLTimer);
      stopTimer(reactionTimer);

      startTimer(reactionWaitTimer);
      /*
         The following barrier makes sure the integrator does not write to data still being read by some
         threads still in updateGate(). With the modified loop order in integratorOffset_psb[], only per
         squad (per core) barriers are needed. With the original loop order in integratorOffset[], a full
         barrier over the reaction cores is necessary.
       */
#ifdef PER_SQUAD_BARRIER
      {
         L2_Barrier_nosync_Arrive(cb_ptr, &core_barrier_h, sqsz);
         L2_Barrier_nosync_WaitAndReset(cb_ptr, &core_barrier_h, sqsz);
      }
#else
      L2_BarrierWithSync_Barrier(loopData.reactionWaitOnNonGateBarrier,
                                 &reactionWaitOnNonGateHandle, sim.reactionThreads_.nThreads());
#endif

      L2_BarrierWithSync_WaitAndReset(loopData.diffusionBarrier, &diffusionHandle, sim.diffusionThreads_.nThreads());
      stopTimer(reactionWaitTimer);

      startTimer(dummyTimer);
      stopTimer(dummyTimer);

      startTimer(integratorTimer);
#ifdef PER_SQUAD_BARRIER
      const int begin = loopData.integratorOffset_psb[tid];
      const int end = loopData.integratorOffset_psb[tid + 1];
#else
      const int begin = loopData.integratorOffset[tid];
      const int end = loopData.integratorOffset[tid + 1];
#endif

      if (PRINT_WP)
      {
         static int inited[64] = {0};

         if (inited[tid] == 0)
         {
            int pid, np;
            const int nt = sim.reactionThreads_.nThreads();
            static volatile int tag[64] = {0};

            MPI_Comm_size(MPI_COMM_WORLD, &np);
            MPI_Comm_rank(MPI_COMM_WORLD, &pid);

            if (tid == 0)
            {
               int i;

               MPI_Barrier(MPI_COMM_WORLD);
               for (i = 0; i < np; i++)
               {
                  if (i == pid)
                  {
                     int j;
                     printf("@pid=%03d,tid=%02d :I WP: offset = %6d  nCells = %6d  end = %6d\n",
                            pid, tid, begin, end - begin, end);

                     tag[tid] = 1;
                     while (tag[nt - 1] == 0)
                     {
                     }
                     for (j = 0; j < nt; j++) { tag[j] = 0; }
                  }
                  MPI_Barrier(MPI_COMM_WORLD);
               }
            }
            else
            {
               while (tag[tid - 1] == 0)
               {
               }

               printf("@pid=%03d,tid=%02d :I WP: offset = %6d  nCells = %6d  end = %6d\n",
                      pid, tid, begin, end - begin, end);

               tag[tid] = 1;
               while (tag[0] > 0)
               {
               }
            }

            inited[tid] = 1;
         }
      }

      {
         VectorDouble32 VmArray(sim.vdata_.VmTransport_.modifyOnDevice());
         VectorDouble32 dVmReaction(sim.vdata_.dVmReactionTransport_.modifyOnDevice());
         VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.modifyOnDevice());
         integrate(begin, end, sim.dt_, &dVmReaction[0], &dVmDiffusion[0],
                   sim.diffusion_->blockIndex(),
                   sim.diffusion_->dVmBlock(),
                   sim.diffusion_->VmBlock(),
                   &VmArray[0],
                   sim.diffusion_->diffusionScale());
      }
      stopTimer(integratorTimer);

      startTimer(rangeCheckTimer);
#ifndef NTIMING
      {
         const VectorDouble32 & VmArray(sim.vdata_.VmTransport_.readOnDevice());
         const VectorDouble32 & dVmReaction(sim.vdata_.dVmReactionTransport_.readOnDevice());
         const VectorDouble32 & dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.readOnDevice());
         if (sim.checkRange_.on) { sim.checkRanges(begin, end, VmArray, dVmReaction, dVmDiffusion); }
      }
#endif
      stopTimer(rangeCheckTimer);
      startTimer(reactionMiscTimer);
      ++loopLocal;
      if (tid == 0)
      {
         sim.time_ += sim.dt_;
         ++sim.loop_;
      }
      if (tid == 0 && sim.loop_ % sim.printRate_ == 0)
      {
         printData(sim);
      }

      if (sim.checkIO(loopLocal)) // every thread should return the same result
      {
         // We're about to call loopIO to do sensors and checkpointing.
         // But before we do we need to move the diffusion data out of
         // the matrix representation and into the array.  This is an
         // addition operation because were adding diffusion to stimulus
         // that is already stored in dVmDiffusion.  It is true that we
         // already traversed the matrix data in the integrator, but we
         // didn't want to take the time to store it since it is only
         // rare time steps that it is actually needed.  At least we get
         // to use all of the reaction threads to copy the data.
         const unsigned* const blockIndex = sim.diffusion_->blockIndex();
         const double* const dVdMatrix = sim.diffusion_->dVmBlock();
         const double diffusionScale = sim.diffusion_->diffusionScale();
         {
            VectorDouble32 dVmDiffusion(sim.vdata_.dVmDiffusionTransport_.modifyOnHost());
            for (unsigned ii = begin; ii < end; ++ii)
            {
               const int index = blockIndex[ii];
               dVmDiffusion[ii] += diffusionScale * dVdMatrix[index];
            }
         }
         loopData.stimIsNonZero = 1;
         sim.bufferReactionData(begin, end);
         L2_BarrierWithSync_Barrier(loopData.reactionWaitOnNonGateBarrier,
                                    &reactionWaitOnNonGateHandle,
                                    sim.reactionThreads_.nThreads());
         if (tid == 0) { loopIO(sim, 0); }
      }

      L2_BarrierWithSync_Barrier(loopData.reactionWaitOnNonGateBarrier,
                                 &reactionWaitOnNonGateHandle, sim.reactionThreads_.nThreads());
      stopTimer(reactionMiscTimer);

      startTimer(haloMove2BufTimer);
      {
         const vector<int>& haloSendMap = loopData.voltageExchange.getSendMap();
         unsigned begin = loopData.fillSendBufOffset[tid];
         unsigned end = loopData.fillSendBufOffset[tid + 1];
         double* sendBuf = loopData.voltageExchange.getSendBuf();
         const VectorDouble32 & VmArray(sim.vdata_.VmTransport_.readOnHost());
         for (unsigned ii = begin; ii < end; ++ii)
         {
            sendBuf[ii] = VmArray[haloSendMap[ii]];
         }
      }
      stopTimer(haloMove2BufTimer);

      startTimer(reactionL2ArriveTimer);
      L2_BarrierWithSync_Arrive(loopData.reactionBarrier, &reactionHandle, sim.reactionThreads_.nThreads());
      stopTimer(reactionL2ArriveTimer);

      startTimer(reactionL2ResetTimer);
      L2_BarrierWithSync_Reset(loopData.reactionBarrier, &reactionHandle, sim.reactionThreads_.nThreads());
      stopTimer(reactionL2ResetTimer);
      //#pragma omp barrier
   }
   profileStop(reactionLoopTimer);
}

void simulationLoopParallelDiffusionReaction(Simulate& sim)
{
   SimLoopData loopData(sim);

   simulationProlog(sim);

#if defined(SPI) && defined(TRACESPI)
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
   cout << "Rank[" << myRank << "]: numOfNeighborToSend=" << sim.commTable_->_sendTask.size() << " numOfNeighborToRecv=" << sim.commTable_->_recvTask.size() << " numOfBytesToSend=" << sim.commTable_->_sendOffset[sim.commTable_->_sendTask.size()] * sizeof (double) << " numOfBytesToRecv=" << sim.commTable_->_recvOffset[sim.commTable_->_recvTask.size()] * sizeof (double) << endl;
#endif

   timestampBarrier("Entering threaded region", MPI_COMM_WORLD);

   #pragma omp parallel
   {
      int ompTid = omp_get_thread_num();

      L2_BarrierHandle_t reactionHandle;
      L2_BarrierHandle_t diffusionHandle;
      L2_BarrierWithSync_InitInThread(loopData.reactionBarrier, &reactionHandle);
      L2_BarrierWithSync_InitInThread(loopData.diffusionBarrier, &diffusionHandle);

      // setup matrix voltages for first timestep.
      {
         VectorDouble32 VmArray(sim.vdata_.VmTransport_.modifyOnHost());
         if (sim.reactionThreads_.teamRank() >= 0)
         {
            sim.diffusion_->updateLocalVoltage(&VmArray[0]);
         }
         if (sim.reactionThreads_.teamRank() == 0)
         {
            loopData.voltageExchange.fillSendBuffer(VmArray);
         }
      }
      #pragma omp barrier
      profileStart(simulationLoopTimer);
      if (sim.diffusionThreads_.teamRank() >= 0)
      {
         diffusionLoop(sim, loopData, reactionHandle, diffusionHandle);
      }
      if (sim.reactionThreads_.teamRank() >= 0)
      {
         reactionLoop(sim, loopData, reactionHandle, diffusionHandle, integrateLoop);
      }
      profileStop(simulationLoopTimer);
   }
}
