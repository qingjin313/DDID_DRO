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


#ifndef KADAPTABLESOLVER_HPP
#define KADAPTABLESOLVER_HPP

#include "problemInfo.hpp"
#include "problemInfo_knp_dd.hpp"
#include <ilcplex/cplexx.h>
#include <vector>



/**
 * Class to solve all CPLEX-based computational examples
 * in K-adaptability paper.
 * 
 * Note that the problem is stored in pInfo (temporary variable),
 * which must be in the same scope as the solver object.
 */
class KAdaptableSolver {
protected:

	/** Pointer to const information */
	KAdaptableInfo *pInfo = NULL;

	/** Best known solution for K-adaptable problem (in MILP representation) */
	std::vector<double> xsol;



public:
	/**
	 * Delete default constructor
	 */
	KAdaptableSolver() = delete;


	/**
	 * Constructor takes a specific instance as input
	 */
	KAdaptableSolver(const KAdaptableInfo& pInfoData);


	/**
	 * Define pre-C++0x methods
	 */
	~KAdaptableSolver();
	KAdaptableSolver (const KAdaptableSolver&);
	KAdaptableSolver& operator=(const KAdaptableSolver&);


	/**
	 * Delete C++11 methods
	 */
	KAdaptableSolver (KAdaptableSolver&&) = delete;
	KAdaptableSolver& operator=(KAdaptableSolver&&) = delete;


	/**
	 * Reset solver with information about a new problem
	 * @param pInfoData reference to a new problem instance
	 */
	void setInfo(const KAdaptableInfo& pInfoData);
	

	/**
	 * Reset the solver with a new problem
	 * @param pInfoData reference to a new problem instance
	 * @param K         upper bound on # of 2nd-stage policies to be supported
	 */
	void reset(const KAdaptableInfo& pInfoData, const unsigned int K = 1);






public:

	/**
	 * Set the best known solution for K-adaptable problem
	 * @param x candidate best known solution for K-adaptable problem
	 * @param K # of second-stage policies
	 */
	void setX(const std::vector<double>& x, const unsigned int K);

	/**
	 * Return # of 2nd-stage policies in candidate solution
	 * @param  x candidate solution
	 * @return   # of 2nd-stage policies in x
	 */
	unsigned int getNumPolicies(const std::vector<double>& x) const;

	/**
	 * Resize the K'-adaptable solution x to the size of a K-adaptable solution
	 * @param x candidate solution
	 * @param K # of 2nd-stage policies
	 */
	void resizeX(std::vector<double>& x, const unsigned int K) const;

	/**
	 * Remove policy number k of K-adaptable solution x (remove only 2nd-stage variables)
	 * @param  x candidate solution
	 * @param  K # of 2nd-stage policies
	 * @param  k policy number of interest
	 */
	void removeXPolicy(std::vector<double>& x, const unsigned int K, const unsigned int k) const;

	/**
	 * Return policy number k of K-adaptable solution x (1st-stage and 2nd-stage variables)
	 * @param  x candidate solution
	 * @param  K # of 2nd-stage policies
	 * @param  k policy number of interest
	 * @return   policy k of candidate solution
	 */
	const std::vector<double> getXPolicy(const std::vector<double>& x, const unsigned int K, const unsigned int k) const;

    //MARK: Qing: map index of y^1 to y^k
    /**
     * map indices from x, y^1 to x, y^k
     * @param  k    k-th policy(start from 0)
     * @param  rmatind    original indices
     * @return mapped indices, where the indices of x is the same, y^0 is mapped to y^k
     */
    inline const std::vector<int> mapK(const unsigned int k, const std::vector<int>& rmatind) {return pInfo->mapK(k, rmatind);}





public:
	/** 
	 * Add 1st-stage variables and constraints (not involving uncertain parameters)
	 * [Does not check if model is of the correct size]
	 * 
	 * @param env solver environment object
	 * @param lp  solver model object
	 */
	void updateX(CPXCENVptr env, CPXLPptr lp) const;

