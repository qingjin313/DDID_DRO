/***************************************************************************************/
/*                                                                                     */
/*  Copyright 2018 by Anirudh Subramanyam, Chrysanthos Gounaris and Wolfram Wiesemann  */
/*                                                                                     */
/*  Licensed under the FreeBSD License (the "License").                                */
/*  You may not use this file except in compliance with the License.                   */
/*  You may obtain a copy of the License at                                            */
/*                                                                                     */
/*  https://www.freebsd.org/copyright/freebsd-license.html                             */
/*                                                                                     */
/***************************************************************************************/


#include "robustSolver.hpp"
#include "Constants.h"
#include <cassert>
#include <cmath>
#include <string>
#include <limits>
#include <iostream>
#include <fstream>
#include <numeric>
///*
#include <time.h>
#include <sys/time.h>
#include <iomanip>
static double get_wall_time(){
	struct timeval time;
	if (gettimeofday(&time,NULL)){
		//  Handle error
		return 0;
	}
	return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
//*/

#define CPX_TOL_INT  (1.E-5)
#define CPX_TOL_FEA  (1.E-5)
#define CPX_TOL_OPT  (1.E-5)
#define OUTPUTLEVEL 2
#define enterCallback(callback_type) {if(OUTPUTLEVEL >= 3) std::cout << " >>>--->>>  ENTER " << #callback_type << " Callback  <<<---<<< " << std::endl;}
#define exitCallback(callback_type)  {if(OUTPUTLEVEL >= 3) std::cout << " >>>--->>>  EXIT  " << #callback_type << " Callback  <<<---<<< " << std::endl; return(0);}
const double EPS_INFEASIBILITY_Q   = 1.E-4;
const double EPS_INFEASIBILITY_X   = 1.E-4;
static std::vector<double> Q_TEMP;
static std::vector<double> X_TEMP;
static int LABEL_TEMP;
static std::vector<std::pair<double, double> > ZT_VALUES, BOUND_VALUES;

// static global members
static int NUM_DUMMY_NODES = 0, TX = 0;
static double START_TS = 0;
volatile int TERMINATOR;
volatile int TERMINATOR_INNER;
static int CPXPUBLIC cutCB_solve_SRO_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p);
static int CPXPUBLIC cutCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p);
static int CPXPUBLIC incCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, double objval, double *x, int *isfeas_p, int *useraction_p);
static int CPXPUBLIC heurCB_solve_KAdaptability_cuttingPlane (CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, double *objval_p, double *x, int *checkfeas_p, int *useraction_p);
static int CPXPUBLIC branchCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int type, CPXDIM sos, int nodecnt, CPXDIM bdcnt, const CPXDIM *nodebeg, const CPXDIM *indices, const char *lu, const double *bd, const double *nodeest, int *useraction_p);
static int CPXPUBLIC nodeCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, CPXCNT *nodeindex_p, int *useraction_p);
static void CPXPUBLIC deletenodeCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, int wherefrom, void *cbhandle, CPXCNT seqnum, void *handle);

static int CPXPUBLIC cutCB_solve_LS_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p);

// Algorithmic control options
const bool GET_MAX_VIOL          = 1;
const int  SEPARATION_STRATEGY   = 4;
const bool COLLECT_RESULTS       = 1;
const bool BB_IMPLEMENT_LAZY_CON = 0;
const bool BNC_BRANCH_ALL_CONSTR = 1;
const bool BNC_DO_STRONG_BRANCH  = 0;
const bool SEPARATE_FROM_SAMPLES = 1;
const bool SEPARATE_ALTERNATE    = 0;
const bool SEPARATE_ALTERNATE_AVG= 0;
const int  BRANCHING_STRATEGY    = 1;
// 1 = always branch as per CPLEX, unless necessary to resort to K-adaptability
// 2 = alternate between CPLEX and K-adaptability branching
// 3 = branch as per K-adaptability until gap <= BNC_GAP_VALUE, then switch to strategy 1
// 4 = always branch as per K-adaptability whenever possible
// 5 = branch as per K-adaptability branching whenever depth modulo K == 0

const double BNC_GAP_VALUE = 10;

//MARK: Qing: whether to do the decision dependent or not
const bool DECISION_DEPENDENT = 1;
const bool SOLVE_RLP_FIRST = 0; // In inner loop, whether to solve the lp relaxation of the RMIP first
const bool USE_INFORM_CUT = 1;
const bool USE_STRE_FEAS_CUT = 0;

//-----------------------------------------------------------------------------------

#ifndef NDEBUG
#define MYERROR(_n) do {\
	std::cerr << __FILE__ << " -- Line " << __LINE__ << std::endl;\
	throw(_n);\
} while (0)
#else
#define MYERROR(_n) do {throw(_n);} while(0)
#endif

//-----------------------------------------------------------------------------------

#define MY_SIZE_X(_K) (pInfo->getNumFirstStage() + ((_K) * pInfo->getNumSecondStage()))

//-----------------------------------------------------------------------------------

#define MY_SIZE_Q (1 + pInfo->getNoOfUncertainParameters())

#define SUCCESS_STATUS ((pInfo->isContinuous() ? ()))

//-----------------------------------------------------------------------------------

static inline void setCPXoptions(CPXENVptr& env) {
	CPXXsetintparam(env, CPXPARAM_ScreenOutput,             CPX_OFF);
	CPXXsetintparam(env, CPXPARAM_Threads,                  NUM_THREADS);
	CPXXsetintparam(env, CPXPARAM_Parallel,                 CPX_PARALLEL_DETERMINISTIC);
	CPXXsetintparam(env, CPXPARAM_ClockType,                2);
	CPXXsetdblparam(env, CPXPARAM_TimeLimit,                TIME_LIMIT);
	CPXXsetdblparam(env, CPXPARAM_MIP_Limits_TreeMemory,    MEMORY_LIMIT);
	// CPXXsetdblparam(env, CPX_PARAM_EPGAP,                   0.0);
	// CPXXsetdblparam(env, CPX_PARAM_EPAGAP,                  1.E-5);
	// CPXXsetdblparam(env, CPX_PARAM_EPINT,                   CPX_TOL_INT);
	// CPXXsetdblparam(env, CPX_PARAM_EPRHS,                   CPX_TOL_FEA);
	// CPXXsetdblparam(env, CPX_PARAM_EPOPT,                   CPX_TOL_OPT);
}

//-----------------------------------------------------------------------------------

static inline void addVariable(CPXCENVptr env, CPXLPptr lp, const char xctype = 'C', const double lb = 0, const double ub = +CPX_INFBOUND, const double obj = 0, const char *cname = "") {
	if (CPXXnewcols(env, lp, 1, &obj, &lb, &ub, &xctype, &cname)) {
		MYERROR(EXCEPTION_CPXNEWCOLS);
	}
}

//-----------------------------------------------------------------------------------

static inline void addVariable(CPXCENVptr env, CPXLPptr lp, const char xctype = 'C', const double lb = 0, const double ub = +CPX_INFBOUND, const double obj = 0, const std::string cname = "") {
	addVariable(env, lp, xctype, lb, ub, obj, cname.c_str());
}

static inline void fixVariable(CPXCENVptr env, CPXLPptr lp, const int varIndex, const double val) {
	char lu = 'B';
	CPXXtightenbds(env, lp, 1, &varIndex, &lu, &val);
}

//-----------------------------------------------------------------------------------

static inline std::pair<double, bool> checkViol(CPXCENVptr env, void *cbdata, int wherefrom, const double rhs, const char sense, const std::vector<int>& cutind, const std::vector<double>& cutval) {

	// get node solution
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
	CPXDIM numcols = CPXXgetnumcols(env, lp);
	std::vector<double> x(numcols); CPXXgetcallbacknodex(env, cbdata, wherefrom, &x[0], 0, numcols - 1);

	// compute left-hand side
	double lhs = 0;
	for (unsigned int i = 0; i < cutind.size(); i++) {
		lhs += x[cutind[i]] * cutval[i];
	}

	// compute violation
	double viol = (lhs - rhs) * ((sense == 'G') ? -1.0 : +1.0);

	return std::make_pair(viol, ((std::trunc(1.E+6 * viol) / 1.E+6) >= -1.E-8));
}

//-----------------------------------------------------------------------------------
// GENERATE ALL P-TUPLES OF THE INDEX SET {startInd, startInd + 1,...,lastInd} WITH REPETITION
static inline std::vector<std::vector<int> > generatePTuples(int startInd, int lastInd, const int P) {
	assert(lastInd >= startInd);
	assert(P >= 1);

	// return value
	std::vector<std::vector<int> > PTuples;

	// Temporary
	std::vector<int> Pt(P, startInd);


	int k = 0; do {
		PTuples.emplace_back(Pt);
		k = P - 1;
		while (k >= 0) {
			Pt.at(k)++;
			if (Pt.at(k) == 1+lastInd) {
				Pt.at(k) = startInd;
				k--;
			}
			else {
				break;
			}
		}
	} while (k >= 0);

	// Must have K^P different tuples
	assert(static_cast<double>(PTuples.size()) == std::pow(lastInd - startInd + 1, P));

	return PTuples;
}

//-----------------------------------------------------------------------------------

static inline void write(std::ostream& out, std::string N, unsigned int K, std::string seed, std::string status, double final_objval, double total_solution_time, double final_gap) {
	out << "  N  " << "  K  " << " Seed " << "  Status  " << " Obj Value " << " Time (sec) " << " Gap (%) \n";
	out << " --- " << " --- " << " ---- " << " -------- " << " --------- " << " ---------- " << " ------- \n";
	out << std::setw(4) << N;
	out << std::setw(4) << K;
	out << "   ";
	out << std::setw(4) << std::fixed << seed;
	out << "  ";
	out << std::setw(8) << status;
	out << "  ";
	out << std::setw(8) << std::setprecision(4) << std::fixed << final_objval;
	out << "    ";
	out << std::setw(7) << std::setprecision(2) << std::fixed << total_solution_time;
	out << "     ";
	out << std::setw(5) << std::setprecision(2) << std::fixed << final_gap;
	out << "  \n\n";
}

static inline void getIndices(std::vector<int>& numBP, std::vector<std::vector<int>>& indices){
    if(!numBP.size())
        return;
    else{
        assert(numBP[0] >= 2);
        std::vector<std::vector<int>> newIndices;
        for(int i = 0; i < numBP[0]; i++){
            if(!indices.size()){
                std::vector<int> newIndex = {i};
                newIndices.emplace_back(newIndex);
            }
            for(int j = 0; j < int(indices.size()); j++){
                std::vector<int> copyIndex = indices.at(j);
                copyIndex.emplace_back(i);
                newIndices.emplace_back(copyIndex);
            }
        }
        numBP.erase(numBP.begin());
        indices = newIndices;
        return getIndices(numBP, indices);
    }
}
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

KAdaptableSolver::KAdaptableSolver(const KAdaptableInfo& pInfoData) {
	pInfo = pInfoData.clone();
}

//-----------------------------------------------------------------------------------

KAdaptableSolver::~KAdaptableSolver() {
	if (pInfo) {
		delete pInfo;
		pInfo = NULL;
	}
}

//-----------------------------------------------------------------------------------

KAdaptableSolver::KAdaptableSolver (const KAdaptableSolver& S) {
	if (S.pInfo) {
		pInfo = S.pInfo->clone();
	}
	xsol = S.xsol;
}

//-----------------------------------------------------------------------------------

