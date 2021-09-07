//
//  problemInfo_pe.cpp
//  kadaptability
//
//  Created by 靳晴 on 6/21/21.
//

#include "problemInfo_pe.hpp"
#include <cassert>

#define USE_SINGLE 1
#define USE_DRO 1

void KAdaptableInfo_PE::makeUncSet() {
    U.clear();
    
    // define uncertain risk factors
    for (unsigned int f = 1; f < data.phi[0].size(); ++f) {
        U.addParam(0, 0, 1);
    }
    
    // get the normalized parameter
    double norm = 0.0;
    for (int i = 0; i < data.N; ++i) {
        if(data.phi[i][0] > norm)
            norm = data.phi[i][0];
    }
    
    std::vector<std::vector<std::pair<int, double> >> constraints_pro;
    // define uncertain untility and calculate the relation between it and the features
    for (int i = 0; i < data.N; ++i) {
        // define the expression for utility
        std::vector<std::pair<int, double> > constraint;
        for (int f = 1; f < int(data.phi[i].size()); f++) {
            constraint.emplace_back(std::make_pair(f, data.phi[i][f]*0.5/norm));
        }
        
        // add uncertain untility
        U.addParam(0.5, 0.0, 1.0);
        // update contraint term for the profit i
        if(data.gamma.first)
            constraint.emplace_back(std::make_pair(data.phi[0].size()+data.N+i, 1.0));
        
        constraint.emplace_back(std::make_pair(data.phi[0].size()+i, -1.0));
        constraints_pro.emplace_back(constraint);
        // set observation decision associated with this uncertain parameter
        U.setObsVar(std::make_pair(data.phi[0].size()+i, i));
    }
    
    if(data.gamma.first){
        // add the true epsilon parameter
        for (int i = 0; i < data.N; ++i) {
            U.addParam(0, -data.gamma.second, data.gamma.second);
        }
        // add the parameter of the absoulte value of epsilon parameter
        for (int i = 0; i < data.N; ++i) {
            U.addParam(0, 0.0, data.gamma.second);
        }
    }
    
    if(USE_DRO){
        U.addParam(0, 0, data.N);
        numAmbCstr += 1;
        if(USE_SINGLE){
            // bounds for single derivation to the single nominal profit
            for(int i = 0; i<= data.N-1; i++){
                U.addParam(0, 0, 1);
                numAmbCstr += 1;
            }
        }
    }
    
    // add constraints for the profits
    for (int i = 0; i < data.N; ++i) {
        U.addFacet(constraints_pro[i], 'E', -0.5);
    }
    
    // add constraints for uncertain budget
    if(data.gamma.first){
        assert(data.gamma.second > 0);
        std::vector<std::pair<int, double> > cstr;
        for (int i = 0; i < data.N; ++i) {
            std::vector<std::pair<int, double> > cstr_pos;
            std::vector<std::pair<int, double> > cstr_neg;
            
            cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, -1.0));
            cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + 2*data.N + i, 1.0));
            cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, 1.0));
            cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + 2*data.N + i, 1.0));
            
            U.addFacet(cstr_pos, 'G', 0.0);
            U.addFacet(cstr_neg, 'G', 0.0);
            
            cstr.emplace_back(std::make_pair(data.phi[0].size() + 2*data.N + i, 1.0));
        }
        U.addFacet(cstr, 'L', data.gamma.second);
    }
    
    
    if(USE_DRO){
        double nominalTotal = 0.5*data.N;
        std::vector<std::pair<int, double> > cstr_pos;
        std::vector<std::pair<int, double> > cstr_neg;
        
        
        cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (1+2*data.gamma.first)*data.N, 1.0));
        cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (1+2*data.gamma.first)*data.N, 1.0));
        for (int i = 0; i <= data.N-1; ++i){
            cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + i, -1.0));
            cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + i, 1.0));
        }
        U.addFacet(cstr_pos, 'G', -nominalTotal);
        U.addFacet(cstr_neg, 'G', nominalTotal);
        
        if(USE_SINGLE){
            for(int i = 0; i<= data.N-1; i++){
                cstr_pos.clear();
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (1+2*data.gamma.first)*data.N +1 + i, 1.0));
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + i, -1.0));
                U.addFacet(cstr_pos, 'G', -0.5);

                cstr_neg.clear();
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (1+2*data.gamma.first)*data.N + 1 + i, 1.0));
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + i, 1.0));
                U.addFacet(cstr_neg, 'G', 0.5);
            }
        }
    }
    
