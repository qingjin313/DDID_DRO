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


#include "problemInfo.hpp"
#include <cassert>


//-----------------------------------------------------------------------------------

void KAdaptableInfo::resize(unsigned int K) {
	assert(isConsistentWithDesign());
	unsigned int l = numPolicies;
//	if (numPolicies < K){
//		numPolicies = K;
//	}
//	for (unsigned int k = l; k < numPolicies; k++) {
//		makeConsY(k);
//	}
    if (numPolicies < K){
        numPolicies = K;
        for (unsigned int k = l; k < numPolicies; k++)
            makeConsY(k);
    }
    else{
        numPolicies = K;
        for (unsigned int k = l; k > numPolicies; k--){
            B_Y.pop_back();
            C_XY.pop_back();
            C_XYQ.pop_back();
        }
    }
    
    makeUncSetK(K);
//    int stat;
//    CPXXwriteprob(getUncSetK().getENVObject(), getUncSetK().getLPObject(&stat), "/Users/lynn/Desktop/research/DRO/BnB/model_output/oriunc","LP");
    
	assert(isConsistentWithDesign());
}

//MARK: Qing: add the function to enlarge the uncertainty set
//-----------------------------------------------------------------------------------

void KAdaptableInfo::makeUncSetK(unsigned int K)
{
    
    if(K<=1){
        Uk = U;
        //std::cout << "no need to enlarge the uncertainty set." << std::endl;
        return;
    }
    
    // w vector
    std::vector<bool> w(U.getVectorW());
    if(!w.size()){
        //std::cout << "no need to enlarge the uncertainty set for decision independent case." << std::endl;
        return;
    }
        
    Uk.clear();
    
    int stat;
    //CPXXwriteprob(Uk.getENVObject(), Uk.getLPObject(&stat), "/Users/lynn/Desktop/research/DRO/BnB/model_output/testK_before", "LP");
    
    // get all data from uncertainty set U
    int numPara = U.getNoOfUncertainParameters();
    int numFacets = U.getNoOfFacets();
    // nominal value and bounds
    std::vector<double> nominalQ = U.getNominal();
    std::vector<double> lowQ = U.getLowerBounds();
    std::vector<double> highQ = U.getUpperBounds();
    // constraints defining facets
    std::vector<std::vector<double> > WQ = U.getMatrixW();
    std::vector<std::vector<double> > VQ = U.getMatrixV();
    std::vector<double> HQ = U.getMatrixH();
    std::vector<char> senseQ = U.getMatrixSense();
    
    // build \bar{\xi} matrix
    // first create all parameters
    for(int i = 1; i <= numPara; i++){
        Uk.addParam(nominalQ[i], lowQ[i], highQ[i]);
    }
    // then add all facets
    // capture data in constraint, do it one time for efficiency
    std::vector<std::vector<std::pair<int, double>>> constraints;
    for(int i = 1 + 2*numPara; i < numFacets; i++){
        std::vector<std::pair<int, double>> constraint;
        for(int j = 1; j <= numPara; j++){
            if(WQ[i][j])
                constraint.emplace_back(std::make_pair(j, WQ[i][j]));
        }
        Uk.addFacet(constraint, senseQ[i], HQ[i]);
        constraints.emplace_back(constraint);
    }
    
    assert(Uk.getNoOfFacets() == numFacets);
    for(unsigned int l = 1; l <= K; l++){
        // add parameter \xi^l
        for(int i = 1; i <= numPara; i++){
            Uk.addParam(nominalQ[i], lowQ[i], highQ[i]);
        }
        // add facets
        for(int i = 1 + 2*numPara; i < numFacets; i++){
            int j = i - 1 - 2*numPara;
            for(unsigned int x = 0; x < constraints[j].size(); x++){
                constraints[j][x].first += numPara;
            }
            Uk.addFacet(constraints[j], senseQ[i], HQ[i]);
        }
        // add constraint w \circ \bar{\xi} = w \circ \xi
        unsigned int numW = 0;
        for(int i = 0; i < numPara; i++){
            if(w[i]){
                std::vector<std::pair<int, double>> constraint;
                constraint.emplace_back(std::make_pair(i+1, 1));
                constraint.emplace_back(std::make_pair(l*numPara+i+1, -1));
                Uk.addFacet(constraint, 'E', 0);
                numW++;
            }
        }
        assert(Uk.getNoOfFacets() == int(numFacets + (numFacets + numW - 1) * l) );
    }
}

//-----------------------------------------------------------------------------------


