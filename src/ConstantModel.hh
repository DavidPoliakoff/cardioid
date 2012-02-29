#ifndef CONSTANTMODEL_HH
#define CONSTANTMODEL_HH_HH

#include "SymmetricTensor.hh"

class ConstantModel
{
 public:
   ConstantModel(const double eta[3],
                 const SymmetricTensor& sigma1,
                 const SymmetricTensor& sigma2,
                 const SymmetricTensor& sigma3,
                 const double alpha,
                 const double beta,
                 const double gamma);
   double calc(const double x, const double y, const double z);

   double getAlpha(){return alpha_;}
   double getBeta() {return beta_; }
   double getGamma(){return gamma_;}

 private:
   double eta_[3];
   double alpha_;
   double beta_;
   double gamma_;
   SymmetricTensor sigma1_;
   SymmetricTensor sigma2_;
   SymmetricTensor sigma3_;
};

#endif
