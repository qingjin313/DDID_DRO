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


#ifndef KADAPTABLEINFO_HPP
#define KADAPTABLEINFO_HPP

#include "indexingTools.h"
#include "constraintExpr.hpp"
#include "uncertainty.hpp"
#include <vector>
#include <string>
#include <map>

/**
 * Abstract class representing information that any
 * two-/multi-stage RO instance must provide.
 * All problems must be minimization problems.
 */
class KAdaptableInfo {

protected:

	/** Continuous or (mixed) integer problem? */
	bool hasInteger;

	/** Uncertainty in objective only */
	bool objectiveUnc;

	/** Any 1st-stage decisions to be made? */
	bool existsFirstStage;

	/** # of 1st-stage variables including worst-case objective */
	unsigned int numFirstStage;
    
    /** # of constraints in the ambiguity set */
    unsigned int numAmbCstr;

	/** # of 2nd-stage variables */
	unsigned int numSecondStage;
    
    /** w in objective funtion with deterministic part only*/
    bool wDetObjOnly;

	/** # of 2nd-stage policies */
	unsigned int numPolicies;

	/** Solution file name */
	std::string solfilename;

	/** Resident uncertainty set */
	UncertaintySet U;
    
    //MARK: Qing: add this
    /** Resident larger uncertainty set  for sparation problem*/
    UncertaintySet Uk;

	/** 1st-stage variables only */
	VarInfo X;

	/** 2nd-stage variables only */
	VarInfo Y;

	/** Constraints with 1st-stage variables only (excluding bounds) */
	std::vector<ConstraintExpression> C_X;

	/** Constraints with 2nd-(and, if applicable, 1st-) stage variables BUT NO uncertain parameters (excluding bounds) */
	std::vector<std::vector<ConstraintExpression> > C_XY;

	/** Constraints with 1st-stage variables AND uncertain parameters */
	std::vector<ConstraintExpression> C_XQ;

	/** Constraints with 2nd-(and, if applicable, 1st-) stage variables AND uncertain parameters */
	std::vector<std::vector<ConstraintExpression> > C_XYQ;

	/** Bound constraints on 1st-stage variables only */
	std::vector<ConstraintExpression> B_X;

	/** Bound constraints on 2nd-stage variables only */
	std::vector<std::vector<ConstraintExpression> > B_Y;
    
    /** Deterministic part of w in the objective function, need to be as the same size of w*/
    std::vector<double> C_W;
	/**
	 * Define 1st-stage and 2nd-stage variables
	 */
	virtual void makeVars() = 0;

	/**
	 * Define uncertainty set
	 */
	virtual void makeUncSet() = 0;

	/**
	 * Define constraints involving 1st-stage variables only
	 */
	virtual void makeConsX() = 0;
	
	/**
	 * Define constraints involving 2nd-stage variables
	 * @param k policy number to make constraints for
	 */
	virtual void makeConsY(unsigned int k = 0) = 0;

public:
	/**
	 * Virtual destructor does nothing
	 */
	virtual ~KAdaptableInfo() {/**/}

	/**
	 * Virtual (default) constructor
	 */
	virtual KAdaptableInfo* create() const = 0;

	/**
	 * Virtual (copy) constructor
	 */
	virtual KAdaptableInfo* clone() const = 0;

	/**
	 * Resizes data structures to contain at least K 2nd-stage policies
	 * @param K Number of 2nd-stage policies to be supported
	 */
	void resize(unsigned int K);
    
    //MARK: Qing: add the map uncertainty set function, called in resize(K)
    void makeUncSetK(unsigned int K);

	/**
	 * Check consistency with class design
	 * @return true if object is conistent with class design
	 */
	bool isConsistentWithDesign() const;
    
    //MARK: Qing: map index of y^1 to y^k
    /**
     * map indices from x, y^1 to x, y^k
     * @param  k    k-th policy(start from 0)
     * @param  rmatind    original indices
     * @return mapped indices, where the indices of x is the same, y^0 is mapped to y^k
     */
    const std::vector<int> mapK(const unsigned int k, const std::vector<int>& rmatind);
    
    //MARK: Qing: map index of q^1 to q^k
    /**
     * map indices from q^1 to q^k
     * @param  k    k-th policy(start from 0)
     * @param  rmatind    original indices
     * @return  mapped indices, where the indices of x is the same, y^0 is mapped to y^k
     */
    const std::vector<int> mapParamK(const unsigned int k, const std::vector<int>& rmatind);
    
    //MARK: Qing: add set w and set xi_bar
    void setW(const std::vector<bool>& wInput);
    
    void setRobSol(const std::vector<bool>& wInput, const std::vector<double>& xInput, const std::vector<std::vector<double>>& yInput);
    inline void setXiBar(const std::vector<double>& xi_bar) {U.setXiBar(xi_bar);}
    inline void resetXiBar() {U.resetXiBar();}
    inline int getWSize() {return U.getWSize();}
    inline int getTrueWSize() {return X.getDefVarTypeSize("w");}
	/**
	 * Linear variable index of 1st-stage variable
	 * [All indices start from 0]
	 * 
	 * @param  type name of variable
	 * @param  ind1 first index
	 * @param  ind2 second index
	 * @param  ind3 third index
	 * @param  ind4 fourth index
	 * @return      the linear index of the variable
	 */
	inline int getVarIndex_1(const std::string type, const int ind1, const int ind2 = -1, const int ind3 = -1, const int ind4 = -1) const {
		return X.getDefVarLinIndex(type, ind1, ind2, ind3, ind4);
	}

