#include <algorithm>
#include "mex.h"
#include "matrix.h"
#include <Eigen/Dense>
#include "Booster.h"
#include "matlab_utils.h"
#include "matlab_eigen.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#define EIGEN_DONT_PARALLELIZE

using namespace std;
using namespace Eigen;
using namespace GBoost;

void check_train_arguments(const mxArray* prhs[]);

void mexFunctionTrain(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
  
  if (nrhs != 13 || nlhs > 4) {
    mexErrMsgTxt("Usage: [trloss, tsloss, pred, imp] = task_boost_learn(I, Itest, levels, X, R, niter, maxDepth, minNodes, minErr, fracFeat, shrink, resume, outfile)"); 
  }
  check_train_arguments(prhs);
  
  const MappedSparseMatrix<double,ColMajor,long> I = spm_matlab2eigen_mapped(prhs[0]);
  const MappedSparseMatrix<double,ColMajor,long> Itest = spm_matlab2eigen_mapped(prhs[1]);  
  const mxArray* L_matlab = prhs[2];
  Map<VectorXd> taskOv(mxGetPr(L_matlab), mxGetM(L_matlab), mxGetN(L_matlab));
  const mxArray* X_matlab = prhs[3];
  const mxArray* R_matlab = prhs[4];
  Map<MatrixXd> X(mxGetPr(X_matlab), mxGetM(X_matlab), mxGetN(X_matlab));
  Map<VectorXd> R(mxGetPr(R_matlab), mxGetM(R_matlab), mxGetN(R_matlab));
  
  unsigned int niter = (unsigned int)getDoubleScalar(prhs[5]);
  unsigned int maxDepth = (unsigned int)getDoubleScalar(prhs[6]);
  unsigned int minNodes = (unsigned int)getDoubleScalar(prhs[7]);
  double minErr = getDoubleScalar(prhs[8]);
  double fracFeat = getDoubleScalar(prhs[9]);
  double shrink = getDoubleScalar(prhs[10]);
  bool resume = (bool)getDoubleScalar(prhs[11]);
  char* filename = mxArrayToString(prhs[12]);

  TaskTreeBooster<  MappedSparseMatrix<double,ColMajor,long>, Map<MatrixXd>, Map<VectorXd> > booster;
  
  if(resume) booster.load(filename);
  booster.learn(I, Itest, taskOv, X, R, niter, maxDepth, minNodes, minErr, fracFeat, shrink, resume, filename);
 
  if(nlhs > 0){
    plhs[0] = mxCreateDoubleMatrix(niter, 1, mxREAL);
    VectorXd trloss = booster.getTrLoss();
    double* trloss_ptr = mxGetPr(plhs[0]);
    for(unsigned i = 0; i < niter; ++i) trloss_ptr[i] = trloss(i);
    
    plhs[1] = mxCreateDoubleMatrix(niter, 1, mxREAL);
    VectorXd tsloss = booster.getTsLoss();
    double* tsloss_ptr = mxGetPr(plhs[1]);
    for(unsigned i = 0; i < niter; ++i) tsloss_ptr[i] = tsloss(i);

    plhs[2] = mxCreateDoubleMatrix(X.rows(), 1, mxREAL);
    VectorXd F = booster.getF();
    double* F_ptr = mxGetPr(plhs[2]);
    for(unsigned i = 0; i < X.rows(); ++i) F_ptr[i] = F(i);
    
    plhs[3] = mxCreateDoubleMatrix(X.cols(), I.cols(), mxREAL); // This should be nfeat-by-ntasks
    Map<MatrixXd> imp(mxGetPr(plhs[3]), X.cols(), I.cols());
    booster.varImportance(imp);
  }
  // booster.printInfo();
  // cout << "########### AFTER ARCHIVING ###########" << endl;
  // TaskTreeBooster<  MappedSparseMatrix<double,ColMajor,long>, Map<MatrixXd>, Map<VectorXd> > booster2;
  // booster2.load(filename);
  // booster2.printInfo();

  // plhs[1] = mxCreateDoubleMatrix(X.rows(), 1, mxREAL);
  // Map<VectorXd> pred(mxGetPr(plhs[1]), X.rows());
  // booster.predict(I, X, pred);
}

