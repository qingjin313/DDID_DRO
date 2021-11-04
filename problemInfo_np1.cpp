//
//  problemInfo_np1.cpp
//  Kadaptability
//
//  Created by 靳晴 on 10/31/21.
//

#include "problemInfo_np1.hpp"

#define NUM_UNC 2
#define NUM_CSTR 1

void KAdaptableInfo_np1::makeUncSet() {
    U.clear();
    
    // define uncertain risk factors
    for (unsigned int f = 0; f < NUM_UNC; ++f) {
        U.addParam(0, 0, 1);
    }
    
    std::vector<std::vector<double>> cstr_coef{std::vector<double>{0.436, 0.026}};
    std::vector<double> rhs{0.275};
    std::vector<std::pair<int, double> > constraint;
    // define uncertain untility and calculate the relation between it and the features
    for (int i = 0; i < NUM_CSTR; ++i) {
        constraint.clear();
        constraint.emplace_back(std::make_pair(1, cstr_coef[i][0]));
        constraint.emplace_back(std::make_pair(2, cstr_coef[i][1]));
        U.addFacet(constraint, 'L', rhs[i]);
        constraint.clear();
        constraint.emplace_back(std::make_pair(2, cstr_coef[i][0]));
        constraint.emplace_back(std::make_pair(1, cstr_coef[i][1]));
        U.addFacet(constraint, 'L', rhs[i]);
    }
}

void KAdaptableInfo_np1::makeVars() {
    X.clear();
    Y.clear();
    
    // objective variable (considered to be 1st-stage)
    // should always be defined and should always be declared first
    X.addVarType("O", 'C', -CPX_INFBOUND, +CPX_INFBOUND, 1);
    
    X.addVarType("w", 'B', 0, 1, NUM_UNC);
    C_W.assign(NUM_UNC, 0.0);
    
    // dual variable for the ambiguity set
    X.addVarType("psi", 'C', -3, 3, NUM_UNC);
    
    // y(i) : invest in project i after observing risk factors
    Y.addVarType("y", 'B', 0, 1, NUM_UNC);
    
    // define objective variable
    X.setVarObjCoeff(1.0, "O", 0);
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_np1::makeConsX() {
    ConstraintExpression temp;
    
    // B_X //
    B_X.clear();
    
    // C_XQ //
    C_XQ.clear();
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_np1::makeConsY(unsigned int l) {
    assert(C_XY.size() == B_Y.size());
    assert(C_XY.size() == C_XYQ.size());
    assert(numPolicies >= l);
    if (l == 0) {
        B_Y.clear();
        C_XY.clear();
        C_XYQ.clear();
    }
    ConstraintExpression temp;
    
    /////////
    // B_Y //
    /////////
    for (unsigned int k = B_Y.size(); k <= l; k++) {
        if (B_Y.size() < k + 1) B_Y.resize(k + 1);
        
        B_Y[k].clear();
        
        // bounds on y(i)
        for (int i = 0; i < NUM_UNC; ++i) {
            temp.clear();
            temp.addTermX(getVarIndex_2(k, "y", i), 1);
            
            temp.rowname("LB_y(" + std::to_string(i) + "," + std::to_string(k) + ")");
            temp.sign('G');
            temp.RHS(0);
            B_Y[k].emplace_back(temp);
            
            temp.rowname("UB_y(" + std::to_string(i) + "," + std::to_string(k) + ")");
            temp.sign('L');
            temp.RHS(1);
            B_Y[k].emplace_back(temp);
        }
    }
    
    
    //////////
    // C_XY //
    //////////
    for (unsigned int k = C_XY.size(); k <= l; k++) {
        if (C_XY.size() < k + 1) C_XY.resize(k + 1);
        C_XY[k].clear();
    }
    
    
    ///////////
    // C_XYQ //
    ///////////
    std::vector<double> mu{0.315, 0.315};
    for (unsigned int k = C_XYQ.size(); k <= l; k++) {
        if (C_XYQ.size() < k + 1) C_XYQ.resize(k + 1);
        
        C_XYQ[k].clear();
        
        // objective function
        
        temp.clear();
        temp.rowname("OBJ_CONSTRAINT(" + std::to_string(k) + ")");
        temp.sign('G');
        temp.RHS(0);
        temp.addTermX(getVarIndex_1("O", 0), 1);
        for (int i = 0; i < NUM_UNC; ++i) {
            temp.addTermProduct(getVarIndex_2(k, "y", i), i+1, 2.0);
            temp.addTermProduct(getVarIndex_1("psi", i), i+1, -1.0);
            temp.addTermX(getVarIndex_2(k, "y", i), -2.0*mu[i]);
            temp.addTermX(getVarIndex_1("psi", i), mu[i]);
            temp.addTermQ(i+1, -1.0);
            temp.addConst(mu[i]);
        }
        
        C_XYQ[k].emplace_back(temp);
    }
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_np1::setInstance() {
    hasInteger = 1;
    objectiveUnc = false;
    existsFirstStage = 1;
    numFirstStage = 1 + 2*NUM_UNC;
    numSecondStage = NUM_UNC;
    numPolicies = 1;
    wDetObjOnly = true;
    solfilename = "np-hard1";
    
    makeUncSet();
    makeVars();
    makeConsX();
    makeConsY(0);
    assert(isConsistentWithDesign());
}