	/**
	 * Add 1st-stage constraints involving uncertain parameters
	 * [Does not check if model is of the correct size]
	 * 
	 * @param env solver environment object
	 * @param lp   solver model object
	 * @param q    candidate scenario (if empty, then attempt to reformulate via dualization)
	 * @param lazy add constraints in a lazy manner
	 */
	void updateXQ(CPXCENVptr env, CPXLPptr lp, const std::vector<double>& q = {}, const bool lazy = false) const;
	
	/**
	 * Add 2nd-stage variables and constraints (not involving uncertain parameters) for policy k
	 * [Does not check if model is of the correct size]
	 * 
	 * @param env solver environment object
	 * @param lp  solver model object
	 * @param k   policy to which to add 2nd-stage variables and constraints
	 */
	void updateY(CPXCENVptr env, CPXLPptr lp, const unsigned int k) const;

	/**
	 * Add 2nd-stage constraints involving uncertain parameters in policy k
	 * [Does not check if model is of the correct size]
	 * 
	 * @param env solver environment object
	 * @param lp   solver model object
	 * @param k    policy to which to add constraints
	 * @param q    candidate scenario (if empty, then attempt to reformulate via dualization)
	 * @param lazy add constraints in a lazy manner
	 */
	void updateYQ(CPXCENVptr env, CPXLPptr lp, const unsigned int k, const std::vector<double>& q = {}, const bool lazy = false) const;