	/**
	 * Linear variable index of 2nd-stage variable
	 * [All indices start from 0]
	 * [Policy numbering starts from 0]
	 *
	 * @param  k    policy number in which you want linear index
	 * @param  type name of variable
	 * @param  ind1 first index
	 * @param  ind2 second index
	 * @param  ind3 third index
	 * @param  ind4 fourth index
	 * @return      the linear index of the variable
	 */
	inline int getVarIndex_2(const unsigned int k, const std::string type, const int ind1, const int ind2 = -1, const int ind3 = -1, const int ind4 = -1) const {
		return numFirstStage + (k * numSecondStage) + Y.getDefVarLinIndex(type, ind1, ind2, ind3, ind4);
	}
    
	/**
	 * Get total # of 1st- and 2nd-stage decisions
	 * @param  K Total number of 2nd-stage policies to consider
	 * @return   Total # of 1st- and 2nd-stage decisions for a K-policy problem
	 */
	inline int getNumVars(const unsigned int K = 1) const {
		return numFirstStage + (K * numSecondStage);
	}

	/**
	 * Get # of 1st-stage decisions
	 * @return # of 1st-stage decisions
	 */
	inline int getNumFirstStage() const {
		return numFirstStage;
	}

	/**
	 * Get # of 2nd-stage decisions
	 * @return # of 2nd-stage decisions
	 */
	inline int getNumSecondStage() const {
		return numSecondStage;
	}

	/**
	 * Get current upper bound on # of 2nd-stage policies
	 * @return current upper bound on # of 2nd-stage policies
	 */
	inline int getNumPolicies() const {
		return numPolicies;
	}

	/**
	 * Get solution file name
	 * @return solution file name
	 */
	inline std::string getSolFileName() const {
		return solfilename;
	}

	/**
	 * Get nominal parameter values
	 * @return nominal parameter values
	 */
	inline std::vector<double> getNominal() const {
		return U.getNominal();
	}

	/**
	 * Get # of uncertain parameters
	 * @return # of uncertain parameters
	 */
	inline int getNoOfUncertainParameters() const {
		return U.getNoOfUncertainParameters();
	}
    
//    /**
//     * Get # of constraints
//     * @return # of constraints
//     */
//    inline unsigned int getNoOfCstrs() const {
//        return C_X.size() + C_XY[0].size() + C_XQ.size() + C_XYQ[0].size();
//    }
    
	/**
	 * Indicates if problem has continuous variables only
	 * @return true if problem has continuous variables only
	 */
	inline bool isContinuous() const {
		return !hasInteger;
	}

	/**
	 * Indicates if problem has objective uncertainty only
	 * @return true if problem has objective uncertainty only
	 */
	inline bool hasObjectiveUncOnly() const {
		return objectiveUnc;
	}

	/**
	 * Indicates if problem has 2nd-stage decisions only
	 * @return true if problem has 2nd-stage decisions only
	 */
	inline bool isSecondStageOnly() const {
		return !existsFirstStage;
	}

    /**
     * Indicates if problem has w variable in the objective funtion with deterministic term only
     * @return true if problem has w variable in the objective funtion with deterministic term only
     */
    inline bool isWDetObjOnly() const {
        return wDetObjOnly;
    }
    
	/**
	 * Get resident uncertainty set
	 * @return return the uncertainty set
	 */
	inline const decltype(U)& getUncSet() {
		return U;
	}
    
    //MARK: Qing: add get UncertaintySetK
    /**
     * Get larger uncertainty set K
     * @return return the uncertainty set
     */
    inline const decltype(Uk)& getUncSetK() {
        return Uk;
    }
    
	/**
	 * Get 1st-stage variables only
	 * @return the 1st-stage variables
	 */
	inline const decltype(X)& getVarsX() {
		return X;
	}

	/**
	 * Get 2nd-stage variables only
	 * @return the 2nd-stage variables
	 */
	inline const decltype(Y)& getVarsY() {
		return Y;
	}

	/**
	 * Get constraints with 1st-stage variables only (excluding bounds)
	 * @return constraints with 1st-stage variables only (excluding bounds)
	 */
	inline const decltype(C_X)& getConstraintsX() {
		return C_X;
	}

	/**
	 * Get constraints with 2nd-(and, if applicable, 1st-) stage variables BUT NO uncertain parameters (excluding bounds)
	 * @return constraints with 2nd-(and, if applicable, 1st-) stage variables BUT NO uncertain parameters (excluding bounds)
	 */
	inline const decltype(C_XY)& getConstraintsXY() {
		return C_XY;
	}

	/**
	 * Get constraints with 1st-stage variables AND uncertain parameters
	 * @return constraints with 1st-stage variables AND uncertain parameters
	 */
	inline const decltype(C_XQ)& getConstraintsXQ() {
		return C_XQ;
	}

	/**
	 * Get constraints with 2nd-(and, if applicable, 1st-) stage variables AND uncertain parameters
	 * @return constraints with 2nd-(and, if applicable, 1st-) stage variables AND uncertain parameters
	 */
	inline const decltype(C_XYQ)& getConstraintsXYQ() {
		return C_XYQ;
	}

	/**
	 * Get bound constraints on 1st-stage variables only
	 * @return bound constraints on 1st-stage variables only
	 */
	inline const decltype(B_X)& getBoundsX() {
		return B_X;
	}

	/**
	 * Get bound constraints on 2nd-stage variables only
	 * @return bound constraints on 2nd-stage variables only
	 */
	inline const decltype(B_Y)& getBoundsY() {
		return B_Y;
	}

    /**
     * Get coeficient of the observation variable w
     * @return coeficient of the observation variable w
     */
    inline const decltype(C_W)& getCoefW() {
        return C_W;
    }
};


#endif