KAdaptableSolver& KAdaptableSolver::operator=(const KAdaptableSolver& S) {
	if (this == &S) {
		return *this;
	}
	if (pInfo) {
		delete pInfo;
		pInfo = NULL;
	}
	if (S.pInfo) {
		pInfo = S.pInfo->clone();
	}
	xsol = S.xsol;
	return *this;
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::setInfo(const KAdaptableInfo& pInfoData) {
	if (pInfo) {
		delete pInfo;
		pInfo = NULL;
	}
	pInfo = pInfoData.clone();
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::reset(const KAdaptableInfo& pInfoData, const unsigned int K) {
	setInfo(pInfoData);
	// if (K >= 1) pInfo->resize(K);
	xsol.clear();
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

void KAdaptableSolver::setX(const std::vector<double>& x, const unsigned int K) {
	std::vector<double> q;
	if (feasible_KAdaptability(x, K, q)) {
		if (xsol.empty()) {
			xsol = x;
		}
		else if (x[0] < xsol[0]) {
			xsol = x;
		}
	}

	return;
}

//-----------------------------------------------------------------------------------

unsigned int KAdaptableSolver::getNumPolicies(const std::vector<double>& x) const {
	const int num_second_stage_cols = (int)x.size() - pInfo->getNumFirstStage();
	if (num_second_stage_cols < 0) return 0;
	if (num_second_stage_cols % pInfo->getNumSecondStage()) return 0;
	return static_cast<unsigned int>(num_second_stage_cols / pInfo->getNumSecondStage());
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::resizeX(std::vector<double>& x, const unsigned int K) const {
	assert(pInfo);

	const unsigned int Kx = getNumPolicies(x);

	// vectors must be of appropriate size
	if (Kx < 1) MYERROR(EXCEPTION_X);
	if (x.size() != MY_SIZE_X(Kx)) MYERROR(EXCEPTION_X);

	// remove extra policies
	if (Kx > K) {
		x.resize(MY_SIZE_X(K));
	}

	// duplicate the last policy
	if (Kx < K) {
		const auto x2 = x.begin() + pInfo->getNumFirstStage() + ((Kx - 1) * pInfo->getNumSecondStage());
		const std::vector<double> xk(x2, x2 + pInfo->getNumSecondStage());
		for (unsigned int k = Kx; k < K; k++) {
			x.insert(x.end(), xk.begin(), xk.end());
		}
	}

	assert(x.size() == MY_SIZE_X(K));
	assert(getNumPolicies(x) == K);
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::removeXPolicy(std::vector<double>& x, const unsigned int K, const unsigned int k) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (K < 1 || k >= K) MYERROR(EXCEPTION_X);
	if (x.size() != MY_SIZE_X(K)) MYERROR(EXCEPTION_X);

	const auto x2 = x.begin() + pInfo->getNumFirstStage() + (k * pInfo->getNumSecondStage());
	x.erase(x2, x2 + pInfo->getNumSecondStage());

	assert(x.size() == MY_SIZE_X(K - 1));
}

//-----------------------------------------------------------------------------------

const std::vector<double> KAdaptableSolver::getXPolicy(const std::vector<double>& x, const unsigned int K, const unsigned int k) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (K < 1 || k >= K) MYERROR(EXCEPTION_K);
	if (x.size() != MY_SIZE_X(K)) MYERROR(EXCEPTION_X);

	const auto x2 = x.begin() + pInfo->getNumFirstStage();

	// 1st-stage solution of policy # k
	std::vector<double> xk(x.begin(), x2);

	// 2nd-stage solution of policy # k
	xk.insert(xk.end(), x2 + (k * pInfo->getNumSecondStage()), x2 + ((k + 1) * pInfo->getNumSecondStage()));

	assert((int)xk.size() == MY_SIZE_X(1));

	return xk;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

void KAdaptableSolver::updateX(CPXCENVptr env, CPXLPptr lp) const {
	assert(pInfo);

	// 1st-stage variables and constraints
	auto& X   = pInfo->getVarsX();
	auto& C_X = pInfo->getConstraintsX();

	// define variables
	for (int i = 0; i < X.getTotalVarSize(); i++) if (!X.isUndefVar(i)) {
		std::string cname;
		int ind1, ind2, ind3, ind4, ind5;
		X.getVarInfo(i, cname, ind1, ind2, ind3, ind4, ind5);
		if (ind1 > -1) {
			cname += "(" + std::to_string(ind1);
			if (ind2 > -1) cname += "," + std::to_string(ind2);
			if (ind3 > -1) cname += "," + std::to_string(ind3);
			if (ind4 > -1) cname += "," + std::to_string(ind4);
			if (ind5 > -1) cname += "," + std::to_string(ind5);
			cname += ")";
		}
		addVariable(env, lp, X.getVarColType(i), X.getVarLB(i), X.getVarUB(i), X.getVarObjCoeff(i), cname);
	}

	// define constraints
	for (const auto& con: C_X) {
		con.addToCplex(env, lp);
	}
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::updateXQ(CPXCENVptr env, CPXLPptr lp, const std::vector<double>& q, const bool lazy) const {
	assert(pInfo);

	// reformulate constraints?
	const bool reformulate = q.empty();
	if (!reformulate) if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	// 1st-stage constraints involving uncertain parameters
	auto& C_XQ = pInfo->getConstraintsXQ();

	// define constraints
	if (lazy) {
		CPXDIM rcnt = 0;
		CPXNNZ nzcnt = 0;
		std::vector<double> rhs;
		std::vector<char> sense;
		std::vector<CPXNNZ> rmatbeg;
		std::vector<CPXDIM> rmatind;
		std::vector<double> rmatval;
		getXQ_fixedQ(q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);

		CPXXaddlazyconstraints(env, lp, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], NULL);
	}
	else for (const auto& con: C_XQ) {
		con.addToCplex(env, lp, &pInfo->getUncSet(), reformulate, q);
	}
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::updateY(CPXCENVptr env, CPXLPptr lp, const unsigned int k) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (k >= pInfo->getConstraintsXY().size()) MYERROR(EXCEPTION_K);

	// 1st-stage variables and constraints
	auto& Y    = pInfo->getVarsY();
	auto& C_XY = pInfo->getConstraintsXY()[k];

	// define variables
	for (int i = 0; i < Y.getTotalVarSize(); i++) if (!Y.isUndefVar(i)) {
		std::string cname;
		int ind1, ind2, ind3, ind4, ind5;
		Y.getVarInfo(i, cname, ind1, ind2, ind3, ind4, ind5);
		cname += "_" + std::to_string(k);
		if (ind1 > -1) {
			cname += "(" + std::to_string(ind1);
			if (ind2 > -1) cname += "," + std::to_string(ind2);
			if (ind3 > -1) cname += "," + std::to_string(ind3);
			if (ind4 > -1) cname += "," + std::to_string(ind4);
			if (ind5 > -1) cname += "," + std::to_string(ind5);
			cname += ")";
		}
		addVariable(env, lp, Y.getVarColType(i), Y.getVarLB(i), Y.getVarUB(i), Y.getVarObjCoeff(i), cname);
	}

	// define constraints
	for (const auto& con: C_XY) {
		con.addToCplex(env, lp);
	}
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::updateYQ(CPXCENVptr env, CPXLPptr lp, const unsigned int k, const std::vector<double>& q, const bool lazy) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (k >= pInfo->getConstraintsXYQ().size()) MYERROR(EXCEPTION_K);

	// reformulate constraints?
	const bool reformulate = q.empty();
	if (!reformulate) if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	// 1st-stage constraints involving uncertain parameters
	auto& C_XYQ = pInfo->getConstraintsXYQ()[k];

	// define constraints
	if (lazy) {
		CPXDIM rcnt = 0;
		CPXNNZ nzcnt = 0;
		std::vector<double> rhs;
		std::vector<char> sense;
		std::vector<CPXNNZ> rmatbeg;
		std::vector<CPXDIM> rmatind;
		std::vector<double> rmatval;
		getYQ_fixedQ(k, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);

		CPXXaddlazyconstraints(env, lp, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], NULL);
	}
	else for (const auto& con: C_XYQ) {
		con.addToCplex(env, lp, &pInfo->getUncSet(), reformulate, q);
	}
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::getXQ_fixedQ(const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	// initialize members
	auto& C_XQ = pInfo->getConstraintsXQ();
	rcnt = 0;
	nzcnt = 0;
	rhs.clear();
	sense.clear();
	rmatbeg.clear();
	rmatind.clear();
	rmatval.clear();

	// Statically add constraints involving uncertain parameters
	for (const auto& con : C_XQ) {
		
		rcnt ++;
		CPXNNZ nzcnt_p = 0;
		rhs.emplace_back(0);
		sense.emplace_back('L');
		rmatbeg.emplace_back(rmatind.size());
		std::vector<CPXDIM> indices;
		std::vector<double> values;

		con.getDeterministicConstraint(q, nzcnt_p, rhs.back(), sense.back(), indices, values);

		nzcnt += nzcnt_p;
		rmatind.insert(rmatind.end(), indices.begin(), indices.end());
		rmatval.insert(rmatval.end(), values.begin(), values.end());
	}

	assert(rhs.size() == rmatbeg.size());
	assert(rhs.size() == sense.size());
	assert((CPXDIM)rhs.size() == rcnt);
	assert(rmatind.size() == rmatval.size());
	assert((CPXNNZ)rmatind.size() == nzcnt);
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::getXQ_fixedX(const std::vector<double>& x, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)x.size() < MY_SIZE_X(1)) MYERROR(EXCEPTION_X);

	// initialize members
	auto& C_XQ = pInfo->getConstraintsXQ();
	rcnt = 0;
	nzcnt = 0;
	rhs.clear();
	sense.clear();
	rmatbeg.clear();
	rmatind.clear();
	rmatval.clear();

	// Statically add constraints involving uncertain parameters
	for (const auto& con : C_XQ) {
		
		rcnt ++;
		CPXNNZ nzcnt_p = 0;
		rhs.emplace_back(0);
		sense.emplace_back('L');
		rmatbeg.emplace_back(rmatind.size());
		std::vector<CPXDIM> indices;
		std::vector<double> values;

		con.getStochasticConstraint(x, nzcnt_p, rhs.back(), sense.back(), indices, values);

		nzcnt += nzcnt_p;
		rmatind.insert(rmatind.end(), indices.begin(), indices.end());
		rmatval.insert(rmatval.end(), values.begin(), values.end());
	}

	assert(rhs.size() == rmatbeg.size());
	assert(rhs.size() == sense.size());
	assert((CPXDIM)rhs.size() == rcnt);
	assert(rmatind.size() == rmatval.size());
	assert((CPXNNZ)rmatind.size() == nzcnt);
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::getYQ_fixedQ(const unsigned int k, const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (k >= pInfo->getConstraintsXYQ().size()) MYERROR(EXCEPTION_K);
	if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	// initialize members
	auto& C_XYQ = pInfo->getConstraintsXYQ()[k];
	rcnt = 0;
	nzcnt = 0;
	rhs.clear();
	sense.clear();
	rmatbeg.clear();
	rmatind.clear();
	rmatval.clear();

	// Statically add constraints involving uncertain parameters
	for (const auto& con : C_XYQ) {
		
		rcnt ++;
		CPXNNZ nzcnt_p = 0;
		rhs.emplace_back(0);
		sense.emplace_back('L');
		rmatbeg.emplace_back(rmatind.size());
		std::vector<CPXDIM> indices;
		std::vector<double> values;

		con.getDeterministicConstraint(q, nzcnt_p, rhs.back(), sense.back(), indices, values);

		nzcnt += nzcnt_p;
		rmatind.insert(rmatind.end(), indices.begin(), indices.end());
		rmatval.insert(rmatval.end(), values.begin(), values.end());
	}

	assert(rhs.size() == rmatbeg.size());
	assert(rhs.size() == sense.size());
	assert((CPXDIM)rhs.size() == rcnt);
	assert(rmatind.size() == rmatval.size());
	assert((CPXNNZ)rmatind.size() == nzcnt);
}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::getSingleYQ_fixedQ(const unsigned int k, const uint labelCstr, const std::vector<double>& q, CPXNNZ& nzcnt, double& rhs, char& sense, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const {
    assert(pInfo);

    // vectors must be of appropriate size
    if (k >= pInfo->getConstraintsXYQ().size()) MYERROR(EXCEPTION_K);
    if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);
    if (labelCstr >= pInfo->getConstraintsXYQ()[k].size()) MYERROR(EXCEPTION_C);
    
    // get constraint
    auto con = pInfo->getConstraintsXYQ()[k][labelCstr];
    // initialize members
    nzcnt = 0;
    rmatind.clear();
    rmatval.clear();

    con.getDeterministicConstraint(q, nzcnt, rhs, sense, rmatind, rmatval);

}

//-----------------------------------------------------------------------------------

void KAdaptableSolver::getYQ_fixedX(const unsigned int k, const std::vector<double>& x, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if (k >= pInfo->getConstraintsXYQ().size()) MYERROR(EXCEPTION_K);
	if (x.size() < MY_SIZE_X(k+1)) MYERROR(EXCEPTION_X);

	// initialize members
	auto& C_XYQ = pInfo->getConstraintsXYQ()[k];
	rcnt = 0;
	nzcnt = 0;
	rhs.clear();
	sense.clear();
	rmatbeg.clear();
	rmatind.clear();
	rmatval.clear();

	// Statically add constraints involving uncertain parameters
	for (const auto& con : C_XYQ) {
		
		rcnt ++;
		CPXNNZ nzcnt_p = 0;
		rhs.emplace_back(0);
		sense.emplace_back('L');
		rmatbeg.emplace_back(rmatind.size());
		std::vector<CPXDIM> indices;
		std::vector<double> values;

		con.getStochasticConstraint(x, nzcnt_p, rhs.back(), sense.back(), indices, values);

		nzcnt += nzcnt_p;
		rmatind.insert(rmatind.end(), indices.begin(), indices.end());
		rmatval.insert(rmatval.end(), values.begin(), values.end());
	}

	assert(rhs.size() == rmatbeg.size());
	assert(rhs.size() == sense.size());
	assert((CPXDIM)rhs.size() == rcnt);
	assert(rmatind.size() == rmatval.size());
	assert((CPXNNZ)rmatind.size() == nzcnt);
}

//MARK: Qing: add getRobustYQ_fixedQ for inner maximization problem
//-----------------------------------------------------------------------------------
void KAdaptableSolver::getRobustYQ_fixedQ(const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval)
{
    assert(pInfo);

    // vectors must be of appropriate size
    if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);
    
    pInfo->setXiBar(q);
    
//    // initialize members
//    auto& C_XYQ = pInfo->getConstraintsXYQ()[0];
    rcnt = 0;
    nzcnt = 0;
    rhs.clear();
    sense.clear();
    rmatbeg.clear();
    rmatind.clear();
    rmatval.clear();
//
//    // Statically add constraints involving uncertain parameters
//    for (const auto& con : C_XYQ) {
//        if (con.isEmpty()) {
//            if(CONSTRAINT_EXPRESSION_OUTPUTLEVEL) std::cerr << " Warning: Attempting to robustify empty constraint. Ignoring. \n";
//            continue;
//        }
//
//        // check if it's deterministic lhs and has uncertainty in rhs
//
//        /* Do the uncertain parameters appear as coefficients of decision variables? */
//        const bool qTermsInProduct = con.existBilinearTerms();
//        /* Do the uncertain parameters appear independently by themselves? */
//        const bool qTermsConst     = con.existConstQTerms();
//
//        // If deterministic lhs and uncertain rhs, only get maximization of the rhs
//        if (!qTermsInProduct) if (qTermsConst){
//
//            rcnt ++;
//            CPXNNZ nzcnt_p = 0;
//            rhs.emplace_back(0);
//            sense.emplace_back('L');
//            rmatbeg.emplace_back(rmatind.size());
//            std::vector<CPXDIM> indices;
//            std::vector<double> values;
//
//            con.getRobustConstraint(&pInfo->getUncSet(), nzcnt_p, rhs.back(), sense.back(), indices, values);
//
//            nzcnt += nzcnt_p;
//            rmatind.insert(rmatind.end(), indices.begin(), indices.end());
//            rmatval.insert(rmatval.end(), values.begin(), values.end());
//
//            continue;
//        }
//
//        // If has uncertain product term, then use cutting plane algorithm
//        // To be implemented!
//        // --------------------------------//
//        assert(qTermsInProduct);
        int status = solve_YQRobust_cuttingplane(q);
        if(!status){
            for(auto qtemp : inner_samples){
                CPXDIM rcnt_p = 0;
                CPXNNZ nzcnt_p = 0;
                std::vector<double> trueRhs;
                std::vector<char> trueSense;
                std::vector<CPXNNZ> begins;
                std::vector<CPXDIM> indices;
                std::vector<double> values;
                
                getYQ_fixedQ(0, qtemp, rcnt_p, nzcnt_p, trueRhs, trueSense, begins, indices, values);
                
                nzcnt += nzcnt_p;
                rcnt += rcnt_p;
                rhs.insert(rhs.end(), trueRhs.begin(), trueRhs.end());
                sense.insert(sense.end(), trueSense.begin(), trueSense.end());
                
                std::vector<int> pos(begins.size(), rmatind.size());
                std::transform(begins.begin(), begins.end(), pos.begin(), begins.begin(), std::plus<int>());
                rmatbeg.insert(rmatbeg.end(), begins.begin(), begins.end());
                
                rmatind.insert(rmatind.end(), indices.begin(), indices.end());
                rmatval.insert(rmatval.end(), values.begin(), values.end());
            }
        }
        else{
            std::cout << "Something wrong happens in the cutting plane loop! The status is: " << status << std::endl;
            assert(0);
        }
//    }

    assert(rhs.size() == rmatbeg.size());
    assert(rhs.size() == sense.size());
    assert((CPXDIM)rhs.size() == rcnt);
    assert(rmatind.size() == rmatval.size());
    assert((CPXNNZ)rmatind.size() == nzcnt);
    
    pInfo->resetXiBar();
    return;
}

void KAdaptableSolver::robustifyW(CPXENVptr env, CPXLPptr lp) const
{
    auto& X   = pInfo->getVarsX();
    auto& C_XQ = pInfo->getConstraintsXQ();
    
    int beginW(X.getVarLinIndex("w", 0));
    int endW( beginW + X.getDefVarTypeSize("w") );
    
    std::vector<int> indices;
    for (const auto& con: C_XQ){
        indices = con.getVarIndices();
        unsigned long i = 0;
        for(; i < indices.size(); i++){
            if(indices[i] >= endW || indices[i] < beginW)
                break;
        }
        if(i >= indices.size())
            con.addToCplex(env, lp, &pInfo->getUncSet(), true);
    }
}

void KAdaptableSolver::feasibleW(CPXENVptr env, CPXLPptr lp) const
{
    // step 1: create inner environment for finding feasible region of w.
    int status;
    CPXENVptr env_;
    CPXLPptr lp_;

    env_ = NULL;
    lp_  = NULL;

    env_ = CPXXopenCPLEX (&status);
    lp_  = CPXXcreateprob(env_, &status, "FEASIBILITY_W");
    
    // step 2: add variables into the environment
    auto& X   = pInfo->getVarsX();
    for (int i = 0; i < X.getTotalVarSize(); i++) if (!X.isUndefVar(i)) {
        std::string cname;
        int ind1, ind2, ind3, ind4, ind5;
        X.getVarInfo(i, cname, ind1, ind2, ind3, ind4, ind5);
        if (ind1 > -1) {
            cname += "(" + std::to_string(ind1);
            if (ind2 > -1) cname += "," + std::to_string(ind2);
            if (ind3 > -1) cname += "," + std::to_string(ind3);
            if (ind4 > -1) cname += "," + std::to_string(ind4);
            if (ind5 > -1) cname += "," + std::to_string(ind5);
            cname += ")";
        }
        addVariable(env_, lp_, X.getVarColType(i), X.getVarLB(i), X.getVarUB(i), 0.0, cname);
    }
    
    auto& Y    = pInfo->getVarsY();
    for (int i = 0; i < Y.getTotalVarSize(); i++) if (!Y.isUndefVar(i)) {
        std::string cname;
        int ind1, ind2, ind3, ind4, ind5;
        Y.getVarInfo(i, cname, ind1, ind2, ind3, ind4, ind5);
        if (ind1 > -1) {
            cname += "(" + std::to_string(ind1);
            if (ind2 > -1) cname += "," + std::to_string(ind2);
            if (ind3 > -1) cname += "," + std::to_string(ind3);
            if (ind4 > -1) cname += "," + std::to_string(ind4);
            if (ind5 > -1) cname += "," + std::to_string(ind5);
            cname += ")";
        }
        addVariable(env_, lp_, Y.getVarColType(i), Y.getVarLB(i), Y.getVarUB(i), 0.0, cname);
    }
    
    // step 3: iterate through all constraint and get the projection to the w space
    std::vector<ConstraintExpression> cstrs;
    auto& C_X = pInfo->getConstraintsX();
    cstrs.insert(cstrs.end(), C_X.begin(), C_X.end());
    auto& C_Y = pInfo->getConstraintsXY();
    assert(C_Y.size() == 1);
    cstrs.insert(cstrs.end(), C_Y[0].begin(), C_Y[0].end());
    auto& C_XQ = pInfo->getConstraintsXQ();
    cstrs.insert(cstrs.end(), C_XQ.begin(), C_XQ.end());
//    auto& C_XYQ = pInfo->getConstraintsXYQ();
//    assert(C_XYQ.size() == 1);
//    cstrs.insert(cstrs.end(), C_XYQ[0].begin()+1, C_XYQ[0].end());
    
    std::vector<ConstraintExpression> wCstrs;
    
    int beginW(X.getVarLinIndex("w", 0));
    int endW( beginW + X.getDefVarTypeSize("w") );
    // iterate thorugh the constraint for finding deterministic constraints for variable other than w
    std::vector<int> indices;
    std::vector<double> coefficients;
    for (const auto& con: cstrs){
        if(!con.existBilinearTerms()){
            indices = con.getVarIndices();
            unsigned long i = 0;
            for(; i < indices.size(); i++){
                if(indices[i] < endW && indices[i] >= beginW)
                    break;
            }
            if(i >= indices.size()  && !con.existConstQTerms() )
                con.addToCplex(env_, lp_);
            else
                wCstrs.emplace_back(con);
        }
    }
    
    std::vector<int> indToDelete;
    double objval;
    double objCoef(0.0);
    for (const auto& con: wCstrs){
        indices = con.getVarIndices();
        coefficients = con.getVarCoeffs();

        unsigned long i = 0;
        indToDelete.clear();
        for(; i < indices.size(); i++){
            if(indices[i] >= endW || indices[i] < beginW){
                CPXXchgobj(env_, lp_, 1, &indices[i], &coefficients[i]);
                indToDelete.emplace_back(indices[i]);
            }
            else
                CPXXchgobj(env_, lp_, 1, &indices[i], &objCoef);
        }
        if(con.getSense() == 'G')
            CPXXchgobjsen(env_, lp_, CPX_MAX);
        else
            CPXXchgobjsen(env_, lp_, CPX_MIN);
        
        status = CPXXmipopt(env_, lp_);
        if(!status){
            status = CPXXgetstat(env_, lp_);
            if(status == CPXMIP_OPTIMAL){
                ConstraintExpression newWCstr(con.projectToW(indToDelete, beginW));
                CPXXgetobjval(env_, lp_, &objval);
                newWCstr.addConst(objval);
                assert(!newWCstr.addToCplex(env, lp));
            }
            else if(status != CPXMIP_UNBOUNDED)
                std::cout << "Something wrong when finding the feasible region for w, status code: " << status << "\n";
        }
    }
    
    CPXXfreeprob(env_, &lp_);
    CPXXcloseCPLEX (&env_);
    
    return;
}
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

double KAdaptableSolver::getLowerBound() const {
	assert(pInfo);

	
	double LB, LB1;
	std::vector<double> xtemp;
	
	int status;
	CPXENVptr env;
	CPXLPptr lp;
	
	std::vector<CPXDIM> indices;
	std::vector<char> xctype;



	// 1> You can always use any deterministic solution as a lower bound
	// 2> In case of objective uncertainty, you can do better


	// 1> Get deterministic objective value for nominal q
	if (solve_DET(pInfo->getNominal(), xtemp)) {
		// did not terminate successfully
		LB = -std::numeric_limits<double>::max();
	}
	else {
		LB = xtemp[0];
	}


	// 2> Objective uncertainty only
	// min_x max_q min_y
	if (pInfo->hasObjectiveUncOnly()) {
		// (1) Make y continuous (lower bound)
		// (2) Flip min_y with max_q
		//     This is valid because of equality in max-min inequality
		//     See, for e.g., the minimax theorem
		// (3) You get classical (static) RO with x as before and y continuous
		

		// initialize CPLEX objects
		env = NULL;
		lp  = NULL;
		env = CPXXopenCPLEX (&status);
		lp  = CPXXcreateprob(env, &status, "LowerBoundingProblem");

		// set options
		setCPXoptions(env);
		CPXXchgprobtype(env, lp, (pInfo->isContinuous() ? CPXPROB_LP : CPXPROB_MILP));
		CPXXchgobjsen(env, lp, CPX_MIN);

		// define variables and constraints
		updateX(env, lp);
		updateXQ(env, lp);
		updateY(env, lp, 0);
		updateYQ(env, lp, 0);


		// change y variables to continuous
		indices.resize(pInfo->getNumSecondStage());
		xctype.resize(indices.size(), CPX_CONTINUOUS);
		std::iota(indices.begin(), indices.end(), static_cast<CPXDIM>(pInfo->getNumFirstStage()));
		CPXXchgctype(env, lp, indices.size(), &indices[0], &xctype[0]);


		// solve problem
		status = CPXXmipopt(env, lp);
		if (!status) {
			// Update lower bound
			CPXXgetbestobjval(env, lp, &LB1);
			if (LB < LB1) {
				LB = LB1;
			}
		}
		else {
			MYERROR(status);
		}
		
		// Free memory
		CPXXfreeprob(env, &lp);
		CPXXcloseCPLEX (&env);
	}


	return LB;
}

//-----------------------------------------------------------------------------------

double KAdaptableSolver::getLowerBound_2(std::vector<double>& q) const {
	assert(pInfo->hasObjectiveUncOnly());
	assert(pInfo->isSecondStageOnly());
	assert(pInfo->getConstraintsXYQ()[0].size() == 1);

	int status;
	CPXENVptr env = NULL;
	CPXLPptr  lp  = NULL;

	env = pInfo->getUncSet().getENVObject();
	lp  = pInfo->getUncSet().getLPObject(&status);

	int ind = 0; double coef = 1; char lb = 'L'; char ub = 'U'; double lbVal = -CPX_INFBOUND; double ubVal = +CPX_INFBOUND;


	CPXXchgobjsen(env, lp, CPX_MAX);
	CPXXchgprobtype(env, lp, CPXPROB_LP);
	CPXXchgobj(env, lp, 1, &ind, &coef);
	for (int l = 1; l <= pInfo->getUncSet().getNoOfUncertainParameters(); ++l) {
		coef = 0;
		CPXXchgobj(env, lp, 1, &l, &coef);
	}
	CPXXchgbds(env, lp, 1, &ind, &lb, &lbVal);
	CPXXchgbds(env, lp, 1, &ind, &ub, &ubVal);

	UncertaintySet MyU;

	// 1st-stage variables and constraints
	auto& Y    = pInfo->getVarsY();
	auto& C_XY = pInfo->getConstraintsXY()[0];

	// define variables
	for (int i = 0; i < Y.getTotalVarSize(); i++) if (!Y.isUndefVar(i)) {
		MyU.addParam(Y.getVarLB(i), Y.getVarLB(i), Y.getVarUB(i));
	}


	// define constraints
	for (const auto& con: C_XY) {
		std::vector<double> qtemp;
		CPXNNZ nzcnt;
		double rhs;
		char sense;
		std::vector<CPXDIM> rmatind;
		std::vector<double> rmatval;
		con.getDeterministicConstraint(qtemp, nzcnt, rhs, sense, rmatind, rmatval);
		std::vector<std::pair<int, double> > facet;
		for (CPXNNZ i = 0; i < nzcnt; i++) {
			facet.emplace_back(rmatind[i], rmatval[i]);
		}
		MyU.addFacet(facet, sense, rhs);
	}

	// define constraint to be dualized
	const ConstraintExpression myexp = pInfo->getConstraintsXYQ()[0][0].flip0();
	myexp.addToCplex(env, lp, &MyU, true);

	CPXXchgobjsen(env, lp, CPX_MAX);
	CPXXchgprobtype(env, lp, CPXPROB_LP);

	// solve problem
	status = CPXXlpopt(env, lp);
	if (status) MYERROR(status);

	// get status
	assert(CPXXgetstat(env, lp) == CPX_STAT_OPTIMAL);

	// Get primal solution vector
	q.resize(1 + pInfo->getUncSet().getNoOfUncertainParameters(), 100);
	CPXXgetx(env, lp, &q[0], 0, q.size() - 1);

	// Free memory
	if (lp) CPXXfreeprob(env, &lp);

	

	return q[0];
}

//-----------------------------------------------------------------------------------

double KAdaptableSolver::getWorstCase(const std::vector<double>& x, const unsigned int K, std::vector<double>& q) const {
	assert(pInfo);

	// must be of appropriate size
	if (K < 1 || (int)K > pInfo->getNumPolicies()) MYERROR(EXCEPTION_K);
	if (!feasible_DET_K(x, K, X_TEMP)) MYERROR(EXCEPTION_X);

	// if infeasible, then any violating scenario is okay
	if (!feasible_KAdaptability(x, K, q)) return +std::numeric_limits<double>::max();


	CPXDIM rcnt = 0;
	CPXNNZ nzcnt = 0;
	int objective_idx = -1;
	std::vector<double> rhs;
	std::vector<char> sense;
	std::vector<CPXNNZ> rmatbeg;
	std::vector<CPXDIM> rmatind;
	std::vector<double> rmatval;


	/////////////////////////////////////////////////////////////////////////
	// NOTE: I'M ASSUMING THAT THE ORIGINAL PROBLEM IS ALWAYS REFORMULATED //
	//       USING AN EPIGRAPHICAL VARIABLE FOR THE OBJECTIVE FUNCTION     //
	/////////////////////////////////////////////////////////////////////////
	getYQ_fixedQ(0, pInfo->getNominal(), rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
	for (CPXDIM j = 0; j < rcnt; j++) {
		auto begin = rmatind.begin() + rmatbeg[j];
		auto end = rmatind.begin() + ((j+1 == rcnt) ? nzcnt : rmatbeg[j+1]);
		if (std::find(begin, end, 0) != end) {
			objective_idx = j;
			break;
		}
	}



	// 1> In case of pure objective uncertainty, solve separation problem
	// 2> In case of pure constraint uncertainty, get any scenario and return deterministic objective
	// 3> In case of both objective and constraint uncertainty, need more work


	// 1> Separation problem already solved
	if (pInfo->hasObjectiveUncOnly()) return x[0] + q[0];


	// 2> Any scenario is okay
	if (objective_idx < 0) return x[0];


	// 3> Solve (approximately) sup_q inf_k [objective(q, k) : such that policy k is feasible for q]
	int status;
	CPXENVptr env = NULL;
	CPXLPptr  lp  = NULL;
	char b;
	double bd;
	int index;
	auto& U = pInfo->getUncSet();
	const auto PTuples = generatePTuples(0, 1, K);
	std::vector<double> max_q(1, -std::numeric_limits<double>::max());
	bool single_constraint = (rcnt == 2);


	// tuple[k] denotes whether policy k is feasible in the current partition
	for (const auto& tuple : PTuples) {
		// ignore the case where all policies are infeasible
		int sum = 0; for (auto t : tuple) sum += t;
		if (sum < 1) continue;

		// get solver objects from uncertainty set
		env = U.getENVObject();
		lp  = U.getLPObject(&status);

		// convert problem to maximization MILP with objective = [tau]
		CPXXchgobjsen(env, lp, CPX_MAX);
		CPXXchgprobtype(env, lp, single_constraint ? CPXPROB_LP : CPXPROB_MILP);
		for (index = 0; index <= U.getNoOfUncertainParameters(); ++index) {
			double coef = static_cast<double>((index == 0));
			CPXXchgobj(env, lp, 1, &index, &coef);
		}

		// Change bounds on objective function variable to make it (-Inf, +Inf)
		index = 0; b = 'L'; bd = -CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = 0; b = 'U'; bd = +CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = CPXXgetnumcols(env, lp) - 1;

		// Add constraints for all policies
		for (unsigned int k = 0; k < K; ++k) {

			// get all uncertain constraints in policy k for fixed x
			getYQ_fixedX(k, x, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);

			// only for infeasible policies: sum(j, z_jk) = 1
			ConstraintExpression zConstraint("z(" + std::to_string(k) + ")");
			zConstraint.sign('E');
			zConstraint.RHS(1);

			for (CPXDIM j = 0; j < rcnt; j++) {
				assert(sense[j] == 'G' || sense[j] == 'L');
				CPXNNZ ilim = (j+1 == rcnt) ? nzcnt : rmatbeg[j+1];
				double linrhs = rhs[j];
				char linsense = sense[j];
				std::vector<CPXDIM> linind(rmatind.begin() + rmatbeg[j], rmatind.begin() + ilim);
				std::vector<double> linval(rmatval.begin() + rmatbeg[j], rmatval.begin() + ilim);
				if (linind.empty()) continue;

				// Policy k must be feasible
				if (tuple[k]) {
					// IF objective function, maximize violation:
					// -tau + [violation expression of constraint j in terms of q for fixed x in policy k] >= 0
					// ELSE
					// add as a regular constraint
					if (j == objective_idx) {
						linsense = (sense[j] == 'G') ? 'L' : 'G';
						linind.emplace_back(0);
						linval.emplace_back((sense[j] == 'G') ? 1 : -1);
					}
					CPXXaddrows(env, lp, 0, 1, linind.size(), &linrhs, &linsense, &rmatbeg[0], &linind[0], &linval[0], NULL, NULL);
				}

				// Policy k must be infeasible
				else {
					if (j == objective_idx) continue;

					// z_jk indicates whether constraint j is violated
					// z_jk => [reversed expression of constraint j in terms of q for fixed x in policy k]
					linrhs += ((sense[j] == 'G') ? -1.0 : 1.0)*EPS_INFEASIBILITY_X;
					linsense = (sense[j] == 'G') ? 'L' : 'G';

					if (single_constraint) {
						CPXXaddrows(env, lp, 0, 1, linind.size(), &linrhs, &linsense, &rmatbeg[0], &linind[0], &linval[0], NULL, NULL);
					}
					else {
						// define z_jk
						const int z_index = ++index;
						addVariable(env, lp, 'B', 0, 1, 0, "z(" + std::to_string(j) + "," + std::to_string(k) + ")");

						// add indicator constraint
						CPXXaddindconstr(env, lp, z_index, 0, linind.size(), linrhs, linsense, &linind[0], &linval[0], NULL);

						// UPDATE original constraints
						zConstraint.addTermX(z_index, 1.0);
					}
				}
			}

			// add constraints only if at least one non-zero was added
			if (!tuple[k]) if (!zConstraint.isEmpty()) if (!single_constraint) {
				zConstraint.addToCplex(env, lp);
			}
		}


		// solve MILP
		status = single_constraint ? CPXXlpopt(env, lp) : CPXXmipopt(env, lp);
		if (!status) {
			status = CPXXgetstat(env, lp);
			if (status == CPX_STAT_OPTIMAL || status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
				status = 0;
				q.resize(1 + U.getNoOfUncertainParameters());
				CPXXgetx(env, lp, &q[0], 0, U.getNoOfUncertainParameters());
				if (q[0] > max_q[0]) {
					max_q = q;
				}
			}
			else if (status == CPX_STAT_INFEASIBLE || status == CPX_STAT_INForUNBD || status == CPXMIP_INFEASIBLE || status == CPXMIP_INForUNBD) {
				CPXXfreeprob(env, &lp);
				continue;
			}
			else {
				MYERROR(status);
			}
		}
		else {
			MYERROR(status);
		}

		// Free memory
		CPXXfreeprob(env, &lp);
	}

	// something went wrong
	if (max_q.size() == 1) MYERROR(999);

	q = max_q;
	return q[0] + x[0];
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_DET_K(const std::vector<double>& x, const unsigned int K1, std::vector<double>& xx) const {
	assert(pInfo);

	unsigned int K  = K1;
	unsigned int Kx = getNumPolicies(x);

	// vectors must be of appropriate size
	if (K < 1 || (int)K > pInfo->getNumPolicies()) MYERROR(EXCEPTION_K);
	if (Kx < 1) MYERROR(EXCEPTION_X);
	
	// Reduce # of policies to check (if possible)
	if (Kx < K) K = Kx; assert(x.size() >= MY_SIZE_X(K));


	// Check deterministic constraints
	for (const auto& con: pInfo->getConstraintsX()) {
		if (getViolation(con, x) > EPS_INFEASIBILITY_X) return 0;
	}

	// Check for feasible 2nd-stage policies
	std::vector<unsigned int> infeas;
	for (unsigned int i = 0; i < K; ++i) {
		for (const auto& con: pInfo->getConstraintsXY()[i]) {
			if (getViolation(con, x) > EPS_INFEASIBILITY_X) {
				infeas.emplace_back(i);
				break;
			}
		}
	}

	// No 2nd-stage policies are feasible
	if (infeas.size() == K) {
		return 0;
	}
	// Discard infeasible 2nd-stage policies;
	else {
		
		// modified x-vector (includes only 'feasible' 2nd-stage policies)
		xx = x;

		for (unsigned int i = 0; i < infeas.size(); i++) {
			const unsigned int k = infeas[i]; // to be discarded
			const auto it = xx.begin() + pInfo->getNumFirstStage() + (k * pInfo->getNumSecondStage());
			
			xx.erase(it, it + pInfo->getNumSecondStage());
			for (unsigned int j = i + 1; j < infeas.size(); j++) {
				infeas[j]--;
			}
			K--;
		}
	}
	assert(K);
	assert(xx.size() >= MY_SIZE_X(K));

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_XQ(const std::vector<double>& x, std::vector<double>& q) const {
	assert ((int)x.size() >= pInfo->getNumFirstStage());

	// Obtain worst violation?
	std::vector<double> max_q(1, -std::numeric_limits<double>::max());

	// Loop through 1st-stage uncertain constraints
	for (const auto& con: pInfo->getConstraintsXQ()) {
		q = getViolation(con, &pInfo->getUncSet(), x);
		if (GET_MAX_VIOL) {
			if (q[0] > max_q[0]) {
				max_q = q;
			}
		}
		else if (q[0] > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}

	// return maximum violation
	if (GET_MAX_VIOL) {
		q = max_q;
		if (q[0] > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_XQ(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, int & label) const {
	assert ((int)x.size() >= pInfo->getNumFirstStage());

	// vectors must be of appropriate size
	if ((int)x.size() != MY_SIZE_X(1)) MYERROR(EXCEPTION_X);
	for (const auto& q : samples) {
		if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);
	}

	// Obtain worst violation?
	double maxViol = -std::numeric_limits<double>::max();
	int label_max = 0;

	// Check each label
	for (label = 0; label < (int)samples.size(); label++) {
		for (const auto& con: pInfo->getConstraintsXQ()) {
			const double viol = getViolation(con, x, samples[label]);
			if (GET_MAX_VIOL) {
				if (viol > maxViol) {
					maxViol   = viol;
					label_max = label;
				}
			}
			else if (viol > EPS_INFEASIBILITY_Q) {
				return false;
			}
		}
	}

	// return maximum violation
	if (GET_MAX_VIOL) {
		label = label_max;
		if (maxViol > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_YQ(const std::vector<double>& x, const unsigned int K, std::vector<double>& q) const {
	assert(K >= 1);
	assert(x.size() >= MY_SIZE_X(K));

	// Obtain worst violation?
	std::vector<double> max_q(1, -std::numeric_limits<double>::max());
    
    //MARK: Qing: add this
    // Get number of uncertain parameter in the original uncertainty set.
    int numParam = pInfo->getUncSet().getNoOfUncertainParameters();

	//////////////////////////////////////////////////////////////////////////////
	// Objective uncertainty only: construct appropriate K-Adaptable expression //
	//////////////////////////////////////////////////////////////////////////////
	if (pInfo->hasObjectiveUncOnly()) {
		// temporary		
		std::vector<ConstraintExpression> CExpr(K);
		
		for (unsigned int i = 0; i < K; ++i) {
			assert(pInfo->getConstraintsXYQ()[i].size() == 1);
            //MARK: Qing, map to \xi^k
            if(DECISION_DEPENDENT && K > 1){
                CExpr[i] = pInfo->getConstraintsXYQ()[i][0].mapParamK(i, numParam);
                //CExpr[i].print();
            }
            else{
                CExpr[i] = pInfo->getConstraintsXYQ()[i][0];
            }
		}
		const KAdaptableExpression KExpr (CExpr, "feasible_KAdaptability");

		// compute violation
        // MARK: Qing: also need to change the evaluation function, need to be careful here
        if(DECISION_DEPENDENT && K > 1)
            q = KExpr.evaluate(&pInfo->getUncSetK(), pInfo->getUncSetK().getNominal(), x);
        else
            q = KExpr.evaluate(&pInfo->getUncSet(), pInfo->getUncSet().getNominal(), x);
        
		if (GET_MAX_VIOL) {
			if (q[0] > max_q[0]) {
				max_q = q;
			}
		}
		else if (q[0] > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}


	///////////////////////////////////////
	// Explicitly solve all possible LPs //
	///////////////////////////////////////
	else if (SEPARATION_STRATEGY == 0 || K == 1) {

		// temporaries
		std::vector<ConstraintExpression> CExpr(K);
		unsigned int i;

		// get # of 2nd-stage constraints
		const auto CI      = pInfo->getConstraintsXYQ()[0].size();
		const auto PTuples = generatePTuples(0, CI - 1, K);

		for (const auto& tuple : PTuples) {
			assert(tuple.size() == K);
			for (i = 0; i < K; ++i) {
				if (tuple[i] < (int)pInfo->getConstraintsXYQ()[i].size()) {
					CExpr[i] = pInfo->getConstraintsXYQ()[i][tuple[i]];
				}
				else {
					break;
				}
			}
			assert(i == K);
			if (i < K) continue;

			const KAdaptableExpression KExpr(CExpr, "Check");
			
			// compute violation
			q = KExpr.evaluate(&pInfo->getUncSet(), pInfo->getUncSet().getNominal(), x);
			if (GET_MAX_VIOL) {
				if (q[0] > max_q[0]) {
					max_q = q;
				}
			}
			else if (q[0] > EPS_INFEASIBILITY_Q) {
				return false;
			}
		}
	}


	/////////////////////////////
	// Generic: formulate MILP //
	/////////////////////////////
	else if (SEPARATION_STRATEGY == 1) {

		///////////////////////////////////////////
		// TODO: normalize constraint violations //
		///////////////////////////////////////////

		int status;
		CPXENVptr env = NULL;
		CPXLPptr  lp  = NULL;
		char b;
		double bd;
		int index;
		auto& U = pInfo->getUncSet();
		const auto qLB = U.getLowerBounds();
		const auto qUB = U.getUpperBounds();

		// get solver objects from uncertainty set
		env = U.getENVObject();
		lp  = U.getLPObject(&status);

		

		// convert problem to maximization MILP with objective = [tau]
		CPXXchgobjsen(env, lp, CPX_MAX);
		CPXXchgprobtype(env, lp, CPXPROB_MILP);
		for (index = 0; index <= U.getNoOfUncertainParameters(); ++index) {
			double coef = static_cast<double>((index == 0));
			CPXXchgobj(env, lp, 1, &index, &coef);
		}
		

		// Change bounds on objective function variable to make it (-Inf, +Inf)
		index = 0; b = 'L'; bd = -CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = 0; b = 'U'; bd = +CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = CPXXgetnumcols(env, lp) - 1;

		// Add constraints maximizing violation
		for (unsigned int k = 0; k < K; ++k) {
			CPXDIM rcnt = 0;
			CPXNNZ nzcnt = 0;
			std::vector<double> rhs;
			std::vector<char> sense;
			std::vector<CPXNNZ> rmatbeg;
			std::vector<CPXDIM> rmatind;
			std::vector<double> rmatval;

			// get all uncertain constraints in policy k for fixed x
			getYQ_fixedX(k, x, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);

			// -tau + sum(j, z_jk * [violation expression in terms of q for fixed x in policy k]) >= 0
			ConstraintExpression tauConstraint("tau(" + std::to_string(k) + ")");
			tauConstraint.sign('G');
			tauConstraint.RHS(0);
			
			// sum(j, z_jk) = 1
			ConstraintExpression zConstraint("z(" + std::to_string(k) + ")");
			zConstraint.sign('E');
			zConstraint.RHS(1);

			for (CPXDIM j = 0; j < rcnt; j++) {
				assert(sense[j] == 'G' || sense[j] == 'L');
				const double f = (sense[j] == 'G') ? (-1.0) : (+1.0);

				// define z_jk
				const std::string z_name = "z(" + std::to_string(j) + "," + std::to_string(k) + ")";
				const int z_index = ++index;
				addVariable(env, lp, 'B', 0, 1, 0, z_name);

				for (CPXNNZ i = rmatbeg[j], ilim = ((j+1 == rcnt) ? nzcnt : rmatbeg[j+1]); i < ilim; i++) {
					
					// lower and upper bounds of q_i
					const double qlb = qLB[rmatind[i]];
					const double qub = qUB[rmatind[i]];

					// define bilinear = z_jk * q_i
					const std::string bl_name = "BL(" + z_name + ",q(" + std::to_string(rmatind[i]) + "))";
					const int bl_index = ++index;
					addVariable(env, lp, 'C', qlb, qub, 0, bl_name);

					// define bilinear constraints
					ConstraintExpression bl;

					// under-estimator 1: bl >= z_jk * qlb
					bl.clear();
					bl.sign('G');
					bl.RHS(0);
					bl.rowname(bl_name + "_l1");
					bl.addTermX(bl_index, 1.0);
					if (qlb != 0.0) bl.addTermX(z_index, -qlb);
					bl.addToCplex(env, lp);

					// over-estimator 1:  bl <= z_jk * qub
					bl.clear();
					bl.sign('L');
					bl.RHS(0);
					bl.rowname(bl_name + "_u1");
					bl.addTermX(bl_index, 1.0);
					if (qub != 0.0) bl.addTermX(z_index, -qub);
					bl.addToCplex(env, lp);

					// under-estimator 2: bl >= (z_jk * qub) + q_i - qub
					bl.clear();
					bl.sign('G');
					bl.RHS(-qub);
					bl.rowname(bl_name + "_l2");
					bl.addTermX(bl_index, 1.0);
					if (qub != 0.0) bl.addTermX(z_index, -qub);
					bl.addTermX(rmatind[i], -1.0);
					bl.addToCplex(env, lp);

					// over-estimator 1:  bl <= (z_jk * qlb) + q_i - qlb
					bl.clear();
					bl.sign('L');
					bl.RHS(-qlb);
					bl.rowname(bl_name + "_u2");
					bl.addTermX(bl_index, 1.0);
					if (qlb != 0.0) bl.addTermX(z_index, -qlb);
					bl.addTermX(rmatind[i], -1.0);
					bl.addToCplex(env, lp);

					// UPDATE original constraints
					if (rmatval[i] != 0.0) tauConstraint.addTermX(bl_index, (f * rmatval[i]));
				}

				// UPDATE original constraints
				zConstraint.addTermX(z_index, 1.0);
				if (rhs[j] != 0.0) tauConstraint.addTermX(z_index, -(f * rhs[j]));
			}

			// add constraints only if at least one non-zero was added
			if (!tauConstraint.isEmpty()) {
				tauConstraint.addTermX(0, -1.0);
				tauConstraint.addToCplex(env, lp);
			}
			if (!zConstraint.isEmpty()) {
				zConstraint.addToCplex(env, lp);
			}
		}


		// solve MILP
		status = CPXXmipopt(env, lp);
		if (!status) {
			status = CPXXgetstat(env, lp);
			if (status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
				status = 0;
				q.resize(1 + U.getNoOfUncertainParameters());
				CPXXgetx(env, lp, &q[0], 0, U.getNoOfUncertainParameters());
				if (GET_MAX_VIOL) {
					if (q[0] > max_q[0]) {
						max_q = q;
					}
				}
				else if (q[0] > EPS_INFEASIBILITY_Q) {
					CPXXfreeprob(env, &lp);
					return false;
				}
			}
			else {
				MYERROR(status);
			}
		}
		else {
			MYERROR(status);
		}
		
		// Free memory
		CPXXfreeprob(env, &lp);
	}


	/////////////////////////////////////////////////////////
	// Generic: formulate MILP using indicator constraints //
	/////////////////////////////////////////////////////////
	else {

		///////////////////////////////////////////
		// TODO: normalize constraint violations //
		///////////////////////////////////////////

		int status;
		CPXENVptr env = NULL;
		CPXLPptr  lp  = NULL;
		char b;
		double bd;
		int index;
        
        //MARK: Qing: use the larger uncertainty set here if decision dependent
        UncertaintySet U;
        if(DECISION_DEPENDENT)
            U = pInfo->getUncSetK();
        else
            U = pInfo->getUncSet();

		// get solver objects from uncertainty set
		env = U.getENVObject();
		lp  = U.getLPObject(&status);


		// convert problem to maximization MILP with objective = [tau]
		CPXXchgobjsen(env, lp, CPX_MAX);
		CPXXchgprobtype(env, lp, CPXPROB_MILP);
		for (index = 0; index <= U.getNoOfUncertainParameters(); ++index) {
			double coef = static_cast<double>((index == 0));
			CPXXchgobj(env, lp, 1, &index, &coef);
		}
		

		// Change bounds on objective function variable to make it (-Inf, +Inf)
		index = 0; b = 'L'; bd = -CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = 0; b = 'U'; bd = +CPX_INFBOUND; CPXXchgbds(env, lp, 1, &index, &b, &bd);
		index = CPXXgetnumcols(env, lp) - 1;

		// Add constraints maximizing violation
		for (unsigned int k = 0; k < K; ++k) {
			CPXDIM rcnt = 0;
			CPXNNZ nzcnt = 0;
			std::vector<double> rhs;
			std::vector<char> sense;
			std::vector<CPXNNZ> rmatbeg;
			std::vector<CPXDIM> rmatind;
            std::vector<CPXDIM> rmatindNew;
			std::vector<double> rmatval;

			// get all uncertain constraints in policy k for fixed x
			getYQ_fixedX(k, x, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
            
            // MARK: Qing: map uncertainty
            if(DECISION_DEPENDENT)
                rmatind = pInfo->mapParamK(k, rmatind);
			
			// sum(j, z_jk) = 1
			ConstraintExpression zConstraint("z(" + std::to_string(k) + ")");
			zConstraint.sign('E');
			zConstraint.RHS(1);

			for (CPXDIM j = 0; j < rcnt; j++) {
				assert(sense[j] == 'G' || sense[j] == 'L');

				// define linear portion of indicator constraint
				// z_jk => -tau + [violation expression of constraint j in terms of q for fixed x in policy k] >= 0
				CPXNNZ ilim = (j+1 == rcnt) ? nzcnt : rmatbeg[j+1];
				double linrhs = rhs[j];
				int linsense = (sense[j] == 'G') ? 'L' : 'G';
                //MARK: Qing: use the new index here
				std::vector<CPXDIM> linind(rmatind.begin() + rmatbeg[j], rmatind.begin() + ilim);
				std::vector<double> linval(rmatval.begin() + rmatbeg[j], rmatval.begin() + ilim);
				std::string indname = "tau(" + std::to_string(j) + "," + std::to_string(k) + ")";

				if (!linind.empty()) {
					// add tau term
					linind.emplace_back(0);
					linval.emplace_back((sense[j] == 'G') ? 1 : -1);

					// define z_jk
					const int z_index = ++index;
					addVariable(env, lp, 'B', 0, 1, 0, "z(" + std::to_string(j) + "," + std::to_string(k) + ")");

					// add indicator constraint
					CPXXaddindconstr(env, lp, z_index, 0, linind.size(), linrhs, linsense, &linind[0], &linval[0], indname.c_str());

					// UPDATE original constraints
					zConstraint.addTermX(z_index, 1.0);
				}
			}

			// add constraints only if at least one non-zero was added
			if (!zConstraint.isEmpty()) {
				zConstraint.addToCplex(env, lp);
			}
		}

		// solve MILP
		status = CPXXmipopt(env, lp);
        
		if (!status) {
			status = CPXXgetstat(env, lp);
			if (status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
				status = 0;
				q.resize(1 + U.getNoOfUncertainParameters());
				CPXXgetx(env, lp, &q[0], 0, U.getNoOfUncertainParameters());
				if (GET_MAX_VIOL) {
					if (q[0] > max_q[0]) {
						max_q = q;
					}
				}
				else if (q[0] > EPS_INFEASIBILITY_Q) {
					CPXXfreeprob(env, &lp);
					return false;
				}
			}
			else {
				MYERROR(status);
			}
		}
		else {
			MYERROR(status);
		}
		
		// Free memory
		CPXXfreeprob(env, &lp);
	}	




	// return maximum violation
	if (GET_MAX_VIOL) {
		q = max_q;
		if (q[0] > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_YQ(const std::vector<double>& x, const unsigned int K, const std::vector<std::vector<double> >& samples, int & label, bool heur) const {
    
	assert(K >= 1);
	assert(x.size() >= MY_SIZE_X(K));
	const double eps = (heur ? 1.E-2 : EPS_INFEASIBILITY_Q);
    
	// Obtain worst violation?
	double maxViol = -std::numeric_limits<double>::max();
	int label_max = 0;
    
	// Check each label
	for (label = 0; label < (int)samples.size(); label++) {
		double labelViol = +std::numeric_limits<double>::max();
		unsigned int k;
        
        UNCSetCPtr U;
        if(DECISION_DEPENDENT){
            pInfo->setXiBar(samples[label]);
            U = &pInfo->getUncSet();
        }
		for (k = 0; k < K; k++) {
			bool isPolicyFeasible = true;
			double policyViol = -std::numeric_limits<double>::max();
			for (const auto& con: pInfo->getConstraintsXYQ()[k]) {
                double v;
                if(DECISION_DEPENDENT){
                    v = getViolation(con, U, x)[0];
                }
                else
                    v = getViolation(con, x, samples[label]);

				if (GET_MAX_VIOL) {
					if (v > policyViol) {
						policyViol = v;
					}
				}
				else if (v > eps) {
					isPolicyFeasible = false;
					if (v < labelViol) {
						labelViol = v;
					}
					break;
				}
			}
			
			if (GET_MAX_VIOL) {
				if (policyViol > eps) {
					isPolicyFeasible = false;
					if (policyViol < labelViol) {
						labelViol = policyViol;
					}
				}
			}

			if (isPolicyFeasible) {
				break;
			}
		}
		assert(k <= K);

		// This means that this sample is not insured by any policy
		if (k >= K) {
			assert(labelViol > eps);
			assert(labelViol < +std::numeric_limits<double>::max());
			if (GET_MAX_VIOL) {
				if (labelViol > maxViol) {
					maxViol = labelViol;
					label_max = label;
				}
			}
			else if (labelViol > eps) {
                pInfo->resetXiBar();
				return false;
			}
		}
	}

	// return maximum violation
	if (GET_MAX_VIOL) {
		label = label_max;
		if (maxViol > eps) {
            pInfo->resetXiBar();
			return false;
		}
	}
    
    pInfo->resetXiBar();
	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_RobustYQ(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, std::vector<double>& q, int& labelCstr, int& labelq, bool heur) const {
    const double eps = (heur ? 1.E-2 : EPS_INFEASIBILITY_Q);
    
    // Obtain worst violation?
    double maxViol = -std::numeric_limits<double>::max();
    
    // Check each label of q
    int label;
    
    for (label = 0; label < (int)samples.size(); label++) {
        
        pInfo->setXiBar(samples[label]);
        UNCSetCPtr U = &pInfo->getUncSet();
        
        int cstrTemp = 0;
        for (const auto& con: pInfo->getConstraintsXYQ()[0]) {
            std::vector<double> qtemp;
            double v;
            
            qtemp = getViolation(con, U, x);
            v = qtemp[0];
        
            if (GET_MAX_VIOL) {
                if (v > maxViol) {
                    maxViol = v;
                    q = qtemp;
                    labelCstr = cstrTemp;
                    labelq = label;
                }
            }
            else if (v > eps) {
                q = qtemp;
                labelq = label;
                pInfo->resetXiBar();
                return false;
            }
            cstrTemp ++;
        }
    }

    // return maximum violation
    if (GET_MAX_VIOL) {
        if (maxViol > eps) {
            pInfo->resetXiBar();
            return false;
        }
    }
    
    pInfo->resetXiBar();
    return true;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_DET(const std::vector<double>& x, const std::vector<double>& q) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)x.size() != MY_SIZE_X(1)) MYERROR(EXCEPTION_X);
	if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	// Loop through all constraints and get violation
	std::vector<double> xx;
	if (!feasible_DET_K(x, 1, xx)) {
		return 0;
	}
	assert(xx == x);
	for (const auto& con: pInfo->getConstraintsXQ()) {
		if (getViolation(con, x, q) > EPS_INFEASIBILITY_Q) return 0;
	}
	for (const auto& con: pInfo->getConstraintsXYQ()[0]) {
		if (getViolation(con, x, q) > EPS_INFEASIBILITY_Q) return 0;
	}

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_SRO(const std::vector<double>& x, std::vector<double>& q) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)x.size() != MY_SIZE_X(1)) return 0;

	// SRO = 1-adaptable ARO
	return feasible_KAdaptability(x, 1, q);
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_SRO(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, int & label) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)x.size() != MY_SIZE_X(1)) MYERROR(EXCEPTION_X);
	for (const auto& q : samples) {
		if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);
	}

	// SRO = 1-adaptable ARO
	return feasible_KAdaptability(x, 1, samples, label);
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_KAdaptability(const std::vector<double>& xx, const unsigned int K1, std::vector<double>& q) const {
		
	// Get true solution
	auto x(xx);

	// Check deterministic feasibility
	if (!feasible_DET_K(xx, K1, x)) {
		return false;
	}

	// Get true # of policies
	unsigned int K  = K1;
	unsigned int Kx = getNumPolicies(x);
	if (Kx < K) K = Kx;
	assert(K);

	// Obtain worst violation?
	std::vector<double> max_q(1, -std::numeric_limits<double>::max());


	// Check 1st-stage uncertain constraints
	if (!feasible_XQ(x, q)) {
		if (GET_MAX_VIOL) {
			if (q[0] > max_q[0]) {
				max_q = q;
			}
		}
		else {
			assert(q[0] > EPS_INFEASIBILITY_Q);
			return false;
		}
	}
	else {
		max_q = q;
	}

	// Check 2nd-stage uncertain constraints
	if (!feasible_YQ(x, K, q)) {
		if (GET_MAX_VIOL) {
			if (q[0] > max_q[0]) {
				max_q = q;
			}
		}
		else {
			assert(q[0] > EPS_INFEASIBILITY_Q);
			return false;
		}
	}
	else {
		max_q = q;
	}

	// return maximum violation
	if (GET_MAX_VIOL) {
		q = max_q;
		if (q[0] > EPS_INFEASIBILITY_Q) {
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::feasible_KAdaptability(const std::vector<double>& xx, const unsigned int K1, const std::vector<std::vector<double> >& samples, int & label) const {
		
	// Get true solution
	auto x(xx);

	// Check deterministic feasibility
	if (!feasible_DET_K(xx, K1, x)) {
		return false;
	}

	// Get true # of policies
	unsigned int K  = K1;
	unsigned int Kx = getNumPolicies(x);
	if (Kx < K) K = Kx;
	assert(K);


	// Check 1st-stage uncertain constraints
	if (!feasible_XQ(x, samples, label)) {
		return false;
	}

	// Check 2nd-stage uncertain constraints
	if (!feasible_YQ(x, K, samples, label)) {
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_DET(const std::vector<double>& q, std::vector<double>& x) const {
	assert(pInfo);

	// vectors must be of appropriate size
	if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);

	int status;
	CPXENVptr env;
	CPXLPptr lp;

	// initialize CPLEX objects
	env = NULL;
	lp  = NULL;
	env = CPXXopenCPLEX (&status);
	lp  = CPXXcreateprob(env, &status, "DETERMINISTIC");

	// define variables and constraints
	updateX(env, lp);
	updateXQ(env, lp, q);
	updateY(env, lp, 0);
	updateYQ(env, lp, 0, q);

	// set options
	setCPXoptions(env);
	CPXXchgprobtype(env, lp, (pInfo->isContinuous() ? CPXPROB_LP : CPXPROB_MILP));
	CPXXchgobjsen(env, lp, CPX_MIN);

	// solve problem
	status = (pInfo->isContinuous() ? CPXXlpopt(env, lp) : CPXXmipopt(env, lp));
	if (!status) {
		status = CPXXgetstat(env, lp);
		if (status == CPX_STAT_OPTIMAL || status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
			status = 0;
			x.resize(CPXXgetnumcols(env, lp));
			CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
			assert(feasible_DET(x, q));
		}
	}
	else {
		MYERROR(status);
	}
	
	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	return status;
}

//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_SRO_duality(std::vector<double>& x) const {
	assert(pInfo);


	int status;
	CPXENVptr env;
	CPXLPptr lp;

	// initialize CPLEX objects
	env = NULL;
	lp  = NULL;
	env = CPXXopenCPLEX (&status);
	lp  = CPXXcreateprob(env, &status, "STATIC_ROBUST");

	// define variables and constraints
	updateX(env, lp);
	updateXQ(env, lp);
	updateY(env, lp, 0);
	updateYQ(env, lp, 0);

	// set options
	setCPXoptions(env);
	CPXXchgprobtype(env, lp, (pInfo->isContinuous() ? CPXPROB_LP : CPXPROB_MILP));
	CPXXchgobjsen(env, lp, CPX_MIN);

	// solve problem
	status = (pInfo->isContinuous() ? CPXXlpopt(env, lp) : CPXXmipopt(env, lp));
	if (!status) {
		status = CPXXgetstat(env, lp);
		if (status == CPX_STAT_OPTIMAL || status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
			status = 0;
			x.resize(MY_SIZE_X(1));
			CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
			#ifndef NDEBUG
			std::vector<double> q;
			assert(feasible_SRO(x, q));
			#endif
		}
	}
	else {
		MYERROR(status);
	}
	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	return status;
}

//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_SRO_cuttingPlane(std::vector<double>& x) {
	assert(pInfo);


	int status;
	CPXENVptr env;
	CPXLPptr lp;

	// initialize CPLEX objects
	env = NULL;
	lp  = NULL;
	env = CPXXopenCPLEX (&status);
	lp  = CPXXcreateprob(env, &status, "STATIC_ROBUST_CP");

	// define variables and constraints using nominal parameters to prevent unboundedness
	updateX(env, lp);
	updateXQ(env, lp, pInfo->getUncSet().getNominal());
	updateY(env, lp, 0);
	updateYQ(env, lp, 0, pInfo->getUncSet().getNominal());

	// set options
	setCPXoptions(env);
	CPXXchgprobtype(env, lp, CPXPROB_MILP); // to use callbacks
	CPXXchgobjsen(env, lp, CPX_MIN);

	// callbacks
	CPXXsetintparam(env, CPX_PARAM_MIPSEARCH, CPX_MIPSEARCH_TRADITIONAL);
	CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
	CPXXsetintparam(env, CPX_PARAM_REDUCE, CPX_PREREDUCE_PRIMALONLY);
	CPXXsetintparam(env, CPX_PARAM_PRELINEAR, CPX_OFF);
	CPXXsetusercutcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);
	CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);

	// solve problem
	status = CPXXmipopt(env, lp);
	if (!status) {
		status = CPXXgetstat(env, lp);
		if (status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
			status = 0;
			x.resize(MY_SIZE_X(1));
			CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
			#ifndef NDEBUG
			std::vector<double> q;
			assert(feasible_SRO(x, q));
			#endif
		}
	}
	else {
		MYERROR(status);
	}
	
	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	return status;
}

//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_ScSRO(const std::vector<std::vector<double> >& samples, std::vector<double>& x) const {
	assert(pInfo);

	// vectors must be of appropriate size
	for (const auto& q : samples) {
		if ((int)q.size() != MY_SIZE_Q) MYERROR(EXCEPTION_Q);
	}

	int status;
	CPXENVptr env;
	CPXLPptr lp;

	// initialize CPLEX objects
	env = NULL;
	lp  = NULL;
	env = CPXXopenCPLEX (&status);
	lp  = CPXXcreateprob(env, &status, "SAMPLE_BASED_STATIC_ROBUST");

	// define variables and constraints
	updateX(env, lp);
	updateY(env, lp, 0);
	for (const auto& q: samples) {
		updateXQ(env, lp, q);
		updateYQ(env, lp, 0, q);
	}

	// set options
	setCPXoptions(env);
	CPXXchgprobtype(env, lp, (pInfo->isContinuous() ? CPXPROB_LP : CPXPROB_MILP));
	CPXXchgobjsen(env, lp, CPX_MIN);

	// solve problem
	status = (pInfo->isContinuous() ? CPXXlpopt(env, lp) : CPXXmipopt(env, lp));
	if (!status) {
		status = CPXXgetstat(env, lp);
		if (status == CPX_STAT_OPTIMAL || status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
			status = 0;
			x.resize(CPXXgetnumcols(env, lp));
			CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
			#ifndef NDEBUG
			int label;
			assert(feasible_SRO(x, samples, label));
			#endif
		}
	}
	else {
		MYERROR(status);
	}
	
	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	return status;
}

//MARK: Qing: Solve the inner robust problem using cutting plane method
//-----------------------------------------------------------------------------------
int KAdaptableSolver::solve_YQRobust_cuttingplane(const std::vector<double>& qini) {
    
    if (!(pInfo->getUncSet().isUncertain())) {
        std::cerr << " Error: Attempting to dualize constraint for deterministic problem. \n";
        assert(0);
    }

    
    int status;
    CPXENVptr env;
    CPXLPptr lp;

    // initialize CPLEX objects
    env = NULL;
    lp  = NULL;
    env = CPXXopenCPLEX (&status);
    lp  = CPXXcreateprob(env, &status, "SAMPLE_BASED_ROBUST");

    // solve the LP relaxation of the RMIP

    // define variables and constraints
    updateX(env, lp);
    updateY(env, lp, 0);
    
    // store the type of decision variables
    std::vector<char> xctype_old;
    xctype_old.resize(pInfo->getNumVars());
    if(!status)
        status = CPXXgetctype (env, lp, &xctype_old[0], 0, pInfo->getNumVars()-1);
    else
        return status;

    // set objective function
    int ind = 0; double coef = 1;
    CPXXchgobjsen(env, lp, CPX_MIN);
    CPXXchgobj(env, lp, 1, &ind, &coef);
    setCPXoptions(env);
    
    // get initial sample
    inner_samples.clear();
    //std::vector<double> qtemp = pInfo->getNominal();
    inner_samples.emplace_back(qini);
    
    CPXDIM rcnt = 0;
    CPXNNZ nzcnt = 0;
    std::vector<double> rhs;
    std::vector<char> sense;
    std::vector<CPXNNZ> rmatbeg;
    std::vector<CPXDIM> rmatind;
    std::vector<double> rmatval;
    
    // add initial constraint
    getYQ_fixedQ(0, qini, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
    CPXXaddrows(env, lp, 0, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], NULL, NULL);
    
    std::vector<CPXDIM> indices;
    indices.resize(pInfo->getNumVars());
    std::iota(indices.begin(), indices.end(), 0);
    
    if(SOLVE_RLP_FIRST){
        // change variable to continuous
        std::vector<char> xctype(indices.size(), CPX_CONTINUOUS);

        CPXXchgctype(env, lp, indices.size(), &indices[0], &xctype[0]);
        
        CPXXchgprobtype(env, lp, CPXPROB_LP);
    }
    
    bool feasible = !SOLVE_RLP_FIRST;
    
    std::vector<double> x;
    x.resize(pInfo->getNumVars());
    std::vector<double> q;
    
    while(!feasible){
        status = CPXXlpopt(env, lp);
        if(status)
            return status;
        
        if (CPXXgetx(env, lp, &x[0], 0, x.size() - 1) == 0) {
            
            rcnt = 0;
            nzcnt = 0;
            rhs.clear();
            sense.clear();
            rmatbeg.clear();
            rmatind.clear();
            rmatval.clear();
            
            if (!feasible_XQ(x, q)) {
                // get all constraints
                getXQ_fixedQ(q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
                CPXXaddrows(env, lp, 0, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], NULL, NULL);
                
                inner_samples.emplace_back(q);
            }
            else if (!feasible_YQ(x, 1, q)) {
                // get all constraints
                getYQ_fixedQ(0, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
                CPXXaddrows(env, lp, 0, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], NULL, NULL);
                
                inner_samples.emplace_back(q);
            }
            else {
                assert(feasible_SRO(x, q));
                feasible = true;
            }
        }
        else
            return status;
    }
    
    if(SOLVE_RLP_FIRST)
        CPXXchgctype(env, lp, indices.size(), &indices[0], &xctype_old[0]);
    
    CPXXchgprobtype(env, lp, CPXPROB_MILP);
    
    // callbacks
    CPXXsetintparam(env, CPX_PARAM_MIPSEARCH, CPX_MIPSEARCH_TRADITIONAL);
    CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
    CPXXsetintparam(env, CPX_PARAM_REDUCE, CPX_PREREDUCE_PRIMALONLY);
    CPXXsetintparam(env, CPX_PARAM_PRELINEAR, CPX_OFF);
    //CPXXsetusercutcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);
    CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);

    // solve problem
    status = CPXXmipopt(env, lp);
    if (!status) {
        status = CPXXgetstat(env, lp);
        if (status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
            status = 0;
            x.resize(MY_SIZE_X(1));
            CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
            #ifndef NDEBUG
            assert(feasible_SRO(x, q));
            #endif
        }
    }
    else {
        MYERROR(status);
    }

    // Free memory
    CPXXfreeprob(env, &lp);
    CPXXcloseCPLEX (&env);

    return status;
}

//int KAdaptableSolver::solve_L_Shaped(const unsigned int K, const bool h, std::ostream& out, CPXENVptr& envCopy, CPXLPptr& lpCopy)
int KAdaptableSolver::solve_L_Shaped(const unsigned int K, const bool h, std::ostream& out)
{
    // vector to store the ub and the ub
//    std::vector<double> ubs;
//    std::vector<double> phis;
//    std::vector<double> lbs;
    //std::vector<double> optsol;
    
//    sol.clear();

    // initialize the problem
    int status;
    int solstat;
    int evalstat;
    
    CPXENVptr env;
    CPXLPptr lp;

    env = NULL;
    lp  = NULL;
    int size = getTrueWSize();
    
    // if(h && K > 1){
    //     env = envCopy;
    //     lp = lpCopy;
    // }
    // else{
    env = CPXXopenCPLEX (&status);
    lp  = CPXXcreateprob(env, &status, "KADAPTABILITY_L_SHAPED");
    
    CPXXchgprobtype(env, lp, CPXPROB_MILP); // to use callbacks
    CPXXchgobjsen(env, lp, CPX_MIN);

    // add epigraph variable, indexd as 0
    // set lower bound of the optimal value
    std::vector<double> xTemp;
    solve_DET(pInfo->getNominal(), xTemp);
    setL(xTemp[0]);
    setBestU(+CPX_INFBOUND);
    addVariable(env, lp, 'C', L, +CPX_INFBOUND, 1.0, "theta");
    
    // add w variables, index begin from 1
    auto& X   = pInfo->getVarsX();
    //std::vector<double> CoefW = pInfo->getCoefW();
    int begin = X.getFirstOfVarType("w");
    for (int i = 0; i < X.getVarTypeSize("w"); i++) if (!X.isUndefVar(i + begin)) {
        std::string cname;
        int ind1, ind2, ind3, ind4, ind5;
        
        X.getVarInfo(begin+i, cname, ind1, ind2, ind3, ind4, ind5);
        if (ind1 > -1) {
            cname += "(" + std::to_string(ind1);
            if (ind2 > -1) cname += "," + std::to_string(ind2);
            if (ind3 > -1) cname += "," + std::to_string(ind3);
            if (ind4 > -1) cname += "," + std::to_string(ind4);
            if (ind5 > -1) cname += "," + std::to_string(ind5);
            cname += ")";
        }
        //addVariable(env, lp, 'B', 0.0, 1.0, CoefW[i], cname);
        addVariable(env, lp, 'B', 0.0, 1.0, 0.0, cname);
    }
    // Set feasible region for w
    feasibleW(env, lp);
    robustifyW(env, lp);
    if(h && rmatbeg_ws.size()){
        assert(rmatbeg_ws.size() == rhs_ws.size());
        assert(sense_ws.size() == rhs_ws.size());
        assert(rmatind_ws.size() == rmatval_ws.size());
        
        CPXXaddrows(env, lp, 0, rmatbeg_ws.size(), rmatind_ws.size(), &rhs_ws[0], &sense_ws[0], &rmatbeg_ws[0], &rmatind_ws[0], &rmatval_ws[0], nullptr, nullptr);
    }
    //}

    
    // get the first index of the second stage variables of each k for later output
    std::vector<unsigned int> secondInd;
    for(unsigned int k = 0; k < K; k++)
        secondInd.emplace_back(pInfo->getVarIndex_2(k, "y", 0));
    
    // fixed parameter for adding cut
    char sense = 'G';
    const CPXNNZ rmatbeg = 0;
    std::vector<CPXDIM> rmatind(size+1);
    std::iota(rmatind.begin(), rmatind.end(), 0);


    std::vector<double> result;
    result.resize(size + 1);
    std::vector<bool> w;
    w.resize(size);
    // set iteration counter t
    int t = 0;
    
    // start the algorithm
    std::vector<double> x;
    std::vector<std::vector<double>> q;
    double phi_wt;
    double objval;
    double tempval;
    double start_time = get_wall_time();
    // CPXDIM numRows = (env? CPXXgetnumrows(env, lp) : 0);
    // std::vector<CPXDIM> rowsToRemove;
    
    // int warm = 10;
    while(true)
    {
        //if(warm >= size){
            // step 1: solve relaxation
            
        std::cout << "------------Iteration " << t << "------------\n\n";
        
        status = CPXXmipopt(env, lp);
        if (!status) {
            solstat = CPXXgetstat(env, lp);
            if (solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL) {
                solstat = 0;
            }

            if (CPXXgetx(env, lp, &result[0], 0, size) != 0) {
                std::cout << "Fail at iteration " << t << std::endl;
                assert(false);
            }
            CPXXgetobjval(env, lp, &objval);
        }
        else {
            MYERROR(status);
        }
        
        std::transform(result.begin()+1, result.end(), w.begin(), [](double x) { return abs(x) > 1.E-5;});
        
        double gap = abs((bestU - objval)*100/bestU);
        std::cout << "The lower bound is: " << std::setprecision(4) << objval << std::endl;
        std::cout << "The upper bound is: " << std::setprecision(4) << bestU << std::endl;
        std::cout << "The gap is: " << std::setprecision(4) << gap << "%\n";
        std::cout << "The opt sol in this turn is: ";
        for(int j = 0; j < size-1; j++)
            std::cout << w[j] << ",";
        
        std::cout << w[size-1] << '\n' << '\n';
        
        // lbs.emplace_back(objval);
        // terminating rule
        if(gap <= 0.1 || ((get_wall_time() - start_time) > 7200))
            break;
        
//        if(h && K >= 2 && bestU < last - 0.001)
//            break;
//        }
//        else{
//            std::fill(w.begin(), w.end(), 0);
//            w[warm] = 1;
//            warm++;
//        }
        // step 2: add cut
        std::cout << "------------Evaluating psi(w)------------\n\n";
        t += 1;
        setW(w);
        
        // pInfo->setVarUB(bestU, 0);
        
        evalstat = solve_KAdaptability(K, false, x, q);
        // feasible_KAdaptability(x, K, Q_TEMP);

//        if(result[0] > x[0] - EPS_INFEASIBILITY_X)
//            break;
        // if given w_t is feasible for the inner problem, add optimality cut
        // if(evalstat == CPXMIP_OPTIMAL || evalstat == CPXMIP_OPTIMAL_TOL || evalstat == CPXMIP_TIME_LIM_FEAS){
        if(q.size()){
            if(!isWDetObjOnly())
            {
                // get subgradient cut
                double rhs_sg;
                char sense_sg;
                CPXNNZ rmatbeg_sg;
                std::vector<CPXDIM> rmatind_sg;
                std::vector<double> rmatval_sg;

                // add sub gradient cut
                addSGCut(w, q, rhs_sg, sense_sg, rmatbeg_sg, rmatind_sg, rmatval_sg);
                status = CPXXaddrows(env, lp, 0, 1, size + 1, &rhs_sg, &sense_sg, &rmatbeg_sg, &rmatind_sg[0], &rmatval_sg[0], NULL, NULL);
                // numRows++;
                
                // subgradient cut is useful for subgradient cut
                assert(rhs_ws.size() == sense_ws.size());
                assert(rhs_ws.size() == rmatbeg_ws.size());
                rhs_ws.emplace_back(rhs_sg);
                sense_ws.emplace_back(sense_sg);
                assert(rmatind_ws.size() == rmatval_ws.size());
                rmatbeg_ws.emplace_back(rmatind_ws.size());
                rmatind_ws.insert(rmatind_ws.end(), rmatind_sg.begin(), rmatind_sg.end());
                rmatval_ws.insert(rmatval_ws.end(), rmatval_sg.begin(), rmatval_sg.end());
                
                // tighter subgradient cut test
//                std::vector<double> rhs_sg2;
//                std::vector<char> sense_sg2;
//                std::vector<CPXNNZ> rmatbeg_sg2;
//                std::vector<CPXDIM> rmatind_sg2;
//                std::vector<double> rmatval_sg2;
//                CPXDIM rcnt;
//                CPXNNZ nzcnt;
//
//                // add sub gradient cut
//                addSGCut(w, q, rhs_sg2, sense_sg2, rmatbeg_sg2, rmatind_sg2, rmatval_sg2, rcnt, nzcnt);
//                status = CPXXaddrows(env, lp, 0, rcnt, nzcnt, &rhs_sg2[0], &sense_sg2[0], &rmatbeg_sg2[0], &rmatind_sg2[0], &rmatval_sg2[0], NULL, NULL);
//
//                char lb = 'l';
//                double lbVal = L;
//                CPXXchgbds(env, lp, 1, 0, &lb, &lbVal);
                if(status){
                    std::cout << "Fail at adding sub-gradient cut\n\n";
                    assert(status);
                }
                std::cout << "Add subgradient cut\n\n";
            }
        }
            
        if(evalstat == CPXMIP_OPTIMAL || evalstat == CPXMIP_OPTIMAL_TOL || evalstat == CPXMIP_TIME_LIM_FEAS || evalstat == CPXMIP_ABORT_FEAS){
            // add integer cut
            phi_wt = x[0];
            tempval = objval - result[0] + phi_wt;
            // phis.emplace_back(tempval);
            
            // print out second stage decision variable
            for(uint i = 1; i <= NK; i++){
                std::cout << "second stage decision at k = " << i << " is: ";
                for (int j = 0; j < size-1; j++){
                    std::cout << x[secondInd[i-1] + j] << " ";
                }
                std::cout << x[secondInd[i-1] + size - 1] << "\n";
            }
            
            if(tempval < bestU){
                setBestU(tempval);
                xsolOut = x;
            }
            
            // ubs.emplace_back(bestU);
            double coef = phi_wt - L;
            
            std::vector<double> rmatval;
            // coefficient for the epigraph variable
            rmatval.emplace_back(1.0);
            
            int sizeN = 0;
            // coefficient for variable w
            for(int i = 0; i < size; i++)
            {
                if(w[i] == 1){
                    rmatval.emplace_back(-coef);
                    sizeN += 1;
                }
                else
                    rmatval.emplace_back(coef);
            }
            
            double rhs = L - coef*(sizeN - 1);
            
            CPXXaddrows(env, lp, 0, 1, size + 1, &rhs, &sense, &rmatbeg, &rmatind[0], &rmatval[0], nullptr, nullptr);
//            rowsToRemove.emplace_back(numRows);
//            numRows++;
            
            // add deterministic part of w(cost term) to the objective function will help, add warm start of |w|_1 = Q will help
            if(isWDetObjOnly()){
                double rhs_inf(phi_wt);
                std::vector<CPXDIM> rmatind_inf;
                std::vector<double> rmatval_inf;
                
                rmatind_inf.emplace_back(0);
                rmatval_inf.emplace_back(1.0);
                for(int i = 0; i < size; i++){
                    if(w[i] == 0){
                        rmatval_inf.emplace_back(coef);
                        rmatind_inf.emplace_back(i+1);
                    }
                }
                
                CPXXaddrows(env, lp, 0, 1, rmatind_inf.size(), &rhs_inf, &sense, &rmatbeg, &rmatind_inf[0], &rmatval_inf[0], nullptr, nullptr);
            }
            
            std::cout << "Add optimality cut\n\n";
        }
        
        // should be treated more carefully
        // if given w_t is infeasible for the inner problem, add feasibility cut
        else if(evalstat == CPXMIP_INFEASIBLE || evalstat == CPXMIP_INForUNBD || evalstat == CPXMIP_TIME_LIM_INFEAS || evalstat == CPXMIP_ABORT_INFEAS){
            if(evalstat == CPXMIP_INFEASIBLE || evalstat == CPXMIP_INForUNBD){

                std::vector<double> rmatval_feas;
                std::vector<CPXDIM> rmatind_feas;
                
                int sizeN = 0;
                
                // std::vector<double> rmatval;
                // coefficient for variable w
    //            for(int i = 0; i < size; i++)
    //            {
    //                if(w[i] == 1){
    //                    rmatval.emplace_back(-1.0);
    //                    sizeN += 1;
    //                }
    //                else
    //                    rmatval.emplace_back(1.0);
    //            }
    //
    //            double rhs = 1 - sizeN;
    //
    //            CPXXaddrows(env, lp, 0, 1, size, &rhs, &sense, &rmatbeg, &rmatind[1], &rmatval[0], nullptr, nullptr);
                for(int i = 0; i < size; i++)
                {
                    if(w[i] == 1){
                        rmatind_feas.emplace_back(i+1);
                        rmatval_feas.emplace_back(-1.0);
                        sizeN += 1;
                    }
                }
                double rhs = 1 - sizeN;
                
                CPXXaddrows(env, lp, 0, 1, sizeN, &rhs, &sense, &rmatbeg, &rmatind_feas[0], &rmatval_feas[0], nullptr, nullptr);
                //numRows++;
                // subgradient cut is useful for subgradient cut
                assert(rhs_ws.size() == sense_ws.size());
                assert(rhs_ws.size() == rmatbeg_ws.size());
                rhs_ws.emplace_back(rhs);
                sense_ws.emplace_back(sense);
                assert(rmatind_ws.size() == rmatval_ws.size());
                rmatbeg_ws.emplace_back(rmatind_ws.size());
                rmatind_ws.insert(rmatind_ws.end(), rmatind_feas.begin(), rmatind_feas.end());
                rmatval_ws.insert(rmatval_ws.end(), rmatval_feas.begin(), rmatval_feas.end());
                
                std::cout << "Add feasibility cut\n\n";
            }
            else{
                int sizeN = 0;
                
                std::vector<double> rmatval;
                // coefficient for variable w
                for(int i = 0; i < size; i++)
                {
                    if(w[i] == 1){
                        rmatval.emplace_back(-1.0);
                        sizeN += 1;
                    }
                    else
                        rmatval.emplace_back(1.0);
                }

                double rhs = 1 - sizeN;

                CPXXaddrows(env, lp, 0, 1, size, &rhs, &sense, &rmatbeg, &rmatind[1], &rmatval[0], nullptr, nullptr);
                std::cout << "Add time limit or upper bound cut-off feasibility cut\n\n";
            }
        }
        
        else{
            std::cout << "Something wrong when evaluate phi(w_t), status code is: " << solstat << "\n";
            assert(false);
        }

    }
    

    double end_time = get_wall_time();

    if (COLLECT_RESULTS) {
        double total_solution_time = end_time - start_time;
        double global_lb; CPXXgetbestobjval(env, lp, &global_lb);
        double final_gap = INFINITY, final_objval = bestU;
        final_gap = 100*(final_objval - global_lb)/(1E-10 + std::abs(final_objval));

        size_t pos_keyword = 0, pos_dash = 0;
        const std::string s = pInfo->getSolFileName();
        const std::string problemType(s, 0, 3);
        pos_keyword = s.find_first_of("n", 3);
        pos_dash = s.find_first_of("-", pos_keyword);
        const std::string n(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
        pos_keyword = s.find_first_of("s", pos_dash);
        pos_dash = s.find_first_of("-.opt", pos_keyword);
        const std::string seed(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
        std::string stat = "Unknown";
        if (solstat == 0) stat = "Optimal";
        if (solstat == CPXMIP_INFEASIBLE || solstat == CPXMIP_INForUNBD) stat = "Infeas";
        if (solstat == CPXMIP_TIME_LIM_FEAS || solstat == CPXMIP_TIME_LIM_INFEAS) stat = "Time Lim";
        if (solstat == CPXMIP_MEM_LIM_FEAS || solstat == CPXMIP_MEM_LIM_INFEAS) stat = "Mem Lim";
        if (heuristic_mode) stat = "Heur";
        
        std::cout << "------------Final Results------------\n";
        write(std::cout, n, K, seed, stat, final_objval, total_solution_time, final_gap);
        if(pInfo->getVarsX().getDefVarTypeSize("psi"))
            // file to record DRO performance
            out << seed << "," << stat << "," << final_objval << "," << total_solution_time << "," << final_gap << "," << t << "\n";
        else{
            // file to record RO solution, style: name-N-K
            int i = 1;
            out << seed << ",";
            for(; i < int(xsolOut.size() - 1); i++)
                out << xsolOut[i] << ",";
            out << xsolOut[i] << "\n";
        }
        xsolOut.clear();
        
        // write to csv file
//        std::string method = (isWDetObjOnly()? "inf" : "sg");
//        std::ofstream myfile("/Users/lynn/Desktop/research/DRO/figures/"+problemType+"_N="+n+"_K="+std::to_string(K)+"_"+method+".txt");
//        int vsize = lbs.size();
//        for(int n = 0; n < vsize-1; n++)
//            myfile << lbs[n] << ",";
//        myfile << lbs[vsize-1] << std::endl;
//
//        for(int n = 0; n < vsize-1; n++)
//            myfile << ubs[n] << ",";
//        myfile << lbs[vsize-1] << std::endl;
//
//        for(int n = 0; n < vsize-1; n++)
//        myfile << phis[n] << ",";
//        myfile << lbs[vsize-1] << std::endl;
        
    }
    
//    sol = x;
//    sol[0] = bestU;
    
    // warm start larger K case
//    if(h){
//        envCopy = env;
//        lpCopy = lp;
//        for (auto it = rowsToRemove.rbegin(); it != rowsToRemove.rend(); ++it)
//            CPXXdelrows (envCopy, lpCopy, *it, *it);
//    }
    
    // Free memory
    CPXXfreeprob(env, &lp);
    CPXXcloseCPLEX (&env);
    
    return solstat;
}

int KAdaptableSolver::solve_L_Shaped2(const unsigned int K, const bool h, std::ostream& out)
{
    // initialize the problem
    NK = K;
    std::vector<double> optsol;
    
    int status;
    int solstat;
    CPXENVptr env;
    CPXLPptr lp;

    env = NULL;
    lp  = NULL;

    env = CPXXopenCPLEX (&status);
    lp  = CPXXcreateprob(env, &status, "KADAPTABILITY_L_SHAPED");
    
    TERMINATOR = 0;
    CPXXsetterminate(env, &TERMINATOR);
    
    // add epigraph variable, indexd as 0
    // set lower bound of the optimal value
    std::vector<double> xTemp;
    solve_DET(pInfo->getNominal(), xTemp);
    setL(xTemp[0]);
    // int size = getTrueWSize();
    setBestU(+CPX_INFBOUND);
    t = 0;
    // checkRep = false;
    
    addVariable(env, lp, 'C', L, +CPX_INFBOUND, 1.0, "theta");
    
    pInfo->isConsistentWithDesign();
    
    // add w variables, index begin from 1
    auto& X   = pInfo->getVarsX();
    std::vector<double> CoefW = pInfo->getCoefW();
    int begin = X.getFirstOfVarType("w");
    for (int i = 0; i < X.getVarTypeSize("w"); i++) if (!X.isUndefVar(i + begin)) {
        std::string cname;
        int ind1, ind2, ind3, ind4, ind5;
        
        X.getVarInfo(begin+i, cname, ind1, ind2, ind3, ind4, ind5);
        if (ind1 > -1) {
            cname += "(" + std::to_string(ind1);
            if (ind2 > -1) cname += "," + std::to_string(ind2);
            if (ind3 > -1) cname += "," + std::to_string(ind3);
            if (ind4 > -1) cname += "," + std::to_string(ind4);
            if (ind5 > -1) cname += "," + std::to_string(ind5);
            cname += ")";
        }
        if(std::abs(CoefW[i]) >= EPS_INFEASIBILITY_X)
            addVariable(env, lp, 'B', 0.0, 1.0, CoefW[i], cname);
        addVariable(env, lp, 'B', 0.0, 1.0, 0.0, cname);
    }
    // Set feasible region for w
    // feasibleW(env, lp);
    // Robustify w
    robustifyW(env, lp);
    
    // CPXXwriteprob(env, lp, "/Users/lynn/Desktop/research/DRO/BnB/model_output/testK", "LP");
    
    if(h && rmatbeg_ws.size()){
        assert(rmatbeg_ws.size() == rhs_ws.size());
        assert(sense_ws.size() == rhs_ws.size());
        assert(rmatind_ws.size() == rmatval_ws.size());
        
        CPXXaddrows(env, lp, 0, rmatbeg_ws.size(), rmatind_ws.size(), &rhs_ws[0], &sense_ws[0], &rmatbeg_ws[0], &rmatind_ws[0], &rmatval_ws[0], nullptr, nullptr);
    }
    
    // set options
    CPXXchgobjsen(env, lp, CPX_MIN);
    CPXXchgprobtype(env, lp, CPXPROB_MILP); // to use callbacks
    setCPXoptions(env);
    CPXXsetintparam(env, CPXPARAM_ScreenOutput, CPX_ON);

    // callbacks
    // CPXXsetintparam(env, CPX_PARAM_MIPSEARCH, CPX_MIPSEARCH_TRADITIONAL);
    // CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
    CPXXsetintparam(env, CPX_PARAM_REDUCE, CPX_PREREDUCE_PRIMALONLY);
    CPXXsetintparam(env, CPX_PARAM_PRELINEAR, CPX_OFF);
    CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
    CPXXsetdblparam(env, CPXPARAM_TimeLimit, 7200);
    // CPXXsetdblparam(env, CPXPARAM_MIP_Tolerances_MIPGap, 0.005);

    CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_LS_cuttingPlane, this);

    double start_time = get_wall_time();
    
    // solve problem
    status = CPXXmipopt(env, lp);
    if (!status) {
        solstat = CPXXgetstat(env, lp);
        if (solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL) {
            solstat = 0;
        }
        optsol.resize(CPXXgetnumcols(env, lp));
        if (CPXXgetx(env, lp, &optsol[0], 0, optsol.size() - 1) != 0) {
            std::cout << "Enable to get optimal w! /n";
            assert(false);
        }
    }
    else {
        MYERROR(status);
    }
    
    double end_time = get_wall_time();

    if (COLLECT_RESULTS) {
        double total_solution_time = end_time - start_time;
        double global_lb; CPXXgetbestobjval(env, lp, &global_lb);
        double final_gap = INFINITY, final_objval = INFINITY;
        if (CPXXgetobjval(env, lp, &final_objval) == 0) {
            final_gap = 100*(final_objval - global_lb)/(1E-10 + std::abs(final_objval));
        }

        size_t pos_keyword = 0, pos_dash = 0;
        const std::string s = pInfo->getSolFileName();
        const std::string problemType(s, 0, 3);
        pos_keyword = s.find_first_of("n", 3);
        pos_dash = s.find_first_of("-", pos_keyword);
        const std::string n(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
        pos_keyword = s.find_first_of("s", pos_dash);
        pos_dash = s.find_first_of("-.opt", pos_keyword);
        const std::string seed(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
        std::string stat = "Unknown";
        if (solstat == 0) stat = "Optimal";
        if (solstat == CPXMIP_INFEASIBLE || solstat == CPXMIP_INForUNBD || solstat == CPXMIP_ABORT_INFEAS) stat = "Infeas";
        if (solstat == CPXMIP_TIME_LIM_FEAS || solstat == CPXMIP_TIME_LIM_INFEAS || solstat == CPXMIP_ABORT_FEAS) stat = "Time Lim";
        if (solstat == CPXMIP_MEM_LIM_FEAS || solstat == CPXMIP_MEM_LIM_INFEAS) stat = "Mem Lim";
        if (heuristic_mode) stat = "Heur";
        
        std::cout << "------------Final Results------------\n";
        write(std::cout, n, K, seed, stat, final_objval, total_solution_time, final_gap);
        std::cout << "----------" << t << " iterations in total----------\n\n";
        if(pInfo->getVarsX().getDefVarTypeSize("psi"))
            // file to record DRO performance
            out << seed << "," << stat << "," << final_objval << "," << total_solution_time << "," << final_gap << "," << t << "\n";
        else{
            // file to record RO solution, style: name-N-K
            int i = 1;
            out << seed << ",";
            for(; i < int(xsolOut.size() - 1); i++)
                out << xsolOut[i] << ",";
            out << xsolOut[i] << "\n";
        }
    }
    
    wBounds.clear();
    xsol.clear();
    xsolOut.clear();
    // Free memory
    CPXXfreeprob(env, &lp);
    CPXXcloseCPLEX (&env);

    return solstat;
}

int KAdaptableSolver::addSGCut(const std::vector<bool>& w, const std::vector<std::vector<double>>& q, double& rhs, char& sense, CPXNNZ& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval)
{
    
    // first start a new problem
    int status;
    CPXENVptr env_sub;
    CPXLPptr lp_sub;
    env_sub = NULL;
    lp_sub  = NULL;
    
    std::vector<CPXDIM> indices;
    std::vector<char> xctype;
    std::vector<double> pi;
    
    env_sub = CPXXopenCPLEX (&status);
    lp_sub  = CPXXcreateprob(env_sub, &status, "Sub_Gradient");
    
    
    // use the update function to add constraints into the problem
    int newK = q.size();
//    std::vector<std::vector<double>> q_samples;
    
//    if (newK > 10){
//        // Get 10 samples from the total samples
//        int bit = newK % 10;
//        int num = newK / 10;
//        for(int total = bit; total + num < newK; total += num)
//            q_samples.emplace_back(q[total]);
//        newK = q_samples.size();
//    }
//    else
    
    pInfo->resize(newK);
    
    updateX(env_sub, lp_sub);
    for(int k = 0; k <= newK - 1; k++){
        updateY(env_sub, lp_sub, k);
        updateXQ(env_sub, lp_sub, q[k]);
        updateYQ(env_sub, lp_sub, k, q[k]);
    }
    
//    updateX(env_sub, lp_sub);
//    for(uint k = 0; k <= NK-1; k++){
//        updateY(env_sub, lp_sub, k);
//        for(auto sample : q[k]){
//            updateXQ(env_sub, lp_sub, sample);
//            updateYQ(env_sub, lp_sub, k, sample);
//        }
//    }
    
    setCPXoptions(env_sub);
    
    // change type of the decision variables to continuous
    // indices.resize(pInfo->getNumVars(NK));
    indices.resize(pInfo->getNumVars(newK));
    xctype.resize(indices.size(), CPX_CONTINUOUS);
    std::iota(indices.begin(), indices.end(), 0);
    CPXXchgctype(env_sub, lp_sub, indices.size(), &indices[0], &xctype[0]);
    
    CPXXchgprobtype(env_sub, lp_sub, CPXPROB_LP);
    CPXXchgobjsen(env_sub, lp_sub, CPX_MIN);
    
    status = CPXXlpopt(env_sub, lp_sub);
    
    if(status)
        return status;

    // get the objval for the relaxation
    CPXXgetobjval(env_sub, lp_sub, &rhs);
    
    //get new lower bound
    if(rhs < L)
        setL(rhs);
    
    // get the value of dual variables
    int numCstr(CPXXgetnumrows(env_sub, lp_sub));
    pi.resize(numCstr);
    CPXXgetpi(env_sub, lp_sub, &pi[0], 0, numCstr - 1);
    
    // get the index of variable w
    auto& X = pInfo->getVarsX();

    int begin = X.getFirstDefOfVarType("w");
    begin = X.getDefVarLinIndex(begin);
    
    int size = X.getDefVarTypeSize("w");
    assert(int(w.size()) == size);
    
    // calculate the subgradient vector and rhs as objval - s*w^t
    // the subgradient starts as rmatval[1], rmtalval[0] is for the epigraph variable
    rmatval.assign(size + 1, 0.0);
    double coef_ij;
    for(int j = 0; j < size; j++){
        // calculate the subgradient for w_j
        for(int i = 0; i < numCstr; i++){
            CPXXgetcoef(env_sub, lp_sub, i, j + begin, &coef_ij);
            if(abs(coef_ij) >= EPS_INFEASIBILITY_X)
                rmatval[j + 1] += coef_ij * pi[i];
        }
        // calculte the term s_j*w^t_j
        rhs += w[j]*rmatval[j + 1];
    }
    
    // add subgradient cut to the master problem
    rmatval[0] = 1.0;
    rmatind.resize(rmatval.size());
    std::iota(rmatind.begin(), rmatind.end(), 0);
    
    sense = 'G';
    rmatbeg = 0;
    
    // reset(*dataInfo, NK);
    return 0;
}

int KAdaptableSolver::addSGCut(const std::vector<bool>& w, const std::vector<std::vector<double>>& q, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval, CPXDIM &rcnt, CPXNNZ &nzcnt)
{
    rhs.clear();
    sense.clear();
    rmatbeg.clear();
    rmatind.clear();
    rmatval.clear();
    rcnt = 0;
    nzcnt = 0;
    
    // use the update function to add constraints into the problem
    int newK = q.size();
    std::vector<std::vector<double>> q_samples;
    
    if (newK > 10){
        // Get 10 samples from the total samples
        int bit = newK % 10;
        int num = newK / 10;
        for(int total = bit; total + num < newK; total += num)
            q_samples.emplace_back(q[total]);
        newK = q_samples.size();
    }
    else
        q_samples = q;
    
    pInfo->resize(1);
    
    // get the index of variable w
    auto& X = pInfo->getVarsX();

    int begin = X.getFirstDefOfVarType("w");
    begin = X.getDefVarLinIndex(begin);
    
    int size = X.getDefVarTypeSize("w");
    assert(int(w.size()) == size);
    
    for(auto &sample : q_samples){
        
//    std::vector<double> newL;
//    solve_DET(sample, newL);
//    if(newL[0] < L)
//        setL(newL[0]);
        
        std::vector<CPXDIM> indices;
        std::vector<char> xctype;
        std::vector<double> pi;
        
        std::vector<double> rmatval_s;
        std::vector<int> rmatind_s;
        double rhs_s;
        
        // first start a new problem
        int status;
        CPXENVptr env_sub;
        CPXLPptr lp_sub;
        env_sub = NULL;
        lp_sub  = NULL;
        
        env_sub = CPXXopenCPLEX (&status);
        lp_sub  = CPXXcreateprob(env_sub, &status, "Sub_Gradient");
        
        updateX(env_sub, lp_sub);
        updateY(env_sub, lp_sub, 0);
        updateXQ(env_sub, lp_sub, sample);
        updateYQ(env_sub, lp_sub, 0, sample);
        
        setCPXoptions(env_sub);
        
        // change type of the decision variables to continuous
        // indices.resize(pInfo->getNumVars(NK));
        indices.resize(pInfo->getNumVars(1));
        xctype.resize(indices.size(), CPX_CONTINUOUS);
        std::iota(indices.begin(), indices.end(), 0);
        CPXXchgctype(env_sub, lp_sub, indices.size(), &indices[0], &xctype[0]);
        
        CPXXchgprobtype(env_sub, lp_sub, CPXPROB_LP);
        CPXXchgobjsen(env_sub, lp_sub, CPX_MIN);
        
        status = CPXXlpopt(env_sub, lp_sub);
        
        if(status)
            return status;
        
        // get the objval for the relaxation
        CPXXgetobjval(env_sub, lp_sub, &rhs_s);
        // get the value of dual variables
        int numCstr(CPXXgetnumrows(env_sub, lp_sub));
        pi.resize(numCstr);
        CPXXgetpi(env_sub, lp_sub, &pi[0], 0, numCstr - 1);
        
        // calculate the subgradient vector and rhs as objval - s*w^t
        // the subgradient starts as rmatval[1], rmtalval[0] is for the epigraph variable
        rmatval_s.assign(size + 1, 0.0);
        double coef_ij;
        for(int j = 0; j < size; j++){
            // calculate the subgradient for w_j
            for(int i = 0; i < numCstr; i++){
                CPXXgetcoef(env_sub, lp_sub, i, j + begin, &coef_ij);
                if(abs(coef_ij) >= EPS_INFEASIBILITY_X)
                    rmatval_s[j + 1] += coef_ij * pi[i];
            }
            // calculte the term s_j*w^t_j
            rhs_s += w[j]*rmatval_s[j + 1];
        }
        
        // add subgradient cut to the master problem
        rmatval_s[0] = 1.0;
        rmatind_s.resize(rmatval_s.size());
        std::iota(rmatind_s.begin(), rmatind_s.end(), 0);
        
        rmatbeg.insert(rmatbeg.end(), rmatval.size());
        rmatind.insert(rmatind.end(), rmatind_s.begin(), rmatind_s.end());
        rmatval.insert(rmatval.end(), rmatval_s.begin(), rmatval_s.end());
        rhs.insert(rhs.end(), rhs_s);
        sense.insert(sense.end(), 'G');
        rcnt += 1;
        nzcnt += (CPXNNZ)rmatval_s.size();
        
        CPXXfreeprob(env_sub, &lp_sub);
    }
    
    assert(rhs.size() == rmatbeg.size());
    assert(rhs.size() == sense.size());
    assert((CPXDIM)rhs.size() == rcnt);
    assert(rmatind.size() == rmatval.size());
    assert((CPXNNZ)rmatind.size() == nzcnt);
    return 0;
}

int KAdaptableSolver::drawPsi(const unsigned int K, const std::vector<double>& roSol, const std::vector<std::pair<double, double>>& bounds, const std::vector<int>& numBP, std::vector<double>& results)
{
    assert(numBP.size() == bounds.size());
    
    //generate index
    std::vector<std::vector<int>> indice;
    //BP should start at 0
    std::vector<int> num = numBP;
    getIndices(num, indice);
    
    for(auto bound : bounds)
        assert(bound.first < bound.second);
    
    for(auto index : indice){
        std::vector<double> psi;
        for(auto i : index){
            psi.emplace_back(bounds[i].first + (bounds[i].second - bounds[i].first)*i);
        }
        setPsi(psi);
        
        std::vector<double> x;
        std::vector<std::vector<double>> q;
        
        int status;
        status = solve_KAdaptability(K, false, x, q, roSol);
        if(status)
            return status;
        
        results.emplace_back(x[0]);
    }
    
    return 0;
}
//int KAdaptableSolver::solve_YQRobust_cuttingplane(const std::vector<double>& qini, CstrCPtr con)
//{
//    if (con->getSense() == 'E') {
//        std::cerr << "Error: Attempting to dualize equality constraint in which uncertain parameters do not appear as coefficients of decision variables. \n";
//        assert(0);
//    }
//
//    if (!(pInfo->getUncSet().isUncertain())) {
//        std::cerr << " Error: Attempting to dualize constraint for deterministic problem. \n";
//        assert(0);
//    }
//
//
//    int status;
//    CPXENVptr env;
//    CPXLPptr lp;
//
//    // initialize CPLEX objects
//    env = NULL;
//    lp  = NULL;
//    env = CPXXopenCPLEX (&status);
//    lp  = CPXXcreateprob(env, &status, "SAMPLE_BASED_ROBUST");
//
//    // solve the LP relaxation of the RMIP
//
//    // define variables and constraints
//    updateX(env, lp);
//    updateY(env, lp, 0);
//
//    // store the type of decision variables
//    std::vector<char> xctype_old;
//    xctype_old.resize(pInfo->getNumVars());
//    if(!status)
//        status = CPXXgetctype (env, lp, &xctype_old[0], 0, pInfo->getNumVars()-1);
//    else
//        return status;
//
//    std::vector<CPXDIM> indices;
//    indices.resize(pInfo->getNumVars());
//    std::iota(indices.begin(), indices.end(), static_cast<CPXDIM>(pInfo->getNumVars()));
//
//    if(SOLVE_RLP_FIRST){
//        // change variable to continuous
//        std::vector<char> xctype;
//
//        xctype.resize(indices.size(), CPX_CONTINUOUS);
//
//        CPXXchgctype(env, lp, indices.size(), &indices[0], &xctype[0]);
//        CPXXchgprobtype(env, lp, CPXPROB_LP);
//    }
//
//    // set objective function
//    int ind = 0; double coef = 1;
//    CPXXchgobjsen(env, lp, CPX_MIN);
//    CPXXchgobj(env, lp, 1, &ind, &coef);
//    setCPXoptions(env);
//
//    // get initial sample
//    inner_samples.clear();
//    //std::vector<double> qtemp = pInfo->getNominal();
//    inner_samples.emplace_back(qini);
//    //add initial constraint
//    con->addToCplex(env, lp, nullptr, false, qini);
//
//    bool feasible = !SOLVE_RLP_FIRST;
//    std::vector<double> x;
//    x.resize(pInfo->getNumVars());
//    std::vector<double> q;
//
//    // temporary KExpr
//    std::vector<ConstraintExpression> CExpr(1);
//    CExpr[0] = *con;
//    const KAdaptableExpression KExpr (CExpr, "feasible_KAdaptability");
////    unsigned int it = 0;
//    // solve problem to converge, save the samples
//    while(!feasible){
////        std::cout << it++ << std::endl;
//        status = CPXXlpopt(env, lp);
//        if(status)
//            return status;
//        if (CPXXgetx(env, lp, &x[0], 0, x.size() - 1) == 0) {
////            for(double a : x)
////                std::cout << a << ',';
////            std::cout << std::endl;
//
//            // compute violation
//            q = KExpr.evaluate(&pInfo->getUncSet(), pInfo->getUncSet().getNominal(), x);
////            for(double a : q)
////                std::cout << a << ',';
////            std::cout << std::endl;
//            if (q[0] > EPS_INFEASIBILITY_Q){
//                assert(int(q.size()) == pInfo->getUncSet().getNoOfUncertainParameters() + 1);
//                feasible = false;
//                // check this!
//                // correct, the first element is for the epigraph.
//                // ----------------done-------------------//
//                //std::vector<double> newq(q.begin()+1, q.end());
//                inner_samples.emplace_back(q);
//
//                // add new constraints
//                con->addToCplex(env, lp, nullptr, false, q);
//            }
//            else
//                feasible = true;
//        }
//        else
//            return status;
//    }
//
////    std::cout << "here \n";
//    // RMIP to be complemented!!!
//    // reset the variables type and solve a original MIP problem
//    CPXXchgctype(env, lp, indices.size(), &indices[0], &xctype_old[0]);
//    CPXXchgprobtype(env, lp, CPXPROB_MILP);
//
//    // tell SRO which constraint to solve
//    inner_obj = con;
//
//    // callbacks
//    CPXXsetintparam(env, CPX_PARAM_MIPSEARCH, CPX_MIPSEARCH_TRADITIONAL);
//    CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
//    CPXXsetintparam(env, CPX_PARAM_REDUCE, CPX_PREREDUCE_PRIMALONLY);
//    CPXXsetintparam(env, CPX_PARAM_PRELINEAR, CPX_OFF);
//    //CPXXsetusercutcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);
//    CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_SRO_cuttingPlane, this);
//
//    // solve problem
//    status = CPXXmipopt(env, lp);
//    if (!status) {
//        status = CPXXgetstat(env, lp);
//        if (status == CPXMIP_OPTIMAL || status == CPXMIP_OPTIMAL_TOL) {
//            status = 0;
//            x.resize(MY_SIZE_X(1));
//            CPXXgetx(env, lp, &x[0], 0, x.size() - 1);
//            #ifndef NDEBUG
//            std::vector<double> q;
//            q = KExpr.evaluate(&pInfo->getUncSet(), pInfo->getUncSet().getNominal(), x);
//            assert(q[0] <= EPS_INFEASIBILITY_Q);
//            #endif
//        }
//    }
//    else {
//        MYERROR(status);
//    }
//
//    // Free memory
//    CPXXfreeprob(env, &lp);
//    CPXXcloseCPLEX (&env);
//
//    return status;
//}


//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_KAdaptability(const unsigned int K, const bool h, std::vector<double>& x, std::vector<std::vector<double>>& q, const std::vector<double>& roSol) {
    
	assert(pInfo);
	if (K < 1) MYERROR(EXCEPTION_K);
    
    // parse RO solutions
    // get solution of variable w, x and y
    std::vector<double> ySol;
    if(roSol.size()){
        int sizeW(pInfo->getVarsX().getDefVarTypeSize("w"));
        int sizeX(pInfo->getVarsX().getDefVarTypeSize("x"));
        int sizeY(pInfo->getNumSecondStage());

        std::vector<bool> wSol;
        wSol.resize(sizeW);
        std::transform(roSol.begin(), roSol.begin()+sizeW, wSol.begin(), [](double x) { return abs(x) > 1.E-5;});
        std::vector<double> xSol(roSol.begin()+sizeW, roSol.begin()+sizeW+sizeX);
        ySol = std::vector<double>(roSol.begin()+sizeW+sizeX, roSol.end());
        assert(uint(ySol.size()) == sizeY*K);

        setRobSolx(wSol, xSol);
    }
    
    
    // Create K set of constraints
    pInfo->resize(K);
    q.clear();
    
    NK = K;

	// Preconditions for heuristic
	heuristic_mode = h;
	if (heuristic_mode && K > 1) {
		if (getNumPolicies(xfix) + 1 != K) MYERROR(EXCEPTION_X);
	}

	int solstat;
	int status;
	CPXENVptr env;
	CPXLPptr lp;

	// initialize CPLEX objects
	env = NULL;
	lp  = NULL;
	env = CPXXopenCPLEX (&status);
	lp  = CPXXcreateprob(env, &status, "KADAPTABILITY_CUTTING_PLANE");

    TERMINATOR_INNER = 0;
    CPXXsetterminate(env, &TERMINATOR_INNER);



	// temporary
	std::vector<double> qtemp = pInfo->getNominal(), xnom;
    
    //solve_SRO_duality(xstatic);

	//MARK: Qing: can comment out the SRO part from here to 2400, also comment out the MIP start, check later
//	// solve SRO to attempt to get a tighter primal bound
//	if (!solve_SRO_duality(xstatic)) {
//		assert(feasible_KAdaptability(xstatic, K, qtemp));
//		if (xstatic[0] < (xsol.empty() ? +std::numeric_limits<double>::max() : xsol[0])) {
//			xsol = xstatic;
//		}
//	}
//
//
//	// Get initial sample
//	double initialObj = -std::numeric_limits<double>::max();
//
//
//	// Get qtemp from SRO
//	if (!feasible_SRO(xstatic, qtemp)) assert(0);
//	if (!solve_DET(qtemp, xnom)) initialObj = xnom[0];
//	if (!solve_DET(pInfo->getNominal(), xnom)) if (xnom[0] > initialObj) {
//		initialObj = xnom[0];
//		qtemp = pInfo->getNominal();
//	}
//	if (0) if (hasObjectiveUncOnly() && isSecondStageOnly()) {
//		getLowerBound_2(qtemp);
//		assert(getLowerBound_2(qtemp) >= initialObj);
//	}

	// Re-compute qtemp for heuristic
	// if (heuristic_mode && K > 1) getWorstCase(xfix, K - 1, qtemp);

	// Generate initial scenario
	bb_samples.assign(1, qtemp);
    bb_samples_all.assign(1, bb_samples);

	// assign sample to root node
	updateX(env, lp);
	for (unsigned int k = 0; k < K; k++) {
        if(ySol.size()){
            std::vector<double> yInput(ySol.begin() + k * pInfo->getNumSecondStage(), ySol.begin() + (k+1) * pInfo->getNumSecondStage());
            setRobSoly(yInput);
        }
		updateY(env, lp, k);
	}
	updateXQ(env, lp, qtemp);
	updateYQ(env, lp, 0, qtemp);
    
	// set options
	setCPXoptions(env);
	CPXXsetintparam(env, CPXPARAM_ScreenOutput, CPX_OFF);
	CPXXchgprobtype(env, lp, CPXPROB_MILP); // to use callbacks
	CPXXchgobjsen(env, lp, CPX_MIN);


	// callbacks
	CPXXsetintparam(env, CPX_PARAM_MIPSEARCH, CPX_MIPSEARCH_TRADITIONAL);
	CPXXsetintparam(env, CPX_PARAM_MIPCBREDLP, CPX_OFF);
	CPXXsetintparam(env, CPX_PARAM_REDUCE, CPX_PREREDUCE_PRIMALONLY);
	CPXXsetintparam(env, CPX_PARAM_PRELINEAR, CPX_OFF);
    CPXXsetdblparam(env, CPXPARAM_TimeLimit, 600.0);
//    CPXXsetdblparam(env, CPXPARAM_MIP_Tolerances_MIPGap, 0.01);
//    if(K >= 2)
//        CPXXsetdblparam(env, CPXPARAM_MIP_Tolerances_UpperCutoff, bestU);

	if (!hasObjectiveUncOnly() && !BNC_BRANCH_ALL_CONSTR){
		CPXXsetusercutcallbackfunc(env, cutCB_solve_KAdaptability_cuttingPlane, this);
		CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_KAdaptability_cuttingPlane, this);
	}
    CPXXsetlazyconstraintcallbackfunc(env, cutCB_solve_KAdaptability_cuttingPlane, this);
	CPXXsetbranchcallbackfunc(env, branchCB_solve_KAdaptability_cuttingPlane, this);
	CPXXsetincumbentcallbackfunc(env, incCB_solve_KAdaptability_cuttingPlane, this);
	if (heuristic_mode && K > 1) CPXXsetheuristiccallbackfunc(env, heurCB_solve_KAdaptability_cuttingPlane, this);
	CPXXsetdeletenodecallbackfunc(env, deletenodeCB_solve_KAdaptability_cuttingPlane, this);
	if (COLLECT_RESULTS && K > 2) {
		CPXXsetnodecallbackfunc(env, nodeCB_solve_KAdaptability_cuttingPlane, this);
	}

    




	// add MIP start
    // if (!xsol.empty())
	if (false) {
		const unsigned int Kx = getNumPolicies(xsol); assert(Kx);
		const auto x2 = xsol.begin() + pInfo->getNumFirstStage();

		// mip start to be added
		std::vector<double> x_mipstart(xsol);

		// All second-stage policies are identical
		for (unsigned int k = Kx; k < K; k++) {
			x_mipstart.insert(x_mipstart.end(), x2, x2 + pInfo->getNumSecondStage());
		}
		assert(x_mipstart.size() == MY_SIZE_X(K));
		assert(feasible_KAdaptability(x_mipstart, K, Q_TEMP));

		// CPLEX structures
		std::vector<int> effortlevel{CPX_MIPSTART_CHECKFEAS};
		std::vector<CPXNNZ> beg{0};
		std::vector<CPXDIM> varindices(x_mipstart.size(), 0);
		std::vector<char> xctype(x_mipstart.size(), 'C');
		std::iota(varindices.begin(), varindices.end(), 0);
		CPXXgetctype(env, lp, &xctype[0], 0, x_mipstart.size() - 1);

		// pass to CPLEX
		CPXXaddmipstarts(env, lp, 1, x_mipstart.size(), &beg[0], &varindices[0], &x_mipstart[0], &effortlevel[0], NULL);
	}

	// Fix policies for heuristic
	if (heuristic_mode && K > 1) {
		assert(getNumPolicies(xfix) + 1 == K);
		std::vector<char> xctype(CPXXgetnumcols(env, lp), 'C');
		CPXXgetctype(env, lp, &xctype[0], 0, xctype.size() - 1);
		for (int init = pInfo->getNumFirstStage(), i = init; i < (int)xfix.size(); i++) {
			double value = xfix[i];
			if (xctype[i] == CPX_BINARY) {
				if (std::abs(value - 0.0) <= 1.E-5) value = 0;
				if (std::abs(value - 1.0) <= 1.E-5) value = 1.0;
			}
			fixVariable(env, lp, i + pInfo->getNumSecondStage(), value);
		}
	}

	// (time, incumbent) data
	ZT_VALUES.clear();
	NUM_DUMMY_NODES = TX = 0;
	START_TS = get_wall_time();
	BOUND_VALUES.clear();


    


    

    
	double start_time = get_wall_time();
    std::cout << "Strat to evaluate. \n\n";
	// solve problem
	status = CPXXmipopt(env, lp);
	if (!status) {
		solstat = CPXXgetstat(env, lp);
//		if (solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL) {
//			solstat = 0;
//		}
		x.resize(MY_SIZE_X(K));
		if (CPXXgetx(env, lp, &x[0], 0, x.size() - 1) == 0) {
			assert(feasible_KAdaptability(x, K, Q_TEMP));
		}
        // ???: Qing use upper bound or lower bound for the problem hit time limitation?
        // MARK: should be lower bound
        if (solstat == CPXMIP_TIME_LIM_FEAS)
            CPXXgetbestobjval(env, lp, &x[0]);
        if (solstat == CPXMIP_OPTIMAL_TOL)
            CPXXgetobjval(env, lp, &x[0]);

	}
	else {
		MYERROR(status);
	}

	double end_time = get_wall_time();
    
    
	// Resize xfix for heuristic
//	if (heuristic_mode) {
//		xfix = this->xsol;
//		resizeX(xfix, K);
//	}


	
	// get results
	if (COLLECT_RESULTS) {
		double total_solution_time = end_time - start_time;
		double global_lb; CPXXgetbestobjval(env, lp, &global_lb);
		double final_gap = INFINITY, final_objval = INFINITY;
		if (CPXXgetobjval(env, lp, &final_objval) == 0) {
			final_gap = 100*(final_objval - global_lb)/(1E-10 + std::abs(final_objval));
		}
		


		size_t pos_keyword = 0, pos_dash = 0;
		const std::string s = pInfo->getSolFileName();
		const std::string problemType(s, 0, 3);
		pos_keyword = s.find_first_of("n", 3);
		pos_dash = s.find_first_of("-", pos_keyword);
		const std::string n(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
		pos_keyword = s.find_first_of("s", pos_dash);
		pos_dash = s.find_first_of("-.opt", pos_keyword);
		const std::string seed(s, pos_keyword + 1, pos_dash - pos_keyword - 1);
		std::string stat = "Unknown";
		if (solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL) stat = "Optimal";
		if (solstat == CPXMIP_INFEASIBLE || solstat == CPXMIP_INForUNBD) stat = "Infeas";
		if (solstat == CPXMIP_TIME_LIM_FEAS || solstat == CPXMIP_TIME_LIM_INFEAS) stat = "Time Lim";
		if (solstat == CPXMIP_MEM_LIM_FEAS || solstat == CPXMIP_MEM_LIM_INFEAS) stat = "Mem Lim";
        if (solstat == CPXMIP_ABORT_FEAS || solstat == CPXMIP_ABORT_INFEAS) stat = "UB Cutoff";
		if (heuristic_mode) stat = "Heur";
		write(std::cout, n, K, seed, stat, final_objval, total_solution_time, final_gap);


		
	}
    
    // comment for testing the new relaxation
//    q.resize(K);
//    for(uint k = 0; k < K; k++){
//        for(auto l : final_labels[k])
//            q[k].insert(q[k].end(), bb_samples_all[l].begin(), bb_samples_all[l].end());
//    }
    if(!roSol.size() && bb_samples_all.size() && final_labels.size())
        if(solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL || solstat == CPXMIP_TIME_LIM_FEAS){
        
        unsigned int totalSamples = 0;
        unsigned int totalSamplesNeed = 10;
        for(uint k = 0; k < K; k++){
            for(auto l : final_labels[k]){
                totalSamples += bb_samples_all[l].size();
            }
        }
        
        for(uint k = 0; k < K; k++){
            for(auto l : final_labels[k]){
                unsigned int sampleNeed = bb_samples_all[l].size() * totalSamplesNeed / totalSamples;
                std::vector<std::vector<double>> sampleL;
                unsigned int sampleNow = 0;
                // find the fixed number of repeated samples
                for(auto s : bb_samples_all[l]){
                    if(sampleNow <= sampleNeed){
                        bool equal = false;
                        for(auto ls : sampleL){
                            equal = std::equal(ls.begin(), ls.end(), s.begin(),
                                                    [](double value1, double value2)
                                                    {
                                                        constexpr double epsilon = 0.01;
                                                        return std::fabs(value1 - value2) < epsilon;
                                                    });
                            if (equal)
                                break;
                        }
                        if (!equal){
                            sampleNow++;
                            sampleL.emplace_back(s);
                        }
                    }
                }
                q.insert(q.end(), sampleL.begin(), sampleL.end());
            }
        }
    }
    
    // clear solutions
    // xsol.clear();
    xstatic.clear();
    xfix.clear();

	// clear samples
	bb_samples.clear();
    bb_samples_all.clear();
    final_labels.clear();
    
	// clear (time, incumbent) data
	ZT_VALUES.clear();

	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	return solstat;
}

//-----------------------------------------------------------------------------------

bool KAdaptableSolver::solve_separationProblem(const std::vector<double>& x, const unsigned int K, int& label, bool heur) {
    
    if (SEPARATE_FROM_SAMPLES ? (!feasible_YQ(x, K, bb_samples, label, heur)) : false) {
		assert(label < static_cast<int>(bb_samples.size()));
		return true;
	}

	if (heur) return false;

	if (!feasible_YQ(x, K, Q_TEMP)) {
        //MARK: Qing: only take \bar \xi out
        if(DECISION_DEPENDENT)
            Q_TEMP = std::vector<double>(Q_TEMP.begin(), Q_TEMP.begin()+getUncSet()->getNoOfUncertainParameters()+1);
		bb_samples.emplace_back(Q_TEMP);
        bb_samples_all.emplace_back(std::vector<std::vector<double>>{});
        
		label = static_cast<int>(bb_samples.size()) - 1;
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------------

int KAdaptableSolver::solve_KAdaptability_minMaxMin(const unsigned int K, std::vector<double>& x) {
	assert(K > 0);
	assert(pInfo);
	assert(pInfo->hasObjectiveUncOnly());
	assert(pInfo->isSecondStageOnly());
	assert(pInfo->getConstraintsXYQ()[0].size() == 1);
	if (!pInfo->hasObjectiveUncOnly()) return 1;
	if (!pInfo->isSecondStageOnly()) return 1;
	if (pInfo->getConstraintsXYQ()[0].size() != 1) return 1;
	if (solve_SRO_duality(xstatic)) MYERROR(1);


	// Algorithm 1 from Buccheim & Kurtz, Math Prog. (2017)
	int status;
	unsigned int i = 0;
	double zstar = +std::numeric_limits<double>::max();
	std::vector<double> qstar = pInfo->getNominal(), xtemp, xm3{xstatic[0] + 1.0}, lambda;
	do {
		// line 3
		if (i) {
			pInfo->resize(i);
			zstar = getWorstCase(xm3, i, qstar);
		}

		// line 4
		status = solve_DET(qstar, xtemp);
		if (status) MYERROR(status);

		// line 5
		i++;
		xm3.insert(xm3.end(), xtemp.begin() + 1, xtemp.end());
		assert(xm3.size() == MY_SIZE_X(i));
	} while (xtemp[0] < zstar - EPS_INFEASIBILITY_Q);


	////////////////////////////////////////////////////////////////////////
	// line 7                                                             //
	// Note that the paper is a bit vague                                 //
	// The true problem to be solved is as follows:                       //
	//         find a continuous solution x* = conv(x[j], j = 1,...,i)    //
	//         such that the worst-case objective evaluated at x* = z*    //
	////////////////////////////////////////////////////////////////////////
	CPXENVptr env = NULL;
	CPXLPptr lp = NULL;
	env = CPXXopenCPLEX(&status);
	lp = CPXXcreateprob(env, &status, "MIN_MAX_MIN");
	addVariable(env, lp, 'C', zstar, zstar, 0.0, "zstar");
	for (int n = 1; n <= pInfo->getNumSecondStage(); n++) {
		addVariable(env, lp, 'C', -CPX_INFBOUND, +CPX_INFBOUND, 0.0, "xstar(" + std::to_string(n) + ")");
	}
	assert(CPXXgetnumcols(env, lp) == (int)xtemp.size());
	for (unsigned int j = 0; j < i; j++) {
		addVariable(env, lp, 'C', 0.0, +CPX_INFBOUND, 0.0, "lambda(" + std::to_string(j) + ")");
	}
	ConstraintExpression constraint;
	constraint.rowname("convex");
	constraint.sign('E');
	constraint.RHS(1.0);
	for (unsigned int j = 0; j < i; j++) {
		constraint.addTermX(xtemp.size() + j, 1.0);
	}
	constraint.addToCplex(env, lp);
	for (int n = 1; n <= pInfo->getNumSecondStage(); n++) {
		constraint.clear();
		constraint.RHS(0);
		constraint.sign('E');
		constraint.rowname("xdef(" + std::to_string(n) + ")");
		for (unsigned int j = 0; j < i; j++) {
			double coef = getXPolicy(xm3, i, j)[n];
			if (coef != 0.0) constraint.addTermX(xtemp.size() + j, coef);
		}
		if (constraint.isEmpty()) {
			fixVariable(env, lp, n, 0);
		}
		else {
			constraint.addTermX(n, -1.0);
			constraint.addToCplex(env, lp);
		}
	}
	// This is where we add the worst-case constraint
	pInfo->getConstraintsXYQ()[0][0].addToCplex(env, lp, &pInfo->getUncSet(), true);


	// set options
	setCPXoptions(env);
	CPXXchgprobtype(env, lp, CPXPROB_LP);
	CPXXchgobjsen(env, lp, CPX_MIN);

	// solve problem
	status = CPXXdualopt(env, lp);
	if (status == 0 && CPXXgetstat(env, lp) == CPX_STAT_OPTIMAL) {
		lambda.resize(i);
		CPXXgetx(env, lp, &lambda[0], xtemp.size(), xtemp.size() + i - 1);
		assert(std::abs(std::accumulate(lambda.begin(), lambda.end(), 0.0) - 1.0) <= EPS_INFEASIBILITY_X);
	}
	else {
		MYERROR((int)(status ? status : CPXXgetstat(env, lp)));
	}

	// Free memory
	CPXXfreeprob(env, &lp);
	CPXXcloseCPLEX (&env);

	// Get K largest lambda
	x.assign(1, xstatic[0] + 1.0);
	std::vector<unsigned int> order(i, 0);
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](const unsigned int& a, const unsigned int& b) {
		return lambda[b] < lambda[a];
	});
	for (unsigned int j = 0; j < std::min(K, i); j++) {
		// std::cout << j << " ---> " << lambda[order[j]] << "\n";
		const auto xk = getXPolicy(xm3, i, order[j]);
		x.insert(x.end(), xk.begin() + 1, xk.end());
	}
	if (i <= K) {
		pInfo->resize(K);
		resizeX(x, K);
	}
	x[0] = getWorstCase(x, K, qstar);

	return status;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------

/**
 * Data structure to be attached to every node of the branch-and-bound tree
 * to implement K-ary branching
 *
 * The structure keeps track of whether the node is a 'dummy node'
 * as well scenario information to complete the branching step at descendant nodes
 *
 * There will always be max(K-2, 0) dummy nodes created at every branching step
 *
 * For example, 3-way branching will be implemented as follows:
 *	   N
 *	 +-+-+
 *	c3  d1
 *	   +-+-+
 *	  c2  c1
 * where N denotes the current node to be branched on,
 *       c1, c2, c3 denote the 3 children nodes we wish to create
 *       d1 denotes a dummy node which will be further branched to create nodes c1 and c2
 *
 * Dummy nodes are created without defining any additional branching constraints
 */
struct CPLEX_CB_node {
	/**
	 * Measures true depth of nodes -- for K <= 2, this must be equal to depth returned by CPLEX
	 */
	unsigned int trueDepth;

	/**
	 * Indicates that the last branching decision was a CPLEX 0-1 branching decision
	 */
	bool br_flag;

	/**
	 * Indicates node be immediately branched on -- recall that CPLEX does not support multi-way branching
	 */
	bool isDummy;

	/**
	 * Node is a direct descendant of incumbent CB - accept or reject by branching accordingly
	 */
	bool fromIncumbentCB;

	/** 
	 * For dummy nodes -- scenario label to be attached to children nodes
	 * From incumbent CB -- if feasible then -1, otherwise equal to violating scenario label
	 * For other cases it does not have any meaning
	 */
	int br_label;

	/**
	 * Makes sense only for dummy nodes -- objective value predicted for children nodes
	 */
	double nodeObjective;

	/**
	 * Makes sense only for dummy nodes -- # of "true" children nodes of this node
	 */
	unsigned int numNodes;

	/**
	 * # of active policies in this node
	 */
	unsigned int numActivePolicies;

	/**
	 * [k] = Scenario labels for policy k in this node -- to be used in cut and lazy constraint callbacks
	 * if objective uncertainty then always empty (for efficiency reasons), otherwise has size K
	 */
	std::vector<std::vector<int> > labels;
    
    std::vector<double> x;
};

//-----------------------------------------------------------------------------------

static int CPXPUBLIC cutCB_solve_SRO_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p) {

	enterCallback(cutCB_solve_SRO_cuttingPlane);

	// Get K-Adaptability solver
	auto S = static_cast<KAdaptableSolver*>(cbhandle);

	// Get # of columns
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
	CPXDIM numcols = CPXXgetnumcols(env, lp);

	// get node solution
	std::vector<double> x(numcols); CPXXgetcallbacknodex(env, cbdata, wherefrom, &x[0], 0, numcols - 1);
	std::vector<double> q;
	*useraction_p = CPX_CALLBACK_DEFAULT;


	// violated cuts
	CPXDIM rcnt = 0;
	CPXNNZ nzcnt;
	std::vector<double> rhs;
	std::vector<char> sense;
	std::vector<CPXNNZ> rmatbeg;
	std::vector<CPXDIM> rmatind;
	std::vector<double> rmatval;
    
//    if(!DECISION_DEPENDENT){
        // check constraints (x, q)
        if (!S->feasible_XQ(x, q)) {

            // get all constraints
            S->getXQ_fixedQ(q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
            //MARK: Qing: add sample for inner cutting plane method
            S->inner_samples.emplace_back(q);
            
        }
        else if (!S->feasible_YQ(x, 1, q)) {
            // get all constraints
            S->getYQ_fixedQ(0, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
            S->inner_samples.emplace_back(q);
        }
        else {
            assert(S->feasible_SRO(x, q));
        }

        // Add only the most violated constraint
        double maxViol = 0;
        double rhs_cut = 0;
        char sense_cut = 'L';
        std::vector<int> cutind;
        std::vector<double> cutval;
        for (CPXDIM i = 0; i < rcnt; i++) {

            // coefficients and indices of this constraint
            CPXDIM next = (i + 1 == rcnt) ? nzcnt : rmatbeg[i+1];
            std::vector<int> cutind_i(rmatind.begin() + rmatbeg[i], rmatind.begin() + next);
            std::vector<double> cutval_i(rmatval.begin() + rmatbeg[i], rmatval.begin() + next);

            // check violation
            const auto W = checkViol(env, cbdata, wherefrom, rhs[i], sense[i], cutind_i, cutval_i);
            if (W.first > maxViol) {
                maxViol = W.first;
                rhs_cut = rhs[i];
                sense_cut = sense[i];
                cutind = cutind_i;
                cutval = cutval_i;
            }
        }

        // Add local cut to be consistent with K-Adaptability implementation
        if (maxViol > EPS_INFEASIBILITY_Q) {
            CPXXcutcallbackaddlocal(env, cbdata, wherefrom, cutind.size(), rhs_cut, sense_cut, &cutind[0], &cutval[0]);//, CPX_USECUT_FORCE);
            *useraction_p = CPX_CALLBACK_SET;
        }
//    }
//    // check constraints (x, y, q)
//    //MARK: Qing: add sample for inner cutting plane method
//    else {
//        q = getViolation(*S->inner_obj, S->getUncSet(), x);
//        if(q[0] > EPS_INFEASIBILITY_Q){
//            rmatbeg.emplace_back(rmatind.size());
//            S->inner_samples.emplace_back(q);
//            rhs.emplace_back(0.0);
//            sense.emplace_back('L');
//            S->inner_obj->getDeterministicConstraint( q, nzcnt, rhs.back(), sense.back(), rmatind, rmatval);
//            CPXXcutcallbackaddlocal(env, cbdata, wherefrom, nzcnt, rhs[0], sense[0], &rmatind[0], &rmatval[0]);//, CPX_USECUT_FORCE);
//            *useraction_p = CPX_CALLBACK_SET;
//        }
//    }
    
	exitCallback(cutCB_solve_SRO_cuttingPlane);
}

//-----------------------------------------------------------------------------------

static void CPXPUBLIC deletenodeCB_solve_KAdaptability_cuttingPlane(CPXCENVptr, int, void *, CPXCNT, void *handle) {
	if (handle) {
		auto oldInfo = static_cast<CPLEX_CB_node*>(handle);
		delete oldInfo;
	}
}

//-----------------------------------------------------------------------------------

static int CPXPUBLIC incCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, double objval, double *x_, int *isfeas_p, int *useraction_p) {
	enterCallback(inc);
	*useraction_p = CPX_CALLBACK_SET;
    
	// Get K-Adaptability solver
	auto S = static_cast<KAdaptableSolver*>(cbhandle);
	const unsigned int K = S->NK;
    
    // Get current lower bound
    double lb = 0; CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_BEST_REMAINING, &lb);
    if(lb >= S->bestU)
        TERMINATOR_INNER = 1;

	// Get node data	
	void *nodeData = NULL;


	// Get node solution
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
    
    CPXLPptr nodelp = NULL; CPXXgetcallbacknodelp(env, cbdata, wherefrom, &nodelp);
    
	CPXDIM numcols = CPXXgetnumcols(env, lp);
	std::vector<double> x(x_, x_ + numcols);
	int label = 0;
    
    // CPXXwriteprob(env, nodelp, "/Users/lynn/Desktop/research/DRO/BnB/model_output/inc", "LP");
    
	// Solution must be structurally feasible
	// assert(S->feasible_DET_K(x, K, X_TEMP));
	// assert(x == X_TEMP);


	///////////////////////////////////////////////////////////////////////////
	// NOTE -- I AM ASSUMING THAT ALL CONSTRAINTS (X,Q) HAVE BEEN DUALIZED!! //
	///////////////////////////////////////////////////////////////////////////
	// assert(S->feasible_XQ(x, Q_TEMP));


	// Exit if node is a dummy node
	if (wherefrom == CPX_CALLBACK_MIP_INCUMBENT_NODESOLN) {
        // ---------- Qing
        //int seqnum;
        //int numiinf;
        //CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_SEQNUM, &seqnum);
        //CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_NIINF, &numiinf);
        
		CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);
		if (nodeData) if (static_cast<CPLEX_CB_node*>(nodeData)->isDummy) {
			assert(K > 2);
			assert(static_cast<CPLEX_CB_node*>(nodeData)->numNodes > 1);
			assert(static_cast<CPLEX_CB_node*>(nodeData)->numNodes < K);
			assert(!static_cast<CPLEX_CB_node*>(nodeData)->fromIncumbentCB);
			// assert(!S->feasible_YQ(x, K, Q_TEMP));
			*isfeas_p = 0;
			exitCallback(inc);
		}
	}



	// Check feasibility
	*isfeas_p = !S->solve_separationProblem(x, K, label);
	if (*isfeas_p) {
		S->setX(x, K);
        CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);
        if (nodeData){
            S->final_labels = static_cast<CPLEX_CB_node*>(nodeData)->labels;
        }
        
		if (COLLECT_RESULTS) ZT_VALUES.emplace_back(objval, get_wall_time());
	}


	// Tag this node -- to be identified in branch CB
	if (wherefrom == CPX_CALLBACK_MIP_INCUMBENT_NODESOLN) {
//        for(auto xx: x)
//            std::cout << xx << ", ";
//        std::cout << std::endl;
		#ifndef NDEBUG
		CPXLONG depth = 0; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_DEPTH_LONG, &depth);
		#endif
		CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);

		// new node data to be attached
		CPLEX_CB_node* newInfo = new CPLEX_CB_node;
		newInfo->trueDepth = 0;
		newInfo->br_flag = 0;
		newInfo->isDummy = 0;
		newInfo->fromIncumbentCB = 1;
		newInfo->br_label = (*isfeas_p ? -1 : label);
		newInfo->nodeObjective = objval;
		newInfo->numNodes = 0;
        newInfo->x = x;
		newInfo->numActivePolicies = (S->heuristic_mode ? K : 1);
		if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
			newInfo->labels.clear();
		} else {
			newInfo->labels.assign(K, std::vector<int>{});
			newInfo->labels[0].emplace_back(0);
		}

		CPXXcallbacksetuserhandle(env, cbdata, wherefrom, newInfo, &nodeData);
        
        // std::cout << "Incumbent node with label" <<newInfo->br_label <<std::endl;
        
		// delete old data
		if (nodeData) {
			auto oldInfo = static_cast<CPLEX_CB_node*>(nodeData);
			newInfo->trueDepth = oldInfo->trueDepth;
			assert((K > 2) || (oldInfo->trueDepth == depth));
			assert(!oldInfo->isDummy);
			assert(oldInfo->numActivePolicies <= K);
			assert(oldInfo->labels.size() == ( (S->hasObjectiveUncOnly()&& !DECISION_DEPENDENT) ? 0 : K));
			newInfo->numActivePolicies = oldInfo->numActivePolicies;
			newInfo->labels = oldInfo->labels;
			delete oldInfo;
		}
		// no data here -- can only happen for root node
		// This is the first 'tag' of the B&B tree
		else {
			assert(depth == 0);
		}
	}

	exitCallback(inc);
}

//-----------------------------------------------------------------------------------

static int CPXPUBLIC heurCB_solve_KAdaptability_cuttingPlane (CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, double *objval_p, double *x_, int *checkfeas_p, int *useraction_p) {
	enterCallback(heur);

	// Get K-Adaptability solver
	auto S = static_cast<KAdaptableSolver*>(cbhandle);
	const unsigned int K = S->NK;


	// Get node solution
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
	CPXDIM numcols = CPXXgetnumcols(env, lp);
	std::vector<double> x(x_, x_ + numcols);


	// Check integer feasibility
	std::vector<int> feas(numcols, 0); CPXXgetcallbacknodeintfeas(env, cbdata, wherefrom, &feas[0], 0, numcols - 1);
	if (std::accumulate(feas.begin(), feas.end(), 0) == 0) {
		double ub = 0; CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_BEST_INTEGER, &ub);
		x[0] = ub;
		x[0] = S->getWorstCase(x, K, Q_TEMP);

		// worst-case objective value is less than current incumbent
		if (x[0] < ub - EPS_INFEASIBILITY_X) {
			x_[0] = x[0] + EPS_INFEASIBILITY_X;
			*objval_p = x_[0];
			*checkfeas_p = 1;
			*useraction_p = CPX_CALLBACK_SET;
		}
		else {
			*useraction_p = CPX_CALLBACK_DEFAULT;
		}
	}

	exitCallback(heur);
}

//-----------------------------------------------------------------------------------

static int CPXPUBLIC branchCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int type, CPXDIM, int nodecnt, CPXDIM bdcnt, const CPXDIM *nodebeg, const CPXDIM *indices, const char *lu, const double *bd, const double *nodeest, int *useraction_p) {
	enterCallback(branch);
	*useraction_p = CPX_CALLBACK_DEFAULT;

	// Get K-Adaptability solver
	auto S = static_cast<KAdaptableSolver*>(cbhandle);
	const unsigned int K = S->NK;


	// Get node data	
	void *nodeData = NULL; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);
	CPXLONG depth = 0;     CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_DEPTH_LONG, &depth);
	double nodeobjval = 0; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_OBJVAL, &nodeobjval);
	int existsfeas = 0;    CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_MIP_FEAS,       &existsfeas);
	double bestinteger = 0;CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_BEST_INTEGER,   &bestinteger);

	double gap = +std::numeric_limits<double>::max();
	if (existsfeas) {
		gap = 100*(bestinteger - nodeobjval)/(1E-10 + std::abs(bestinteger));
	}
	if (COLLECT_RESULTS) {
		double time_elapsed = get_wall_time() - START_TS;
		if (time_elapsed > (double)TX*10) {
			double lb = 0; CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_BEST_REMAINING, &lb);
			BOUND_VALUES.emplace_back(lb, (existsfeas ? bestinteger : 0));
			TX++;
		}
	}


	// Get node solution
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
	CPXDIM numcols = CPXXgetnumcols(env, lp);
	std::vector<double> x(numcols); CPXXgetcallbacknodex(env, cbdata, wherefrom, &x[0], 0, numcols - 1);