void mexFunctionGetModel(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
  
  if (nrhs != 2 || nlhs > 6) {
    mexErrMsgTxt("Usage:  [trloss, tsloss, pred, bestTasks, imp, featMat] = task_boost_model(filename, niter)"); 
  }
  if(!mxIsChar(prhs[0])){
    mexErrMsgTxt("filename must be a string");
  }
  if(!isIntegerScalar(prhs[1]) || getDoubleScalar(prhs[1]) < 0){
    mexErrMsgTxt("niter must be a non-negative integer");
  }

  char* filename = mxArrayToString(prhs[0]);
  unsigned int in_niter = (unsigned int)getDoubleScalar(prhs[1]);

  TaskTreeBooster< MappedSparseMatrix<double,ColMajor,long>, Map<MatrixXd>, Map<VectorXd> > booster;
  booster.load(filename);

  if(nlhs > 0){
    VectorXd trloss = booster.getTrLoss();
    unsigned niter = trloss.size();
    if(niter < in_niter){
      mexErrMsgTxt("niter exceeds model's size");
    }
    niter = in_niter;
    plhs[0] = mxCreateDoubleMatrix(niter, 1, mxREAL);
    double* trloss_ptr = mxGetPr(plhs[0]);
    for(unsigned i = 0; i < niter; ++i) trloss_ptr[i] = trloss(i);
    
    if(nlhs > 1){
      plhs[1] = mxCreateDoubleMatrix(niter, 1, mxREAL);
      VectorXd tsloss = booster.getTsLoss();
      double* tsloss_ptr = mxGetPr(plhs[1]);
      for(unsigned i = 0; i < niter; ++i) tsloss_ptr[i] = tsloss(i);
    
      if(nlhs > 2){
	// TODO: Fix so that this returns the prediction from a smaller iteration if necessary.
	VectorXd F = booster.getF();
	plhs[2] = mxCreateDoubleMatrix(F.size(), 1, mxREAL);
	double* F_ptr = mxGetPr(plhs[2]);
	for(unsigned i = 0; i < F.size(); ++i) F_ptr[i] = F(i);
    
	if(nlhs > 3){
	  vector<unsigned> bestTasks = booster.getBestTasks();
	  plhs[3] = mxCreateDoubleMatrix(niter, 1, mxREAL);
	  double* bt_ptr = mxGetPr(plhs[3]);
	  // Add 1 for matlab indexing
	  for(unsigned i = 0; i < niter; ++i) bt_ptr[i] = bestTasks[i] + 1;

	  if(nlhs > 4){
	    unsigned ntasks = booster.numTasks();
	    unsigned nfeat = booster.numFeat();
	    plhs[4] = mxCreateDoubleMatrix(nfeat, ntasks, mxREAL); // This should be nfeat-by-ntasks
	    Map<MatrixXd> imp(mxGetPr(plhs[4]), nfeat, ntasks);
	    booster.varImportance(imp, niter);
	    
	    if(nlhs > 5){
	      unsigned ninternal = booster.getNumInternal();
	      plhs[5] = mxCreateDoubleMatrix(ninternal, 3, mxREAL);
	      Map<MatrixXd> fmat_out(mxGetPr(plhs[5]), ninternal, 3);
	      booster.getFeatMat(fmat_out);
	      for(unsigned i = 0; i < ninternal; ++i){
		fmat_out(i, 0) = fmat_out(i, 0) + 1;
		fmat_out(i, 1) = fmat_out(i, 1) + 1;
	      }
	    }
	  }
	}
      }
    }
  }
}