	/**
	 * Get all constraints involving uncertain parameters and first-stage variables using realization q
	 * @param q       candidate scenario
	 * @param rcnt    # of constraints will be returned here (size of rhs, sense, rmatbeg)
	 * @param nzcnt   # of non-zeros across all constraints (size of rmatind, rmatval)
	 * @param rhs     rhs of each constraint
	 * @param sense   sense of each constraint
	 * @param rmatbeg rmatbeg[i] = starting position of constraint i in rmatind/rmatval, rmatbeg[0] = 0
	 * @param rmatind linear indices of variables appearing in constraints
	 * @param rmatval coefficients of variables appearing in constraints
	 */
	void getXQ_fixedQ(const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const;

	/**
	 * Get all constraints involving uncertain parameters and first-stage variables using decisions x
	 * @param x       candidate solution
	 * @param rcnt    # of constraints will be returned here (size of rhs, sense, rmatbeg)
	 * @param nzcnt   # of non-zeros across all constraints (size of rmatind, rmatval)
	 * @param rhs     rhs of each constraint
	 * @param sense   sense of each constraint
	 * @param rmatbeg rmatbeg[i] = starting position of constraint i in rmatind/rmatval, rmatbeg[0] = 0
	 * @param rmatind linear indices of parameters appearing in constraints
	 * @param rmatval coefficients of parameters appearing in constraints
	 */
	void getXQ_fixedX(const std::vector<double>& x, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const;

	/**
	 * Get all constraints involving uncertain parameters in policy k using realization q
	 * @param k       policy for which to get constraints
	 * @param q       candidate scenario
	 * @param rcnt    # of constraints will be returned here (size of rhs, sense, rmatbeg)
	 * @param nzcnt   # of non-zeros across all constraints (size of rmatind, rmatval)
	 * @param rhs     rhs of each constraint
	 * @param sense   sense of each constraint
	 * @param rmatbeg rmatbeg[i] = starting position of constraint i in rmatind/rmatval, rmatbeg[0] = 0
	 * @param rmatind linear indices of variables appearing in constraints
	 * @param rmatval coefficients of variables appearing in constraints
	 */
	void getYQ_fixedQ(const unsigned int k, const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const;
    
    /**
     * Get the single constraint labelCstr involving uncertain parameters in policy k using realization q
     * @param k       policy for which to get constraints
     * @param labelCstr     index of the constraint
     * @param q       candidate scenario
     * @param nzcnt   # of non-zeros of the constraint
     * @param rhs     rhs of the constraint
     * @param sense   sense of the constraint
     * @param rmatind linear indices of variables appearing in the constraint
     * @param rmatval coefficients of variables appearing in the constraint
     */
    void getSingleYQ_fixedQ(const unsigned int k, const uint labelCstr, const std::vector<double>& q, CPXNNZ& nzcnt, double& rhs, char& sense, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const;
    
	/**
	 * Get all constraints involving uncertain parameters in policy k using decisions x
	 * @param k       policy for which to get constraints
	 * @param x       candidate solution
	 * @param rcnt    # of constraints will be returned here (size of rhs, sense, rmatbeg)
	 * @param nzcnt   # of non-zeros across all constraints (size of rmatind, rmatval)
	 * @param rhs     rhs of each constraint
	 * @param sense   sense of each constraint
	 * @param rmatbeg rmatbeg[i] = starting position of constraint i in rmatind/rmatval, rmatbeg[0] = 0
	 * @param rmatind linear indices of parameters appearing in constraints
	 * @param rmatval coefficients of parameters appearing in constraints
	 */
	void getYQ_fixedX(const unsigned int k, const std::vector<double>& x, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval) const;

    //MARK: Qing: add getRobustYQ_fixedQ for inner maximization problem
    /**
     * Get all cutting constraints for the inner problem using constraint generation method, only caculate for policy 0 for effciency reason, map to other policy constraint later.
     * @param q     current uncertainty, where w \circ \xi = w \circ q
     * @param rcnt    # of constraints will be returned here (size of rhs, sense, rmatbeg)
     * @param nzcnt   # of non-zeros across all constraints (size of rmatind, rmatval)
     * @param rhs     rhs of each constraint
     * @param sense   sense of each constraint
     * @param rmatbeg rmatbeg[i] = starting position of constraint i in rmatind/rmatval, rmatbeg[0] = 0
     * @param rmatind linear indices of parameters appearing in constraints
     * @param rmatval coefficients of parameters appearing in constraints
     */
    void getRobustYQ_fixedQ(const std::vector<double>& q, CPXDIM& rcnt, CPXNNZ& nzcnt, std::vector<double>& rhs, std::vector<char>& sense, std::vector<CPXNNZ>& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval);
    
    /**
     * Add the projection of the feasible set defined by constraint don't have bilinear term to the w space
     * @param env   environment of the outer loop l-shaped algorithm
     * @param lp    linear problem of the out loop l-shaped algorithm
     */
    void feasibleW(CPXENVptr env, CPXLPptr lp);


public:
	
	/**
	 * Get a lower bound on the fully adaptive solution value
	 * @return lower bound on fully adaptive solution value
	 */
	double getLowerBound() const;

	/**
	 * Get the worst-case deterministic objective value
	 * @param  q scenario which generates the worst-case deterministic value is returned here
	 * @return   worst-case deterministic objective value
	 */
	double getLowerBound_2(std::vector<double>& q) const;

	/**
	 * Get the worst-case objective value of the given solution
	 * @param  x candidate solution
	 * @param  K # of 2nd-stage policies
	 * @param  q scenario which generates the worst-case objective value is returned here
	 * @return   worst-case objective value
	 */
	double getWorstCase(const std::vector<double>& x, const unsigned int K, std::vector<double>& q) const;






public:

	/**
	 * Check if the given solution is 'structurally' feasible for the K-adaptable problem
	 * Structural feasibility = satisfaction of all uncertainty-independent constraints.
	 * This includes bounds and constraints linking first- and second-stage variables.
	 * 
	 * @param  x candidate k-adaptable solution with k <= K
	 * @param  K  # of 2nd-stage policies
	 * @param  xx structurally feasible solution with (possibly) reduced number of policies will be returned here (if feasible)
	 * @return    true if feasible
	 */
	bool feasible_DET_K(const std::vector<double>& x, const unsigned int K, std::vector<double>& xx) const;

	/**
	 * Check if the given solution satisfies 1st-stage constraints involving uncertain parameters
	 * [to be used by internal routines only]
	 * 
	 * @param  x candidate solution
	 * @param  q scenario from the uncertainty set will be returned here
	 * @return   true if solution is feasible
	 */
	bool feasible_XQ(const std::vector<double>& x, std::vector<double>& q) const;

	/**
	 * Check if the given solution satisfies 1st-stage constraints involving uncertain parameter vectors contained in 'samples'
	 * [to be used by internal routines only]
	 * 
	 * @param  x       candidate solution
	 * @param  samples collection of scenarios to check feasibility against
	 * @param  label   sequence number of violating scenario in samples will be returned here (if not feasible)
	 * @return         true if solution is feasible
	 */
	bool feasible_XQ(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, int & label) const;

	/**
	 * Check if the given K-adaptable solution satisfies 2nd-stage constraints involving uncertain parameters
	 * [to be used by internal routines only]
	 * 
	 * @param  x candidate solution
	 * @param  K # of 2nd-stage policies
	 * @param  q scenario from the uncertainty set will be returned here
	 * @return   true if solution is feasible
	 */
	bool feasible_YQ(const std::vector<double>& x, const unsigned int K, std::vector<double>& q) const;

	/**
	 * Check if the given K-adaptable solution satisfies 2nd-stage constraints involving uncertain parameter vectors contained in 'samples'
	 * [to be used by internal routines only]
	 * 
	 * @param  x       candidate solution
	 * @param  K       # of 2nd-stage policies
	 * @param  samples collection of scenarios to check feasibility against
	 * @param  label   sequence number of violating scenario in samples will be returned here (if not feasible)
	 * @return         true if feasible
	 */
    bool feasible_YQ(const std::vector<double>& x, const unsigned int K, const std::vector<std::vector<double> >& samples, int & label, bool heur = 0) const;
    
    /**
     * Check if the given K-adaptable solution is robustly feasible against the unobserved elements of the uncertain parameter vectors contained in 'samples'
     * [to be used by internal routines only]
     *
     * @param  x       candidate solution
     * @param  samples collection of scenarios to check feasibility against
     * @param  q        scenario of uncertainty set will be returned here(if not feasible)
     * @param  labelCstr   number of most violated constraint (if not feasible and use GET_MAXVIOL)
     * @param  labelq   label of the most violated scenario
     * @return         true if feasible
     */
    bool feasible_RobustYQ(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, std::vector<double>& q, int & labelCstr, int & labelq, bool heur = 0) const;




public:

	/**
	 * Check if the given solution is deterministically feasible for the given scenario
	 * @param  x candidate solution
	 * @param  q candidate scenario
	 * @return   true if feasible
	 */
	bool feasible_DET(const std::vector<double>& x, const std::vector<double>& q) const;

	/**
	 * Check if the given solution is static robust feasible
	 * @param  x candidate solution
	 * @param  q scenario from the uncertainty set will be returned here (if not feasible)
	 * @return   true if feasible
	 */
	bool feasible_SRO(const std::vector<double>& x, std::vector<double>& q) const;

	/**
	 * Check if the given solution is feasible for the collection of scenarios contained in 'samples'
	 * @param  x       candidate solution
	 * @param  samples collection of scenarios to check feasibility against
	 * @param  label   sequence number of violating scenario in samples will be returned here (if not feasible)
	 * @return         true if feasible
	 */
	bool feasible_SRO(const std::vector<double>& x, const std::vector<std::vector<double> >& samples, int & label) const;

	/**
	 * Check if the given solution is feasible for the K-adaptable problem
	 * @param  x candidate k-adaptable solution with k <= K
	 * @param  K # of 2nd-stage policies
	 * @param  q scenario from the uncertainty set will be returned here (if not feasible)
	 * @return   true if feasible
	 */
	bool feasible_KAdaptability(const std::vector<double>& x, const unsigned int K, std::vector<double>& q) const;

	/**
	 * Check if the given solution is K-adaptable feasible for the collection of scenarios contained in 'samples'
	 * @param  x       candidate k-adaptable solution with k <= K
	 * @param  K       # of 2nd-stage policies
	 * @param  samples collection of scenarios to check feasibility against
	 * @param  label   sequence number of violating scenario in samples will be returned here (if not feasible)
	 * @return         true if feasible
	 */
	bool feasible_KAdaptability(const std::vector<double>& x, const unsigned int K, const std::vector<std::vector<double> >& samples, int & label) const;






public:
	/**
	 * Solve the deterministic problem for the given scenario
	 * @param  q scenario from the uncertainty set
	 * @param  x optimal solution (if any) will be returned here
	 * @return   solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_DET(const std::vector<double>& q, std::vector<double>& x) const;

	/**
	 * Solve the static robust problem via duality-based reformulation
	 * @param  x optimal solution (if any) will be returned here
	 * @return   solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_SRO_duality(std::vector<double>& x) const;

	/**
	 * Solve the static robust problem via cutting plane method implemented using solver callbacks
	 * @param  x optimal solution (if any) will be returned here
	 * @return   solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_SRO_cuttingPlane(std::vector<double>& x);

	/**
	 * Solve the scenario-based static robust problem (uncertainty set is a finite set of points)
	 * @param  samples collection of scenarios to be robust against
	 * @param  x       optimal solution (if any) will be returned here
	 * @return      solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_ScSRO(const std::vector<std::vector<double> >& samples, std::vector<double>& x) const;
    
    //MARK: Qing: Solve the inner robust problem using cutting plane method
    /**
     * Solve the inner robust problem using cutting plane method
     * @param  qini     samples xi to be added as initial constraint
     * @return      solve status (non-zero value indicates unsuccessful termination)
     */
    int solve_YQRobust_cuttingplane(const std::vector<double>& qini);

    
public:
	/** Library of samples (temporary var -- to be used by solve_KAdaptability() only) */
	std::vector<std::vector<double> > bb_samples;
    
    /** Library of samples contains all samples from \Xi(w, \bar{\xi}), each samples in bb_samples corespond to a vector in bb_samples_all */
    std::vector< std::vector< std::vector<double> > > bb_samples_all;
    
    /** Library of samples (temporary var -- to be used by solve_YQRobust_cuttingplane() only) */
    std::vector<std::vector<double> > inner_samples;

	/** Feasible (ideally optimal) static robust solution -- to be used by solve_KAdaptability() only) */
	std::vector<double> xstatic;

	/** (K - 1)-adaptable solution -- to be used by solve_KAdaptability() in heuristic_mode only */
	std::vector<double> xfix;

	/** Run in heuristic mode -- to be used by solve_KAdaptability() only */
	bool heuristic_mode;

	/** Current value of K in K-Adaptability -- to be used by solve_KAdaptability() only) */
	unsigned int NK;
    
    /**
     * Assignment of the \bar{\xi} to policies in the optimal node, work with bb_samples_all to get gradient
     */
    std::vector<std::vector<int> > final_labels;
    
	/**
	 * Get best solution found
	 * @return best solution found
	 */
	inline const std::vector<double>& getX() const {
		return xsol;
	}

	/**
	 * Get nominal scenario
	 * @return nominal scenario from uncertainty set
	 */
	inline std::vector<double> getNominal() const {
		return pInfo->getNominal();
	}
    
    //MARK: Qing: add
    /**
     * Get uncertainty set
     * @return uncertainty set of the problem
     */
    inline UNCSetCPtr getUncSet() const {
        return &pInfo->getUncSet();
    }
    
    /**
     * Set observation decision to the problem
     */
    inline void setW(const std::vector<bool> &wInput) const {
        pInfo->setW(wInput);
    }

	/**
	 * Is the underlying problem a pure LP?
	 * @return true if the problem is continuous
	 */
	inline bool isContinuous() const {
		return pInfo->isContinuous();
	}

	/**
	 * Indicates if problem has 2nd-stage decisions only
	 * @return true if problem has 2nd-stage decisions only
	 */
	inline bool isSecondStageOnly() const {
		return pInfo->isSecondStageOnly();
	}

	/**
	 * Indicates if problem has objective uncertainty only
	 * @return true if problem has objective uncertainty only
	 */
	inline bool hasObjectiveUncOnly() const {
		return pInfo->hasObjectiveUncOnly();
	}

	/**
	 * Solve the K-adaptability problem using solver callbacks
	 * @param  K # of 2nd-stage policies
	 * @param  h run in heuristic mode
	 * @param  x optimal solution (if any) will be returned here
     *@param q scenarios genrated by branch and bound and cut algorithm for each policy
	 * @return   solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_KAdaptability(const unsigned int K, const bool h, std::vector<double>& x, std::vector<std::vector<std::vector<double>>>& q);

	/**
	 * Solve the separation problem arising in solve_KAdaptability()
	 * @param  x     candidate solution
	 * @param  K     # of 2nd-stage policies
	 * @param  label sequence number of violating scenario in bb_samples will be returned here
	 * @param  heur  separation is not guaranteed to be exact if this parameter is true
	 * @return       true if separating scenario was found, false otherwise
	 */
	bool solve_separationProblem(const std::vector<double>& x, const unsigned int K, int& label, bool heur = 0);

	/**
	 * Solve the min-max-min robust combinatorial optimization problem (as in Buchheim & Kurtz)
	 * @param  K # of 2nd-stage policies
	 * @param  x optimal solution (if any) will be returned here
	 * @return   solve status (non-zero value indicates unsuccessful termination)
	 */
	int solve_KAdaptability_minMaxMin(const unsigned int K, std::vector<double>& x);

public:
    
    double L;
    double bestU;
    int t;

    std::vector<std::string> wBounds;
    
    inline void setL(double lb) {L = lb;}
    inline void setBestU(double ub) {bestU = ub;}
    inline void addwBounds(std::vector<bool> wSols){
        std::string newW;
        for(auto w : wSols)
            newW += (w? "1" : "0");
        wBounds.emplace_back(newW);
    }
    inline bool checkW(std::vector<bool> wSols){
        std::string newW;
        for(auto w : wSols)
            newW += (w? "1" : "0");
        
        std::vector<std::string>::reverse_iterator rit = wBounds.rbegin();
          for (; rit!= wBounds.rend(); ++rit)
            if (*rit == newW)
                return true;
        
        return false;
    }
    
    inline int getTrueWSize() {return pInfo->getTrueWSize();}
    
    /**
     * Optimize over w using L-Shaped method
     * @param   K       number of K
     * @param   h       whether use heuristic mode
     * @param   sol     vector of solution
     * @return  solve status (non-zero value indicates unsuccessful termination)
     */
    int solve_L_Shaped(const unsigned int K, const bool h, std::vector<double>& sol);
    
    /**
     * Optimize over w using L-Shaped method
     * @param   K       number of K
     * @param   h       whether use heuristic mode
     * @param   sol     vector of solution
     * @return  solve status (non-zero value indicates unsuccessful termination)
     */
    int solve_L_Shaped2(const unsigned int K, const bool h, std::vector<double>& sol);
    
    /**
     * Return the subgradient cut to the problem using the realization defined by the finite set of scenarios
     * @param   w     value of w in this iteration
     * @param   q   scenarios define the relaxation, q[k] contains all scenarios to be covered by policy k
     * @param   rhs     left hand side parameter of the sub-gradient cut
     * @param   sense   sense of the sub-gradient cut
     * @param   rmatbeg     begin of the indices of the cuts
     * @param   ramtind     indices of the decision variables in the cut
     * @param   rmatval     coefficient of each decision variables in the cut
     * @return  solve status (non-zero value indicates unsuccessful termination)
     */
    int addSGCut(const std::vector<bool>& w, const std::vector<std::vector<std::vector<double>>>& q, double& rhs, char& sense, CPXNNZ& rmatbeg, std::vector<CPXDIM>& rmatind, std::vector<double>& rmatval);
};

#endif