//    CPXLPptr nodelp = NULL; CPXXgetcallbacknodelp(env, cbdata, wherefrom, &nodelp);
//    CPXXwriteprob(env, nodelp, "/Users/lynn/Desktop/research/DRO/BnB/model_output/node", "LP");

	///////////////////////////////////////////////////////////////////////////
	// NOTE -- I AM ASSUMING THAT ALL CONSTRAINTS (X,Q) HAVE BEEN DUALIZED!! //
	///////////////////////////////////////////////////////////////////////////
	// assert(S->feasible_XQ(x, Q_TEMP));









	
	


	///////////////////////////////////////////////////////////////////
	// DECIDE WHETHER TO RUN THE SEPARATION PROBLEM IN THE BRANCH CB //
	///////////////////////////////////////////////////////////////////
	// non-zero value for label will indicate
	// the sequence number of violating scenario to branch upon
    int label = 0;
	bool compute_separation = false;
	bool possible_branching = false;

    bool isDummy = false;
    
	/////////////////////////////////
	// CASE 1: NO NODE DATA EXISTS //
	/////////////////////////////////
	if (!nodeData) {
		// NOTE: this can only happen for root node
		// NOTE: this cannot be from an incumbent CB -- because the latter always tags each node
		assert(depth == 0);
		compute_separation = (BRANCHING_STRATEGY >= 2);
	}
	//////////////////////////////////////
	// CASE 2: NODE DATA ALREADY EXISTS //
	//////////////////////////////////////
	else {
		auto oldInfo = static_cast<CPLEX_CB_node*>(nodeData);
		assert((K > 2) || (oldInfo->trueDepth == depth));
		assert(oldInfo->numActivePolicies >= 1);
		assert(oldInfo->numActivePolicies <= K);
		assert(oldInfo->labels.size() == ((S->hasObjectiveUncOnly()&& !DECISION_DEPENDENT) ? 0 : K));

        if (oldInfo->fromIncumbentCB)
            x = oldInfo->x;
		// if the incumbent CB rejected the solution and
		// if the label suggested does not actually violate x
		// then re-compute a new label
		if (oldInfo->fromIncumbentCB && oldInfo->br_label >= 0) {
			// check for infeasibility with this label or re-compute
			std::vector<std::vector<double> > qcheck{S->bb_samples.at(oldInfo->br_label)};
			compute_separation = S->feasible_YQ(x, K, qcheck, LABEL_TEMP);
			if (!compute_separation) label = oldInfo->br_label;
            //Qing add
            //std::cout << "cb, " << label << std::endl;
		}

		
		// if the node is not a dummy node and
		// if the node is not coming directly from the incumbent CB
		// then attempt to branch as per k-adaptability
		if (!oldInfo->isDummy && !oldInfo->fromIncumbentCB) {
			switch (BRANCHING_STRATEGY) {
				case 1: compute_separation = false; break;
				case 2: compute_separation = oldInfo->br_flag; break;
				case 3: compute_separation = (gap > BNC_GAP_VALUE); break;
				case 4: compute_separation = true; break;
				case 5: compute_separation = ((K == 1) ? 1 : !((int)oldInfo->trueDepth % (1 + (int)std::ceil(K/2)))); break;
			}
			if (compute_separation) possible_branching = true;
		}
        isDummy = oldInfo->isDummy;
	}
    
    CPXLONG seqnum1; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_SEQNUM_LONG, &seqnum1);
    
	/////////////////////////////////////////////
	// SOLVE THE SEPARATION PROBLEM FOR THIS X //
	/////////////////////////////////////////////
	if (compute_separation) {
		if (S->solve_separationProblem(x, K, label, possible_branching)) {
//            std::cout << "check: ";
//            for(auto xx: x)
//                std::cout << xx << ", ";
//            std::cout << std::endl;
            //std::cout << "Find violated scenario:" << label << std::endl;
			assert(label);
		}
		else {
			label = 0;

            
			// if the incumbent CB rejected the solution,
			// then we must be able to branch as per k-adaptability
			#ifndef NDEBUG
			if (nodeData) {
				CPXINT numiinf; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_NIINF, &numiinf) ;
				auto oldInfo = static_cast<CPLEX_CB_node*>(nodeData);
				assert(!(oldInfo->fromIncumbentCB && oldInfo->br_label >= 0) || numiinf);
                //if (!(oldInfo->fromIncumbentCB && oldInfo->br_label >= 0))
                  //  std::cout << "CPLEX branch:" << seqnum1 << ", "<< oldInfo->fromIncumbentCB << ", " << (oldInfo->br_label >= 0) << ", " << numiinf << ", " << oldInfo->isDummy << std::endl;
			}
			#endif
		}
	}



    
    
    //std::cout << compute_separation << ", " << seqnum1 << ", " << label << ", "<< isDummy << std::endl;






	///////////////////////////////////////////////////////////////
	// CHECK IF IT IS WORTH TRYING OUT THE K-ADAPTABILITY BRANCH //
	///////////////////////////////////////////////////////////////
	if (possible_branching && label) {
		// don't use K-Adaptability branch if CPLEX is proposing something 'unique'
		if (type != CPX_TYPE_VAR) label = 0;
	}
	if (BNC_DO_STRONG_BRANCH && gap <= BNC_GAP_VALUE) if (possible_branching && label) {
		assert(nodecnt);
		int status = 0;
		CPXLPptr nodelp = NULL; CPXXgetcallbacknodelp(env, cbdata, wherefrom, &nodelp);

		// don't mess with original nodelp -- remember to free memory at the end
		CPXLPptr lpcopy = CPXXcloneprob(env, nodelp, &status);

		// get current basis
		std::vector<int> cstat(CPXXgetnumcols(env, nodelp));
		std::vector<int> rstat(CPXXgetnumrows(env, nodelp));
		status = CPXXgetbase(env, nodelp, &cstat[0], &rstat[0]);

		// get K-Adaptability branches
		std::vector<CPXNNZ> rmatbeg_cut(K, 0);
		std::vector<double> rhs_cut(K, 0);
		std::vector<char> sense_cut(K, 'L');
		std::vector<std::vector<int> > cutind(K);
		std::vector<std::vector<double> > cutval(K);
        
        // violated constraints
        CPXDIM rcnt = 0;
        CPXNNZ nzcnt;
        std::vector<double> rhs;
        std::vector<char> sense;
        std::vector<CPXNNZ> rmatbeg;
        std::vector<CPXDIM> rmatind;
        std::vector<CPXDIM> rmatind_dd;
        std::vector<double> rmatval;
        
        if(DECISION_DEPENDENT)
            S->getRobustYQ_fixedQ(S->bb_samples[label], rcnt, nzcnt, rhs, sense, rmatbeg, rmatind_dd, rmatval);;
        
		for (unsigned int k = 0; k < K; k++) {
			auto xk = S->getXPolicy(x, K, k);

//			// violated constraints
//			CPXDIM rcnt = 0;
//			CPXNNZ nzcnt;
//			std::vector<double> rhs;
//			std::vector<char> sense;
//			std::vector<CPXNNZ> rmatbeg;
//			std::vector<CPXDIM> rmatind;
//			std::vector<double> rmatval;
            
            if(DECISION_DEPENDENT){
                if(K == 1)
                    rmatind = rmatind_dd;
                else
                    rmatind = S->mapK(k, rmatind_dd);
            }
            else
                S->getYQ_fixedQ(k, S->bb_samples[label], rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);

			// Add only the most violated constraint
			double maxViol = 0;
			for (CPXDIM i = 0; i < rcnt; i++) {

				// coefficients and indices of this constraint
				CPXDIM next = (i + 1 == rcnt) ? nzcnt : rmatbeg[i+1];
				std::vector<int> cutind_i(rmatind.begin() + rmatbeg[i], rmatind.begin() + next);
				std::vector<double> cutval_i(rmatval.begin() + rmatbeg[i], rmatval.begin() + next);

				// check violation
				const auto W = checkViol(env, cbdata, wherefrom, rhs[i], sense[i], cutind_i, cutval_i);
				if (W.first > maxViol) {
					maxViol = W.first;
					rhs_cut[k] = rhs[i];
					sense_cut[k] = sense[i];
					cutind[k] = cutind_i;
					cutval[k] = cutval_i;
				}
			}

			// Violation must exist
			assert (maxViol > EPS_INFEASIBILITY_Q);
		}


		// EVALUATE CPLEX branches first
		double minChildrenBound_CPLEX = +std::numeric_limits<double>::max();
		double maxChildrenBound_CPLEX = -std::numeric_limits<double>::max();
		for (int i = 0; i < nodecnt; i++) {

			// copy basis
			if (!status) CPXXcopybase(env, lpcopy, &cstat[0], &rstat[0]);

			// add CPLEX sepecified rows
			CPXDIM numrowsadded = 0;
			for (CPXDIM j = nodebeg[i], next = ((i + 1 == nodecnt) ? bdcnt : nodebeg[i+1]); j < next; j++) {
				ConstraintExpression bound;
				bound.addTermX(indices[j], 1);
				bound.RHS(bd[j]);
				switch (lu[j]) {
					case 'L': bound.sign('G'); break;
					case 'U': bound.sign('L'); break;
					case 'B': bound.sign('E'); break;
				}
				bound.addToCplex(env, lpcopy);
				numrowsadded++;
			}

			// solve the LP
			CPXXdualopt(env, lpcopy);
			if (CPXXgetstat(env, lpcopy) == CPX_STAT_OPTIMAL) {
				double objval; CPXXgetobjval(env, lpcopy, &objval);
				objval = objval - nodeobjval;
				if (objval < minChildrenBound_CPLEX) {
					minChildrenBound_CPLEX = objval;
				}
				if (objval > maxChildrenBound_CPLEX) {
					maxChildrenBound_CPLEX = objval;
				}
			}

			// Delete last few rows
			if (numrowsadded) CPXXdelrows(env, lpcopy, rstat.size(), (CPXDIM)rstat.size() + numrowsadded - 1);
		}


		// NEXT, evaluate K-Adaptability branches
		double minChildrenBound_KAd = +std::numeric_limits<double>::max();
		double maxChildrenBound_KAd = -std::numeric_limits<double>::max();
		for (unsigned int k = 0; k < K; k++) {

			// copy basis
			if (!status) CPXXcopybase(env, lpcopy, &cstat[0], &rstat[0]);

			// add K-Adaptability branches
			CPXXaddrows(env, lpcopy, 0, 1, cutind[k].size(), &rhs_cut[k], &sense_cut[k], &rmatbeg_cut[k], &cutind[k][0], &cutval[k][0], NULL, NULL);

			// solve the LP
			CPXXdualopt(env, lpcopy);
			if (CPXXgetstat(env, lpcopy) == CPX_STAT_OPTIMAL) {
				double objval; CPXXgetobjval(env, lpcopy, &objval);
				objval = objval - nodeobjval;
				if (objval < minChildrenBound_KAd) {
					minChildrenBound_KAd = objval;
				}
				if (objval > maxChildrenBound_KAd) {
					maxChildrenBound_KAd = objval;
				}
			}

			// Delete last row
			CPXXdelrows(env, lpcopy, rstat.size(), rstat.size());
		}


		// free allocated memory
		CPXXfreeprob(env, &lpcopy);


		
		// compute CPLEX score
		double mu = 0.5;
		double score_CPLEX = (mu * minChildrenBound_CPLEX) + ((1 - mu) * maxChildrenBound_CPLEX);
		double score_KAd = (mu * minChildrenBound_KAd) + ((1 - mu) * maxChildrenBound_KAd);

		static int count1 = 0;
		if (score_CPLEX >= score_KAd) label = 0;
		else {
			if (++count1%1000 == 0) std::cout << count1 << "\n";
		}
	}

















	////////////////////////////////////////////////////////////////////////
	// COMPUTE NEW VALUES FOR NODE DATA TO BE ATTACHED TO CHILDREN NODES  //
	// THIS DATA WILL BE ATTACHED IRRESPECTIVE OF THE BRANCHING RULE USED //
	////////////////////////////////////////////////////////////////////////
	CPXCNT seqnum = 0;
	unsigned int k_max = K;
	CPLEX_CB_node *newInfo0 = NULL;
	CPLEX_CB_node *newInfo1 = NULL;
	
	/////////////////////////////////
	// CASE 1: NO NODE DATA EXISTS //
	/////////////////////////////////
	if (!nodeData) {		
		// no option but to branch as CPLEX would
		if (!label) {
			for (int i = 0; i < nodecnt; i++) {
				CPLEX_CB_node *newInfo = new CPLEX_CB_node;
				newInfo->trueDepth = depth + 1;
				newInfo->br_flag = 1;
				newInfo->isDummy = 0;
				newInfo->fromIncumbentCB = 0;
				newInfo->br_label = 0;
				newInfo->nodeObjective = nodeest[i];
				newInfo->numNodes = 0;
				newInfo->numActivePolicies = 1;
				if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
					newInfo->labels.clear();
				} else {
					newInfo->labels.assign(K, std::vector<int>{});
					newInfo->labels[0].emplace_back(0);
				}
				CPXXbranchcallbackbranchasCPLEX(env, cbdata, wherefrom, i, newInfo, &seqnum);
			}
			*useraction_p = CPX_CALLBACK_SET;
			exitCallback(branch);
		}


		// create your own branches as per k-adaptability (heuristic_mode only)
		else if (S->heuristic_mode && K > 1) {
			k_max = K - 1;

			// Node data for policy 0 branch
			newInfo1 = new CPLEX_CB_node;
			newInfo1->trueDepth = depth + 1;
			newInfo1->br_flag = 0;
			newInfo1->isDummy = 0;
			newInfo1->fromIncumbentCB = 0;
			newInfo1->br_label = -1;
			newInfo1->nodeObjective = x[0];
			newInfo1->numNodes = 0;
			newInfo1->numActivePolicies = K;
			if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
				newInfo1->labels.clear();
			} else {
				newInfo1->labels.assign(K, std::vector<int>{});
				newInfo1->labels[0].emplace_back(0);
				newInfo1->labels[k_max].emplace_back(label);
			}


			//
			// Next, create the second node data
			// This is either a dummy node too
			// or
			// it is the last true child node corresponding to policy number 0
			//
			if (K > 2) {
				// dummy  node
				newInfo0 = new CPLEX_CB_node(*newInfo1);
				newInfo0->isDummy = 1;
				newInfo0->br_label = label;
				newInfo0->numNodes = k_max;
				if (!S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
					newInfo0->labels.assign(K, std::vector<int>{});
					newInfo0->labels[0].emplace_back(0);
				}
			}
			else {
				// last child node corresponding to policy number 0
				newInfo0 = new CPLEX_CB_node(*newInfo1);
				if (!S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
					newInfo0->labels.assign(K, std::vector<int>{});
					newInfo0->labels[0].emplace_back(0);
					newInfo0->labels[0].emplace_back(label);
				}
			}
		}


		// create your own branches as per k-adaptability
		else {
			k_max = ((K < 2) ? 0 : 1);

			// Node data for policy 0 branch
			newInfo0 = new CPLEX_CB_node;
			newInfo0->trueDepth = depth + 1;
			newInfo0->br_flag = 0;
			newInfo0->isDummy = 0;
			newInfo0->fromIncumbentCB = 0;
			newInfo0->br_label = 0;
			newInfo0->nodeObjective = x[0];
			newInfo0->numNodes = 0;
			newInfo0->numActivePolicies = 1;
			if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
				newInfo0->labels.clear();
			} else {
				newInfo0->labels.assign(K, std::vector<int>{});
				newInfo0->labels[0].emplace_back(0);
				newInfo0->labels[0].emplace_back(label);
			}

			// Node data for policy 1 branch
			if (k_max) {
				newInfo1 = new CPLEX_CB_node(*newInfo0);
				newInfo1->numActivePolicies = 2;
				if (!S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
					newInfo1->labels.assign(K, std::vector<int>{});
					newInfo1->labels[0].emplace_back(0);
					newInfo1->labels[1].emplace_back(label);
				}
			}
		}
	}
	//////////////////////////////////////
	// CASE 2: NODE DATA ALREADY EXISTS //
	//////////////////////////////////////
	else {
		auto oldInfo = static_cast<CPLEX_CB_node*>(nodeData);



		// ======================
		// SUBCASE 1: DUMMY NODE
		// ======================
		if (oldInfo->isDummy) {
			assert(K > 2);
			assert(oldInfo->numNodes > 1);
			assert(oldInfo->numNodes < K);
			assert(oldInfo->numActivePolicies >= oldInfo->numNodes);
			assert(!oldInfo->fromIncumbentCB);


			// First create the "true node"
			// This corresponds to attaching label to policy number [numNodes - 1]
			k_max = oldInfo->numNodes - 1;
			label = oldInfo->br_label;


			// "True node" data
			newInfo1 = new CPLEX_CB_node(*oldInfo);
			newInfo1->isDummy = 0;
			newInfo1->fromIncumbentCB = 0;
			newInfo1->br_label = 0;
			newInfo1->numNodes = 0;
			if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
				assert(newInfo1->labels.empty());
			} else {
				newInfo1->labels[k_max].emplace_back(label);
			}




			// 
			// Next, create the second node data
			// This is either a dummy node too
			// or
			// it is the last true child node corresponding to policy number 0
			// 
			if (oldInfo->numNodes > 2) {
				// dummy node
				newInfo0 = new CPLEX_CB_node(*oldInfo);
				newInfo0->numNodes--;
			}
			else {
				assert(k_max == 1);

				// last child node corresponding to policy number 0
				newInfo0 = new CPLEX_CB_node(*oldInfo);
				newInfo0->isDummy = 0;
				newInfo0->fromIncumbentCB = 0;
				newInfo0->br_label = 0;
				newInfo0->numNodes = 0;
				if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
					assert(newInfo0->labels.empty());
				} else {
					newInfo0->labels[0].emplace_back(label);
				}
			}
		}



		// =============================================
		// SUBCASE 2: COMING DIRECTLY FROM INCUMBENT CB
		//            AND FLAGGED AS FEASIBLE
		// =============================================
		else if (oldInfo->fromIncumbentCB && oldInfo->br_label == -1) {
			// feasible node
			assert((K > 2) || (oldInfo->trueDepth == depth));
			assert(!oldInfo->isDummy);
			assert(nodecnt == 0);
			assert(S->feasible_KAdaptability(x, K, Q_TEMP));
		}

		// ==========================================================================
		// SUBCASE 3: INFEASIBLE NODE EITHER BECAUSE
		//        (A) COMING DIRECTLY FROM INCUMBENT CB AND FLAGGED AS INFEASIBLE OR
		//        (B) IT DOES NOT SATISFY INTEGRALITY
		// ==========================================================================
		else {
			// infeasible node
			if (oldInfo->fromIncumbentCB) assert(!oldInfo->isDummy);

			// no option but to branch as CPLEX would
			if (!label) {
				for (int i = 0; i < nodecnt; i++) {
					CPLEX_CB_node *newInfo = new CPLEX_CB_node(*oldInfo);
					newInfo->trueDepth++;
					newInfo->br_flag = 1;
					CPXXbranchcallbackbranchasCPLEX(env, cbdata, wherefrom, i, newInfo, &seqnum);
				}
			}


			// create your own branches as per k-adaptability
			else {
				k_max = oldInfo->numActivePolicies;
				if (k_max >= K) k_max--;

				// SRO case
				if (K < 2) {
					assert(!k_max);
					assert(oldInfo->numActivePolicies == 1);
					
					// node data corresponding to policy number 0 branch
					newInfo0 = new CPLEX_CB_node(*oldInfo);
					newInfo0->trueDepth++;
					newInfo0->br_flag = 0;
					newInfo0->isDummy = 0;
					newInfo0->fromIncumbentCB = 0;
					newInfo0->br_label = 0;
					newInfo0->numNodes = 0;
					if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
						assert(newInfo0->labels.empty());
					} else {
						newInfo0->labels[0].emplace_back(label);
					}
				}
				else {
					assert(k_max);

					// "True node" data
					newInfo1 = new CPLEX_CB_node(*oldInfo);
					newInfo1->trueDepth++;
					newInfo1->br_flag = 0;
					newInfo1->isDummy = 0;
					newInfo1->fromIncumbentCB = 0;
					newInfo1->br_label = 0;
					newInfo1->numNodes = 0;
					newInfo1->numActivePolicies = k_max + 1;
					if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
						assert(newInfo1->labels.empty());
					} else {
						newInfo1->labels[k_max].emplace_back(label);
					}


					// 
					// Next, create the second node data
					// This is either a dummy node too
					// or
					// it is the last true child node corresponding to policy number 0
					// 
					if (k_max > 1) {
						// dummy  node
						newInfo0 = new CPLEX_CB_node(*oldInfo);
						newInfo0->trueDepth++;
						newInfo0->br_flag = 0;
						newInfo0->isDummy = 1;
						newInfo0->fromIncumbentCB = 0;
						newInfo0->br_label = label;
						newInfo0->numNodes = k_max;
					}
					else {
						assert(k_max == 1);

						// last child node corresponding to policy number 0
						newInfo0 = new CPLEX_CB_node(*oldInfo);
						newInfo0->trueDepth++;
						newInfo0->br_flag = 0;
						newInfo0->isDummy = 0;
						newInfo0->fromIncumbentCB = 0;
						newInfo0->br_label = 0;
						newInfo0->numNodes = 0;
						if (S->hasObjectiveUncOnly() && !DECISION_DEPENDENT) {
							assert(newInfo0->labels.empty());
						} else {
							newInfo0->labels[0].emplace_back(label);
						}
					}
				}	
			}
		}
	}






	












	/////////////////////
	// CREATE BRANCHES //
	/////////////////////
	if (k_max == K) {
        assert(!label);
		assert(!newInfo0);
		assert(!newInfo1);
	}
	else {
        assert(label);
		assert(label < (int)S->bb_samples.size());
        
        // violated constraints
        CPXDIM rcnt = 0;
        CPXNNZ nzcnt;
        std::vector<double> rhs;
        std::vector<char> sense;
        std::vector<CPXNNZ> rmatbeg;
        std::vector<CPXDIM> rmatind;
        std::vector<CPXDIM> rmatind_dd;
        std::vector<double> rmatval;
        
//        if(DECISION_DEPENDENT){
//            S->getRobustYQ_fixedQ(S->bb_samples[label], rcnt, nzcnt, rhs, sense, rmatbeg, rmatind_dd, rmatval);
//        }
        
        // new vector of samples \xi for the new sample \bar{\xi}
            
		for (int k = k_max, k_min = ((k_max <= 1) ? 0 : k_max); k >= k_min; k--) {
			auto xk = S->getXPolicy(x, K, k);
			auto newInfo = (k == 0) ? newInfo0 : newInfo1; assert(newInfo);
            
            //MARK: use feasible_YQ to get the worst case scenario for the policy y_k and use getYQ_fixedQ to create k different branches
//			// violated constraints
//			CPXDIM rcnt = 0;
//			CPXNNZ nzcnt;
//			std::vector<double> rhs;
//			std::vector<char> sense;
//			std::vector<CPXNNZ> rmatbeg;
//			std::vector<CPXDIM> rmatind;
//			std::vector<double> rmatval;
            
            if(DECISION_DEPENDENT){
                std::vector<double> q;
                int labelq;
                int labelCstr;
                std::vector<std::vector<double> > samples_k;
                samples_k.emplace_back(S->bb_samples[label]);
                
                S->feasible_RobustYQ(xk, samples_k, q, labelCstr, labelq);
                S->getYQ_fixedQ(k, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
                
                // update the samples in bb_samples_all, this is the first sample corresponding with the lable-th sample in bb_samples
                S->bb_samples_all[label].emplace_back(q);
            }
            else
                S->getYQ_fixedQ(k, S->bb_samples[label], rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);



			// Add only the most violated constraint
			if (!BNC_BRANCH_ALL_CONSTR && !S->hasObjectiveUncOnly()) {
				double maxViol = -std::numeric_limits<double>::max();
				std::vector<double> rhs_cut = {0};
				std::vector<char> sense_cut = {'L'};
				std::vector<int> cutind;
				std::vector<double> cutval;
				for (CPXDIM i = 0; i < rcnt; i++) {

					// coefficients and indices of this constraint
					CPXDIM next = (i + 1 == rcnt) ? nzcnt : rmatbeg[i+1];
					std::vector<int> cutind_i(rmatind.begin() + rmatbeg[i], rmatind.begin() + next);
					std::vector<double> cutval_i(rmatval.begin() + rmatbeg[i], rmatval.begin() + next);

					// check violation
					const auto W = checkViol(env, cbdata, wherefrom, rhs[i], sense[i], cutind_i, cutval_i);
					if (W.first > maxViol) {
						maxViol = W.first;
						rhs_cut[0] = rhs[i];
						sense_cut[0] = sense[i];
						cutind = cutind_i;
						cutval = cutval_i;
					}
				}

				// Violation must exist
				// UNLESS node is a dummy node and solution changed because of local cuts (for e.g.)
				#ifndef NDEBUG
				if (maxViol <= EPS_INFEASIBILITY_Q) {
					std::cout << maxViol << " !!!!!!!!!!!!!!!!!\n";
					assert(static_cast<CPLEX_CB_node*>(nodeData)->isDummy);
				}
				#endif

				// Create branch
				CPXXbranchcallbackbranchconstraints(env, cbdata, wherefrom, 1, cutind.size(), &rhs_cut[0], &sense_cut[0], &rmatbeg[0], &cutind[0], &cutval[0], newInfo->nodeObjective, newInfo, &seqnum);
			}
			// Add all constraints
			else {
				CPXXbranchcallbackbranchconstraints(env, cbdata, wherefrom, rcnt, nzcnt, &rhs[0], &sense[0], &rmatbeg[0], &rmatind[0], &rmatval[0], newInfo->nodeObjective, newInfo, &seqnum);
			}
		}

		// Create branch for dummy node
		if (k_max > 1) {
			assert(newInfo0);
			CPXXbranchcallbackbranchconstraints(env, cbdata, wherefrom, 0, 0, NULL, NULL, NULL, NULL, NULL, newInfo0->nodeObjective, newInfo0, &seqnum);
		}
	}




	*useraction_p = CPX_CALLBACK_SET;
	exitCallback(branch);
}