void check_train_arguments(const mxArray* prhs[]){
  const mxArray* I_matlab = prhs[0];
  const mxArray* Itest_matlab = prhs[1];  
  const mxArray* L_matlab = prhs[2];
  const mxArray* X_matlab = prhs[3];
  const mxArray* R_matlab = prhs[4];
   
  int irows = mxGetM(I_matlab);
  int icols = mxGetN(I_matlab);
  int irows2 = mxGetM(Itest_matlab);
  int icols2 = mxGetN(Itest_matlab);  
  int lrows = mxGetM(L_matlab);
  int lcols = mxGetN(L_matlab);
  int xrows = mxGetM(X_matlab);
  int xcols = mxGetN(X_matlab);
  int rrows = mxGetM(R_matlab);
  int rcols = mxGetN(R_matlab);
  
  if (rcols > 1) {
    mexErrMsgTxt("The response R must be a column vector");
  }
  if (xrows != rrows) {
    mexErrMsgTxt("R and X must have the same number of rows");
  }
  if (xrows == 0) {
    mexErrMsgTxt("X contains no examples");
  }
  if (xcols == 0) {
    mexErrMsgTxt("X has no features");
  }
  if (irows != xrows) {
    mexErrMsgTxt("X and I must have the same number of rows");
  }
  if (icols == 0) {
    mexErrMsgTxt("Number of tasks is 0");
  }
  if (irows2 != xrows) {
    mexErrMsgTxt("X and Itest must have the same number of rows");
  }
  if (icols2 == 0) {
    mexErrMsgTxt("Number of test tasks is 0");
  }
  if(!mxIsSparse(I_matlab)){
    mexErrMsgTxt("I must be sparse");
  }
  if(!mxIsSparse(Itest_matlab)){
    mexErrMsgTxt("Itest must be sparse");
  }
  // if(lrows != icols || lcols != icols){
  //   mexErrMsgTxt("The task matrix must be a double matrix ntask-by-ntask");
  // }

  const mxArray* niter_matlab = prhs[5];
  const mxArray* maxDepth_matlab = prhs[6];
  const mxArray* minNodes_matlab = prhs[7];
  const mxArray* minErr_matlab = prhs[8];
  const mxArray* fracFeat_matlab = prhs[9];
  const mxArray* shrink_matlab = prhs[10];
  const mxArray* filename_matlab = prhs[12];

  if (!isIntegerScalar(niter_matlab) || getDoubleScalar(niter_matlab) <= 0){
    mexErrMsgTxt("niter must be a positive integer");
  }
  if (!isIntegerScalar(maxDepth_matlab) || getDoubleScalar(maxDepth_matlab) < 0){
    mexErrMsgTxt("maxDepth must be a non-negative integer");
  }
  if (!isIntegerScalar(minNodes_matlab) || getDoubleScalar(minNodes_matlab) < 0){
    mexErrMsgTxt("minNodes must be a non-negative integer");
  }
  if (!isDoubleScalar(minErr_matlab) || getDoubleScalar(minErr_matlab) < 0){
    mexErrMsgTxt("minErr must be a non-negative real");
  }
  if (!isDoubleScalar(fracFeat_matlab) || getDoubleScalar(fracFeat_matlab) < 0 || getDoubleScalar(fracFeat_matlab) > 1){
    mexErrMsgTxt("fracFeat must be a real in [0 1]");
  }
  if (!isDoubleScalar(shrink_matlab) || getDoubleScalar(shrink_matlab) <= 0){
    mexErrMsgTxt("shrink must be a positive real");
  }
  if(!mxIsChar(filename_matlab)){
    mexErrMsgTxt("filename must be a string");
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
#ifdef TRAIN
  mexFunctionTrain(nlhs, plhs, nrhs, prhs);
#else
  mexFunctionGetModel(nlhs, plhs, nrhs, prhs);
#endif
}
