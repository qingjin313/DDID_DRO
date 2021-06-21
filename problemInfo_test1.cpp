//
//  problemInfo_test1.cpp
//  kadaptability
//
//  Created by 靳晴 on 3/10/21.
//

#include "problemInfo_test1.hpp"

//-----------------------------------------------------------------------------------

void KAdaptableInfo_test1::makeUncSet() {
    U.clear();

    // define uncertain risk factors
    U.addParam(1, -1, 1);
    U.addParam(-0.5, -0.5, 0.5);
    
    U.setObsVar(std::make_pair(1, 0));
    U.setObsVar(std::make_pair(2, 1));
    
    std::vector<std::pair<int, double> > constraint;
    constraint.emplace_back(std::make_pair(1, 0.5));
    constraint.emplace_back(std::make_pair(2, 2));
    U.addFacet(constraint, 'L', 1.0);
    //std::cout << U.getNoOfUncertainParameters() << std::endl;
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_test1::makeVars() {
    X.clear();
    Y.clear();

    // objective variable (considered to be 1st-stage)
    // should always be defined and should always be declared first
    X.addVarType("O", 'C', -CPX_INFBOUND, +CPX_INFBOUND, 1);

    Y.addVarType("y", 'B', 0, 1, 2);

    // define objective variable
    X.setVarObjCoeff(1.0, "O", 0);
}


//-----------------------------------------------------------------------------------

void KAdaptableInfo_test1::makeConsX() {
   
    // nothing to add
    C_X.clear();
    C_XQ.clear();
}


//-----------------------------------------------------------------------------------

void KAdaptableInfo_test1::makeConsY(unsigned int l) {
   
    assert(C_XY.size() == B_Y.size());
    assert(C_XY.size() == C_XYQ.size());
    assert(numPolicies >= l);
    if (l == 0) {
        B_Y.clear();
        C_XY.clear();
        C_XYQ.clear();
    }
    ConstraintExpression temp;
    
    for (unsigned int k = B_Y.size(); k <= l; k++) {
        if (B_Y.size() < k + 1) B_Y.resize(k + 1);
        B_Y[k].clear();
        if (C_XY.size() < k + 1) C_XY.resize(k + 1);
        C_XY[k].clear();
    }
    
    for (unsigned int k = C_XYQ.size(); k <= l; k++) {
        if (C_XYQ.size() < k + 1) C_XYQ.resize(k + 1);
        
        C_XYQ[k].clear();

        // objective function
        temp.clear();
        temp.rowname("OBJ_CONSTRAINT(" + std::to_string(k) + ")");
        temp.sign('G');
        temp.RHS(0);
        temp.addTermX(getVarIndex_1("O", 0), 1);
        temp.addTermQ( U.getParamIndex('q', 1), -2.0);
        temp.addTermX(getVarIndex_2(k, "y", 0), 3.0);
        temp.addTermProduct(getVarIndex_2(k, "y", 0), U.getParamIndex('q', 1), -2.0);
        temp.addTermProduct(getVarIndex_2(k, "y", 1), U.getParamIndex('q', 2), -1.0);
        C_XYQ[k].emplace_back(temp);
        
        // constraint
        temp.clear();
        temp.rowname("CONSTRAINT(" + std::to_string(k) + ")");
        temp.sign('G');
        temp.RHS(0.5);
        temp.addTermX(getVarIndex_2(k, "y", 0), 1);
        temp.addTermX(getVarIndex_2(k, "y", 1), 2.0);
        temp.addTermQ(U.getParamIndex('q', 1), -0.5);
        temp.addTermQ(U.getParamIndex('q', 2), 0.5);
        C_XYQ[k].emplace_back(temp);
    }
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_test1::setInstance() {
    hasInteger = 1;
    objectiveUnc = 0;
    existsFirstStage = 0;
    numFirstStage = 1;
    numSecondStage = 2;
    numPolicies = 1;
    solfilename = "test1";

    makeUncSet();
    
    makeVars();
    makeConsX();
    makeConsY(0);
    assert(isConsistentWithDesign());
}