bool KAdaptableInfo::isConsistentWithDesign() const {
	if (numPolicies < 1) return 0;
	if (C_XY.size() != numPolicies) return 0;
	if (C_XYQ.size() != numPolicies) return 0;
	if (B_Y.size() != numPolicies) return 0;
	if (X.getTotalDefVarSize() <= 0) return 0;
	if (Y.getTotalDefVarSize() < 0) return 0;
	if (X.getTotalDefVarSize() != getNumFirstStage()) return 0;
	if (Y.getTotalDefVarSize() != getNumSecondStage()) return 0;
	if (existsFirstStage) {
		if (getNumFirstStage() <= 1) return 0;
	}
	if (U.getNoOfUncertainParameters() == 0) {
		if (!C_XQ.empty()) return 0;
		for (unsigned int p = 0; p < numPolicies; p++) {
			if (!C_XYQ[p].empty()) return 0;
		}
	}
	if (objectiveUnc) {
		if (!C_XQ.empty()) return 0;
		for (unsigned int p = 0; p < numPolicies; p++) {
			if (C_XYQ[p].size() != 1) return 0;
		}
	}
	bool check = 0;
	for (int x = 0; x < X.getTotalVarSize(); x++) {
		if (X.getVarColType(x) == 'B') {
			check = 1;
			if (!hasInteger) return 0;
		}
	}
	for (int y = 0; y < Y.getTotalVarSize(); y++) {
		if (Y.getVarColType(y) == 'B') {
			check = 1;
			if (!hasInteger) return 0;
		}
	}
	if (hasInteger && !check) return 0;
    // get the index of w
    int begin(X.getVarLinIndex("w", 0));
    int end(begin + X.getDefVarTypeSize("w") - 1);
    
    bool wCheck = true;
	for (const auto& con: C_X) {
		if (con.isEmpty()) return 0;
		if (con.existBilinearTerms()) return 0;
		if (con.existConstQTerms()) return 0;
        if (!con.wDetObjOnly(begin, end)) wCheck = false;
	}
	for (const auto& con: C_XQ) {
		if (con.isEmpty()) return 0;
		if (!con.existBilinearTerms() && !con.existConstQTerms()) return 0;
        if (!con.wDetObjOnly(begin, end)) wCheck = false;
	}
	for (const auto& con_K: C_XY) {
		for (const auto& con: con_K) {
			if (con.isEmpty()) return 0;
			if (con.existBilinearTerms()) return 0;
			if (con.existConstQTerms()) return 0;
            if (!con.wDetObjOnly(begin, end)) wCheck = false;
		}
	}
	for (const auto& con_K: C_XYQ) {
		for (const auto& con: con_K) {
			if (con.isEmpty()) return 0;
			if (!con.existBilinearTerms() && !con.existConstQTerms()) return 0;
            if (!con.wDetObjOnly(begin, end)) wCheck = false;
		}
	}
    if(wCheck != wDetObjOnly) return 0;
	for (const auto& con: B_X) {
		if (con.isEmpty()) return 0;
		if (con.existBilinearTerms()) return 0;
		if (con.existConstQTerms()) return 0;
	}
	for (const auto& con_K: B_Y) {
		for (const auto& con: con_K) {
			if (con.isEmpty()) return 0;
			if (con.existBilinearTerms()) return 0;
			if (con.existConstQTerms()) return 0;
		}
	}
    
    //MARK: Qing: check w vector
    if( U.getWSize() && U.getWSize() != U.getNoOfUncertainParameters()) return 0;
	return 1;
}


//-----------------------------------------------------------------------------------

//MARK: Qing add
const std::vector<int> KAdaptableInfo::mapK(const unsigned int k, const std::vector<int>& rmatind)
{
    std::vector<int> rmatindNew;
    rmatindNew.clear();
    
    int numFirst = getNumFirstStage();
    int numSecond = getNumSecondStage();
    int numVar = getNumVars();
    for(int ind : rmatind){
        assert(ind < numVar);
        rmatindNew.emplace_back(ind < numFirst? ind : ind + k*numSecond);
    }
    
    assert(rmatind.size() == rmatindNew.size());
    
    return rmatindNew;
}

const std::vector<int> KAdaptableInfo::mapParamK(const unsigned int k, const std::vector<int>& rmatind)
{
    assert(getUncSetK().getNoOfUncertainParameters());
    
    std::vector<int> rmatindNew;
    rmatindNew.clear();

    int numParam = getNoOfUncertainParameters();
    for(int ind : rmatind){
        assert(ind <= numParam);
        if(ind == 0)
            rmatindNew.emplace_back(0);
        else
            rmatindNew.emplace_back(ind + (k+1)*numParam);
    }
    
    assert(rmatind.size() == rmatindNew.size());
    
    return rmatindNew;
}

void KAdaptableInfo::setW(const std::vector<bool>& wInput)
{
    int start(X.getFirstDefOfVarType("w") - X.getFirstOfVarType("w"));
    
    // Always assume the observation variables have the name w and the dimension of w is 1
    for(unsigned int i = 0; i < wInput.size(); i++)
    {
        X.setVarLB(wInput[i], "w", i+start);
        X.setVarUB(wInput[i], "w", i+start);
    }
    
    U.setW(wInput);
}
