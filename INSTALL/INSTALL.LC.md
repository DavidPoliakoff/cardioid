# INSTALLATION ON LC ACCOUNT

## INITIAL SETUP
~~~
ssh -Y hafez1@cab.llnl.gov
~~~

## MOVE TO DESIRED DIRECTORY
~~~
mkdir cardioid && cd cardioid
echo 'export CARDIOID='$PWD >> ~/.profile && source ~/.profile
~~~

## GET OPEN-MPI LIBRARIES
~~~
use mvapich2-gnu-2.2
~~~

## GET HYPRE
~~~
wget https://computation.llnl.gov/projects/hypre-scalable-linear-solvers-multigrid-methods/download/hypre-2.10.0b.tar.gz
tar -xvf hypre-2.10.0b.tar.gz
cd hypre-2.10.0b/src
CC=/usr/local/tools/mvapich2-gnu-2.2/bin/mpicc CXX=/usr/local/tools/mvapich2-gnu-2.2/bin/mpicxx F77=/usr/local/tools/mvapich2-gnu-2.2/bin/mpif77 ./configure
make -j4 && cd -
rm *.tar.gz
~~~

## GET METIS
~~~
wget http://glaros.dtc.umn.edu/gkhome/fetch/sw/metis/OLD/metis-4.0.3.tar.gz
tar -xvf metis-4.0.3.tar.gz
ln -sf metis-4.0.3 metis-4.0
cd metis-4.0
make -j4 && cd -
rm *.tar.gz
~~~

## GET MFEM
~~~
wget --trust-server-names http://goo.gl/Vrpsns
tar -xvf mfem-3.3.tgz
cd mfem-3.3
make parallel -j4
rm ../*.tgz
~~~

## GET FIB-GEN
~~~
LCUSERNAME=hafez1;
git clone https://$LCUSERNAME@lc.llnl.gov/bitbucket/scm/cardioid/fib-gen.git
cd fib-gen
use mvapich2-gnu-2.2
echo 'export TMPDIR=/tmp' >> ~/.profile && source ~/.profile
tar -xf ddcMD_files_r2265.tgz
sed -i -e 's/matinv/matinv_g/g' ddcMD_files/src/three_algebra.c 
sed -i -e 's/matinv/matinv_g/g' ddcMD_files/src/three_algebra.h
make -j4
cd $CARDIOID
ln -sf mfem-3.3/fib-gen
~~~

---

## GO TO README FOR USAGE
