#include "mfem.hpp"
#include "object.h"
#include "object_cc.hh"
#include "ddcMalloc.h"
#include "pio.h"
#include "pioFixedRecordHelper.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <cassert>
#include <memory>
#include <set>
#include <dirent.h>
#include <regex.h>
#include <unistd.h>
#include "util.hpp"
#include "MatrixElementPiecewiseCoefficient.hpp"

#define StartTimer(x)
#define EndTimer()

using namespace mfem;

MPI_Comm COMM_LOCAL = MPI_COMM_WORLD;

int main(int argc, char *argv[])
{
   MPI_Init(NULL,NULL);
   int num_ranks, my_rank;
   MPI_Comm_size(COMM_LOCAL,&num_ranks);
   MPI_Comm_rank(COMM_LOCAL,&my_rank);

   std::cout << "Initializing with " << num_ranks << " MPI ranks." << std::endl;

   int order = 1;

   std::vector<std::string> objectFilenames;
   if (argc == 1)
      objectFilenames.push_back("femheart.data");

   for (int iargCursor=1; iargCursor<argc; iargCursor++)
      objectFilenames.push_back(argv[iargCursor]);

   if (my_rank == 0) {
      for (int ii=0; ii<objectFilenames.size(); ii++)
	 object_compilefile(objectFilenames[ii].c_str());
   }
   object_Bcast(0,MPI_COMM_WORLD);

   OBJECT* obj = object_find("femheart", "HEART");
   assert(obj != NULL);

   StartTimer("Read the mesh");
   // Read shared global mesh
   mfem::Mesh *mesh = ecg_readMeshptr(obj, "mesh");
   EndTimer();
   int dim = mesh->Dimension();

   //Fill in the MatrixElementPiecewiseCoefficients
   std::vector<int> heartRegions;
   objectGet(obj,"heart_regions", heartRegions);

   std::vector<double> sigma_i, sigma_e;
   objectGet(obj,"sigma_i",sigma_i);
   objectGet(obj,"sigma_e",sigma_e);

   double dt;
   objectGet(obj,"dt",dt,"0.01 ms");
   double Bm;
   objectGet(obj,"Bm",Bm,"0.14 1/mm"); // 1/mm
   double Cm;
   objectGet(obj,"Cm",Cm,"1 1/cm^2"); // 1 uF/cm^2
 
   string reactionName;
   objectGet(obj, "reaction", reactionName, "BetterTT06");
   
   // Verify Inputs
   assert(heartRegions.size()*3 == sigma_i.size());
   assert(heartRegions.size()*3 == sigma_e.size());

   /*
     Ok, we're solving:

     div(sigma_m*grad(Vm)) = Bm*Cm*(dVm/dt + Iion - Istim)

     time in {ms}
     space in mm
     Vm in {mV}
     Iion in uA/uF
     Istim in uA/uF
     Cm in uF/mm^2
     Bm in 1/mm
     sigma in mS/mm

     To solve this, I use a semi implicit crank nicolson:

     div(sigma_m*grad[(Vm_new+Vm_old)/2]) = Bm*Cm*[(Vm_new-Vm_old)/dt + Iion]

     with some algebra, that comes to

     {1-dt/(2*Bm*Cm)*(div sigma_m*grad)}*Vm_new = {1+dt/(2*Bm*Cm)*(div sigma_m*grad)}*Vm_old - dt*Iion + dt*Istim

     One easy way to check this is to set sigma_m to zero, then you get Forward euler for isolated equations.

     Each {} is a matrix that is done with FEM.

     sigma_m = sigma_e*sigma_i/(sigma_e+sigma_i)

     This is the monodomain conductivity.  It really only approximates the bidomain conductivity if the ratio
     of sigma_e_fiber/sigma_e_transverse = sigma_i_fiber/sigma_i_transverse
   */

   
   StartTimer("Setting Attributes");
   mesh->SetAttributes();
   EndTimer();

   StartTimer("Partition Mesh");
   // If I read correctly, pmeshpart will now point to an integer array
   //  containing a partition ID (rank!) for every element ID.
   int *pmeshpart = mesh->GeneratePartitioning(num_ranks);
   EndTimer();
   
   
   // Elements per rank
   std::map<int,int> local_counts;
   for(int i = 0; i < num_ranks; i++) {
      local_counts[i]=0;
   }

   // Map local indices manually and keep counts for later
   int global_size = mesh->GetNE();
   std::vector<int> local_from_global(global_size);
   std::cout << "Global problem size " << global_size << std::endl;
   for(int i=0; i<global_size; i++) {
      // This calculates everybody's local indices, not just mine.
      local_from_global[i] = local_counts[pmeshpart[i]]++;
   }

   for(int i=0; i<num_ranks; i++) {
      std::cout << "Rank " << i << " has " << local_counts[i] << " elements!" << std::endl;
   }
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh, pmeshpart);

   // Build a new FEC...
   FiniteElementCollection *fec;
   std::cout << "Creating new FEC..." << std::endl;
   fec = new H1_FECollection(order, dim);
   // ...and corresponding FES
   ParFiniteElementSpace *pfespace = new ParFiniteElementSpace(pmesh, fec);
   FiniteElementSpace *fespace = new FiniteElementSpace(mesh, fec);
   std::cout << "Number of finite element unknowns: "
	     << pfespace->GetTrueVSize() << std::endl;

   // 5. Determine the list of true (i.e. conforming) essential boundary DOFs
   Array<int> ess_tdof_list;   // Essential true degrees of freedom
   // "true" takes into account shared vertices.
   {
      Array<int> ess_bdr(pmesh->bdr_attributes.Max());
      ess_bdr = 0;
      pfespace->GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }

   // 7. Define the solution vector x as a finite element grid function
   //    corresponding to pfespace. Initialize x with initial guess of zero,
   //    which satisfies the boundary conditions.
   ParGridFunction gf_Vm(pfespace);
   ParGridFunction gf_b(pfespace);
   gf_vm = 0.0;
   gf_b = 0.0;

   // Load fiber quaternions from file
   std::shared_ptr<GridFunction> flat_fiber_quat;
   ecg_readGF(obj, "fibers", mesh, flat_fiber_quat);
   std::shared_ptr<ParGridFunction> fiber_quat;
   fiber_quat = std::make_shared<mfem::ParGridFunction>(pmesh, flat_fiber_quat.get(), pmeshpart);

   
   // Load conductivity data
   MatrixElementPiecewiseCoefficient sigma_m_pos_coeffs(fiber_quat);
   MatrixElementPiecewiseCoefficient sigma_m_neg_coeffs(fiber_quat);
   for (int ii=0; ii<heartRegions.size(); ii++) {
      int heartCursor=3*ii;
      Vector sigma_i_vec(&sigma_i[heartCursor],3);
      Vector sigma_e_vec(&sigma_e[heartCursor],3);
      Vector sigma_m_pos_vec(3);
      Vector sigma_m_neg_vec(3);
      for (int jj=0; jj<3; jj++)
      {
         double value = (sigma_e_vec[jj]*sigma_i_vec[jj])/(sigma_i_vec[jj]+sigma_e_vec[jj]);
         value *= dt/2/Bm/Cm;
         sigma_m_pos_coeffs[jj] = value;
         sigma_m_neg_coeffs[jj] = -value;
      }
    
      sigma_m_pos_coeffs.heartConductivities_[heartRegions[ii]] = sigma_m_pos_vec;
      sigma_m_neg_coeffs.heartConductivities_[heartRegions[ii]] = sigma_m_neg_vec;
   }

   // 8. Set up the bilinear form a(.,.) on the finite element space
   //    corresponding to the Laplacian operator -Delta, by adding the Diffusion
   //    domain integrator.

   StartTimer("Forming bilinear system (RHS)");

   ParBilinearForm *b = new ParBilinearForm(pfespace);
   b->AddDomainIntegrator(new DiffusionIntegrator(sigma_m_pos_coeffs));
   b->AddDomainIntegrator(new MassIntegrator(one));
   b->Assemble();
   // This creates the linear algebra problem.
   HypreParMatrix RHS_mat;
   b->FormSystemMatrix(ess_tdof_list, RHS_mat);
   EndTimer();

   StartTimer("Forming bilinear system (LHS)");
   
   // Brought out of loop to avoid unnecessary duplication
   ParBilinearForm *a = new ParBilinearForm(pfespace);   // defines a.
   a->AddDomainIntegrator(new DiffusionIntegrator(sigma_m_neg_coeffs));
   b->AddDomainIntegrator(new MassIntegrator(one));
   a->Update(pfespace);
   a->Assemble();
   HypreParMatrix LHS_mat;
   a->FormSystemMatrix(ess_tdof_list,LHS_mat);
   EndTimer();

   //Set up the solve
   HyprePCG pcg(torso_mat);
   pcg.SetTol(1e-12);
   pcg.SetMaxIter(2000);
   pcg.SetPrintLevel(2);
   HypreSolver *M_test = new HypreBoomerAMG(LHS_mat);
   pcg.SetPreconditioner(*M_test);

   //Set up the ionic models
   shared_ptr<QuadratureSpace> quadSpace;
   ThreadGroup defaultGroup;
   shared_ptr<ReactionFunction> rf(quadSpace.get(),pfespace,dt,reactionName,defaultGroup);
   rf->Initialize(gf_Vm);

   ParLinearForm *c = new ParLinearForm(pfespace);
   c->AddDomainIntegrator(new QuadratureIntegrator(rf.get(), -dt));

   while (0) //for all time
   {
      //output if appropriate
      //if end time, then exit
      if (0) { break; }

      a->FormLinearSystem(ess_tdof_list,gf_Vm, gf_b, LHS_mat, actual_Vm, actual_b);
                          
      //compute the RHS matrix contribution
      RHS_mat.Mult(actual_Vm, actual_b);
      
      //compute the Iion contribution
      rf->Calc(actual_Vm);
      XXX; c->Assemble(actual_b);

      //add stimulii
      
      //solve the matrix
      pcg.Mult(actual_b, actual_Vm);

      a->RecoverFEMSolution(actual_b, actual_Vm, gf_Vm);
   }

   // 14. Free the used memory.
   delete M_test;
   delete a;
   delete b;
   delete c;
   delete pfespace;
   if (order > 0) { delete fec; }
   delete mesh, pmesh, pmeshpart;
   
   return 0;
}
