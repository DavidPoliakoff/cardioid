#ifndef TT06NONGATE_H
#define TT06NONGATE_H
#include <stdio.h>
typedef struct currentScales_st        { double   K1,    Kr,    Ks,    Na,    bNa,    CaL,    bCa,    to,    NaK,    NaCa,    pCa,    pK,    NaL;} CURRENT_SCALES;    
static const char * currentNames[14] =      { "I_K1","I_Kr","I_Ks","I_Na","I_bNa","I_CaL","I_bCa","I_to","I_NaK","I_NaCa","I_pCa","I_pK","I_NaL",""}; 
static CURRENT_SCALES currentScalesDefault = {    1.0,   1.0,   1.0,   1.0,    1.0,    1.0,     1.0,  1.0,    1.0,     1.0,    1.0,   1.0,    1.0  }; 
struct LogParms     { FILE *file; int loop, cellType, minK_i,maxK_i,midK_i,minNa_i,maxNa_i,midNa_i;}  ;
struct nonGateCnst { double c2,  c3,  c4,  c5,  c6,  c7,  c8,  c9,
                    c11, c13, c14, c15, c16, c17, c18, c19,
               c20, c21, c22, c23, c24, c25, c26, c27, c28, c29,
               c30, c31, c32, c33, c34, c36,
               c40, c43, c44,twenty,eighty,point6,point4;};
#ifdef __cplusplus
extern "C" {
#endif
void update_nonGate(void *fit, CURRENT_SCALES *currentScales, double dt, struct CellTypeParms *cellTypeParms, int nCells, int *cellTypeVector, double *VM, int offset, double **state, double *dVdt);
void update_nonGate_v1(void *fit, CURRENT_SCALES *currentScales, double dt, struct CellTypeParms *cellTypeParms, int nCells, int *cellTypeVector, double *VM, int offset, double **state, double *dVdt);
void sampleLog(struct LogParms *logParms, int nCells, int offset, int *cellTypeVector, double *VM, double **state);
void set_SP(); 
void initNonGateCnst(); 
double get_c9( ); 
double fv0(double Vm, void *parms) ;
double fv1(double Vm, void *parms) ;
double fv2(double Vm, void *parms) ;
double fv3(double Vm, void *parms) ;
double fv4(double Vm, void *parms) ;
double fv5(double Vm, void *parms) ;
double fv6(double Vm, void *parms) ;
#ifdef __cplusplus
}
#endif
#endif 
