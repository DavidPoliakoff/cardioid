#! /usr/bin/perl -w
#
# generate Cardioid object.data input files for timing runs.
#
# written by Erik Draeger, LLNL, 10/15/2012

# global variables

$AnatomyDir180M = "/p/ls1/emhm/180M/anatomy-15-Mar-2012-nospur";

$thisdir = `pwd`;  chomp $thisdir;
$bgqSPIExe = "../../../bin/cardioid-bgq-spi";
$bgqHPMExe = "../../../bin/cardioid-bgq-spi-hpm";
$pelotonExe = "../../../bin/cardioid-peloton";
$nthreadsBGQ = 64;
$nthreadsPeloton = 4;

$useStateSensor = 0;  # add code for the new stateVariables sensor

$nIterations = 100000;
$printRate = $nIterations/10;
$checkpointRate = 50000000;

$anatomy = "180M";
$reaction = "TT06RRGOpt";
$fastgates = 1;
$rationalfns = 1;
$smoothing = 1;
$ntasks = 98304;
$machine = "bgqspi";
$loadbal = "work";

foreach $dt (30, 35, 40, 45, 50)
{
   printObject($anatomy,$ntasks,$dt,$machine);
}

# details of how to build object.data files for each set of parameters
sub printObject
{
   my($anatomy,$ntasks,$dt,$machine) = @_;

   $nnodes = $ntasks;  # BGQ default
   if ($machine eq "peloton") { $nnodes = $ntasks/(16/$nthreadsPeloton); }
   if ($ntasks%$nnodes != 0) { $nnodes++; }
   $nthreads = $nthreadsBGQ;
   if ($machine eq "peloton") { $nthreads = $nthreadsPeloton; }

   $date = `date +%m%d%y`;  chomp $date;
   $maindir = join '','timestep-runs-',$date;
   $dirname = join '',$anatomy,'-',$machine,'-',$loadbal,'bal','-dt',$dt,'us','-N',$nnodes;
   system("mkdir -p $maindir/$dirname");

# store different process grids in hashes
   $px{16} = 2;     $py{16} = 4;     $pz{16} = 2;  
   $px{32} = 4;     $py{32} = 4;     $pz{32} = 2;  
   $px{64} = 4;     $py{64} = 4;     $pz{64} = 4;  
   $px{128} = 4;    $py{128} = 8;    $pz{128} = 4;  
   $px{256} = 8;    $py{256} = 8;    $pz{256} = 4;  
   $px{512} = 8;    $py{512} = 8;    $pz{512} = 8;  
   $px{1024} = 8;   $py{1024} = 16;  $pz{1024} = 8;  
   $px{2048} = 16;  $py{2048} = 16;  $pz{2048} = 8;  
   $px{4096} = 16;  $py{4096} = 16;  $pz{4096} = 16;  
   $px{8192} = 16;  $py{8192} = 32;  $pz{8192} = 16;  
   $px{16384} = 32; $py{16384} = 32; $pz{16384} = 16;  
   $px{24576} = 32; $py{24576} = 32; $pz{24576} = 24;  
   $px{32768} = 32; $py{32768} = 32; $pz{32768} = 32;  
   $px{36864} = 32; $py{36864} = 36; $pz{36864} = 32;  
   $px{49152} = 32; $py{49152} = 32; $pz{49152} = 48;  
   $px{73728} = 32; $py{73728} = 64; $pz{73728} = 36;  
   $px{98304} = 48; $py{98304} = 32; $pz{98304} = 64;  
      
# store workbound balancer block sizes for each task count
   if ($anatomy eq "180M")
   {
      $wx{98304} = 12; $wy{98304} = 12; $wz{98304} = 14;  

      if ($ntasks != 98304)
      {
         print "No basic block definition for ntasks = $ntasks!\n";
         exit;
      }
   }
   else
   {
      print "Unknown anatomy:  $anatomy\n";
      exit;
   }

   if ($ntasks != $px{$ntasks}*$py{$ntasks}*$pz{$ntasks})
   {
      print "Process grid not defined correctly for ntasks = $ntasks:  px = $px{$ntasks}, py = $py, pz = $pz{$ntasks}\n";
      exit;
   }

   open OBJECT, ">$maindir/$dirname/object.data";

   # simulate block
   print OBJECT "simulate SIMULATE\n";
   print OBJECT "{\n";
   print OBJECT "   anatomy = $anatomy;\n";   
   print OBJECT "   decomposition = $loadbal;\n";
   print OBJECT "   diffusion = FGR;\n";
   print OBJECT "   reaction = $reaction;\n";

   if ($anatomy eq "180M")
   {
      print OBJECT "   stimulus = box0 box1 box2 box3 box4 box5 box6 box7 box8;\n";
   }
   else {
      print "Unknown anatomy.\n";
      exit;
   }
   print OBJECT "   loop = 0;\n";
   print OBJECT "   maxLoop = $nIterations;\n";
   print OBJECT "   dt = $dt us;\n";
   print OBJECT "   time = 0;\n";
   print OBJECT "   checkRanges = 0;\n";
   print OBJECT "   printRate = $printRate;\n";
   print OBJECT "   snapshotRate = $checkpointRate;\n";
   print OBJECT "   checkpointRate = $checkpointRate;\n";
   if ($useStateSensor == 1)
   {
      print OBJECT "   sensor = stateVariable;\n";
   }
   if ($reaction =~ /Opt/) 
   {
      print OBJECT "   loopType = pdr;\n";
      if ($loadbal eq "grid")
      {
         # comment this out for now, let assignCellsToTasks try to figure it out
         #print OBJECT "   nDiffusionCores = 3;\n";
         #print OBJECT "   nDiffusionCores = 4;\n";
      }
   }
   print OBJECT "}\n\n";

   if ($anatomy eq "180M")
   {
      print OBJECT "$anatomy ANATOMY\n";
      print OBJECT "{\n";
      print OBJECT "   method = pio;\n";
      print OBJECT "   fileName = $AnatomyDir180M\/anatomy\#;\n";
      print OBJECT "   conductivity = conductivity;\n";
      print OBJECT "   dx = 0.129237;\n";
      print OBJECT "   dy = 0.127609;\n";
      print OBJECT "   dz = 0.130206;\n";
      print OBJECT "}\n\n";
      print OBJECT "conductivity CONDUCTIVITY\n";
      print OBJECT "{\n";
      print OBJECT "   method = pio;\n";
      print OBJECT "}\n\n";
   }
   else {
      print "Unknown anatomy.\n";
      exit;
   }

   # diffusion block
   print OBJECT "FGR DIFFUSION\n";
   print OBJECT "{\n";
   print OBJECT "   method = FGR;\n";
   print OBJECT "   diffusionScale = 714.2857143;\n";
   print OBJECT "   variant = strip;\n";
   print OBJECT "}\n\n";

   # decomposition block
   if ($loadbal eq "grid")
   {
      print OBJECT "$loadbal DECOMPOSITION \n";
      print OBJECT "{\n";
      print OBJECT "   method = grid;\n";
      print OBJECT "   nx = $px{$ntasks};\n";
      print OBJECT "   ny = $py{$ntasks};\n";
      print OBJECT "   nz = $pz{$ntasks};\n";
      print OBJECT "   diffCost = 0.5;\n";
      print OBJECT "   nDiffCores = 2;\n";
      print OBJECT "   printTaskInfo = 1;\n";
      print OBJECT "}\n\n";
   }
   elsif ($loadbal eq "work")
   {
      print OBJECT "$loadbal DECOMPOSITION \n";
      print OBJECT "{\n";
      print OBJECT "   method = workBound;\n";
      print OBJECT "   dx = $wx{$ntasks};\n";
      print OBJECT "   dy = $wy{$ntasks};\n";
      print OBJECT "   dz = $wz{$ntasks};\n";
      print OBJECT "   nCores = 16;\n";
      print OBJECT "   nRCoresBB = 2;\n";
      print OBJECT "   alpha = 0.1;\n";
      print OBJECT "   beta = 100.0;\n";
      print OBJECT "   printStats = 0;\n";
      print OBJECT "   printTaskInfo = 1;\n";
      print OBJECT "}\n\n";      
   }
   else {
      print "loadbal = $loadbal undefined!\n";
      exit;
   }

   # reaction block
   print OBJECT "$reaction REACTION\n";
   print OBJECT "{\n";

   if ($reaction eq "TT06RRGOpt")
   {
      print OBJECT "   method = TT06Opt;\n";
      if ($rationalfns == 1) {
          print OBJECT "   tolerance = 0.0001;\n";
      }
      elsif ($rationalfns == 0) {
          print OBJECT "   tolerance = 0.0;\n";
      }
      else {
         print "Invalid choice of rationalfns = $rationalfns!\n";
         exit;
      }
      print OBJECT "   mod = $smoothing;\n";
      print OBJECT "   fitFile = fit.data;\n";
      print OBJECT "   fastGate =$fastgates;\n"; 
      print OBJECT "   fastNonGate =$fastgates;\n";
      print OBJECT "   cellTypes = endo mid epi;\n";
      print OBJECT "}\n\n";
      print OBJECT "endo CELLTYPE { clone=endoRRG; }\n";
      print OBJECT "mid CELLTYPE { clone=midRRG;  P_NaK=3.0; g_NaL=0.6; }\n";
      print OBJECT "epi CELLTYPE { clone=epiRRG; }\n\n";
   }
   elsif ($reaction eq "TT06RRG") 
   {
      print OBJECT "   method = TT06_RRG;\n";
      print OBJECT "}\n\n";
   }
   elsif ($reaction eq "TT06") 
   {
      print OBJECT "   method = TT06_CellML;\n";
      print OBJECT "   integrator = rushLarsen;\n";
      print OBJECT "}\n\n";
   }
   elsif ($reaction eq "TT06Opt")
   {
      print OBJECT "   method = TT06Opt;\n";
      if ($rationalfns == 1) {
          print OBJECT "   tolerance = 0.0001;\n";
      }
      elsif ($rationalfns == 0) {
          print OBJECT "   tolerance = 0.0;\n";
      }
      else {
         print "Invalid choice of rationalfns = $rationalfns!\n";
         exit;
      }
      print OBJECT "   mod = $smoothing;\n";
      print OBJECT "   fastGate =$fastgates;\n"; 
      print OBJECT "   fastNonGate =$fastgates;\n";
      print OBJECT "   cellTypes = endo mid epi;\n";
      print OBJECT "}\n\n";
      print OBJECT "endo CELLTYPE { clone=endoCellML; }\n";
      print OBJECT "mid CELLTYPE { clone=midCellML; }\n";
      print OBJECT "epi CELLTYPE { clone=epiCellML; }\n\n";
   }
   else
   {
      print "Reaction $reaction not defined in printObject!\n";
      exit;
   }

   if ($anatomy eq "180M")
   {
      print OBJECT "box0 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 352;\n";
      print OBJECT "   xMax = 372;\n";
      print OBJECT "   yMin = 253;\n";
      print OBJECT "   yMax = 273;\n";
      print OBJECT "   zMin = 389;\n";
      print OBJECT "   zMax = 409;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n\n";
      print OBJECT "box1 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 370;\n";
      print OBJECT "   xMax = 390;\n";
      print OBJECT "   yMin = 246;\n";
      print OBJECT "   yMax = 266;\n";
      print OBJECT "   zMin = 462;\n";
      print OBJECT "   zMax = 482;\n";
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "box2 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 319;\n";
      print OBJECT "   xMax = 339;\n";
      print OBJECT "   yMin = 248;\n";
      print OBJECT "   yMax = 268;\n"; 
      print OBJECT "   zMin = 309;\n";
      print OBJECT "   zMax = 329;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "box3 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 433;\n";
      print OBJECT "   xMax = 453;\n";
      print OBJECT "   yMin = 332;\n";
      print OBJECT "   yMax = 352;\n"; 
      print OBJECT "   zMin = 366;\n";
      print OBJECT "   zMax = 386;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "\n";
      print OBJECT "box4 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 486;\n";
      print OBJECT "   xMax = 506;\n";
      print OBJECT "   yMin = 303;\n";
      print OBJECT "   yMax = 323;\n"; 
      print OBJECT "   zMin = 397;\n";
      print OBJECT "   zMax = 417;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "box5 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 417;\n";
      print OBJECT "   xMax = 437;\n";
      print OBJECT "   yMin = 323;\n";
      print OBJECT "   yMax = 343;\n"; 
      print OBJECT "   zMin = 279;\n";
      print OBJECT "   zMax = 299;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "\n";
      print OBJECT "box6 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 173;\n";
      print OBJECT "   xMax = 193;\n";
      print OBJECT "   yMin = 438;\n";
      print OBJECT "   yMax = 458;\n"; 
      print OBJECT "   zMin = 516;\n";
      print OBJECT "   zMax = 536;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "box7 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 179;\n";
      print OBJECT "   xMax = 199;\n";
      print OBJECT "   yMin = 354;\n";
      print OBJECT "   yMax = 374;\n"; 
      print OBJECT "   zMin = 546;\n";
      print OBJECT "   zMax = 566;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
      print OBJECT "box8 STIMULUS\n";
      print OBJECT "{\n";
      print OBJECT "   method = box;\n";
      print OBJECT "   xMin = 322;\n";
      print OBJECT "   xMax = 342;\n";
      print OBJECT "   yMin = 392;\n";
      print OBJECT "   yMax = 412;\n"; 
      print OBJECT "   zMin = 450;\n";
      print OBJECT "   zMax = 470;\n"; 
      print OBJECT "   vStim = -36 mV/ms;\n";
      print OBJECT "   tStart = 0;\n";
      print OBJECT "   duration = 1;\n";
      print OBJECT "   period = 2000;\n";
      print OBJECT "}\n";
   }
   else 
   {
      print "Stimulus not defined for anatomy $anatomy\n";
      exit;
   }

   if ($useStateSensor == 1)
   {
      print OBJECT "stateVariable SENSOR\n";
      print OBJECT "{\n";
      print OBJECT "   gid = 727354661;\n";
      print OBJECT "   radius = 4.0;\n";
      print OBJECT "   method = stateVariables;\n";
      print OBJECT "   fields = all;\n";
      print OBJECT "   startTime = 0.0;\n";
      print OBJECT "   endTime = 500.0;\n";
      print OBJECT "   dirname = sensorData;\n";
      print OBJECT "   printRate = 1;\n";
      print OBJECT "}\n\n";
   }

   close OBJECT;

   # print batch script
   if ($machine eq "bgqspi")
   {
      $bgqbatch = "sbatch.bgq";
      open BGQ, ">$maindir/$dirname/$bgqbatch";
      print BGQ "\#!/bin/bash\n";
      print BGQ "\#SBATCH --nodes=$nnodes\n";
      print BGQ "\n";
      print BGQ "export OMP_NUM_THREADS=$nthreadsBGQ\n";
      print BGQ "export MUSPI_NUMBATIDS=203\n";
      print BGQ "export MUSPI_NUMINJFIFOS=3\n";
      print BGQ "export MUSPI_NUMRECFIFOS=3\n";
      print BGQ "export MUSPI_NUMCLASSROUTES=3\n\n";
      print BGQ "srun --ntasks=$ntasks $bgqSPIExe\n";
      close BGQ;
   }
   # print batch script
   elsif ($machine eq "bgqhpm")
   {
      $bgqbatch = "sbatch.bgq";
      open BGQ, ">$maindir/$dirname/$bgqbatch";
      print BGQ "\#!/bin/bash\n";
      print BGQ "\#SBATCH --nodes=$nnodes\n";
      print BGQ "\n";
      print BGQ "export OMP_NUM_THREADS=$nthreadsBGQ\n";
      print BGQ "export MUSPI_NUMBATIDS=203\n";
      print BGQ "export MUSPI_NUMINJFIFOS=3\n";
      print BGQ "export MUSPI_NUMRECFIFOS=3\n";
      print BGQ "export MUSPI_NUMCLASSROUTES=3\n\n";
      print BGQ "srun --ntasks=$ntasks $bgqHPMExe\n";
      close BGQ;
   }
   elsif ($machine eq "peloton")
   {
      $pelbatch = "msub.pel";
      open PEL, ">$maindir/$dirname/$pelbatch";
      print PEL "\#!/bin/bash\n";
      print PEL "\#MSUB -l nodes=$nnodes\n";
      print PEL "\#MSUB -l walltime=12:00:00\n";
      print PEL "\#MSUB -A gbcq\n";
      print PEL "\n";
      print PEL "export OMP_NUM_THREADS=$nthreadsPeloton\n";
      print PEL "srun -n $ntasks $pelotonExe\n";
      close PEL;

      $peldebug = "rundebug.pel";
      open DEB, ">$maindir/$dirname/$peldebug";
      print DEB "\#!/bin/bash\n";
      print DEB "export OMP_NUM_THREADS=$nthreadsPeloton\n";
      print DEB "srun -N $nnodes -n $ntasks -p pdebug $pelotonExe object.data > slurm.out\n";   
      close DEB;
      system("chmod u+x $maindir/$dirname/$peldebug");
   }
}
