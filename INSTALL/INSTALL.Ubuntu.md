# INSTALLATION ON PERSONAL UBUNTU MACHINE

## INITIAL SETUP
Go to https://access.llnl.gov/cgi-bin/client-download.cgi
Click Linux to download the tarball
tar -xvf anyconnect-predeploy-linux-64-4.3.03086-k9.tar.gz
cd anyconnect-4.3.03086/vpn
sudo ./vpn_install.sh

## MOVE TO DESIRED DIRECTORY
~~~
mkdir cardioid && cd cardioid
echo 'export CARDIOID='$PWD >> ~/.bashrc && source ~/.bashrc 
~~~

## GET OPEN-MPI LIBRARIES
~~~
export PATH=/usr/bin/:$PATH
export LD_LIBRARY_PATH=/usr/lib/:$LD_LIBRARY_PATH
sudo apt-get install mpich libopenmpi-dev openmpi-doc openmpi-bin
~~~

## GET HYPRE
~~~
wget https://computation.llnl.gov/projects/hypre-scalable-linear-solvers-multigrid-methods/download/hypre-2.10.0b.tar.gz
tar -xvf hypre-2.10.0b.tar.gz
cd hypre-2.10.0b/src
./configure
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
Open Cisco AnyConnect Secure Mobililty Client using Ubuntu Dash menu
Connect to vpn.llnl.gov
Group: llnl-vpnc
Username: hafez1
Enter Password and Connect
~~~
LCUSERNAME=hafez1;
git clone https://$LCUSERNAME@lc.llnl.gov/bitbucket/scm/cardioid/fib-gen.git
cd fib-gen
echo 'export TMPDIR=/tmp' >> ~/.bashrc && source ~/.bashrc
tar -xf ddcMD_files_r2265.tgz
sed -i -e 's/matinv/matinv_g/g' ddcMD_files/src/three_algebra.c
sed -i -e 's/matinv/matinv_g/g' ddcMD_files/src/three_algebra.h
make -j4
cd $CARDIOID
ln -sf mfem-3.3/fib-gen
~~~

## MAKE SURFACE in FIB-GEN
~~~
make surface
~~~

---

## GO TO README FOR USAGE