//    int stat;
//    CPXXwriteprob(U.getENVObject(), U.getLPObject(&stat), "/Users/lynn/Desktop/research/DRO/BnB/model_output/testK_before", "LP");
    
    numFirstStage += numAmbCstr;
}

void KAdaptableInfo_PE::makeVars() {
    X.clear();
    Y.clear();
    
    // objective variable (considered to be 1st-stage)
    // should always be defined and should always be declared first
    X.addVarType("O", 'C', -CPX_INFBOUND, +CPX_INFBOUND, 1);
    
    // dual variable for the ambiguity set
    if(USE_DRO)
        X.addVarType("psi", 'C', 0, 100, 1+data.N*USE_SINGLE );
    
    // x(i) : invest in project i before observing risk factors
    X.addVarType("w", 'B', 0, 1, data.N);
    C_W.assign(data.N, 0.0);
    
    // y(i) : invest in project i after observing risk factors
    Y.addVarType("y", 'B', 0, 1, data.N);
    
    // define objective variable
    X.setVarObjCoeff(1.0, "O", 0);
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_PE::makeConsX() {
    ConstraintExpression temp;
    
    // B_X //
    B_X.clear();
    
    // bounds on w(i)
    for (int i = 0; i < data.N; ++i) {
        temp.clear();
        temp.addTermX(getVarIndex_1("w", i), 1);
        
        temp.rowname("LB_w(" + std::to_string(i) + ")");
        temp.sign('G');
        temp.RHS(0);
        B_X.emplace_back(temp);
        
        temp.rowname("UB_w(" + std::to_string(i) + ")");
        temp.sign('L');
        temp.RHS(1);
        B_X.emplace_back(temp);
    }
    
    // C_X //
    C_X.clear();
    temp.clear();
    for (int i = 0; i < data.N; ++i) {
        temp.addTermX(getVarIndex_1("w", i), 1);
    }
    temp.rowname("BUDGET");
    temp.sign('E');
    temp.RHS(data.Q);
    C_X.emplace_back(temp);
    
    // C_XQ //
    C_XQ.clear();
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_PE::makeConsY(unsigned int l) {
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
        for (int i = 0; i < data.N; ++i) {
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
        
        // deterministic cost constraint
        temp.clear();
        for (int i = 0; i < data.N; ++i) {
            temp.rowname("RECOMEND(" + std::to_string(k) + ")");
            temp.sign('E');
            temp.RHS(1.0);
            temp.addTermX(getVarIndex_2(k, "y", i), 1.0);
        }
        C_XY[k].emplace_back(temp);
    }
    
    
    ///////////
    // C_XYQ //
    ///////////
    for (unsigned int k = C_XYQ.size(); k <= l; k++) {
        if (C_XYQ.size() < k + 1) C_XYQ.resize(k + 1);
        
        C_XYQ[k].clear();
        
        // objective function
        
        double nomProfit = 0.5*data.N;
        temp.clear();
        temp.rowname("OBJ_CONSTRAINT(" + std::to_string(k) + ")");
        temp.sign('G');
        temp.RHS(0);
        temp.addTermX(getVarIndex_1("O", 0), 1);
        for (int i = 0; i < data.N; ++i) {
            temp.addTermProduct(getVarIndex_2(k, "y", i), data.phi[0].size() + i);
            // temp.addTermProduct(getVarIndex_1("psi", 0), data.phi[0].size() + i, -1.0);
        }
        
        if(USE_DRO)
        {
            temp.addTermProduct(getVarIndex_1("psi", 0), data.phi[0].size() + (1+2*data.gamma.first)*data.N, 1.0);
            temp.addTermX(getVarIndex_1("psi", 0), -0.15*nomProfit/sqrt(data.N));
            
            if(USE_SINGLE){
                for (int i = 0; i <= data.N-1; ++i)
                {
                    temp.addTermProduct(getVarIndex_1("psi", i + 1), data.phi[0].size() + (1+2*data.gamma.first)*data.N + 1 + i, 1.0);
                    temp.addTermX(getVarIndex_1("psi", i + 1), -0.15*0.5);
                }
            }
            
        }
        
        C_XYQ[k].emplace_back(temp);
    }
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_PE::setInstance(const PE& d) {
    data = d;
    hasInteger = 1;
    objectiveUnc = true;
    existsFirstStage = 1;
    numFirstStage = 1 + data.N;
    numSecondStage = data.N;
    numPolicies = 1;
    wDetObjOnly = true;
    solfilename = data.solfilename;
    
    makeUncSet();
    makeVars();
    makeConsX();
    makeConsY(0);
    assert(isConsistentWithDesign());
}