//-----------------------------------------------------------------------------------

static int CPXPUBLIC cutCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p) {
	enterCallback(cut);
	*useraction_p = CPX_CALLBACK_DEFAULT;

	// Get K-Adaptability solver
	auto S = static_cast<KAdaptableSolver*>(cbhandle);
	const unsigned int K = S->NK;


	// Attempt to add cuts for insured scenarios
	// Note that in case of objective only uncertainty,
	// all constraints are defined at the time of branching
	if (!DECISION_DEPENDENT && (S->hasObjectiveUncOnly() || BNC_BRANCH_ALL_CONSTR)) exitCallback(cut);


	// Get node data	
	void *nodeData = NULL; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);

	// no node data -- can happen only for root node
	if (!nodeData) exitCallback(cut);

	// cannot be from incumbent CB
	auto nodeInfo = static_cast<CPLEX_CB_node*>(nodeData);


	// exit if node is a dummy node
	if (nodeInfo->isDummy) {
		assert(K > 2);
		assert(nodeInfo->numNodes > 1);
		assert(nodeInfo->numNodes < K);
		assert(nodeInfo->numActivePolicies <= K);
		assert(nodeInfo->numActivePolicies >= nodeInfo->numNodes);
		assert(!nodeInfo->fromIncumbentCB);
		assert(nodeInfo->labels.size() == K);
		exitCallback(cut);
	}


	// Get node solution
	CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
	CPXDIM numcols = CPXXgetnumcols(env, lp);
	std::vector<double> x(numcols); CPXXgetcallbacknodex(env, cbdata, wherefrom, &x[0], 0, numcols - 1);
	int label = 0;


	// Solution must be structurally feasible
	// assert(S->feasible_DET_K(x, K, X_TEMP));
	// assert(x == X_TEMP);


	///////////////////////////////////////////////////////////////////////////
	// NOTE -- I AM ASSUMING THAT ALL CONSTRAINTS (X,Q) HAVE BEEN DUALIZED!! //
	///////////////////////////////////////////////////////////////////////////
	// assert(S->feasible_XQ(x, Q_TEMP));
	

	// construct policy k solution
	for (unsigned int k = 0; k < K; k++) {
		auto xk = S->getXPolicy(x, K, k);

		// scenarios that xk must insure against
		std::vector<std::vector<double> > samples_k;
        
        //std::cout << "l of " << k << " is: " << std::endl;
        
		for (const auto& l : nodeInfo->labels[k]) {
            //std::cout << l << ',';
            samples_k.emplace_back(S->bb_samples[l]);
		}
        //std::cout << std::endl;

        
        //for (const auto& xx : xk) {
            //std::cout << xx << ',';
        //}
        //std::cout << std::endl;
        
        if (samples_k.empty()) continue;
        
		// violated cuts
		CPXDIM rcnt = 0;
		CPXNNZ nzcnt;
		std::vector<double> rhs;
		std::vector<char> sense;
		std::vector<CPXNNZ> rmatbeg;
		std::vector<CPXDIM> rmatind;
		std::vector<double> rmatval;
        
        // violated q and constraint label
        std::vector<double> q;
        int labelCstr;
        int labelq;

		// check constraints (x, y, q)
        // MARK: Qing get the worst case scenario \xi^k for y_k by the v inside feasible_YQ
//		if (!S->feasible_YQ(xk, 1, samples_k, label)) {
//
//			// get all constraints
//			S->getYQ_fixedQ(k, samples_k[label], rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
//		}
//        if(!S->feasible_RobustYQ(xk, samples_k, q, labelCstr)){
//            S->getYQ_fixedQ(k, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
//        }
//		else {
//			assert(S->feasible_SRO(xk, samples_k, label));
//            exitCallback(cut);
//		}
        if(S->feasible_RobustYQ(xk, samples_k, q, labelCstr, labelq)){
            assert(S->feasible_SRO(xk, samples_k, label));
            continue;
        }


		// Add only the most violated constraint
		double maxViol = 0;
		double rhs_cut = 0;
		char sense_cut = 'L';
		std::vector<int> cutind;
		std::vector<double> cutval;
        
        if(GET_MAX_VIOL){
            S->getSingleYQ_fixedQ(k, labelCstr, q, nzcnt, rhs_cut, sense_cut, cutind, cutval);
            maxViol = q[0];
        }
        else{
            
            S->getYQ_fixedQ(k, q, rcnt, nzcnt, rhs, sense, rmatbeg, rmatind, rmatval);
            
            for (CPXDIM i = 0; i < rcnt; i++) {

                // coefficients and indices of this constraint
                CPXDIM next = (i + 1 == rcnt) ? nzcnt : rmatbeg[i+1];
                std::vector<int> cutind_i(rmatind.begin() + rmatbeg[i], rmatind.begin() + next);
                std::vector<double> cutval_i(rmatval.begin() + rmatbeg[i], rmatval.begin() + next);

                // check violation
                const auto W = checkViol(env, cbdata, wherefrom, rhs[i], sense[i], cutind_i, cutval_i);
                if (W.first > maxViol) {
                    maxViol = W.first;
                    rhs_cut = rhs[i];
                    sense_cut = sense[i];
                    cutind = cutind_i;
                    cutval = cutval_i;
                }
            }
        }
		// Add local cut
		if (maxViol > EPS_INFEASIBILITY_Q) {
			CPXXcutcallbackaddlocal(env, cbdata, wherefrom, cutind.size(), rhs_cut, sense_cut, &cutind[0], &cutval[0]);
            S->bb_samples_all[nodeInfo->labels[k][labelq]].emplace_back(q);
            //std::cout << "here! add constraints for violation!" << k << std::endl;
			*useraction_p = CPX_CALLBACK_SET;
		}
	}
    
	exitCallback(cut);
}

//-----------------------------------------------------------------------------------

static int CPXPUBLIC nodeCB_solve_KAdaptability_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *, CPXCNT *, int *useraction_p) {
	*useraction_p = CPX_CALLBACK_DEFAULT;

	// Get node data
	void *nodeData = NULL; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_USERHANDLE, &nodeData);
	if (nodeData) {
		NUM_DUMMY_NODES += (static_cast<CPLEX_CB_node*>(nodeData)->isDummy);
	}

	return 0;
}

//-----------------------------------------------------------------------------------
static int CPXPUBLIC cutCB_solve_LS_cuttingPlane(CPXCENVptr env, void *cbdata, int wherefrom, void *cbhandle, int *useraction_p) {
    enterCallback(cut);
    *useraction_p = CPX_CALLBACK_DEFAULT;
    
    double time = 0; CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_STARTTIME,   &time);
    if((get_wall_time() - time)>= 7200){
        TERMINATOR = 1;
    }
    
    // Get node data
//    double nodeobjval = 0; CPXXgetcallbacknodeinfo(env, cbdata, wherefrom, 0, CPX_CALLBACK_INFO_NODE_OBJVAL, &nodeobjval);
    double bestinteger = 0; CPXXgetcallbackinfo(env, cbdata, wherefrom, CPX_CALLBACK_INFO_BEST_INTEGER,   &bestinteger);
    int status; CPXLPptr nodelp = NULL; status = CPXXgetcallbacknodelp (env, cbdata, wherefrom, &nodelp);
    double objval;
    CPXXgetobjval(env, nodelp, &objval);
    
    // Get K-Adaptability solver
    auto S = static_cast<KAdaptableSolver*>(cbhandle);

    // Get L-shaped algorithm related value
    const unsigned int K = S->NK;
    double L = S->L;
    int size = S->getTrueWSize();
    
    // Get node solution
    CPXCLPptr lp = NULL; CPXXgetcallbacklp(env, cbdata, wherefrom, &lp);
    // CPXDIM numcols = CPXXgetnumcols(env, lp);
    std::vector<double> rawW(size); CPXXgetcallbacknodex(env, cbdata, wherefrom, &rawW[0], 1, size);
    
    // std::cout << nodeobjval << "\n";
    std::cout << "The node obj is: " << objval << "\n";
    std::cout << "The best integer solution is: " << bestinteger << "\n";
    
    // Add cuts on new best integer solution, exist if not
    if (objval > bestinteger) exitCallback(cut);
    
    std::vector<bool> w;
    w.resize(size);
    std::transform(rawW.begin(), rawW.end(), w.begin(), [](double x) { return abs(x) > 1.E-5;});
    
    // variables for adding cut
    char sense = 'G';
    std::vector<CPXDIM> rmatind(size+1);
    std::iota(rmatind.begin(), rmatind.end(), 0);
    std::vector<double> rmatval;
    
    // Add cuts when the current w hasn't been iterated, exist if yes
    if ( S->checkW(w) ) exitCallback(cut);
    
    std::cout << "------------Iteration " << ++S->t << "------------\n\n";
    
    std::cout << "The opt sol in this node is: ";
    for(int j = 0; j < size-1; j++)
        std::cout << w[j] << ",";
    
    std::cout << w[size-1] << '\n' << '\n';
    
    std::cout << "------------Evaluating phi(w)------------\n\n";
    
    // evaluate phi(w)
    S->setW(w);
    
    std::vector<double> x;
    std::vector<std::vector<double>> q;
    // S->setVarUB(S->bestU, 0);
    int solstat = S->solve_KAdaptability(K, false, x, q);
    S->addwBounds(w);
    

    // if have collected scenario from the branch&bound tree, add subgradient cut
    if(q.size()){
        // add subgradient cut
        if(!S->isWDetObjOnly()){
            double rhs_sg;
            char sense_sg;
            CPXNNZ rmatbeg_sg;
            std::vector<CPXDIM> rmatind_sg;
            std::vector<double> rmatval_sg;
            
            S->addSGCut(w, q, rhs_sg, sense_sg, rmatbeg_sg, rmatind_sg, rmatval_sg);

            CPXXcutcallbackadd(env, cbdata, wherefrom, size + 1, rhs_sg, sense_sg, &rmatind_sg[0], &rmatval_sg[0], CPX_USECUT_FORCE);
            
            std::cout << "Add subgradient cut\n\n";
            // subgradient cut is useful for subgradient cut
            assert(S->rhs_ws.size() == S->sense_ws.size());
            assert(S->rhs_ws.size() == S->rmatbeg_ws.size());
            S->rhs_ws.emplace_back(rhs_sg);
            S->sense_ws.emplace_back(sense_sg);
            assert(S->rmatind_ws.size() == S->rmatval_ws.size());
            S->rmatbeg_ws.emplace_back(S->rmatind_ws.size());
            S->rmatind_ws.insert(S->rmatind_ws.end(), rmatind_sg.begin(), rmatind_sg.end());
            S->rmatval_ws.insert(S->rmatval_ws.end(), rmatval_sg.begin(), rmatval_sg.end());
        }
    }
    // if given w_t is feasible for the inner problem, add optimality cut
    if(solstat == CPXMIP_OPTIMAL || solstat == CPXMIP_OPTIMAL_TOL || solstat == CPXMIP_TIME_LIM_FEAS || solstat == CPXMIP_ABORT_FEAS){
            
        if(x[0] < S->bestU){
            S->setBestU(x[0]);
            S->xsolOut = x;
        }
            
        // add integer cut
        double coef = x[0] - L;
        
        // coefficient for the epigraph variable
        rmatval.emplace_back(1.0);
        
        int sizeN = 0;
        // coefficient for variable w
        for(int i = 0; i < size; i++)
        {
            if(w[i] == 1){
                rmatval.emplace_back(-coef);
                sizeN += 1;
            }
            else
                rmatval.emplace_back(coef);
        }
        
        double rhs = L - coef*(sizeN - 1);
        
        CPXXcutcallbackadd(env, cbdata, wherefrom, size + 1, rhs, sense, &rmatind[0], &rmatval[0], CPX_USECUT_FORCE);
        
        // add deterministic part of w(cost term) to the objective function will help, add warm start of |w|_1 = Q will help
        // if(S->isWDetObjOnly()){
        if(USE_INFORM_CUT){
            double rhs_inf(x[0]);
            std::vector<CPXDIM> rmatind_inf;
            std::vector<double> rmatval_inf;
            
            rmatind_inf.emplace_back(0);
            rmatval_inf.emplace_back(1.0);
            for(int i = 0; i < size; i++){
                if(w[i] == 0){
                    rmatval_inf.emplace_back(coef);
                    rmatind_inf.emplace_back(i+1);
                }
            }
            
            CPXXcutcallbackadd(env, cbdata, wherefrom, rmatind_inf.size(), rhs_inf, sense, &rmatind_inf[0], &rmatval_inf[0], CPX_USECUT_FORCE);
        }
        
        std::cout << "Add optimality cut\n\n";
        
        *useraction_p = CPX_CALLBACK_SET;
    }
    
    // should be treated more carefully
    // if given w_t is infeasible for the inner problem, add feasibility cut
    else if(solstat == CPXMIP_INFEASIBLE || solstat == CPXMIP_INForUNBD || solstat == CPXMIP_TIME_LIM_INFEAS || solstat == CPXMIP_ABORT_INFEAS){
        if(USE_STRE_FEAS_CUT && (solstat == CPXMIP_INFEASIBLE || solstat == CPXMIP_INForUNBD)){
            int sizeN = 0;
            
            std::vector<double> rmatval_feas;
            std::vector<CPXDIM> rmatind_feas;

            for(int i = 0; i < size; i++)
            {
                if(w[i] == 1){
                    rmatind_feas.emplace_back(i+1);
                    rmatval_feas.emplace_back(-1.0);
                    sizeN += 1;
                }
            }
            
    //        std::vector<double> rmatval;
    //        // coefficient for variable w
    //        for(int i = 0; i < size; i++)
    //        {
    //            if(w[i] == 1){
    //                rmatval.emplace_back(-1.0);
    //                sizeN += 1;
    //            }
    //            else
    //                rmatval.emplace_back(1.0);
    //        }
            
            double rhs = 1 - sizeN;
            
            CPXXcutcallbackadd(env, cbdata, wherefrom, sizeN, rhs, sense, &rmatind_feas[0], &rmatval_feas[0], CPX_USECUT_FORCE);
            
            //CPXXcutcallbackadd(env, cbdata, wherefrom, size, rhs, sense, &rmatind[1], &rmatval[0], CPX_USECUT_FORCE);
            
            // feasibility cut is not always useful
            assert(S->rhs_ws.size() == S->sense_ws.size());
            assert(S->rhs_ws.size() == S->rmatbeg_ws.size());
            S->rhs_ws.emplace_back(rhs);
            S->sense_ws.emplace_back(sense);
            assert(S->rmatind_ws.size() == S->rmatval_ws.size());
            S->rmatbeg_ws.emplace_back(S->rmatind_ws.size());
            S->rmatind_ws.insert(S->rmatind_ws.end(), rmatind_feas.begin(), rmatind_feas.end());
            S->rmatval_ws.insert(S->rmatval_ws.end(), rmatval_feas.begin(), rmatval_feas.end());
    //        S->rmatind_ws.insert(S->rmatind_ws.end(), rmatind.begin()+1, rmatind.end());
    //        S->rmatval_ws.insert(S->rmatval_ws.end(), rmatval.begin(), rmatval.end());
            
            std::cout << "Add feasibility cut\n\n";
            
            *useraction_p = CPX_CALLBACK_SET;
        }
    
        else{
            int sizeN = 0;
            
            std::vector<double> rmatval;
            // coefficient for variable w
            for(int i = 0; i < size; i++)
            {
                if(w[i] == 1){
                    rmatval.emplace_back(-1.0);
                    sizeN += 1;
                }
                else
                    rmatval.emplace_back(1.0);
            }
            
            double rhs = 1 - sizeN;
            
            CPXXcutcallbackadd(env, cbdata, wherefrom, size, rhs, sense, &rmatind[1], &rmatval[0], CPX_USECUT_FORCE);
            std::cout << "Add time limit or upper bound cut-off feasibility cut\n\n";
        }
    }
    else{
        std::cout << "Something wrong when evaluate phi(w_t), status code is: " << solstat << "\n";
        assert(false);
    }
    
    exitCallback(cut);
}
