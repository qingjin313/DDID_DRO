/******************************************************************************************/
/*                                                                                        */
/*  Copyright 2024 by Qing Jin, Angelos Georghiou, Phebe Vayanos and Grani A. Hanasusanto */
/*                                                                                        */
/*  Licensed under the FreeBSD License (the "License").                                   */
/*  You may not use this file except in compliance with the License.                      */
/*  You may obtain a copy of the License at                                               */
/*                                                                                        */
/*  https://www.freebsd.org/copyright/freebsd-license.html                                */
/*                                                                                        */
/******************************************************************************************/

#include "problemInfo_bb.hpp"
#include <cassert>

#define CSTR_UNC 1
#define USE_PAIR 0
#define USE_DRO 1
//-----------------------------------------------------------------------------------

void KAdaptableInfo_BB::makeUncSet() {
    U.clear();
    numAmbCstr = 0;

    // define uncertain risk factors
    for (unsigned int f = 1; f < data.phi[0].size(); ++f) {
        U.addParam(0, -1, 1);
    }
    
    std::vector<std::vector<std::pair<int, double> >> constraints_pro;
    double profit_dev = 0.0;
    // define uncertain profit and calculate the relation between it and the risk factors
    for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
        // calculate the nominal value of the profit
        double high = data.profit[i];
        double low = data.profit[i];
        
        // define the expression for profit
        std::vector<std::pair<int, double> > constraint;
        for (int f = 1; f < int(data.ksi[i].size()); f++) {
            double coef = data.ksi[i][f] * data.profit[i] * 0.5;
            high += abs(coef);
            low -= abs(coef);
            if (coef != 0.0) {
                constraint.emplace_back(std::make_pair(f, coef));
            }
        }
        profit_dev += high - data.profit[i];
        // add uncertain profit
        U.addParam(data.profit[i], low, high);
        // update contraint term for the profit i
        constraint.emplace_back(std::make_pair(data.phi[0].size()+i, -1.0));
        constraints_pro.emplace_back(constraint);
        // set observation decision associated with this uncertain parameter
        U.setObsVar(std::make_pair(data.phi[0].size()+i, i));
    }
    
    std::vector<std::vector<std::pair<int, double> >> constraints_cost;
    double cost_dev = 0.0;
    if(CSTR_UNC){
        // define uncertain cost and calculate the relation between it and the risk factors
        for (int i = 0; i <= data.N-1; ++i) if (data.cost[i] != 0.0) {
            // calculate the nominal cost of the profit
            double high = data.cost[i];
            double low = data.cost[i];
            
            // define the expression for cost
            std::vector<std::pair<int, double> > constraint;
            for (int f = 1; f < int(data.phi[i].size()); f++) {
                double coef = data.phi[i][f] * data.cost[i] * 0.5;
                high += abs(coef);
                low -= abs(coef);
                if (coef != 0.0) {
                    constraint.emplace_back(std::make_pair(f, coef));
                }
            }
            cost_dev += high - data.cost[i];
            // add uncertain cost
            U.addParam(data.cost[i], low, high);
            // update contraint term for the cost i
            constraint.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, -1.0));
            constraints_cost.emplace_back(constraint);
            // set observation decision associated with this uncertain parameter
            U.setObsVar(std::make_pair(data.phi[0].size()+ data.N + i, i));
        }
    }
    
    if(USE_DRO){
        // lifting uncertainty set for the ambiguity set
        // bounds for total deviation to the total nominal profit
        U.addParam(0, 0, profit_dev);
        numAmbCstr += 1;
        
        // bounds for total deviation to the total nominal cost
        if(CSTR_UNC){
            U.addParam(0, 0, cost_dev);
            numAmbCstr += 1;
        }
        
        if(USE_PAIR){
            // bounds for single derivation to the single nominal profit
            for(int i = 0; i<= data.N-2; i++){
                // U.addParam(0, 0, profit_high[i]);
                U.addParam(0, 0, data.profit[i]+data.profit[i+1]);
                numAmbCstr += 1;
            }
            
            if(CSTR_UNC){
                // bounds for single derivation to the single nominal cost
                for(int i = 0; i<= data.N-2; i++){
                    // U.addParam(0, 0, cost_high[i]);
                    U.addParam(0, 0, data.cost[i]+data.cost[i+1]);
                    numAmbCstr += 1;
                }
            }
        }
    }
    
    // add constraints for the profits
    for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
        U.addFacet(constraints_pro[i], 'E', -data.profit[i]);
    }
    
    // add constraints for the costs
    if(CSTR_UNC){
        for (int i = 0; i <= data.N-1; ++i) if (data.cost[i] != 0.0) {
            U.addFacet(constraints_cost[i], 'E', -data.cost[i]);
        }
    }
    
    if(USE_DRO){
        std::vector<std::pair<int, double> > cstr_pos;
        std::vector<std::pair<int, double> > cstr_neg;
        double nominalTotal = 0.0;
        
        cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*data.N, 1.0));
        cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*data.N, 1.0));
        for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0){
            nominalTotal += data.profit[i];
            cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + i, -1.0));
            cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + i, 1.0));
        }
        // std::cout << nominalTotal << std::endl;
        U.addFacet(cstr_pos, 'G', -nominalTotal);
        U.addFacet(cstr_neg, 'G', nominalTotal);
        
        if(CSTR_UNC){
            cstr_pos.clear();
            cstr_neg.clear();
            nominalTotal = 0.0;

            cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*data.N + 1, 1.0));
            cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*data.N + 1, 1.0));
            for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0){
                nominalTotal += data.cost[i];
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, -1.0));
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, 1.0));
            }
            // std::cout << nominalTotal << std::endl;
            U.addFacet(cstr_pos, 'G', -nominalTotal);
            U.addFacet(cstr_neg, 'G', nominalTotal);
        }
        
        if(USE_PAIR){
            for(int i = 0; i<= data.N-2; i++){
                cstr_pos.clear();
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*(data.N + 1) + i, 1.0));
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + i, -1.0));
                cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + i + 1, 1.0));
                // U.addFacet(cstr_pos, 'G', -data.profit[i] + data.profit[i+1]);
                U.addFacet(cstr_pos, 'G', 0.0);
                
                cstr_neg.clear();
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (1+CSTR_UNC)*(data.N + 1) + i, 1.0));
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + i, 1.0));
                cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + i + 1, -1.0));
                // U.addFacet(cstr_neg, 'G', data.profit[i] - data.profit[i+1]);
                U.addFacet(cstr_neg, 'G', 0.0);
            }
            
            if(CSTR_UNC){
                for(int i = 0; i<= data.N-2; i++){
                    cstr_pos.clear();
                    cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + (2+CSTR_UNC)*data.N + 1 + CSTR_UNC - USE_PAIR + i, 1.0));
                    cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, -1.0));
                    cstr_pos.emplace_back(std::make_pair(data.phi[0].size() + data.N + i + 1, 1.0));
                    // U.addFacet(cstr_pos, 'G', -data.cost[i] + data.cost[i+1]);
                    U.addFacet(cstr_pos, 'G', 0.0);
                    
                    cstr_neg.clear();
                    cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + (2+CSTR_UNC)*data.N + 1 + CSTR_UNC - USE_PAIR + i, 1.0));
                    cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + data.N + i, 1.0));
                    cstr_neg.emplace_back(std::make_pair(data.phi[0].size() + data.N + i + 1, -1.0));
                    // U.addFacet(cstr_neg, 'G', data.cost[i] - data.cost[i+1]);
                    U.addFacet(cstr_neg, 'G', 0.0);
                }
            }
        }
    }
    numFirstStage += numAmbCstr;
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_BB::makeVars() {
    X.clear();
    Y.clear();

    // objective variable (considered to be 1st-stage)
    // should always be defined and should always be declared first
    X.addVarType("O", 'C', -CPX_INFBOUND, +CPX_INFBOUND, 1);
    
    // x(i) : observe box i
    X.addVarType("w", 'B', 0, 1, data.N);
    C_W.assign(data.N, 0.0);
    
    // dual variable for the ambiguity set
    if(USE_DRO)
        X.addVarType("psi", 'C', 0, 100, (1+CSTR_UNC)*(1+data.N*USE_PAIR - USE_PAIR) );

    // y(i) : take box i
    Y.addVarType("y", 'B', 0, 1, data.N);
    
    // z(i) : change box i
    // Y.addVarType("z", 'B', 0, 1, data.N);

    // define objective variable
    X.setVarObjCoeff(1.0, "O", 0);
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_BB::makeConsX() {
    ConstraintExpression temp;

    /////////
    // B_X //
    /////////
    B_X.clear();

    // bounds on w(i)
    for (int i = 0; i <= data.N-1; ++i) {
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



    /////////
    // C_X //
    /////////
    C_X.clear();
    if(!CSTR_UNC){
        temp.clear();
        for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
            temp.rowname("BUDGET");
            temp.sign('L');
            temp.RHS(data.B);
            temp.addTermX(getVarIndex_1("w", i), data.cost[i]);
        }
        C_X.emplace_back(temp);
        //temp.print();
    }


    //////////
    // C_XQ //
    //////////
    C_XQ.clear();
    // All the XQ constraint should be dualized before adding into the problem, or it may cause error
    // In this problem, this constraint only involve w, so we add it into the problem through the function robustifyW in the outerloop
    if(CSTR_UNC){
        temp.clear();
        temp.rowname("BUDGET");
        temp.sign('L');
        temp.RHS(data.B);
        for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
            temp.addTermProduct(getVarIndex_1("w", i), data.phi[0].size() + + data.N + i, 1.0);
        }
        C_XQ.emplace_back(temp);
    }
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_BB::makeConsY(unsigned int l) {
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
        for (int i = 0; i <= data.N-1; ++i) {
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

        // take only after open
        for (int i = 0; i <= data.N-1; ++i) {
            temp.clear();
            temp.rowname("TAKE(" + std::to_string(i) + "," + std::to_string(k) + ")");
            temp.sign('L');
            temp.RHS(0.0);
            temp.addTermX(getVarIndex_1("w", i), -1.0);
            temp.addTermX(getVarIndex_2(k, "y", i), 1.0);
            // temp.addTermX(getVarIndex_2(k, "z", i), -1.0);

            C_XY[k].emplace_back(temp);
            //temp.print();
        }
        
        // take only one
        temp.clear();
        for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
            temp.rowname("ONE(" + std::to_string(k) + ")");
            temp.sign('L');
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
        
        double nomProfit = 0.0;
        double nomCost = 0.0;
        temp.clear();
        temp.rowname("OBJ_CONSTRAINT(" + std::to_string(k) + ")");
        temp.sign('G');
        temp.RHS(0.0);
        temp.addTermX(getVarIndex_1("O", 0), 1);
        for (int i = 0; i <= data.N-1; ++i) if (data.profit[i] != 0.0) {
            temp.addTermProduct(getVarIndex_2(k, "y", i), data.phi[0].size() + i);
            // temp.addTermX(getVarIndex_2(k, "z", i), -0.1);
            // temp.addTermProduct(getVarIndex_1("w", i), data.phi[0].size() + data.N + i, -1.0);
            nomProfit += data.profit[i];
            nomCost += data.cost[i];
        }
        if(USE_DRO){
            // std::cout << nomProfit << std::endl;
            // std::cout << nomCost << std::endl;
            temp.addTermProduct(getVarIndex_1("psi", 0), data.phi[0].size() + (1+CSTR_UNC)*data.N, 1.0);
            temp.addTermX(getVarIndex_1("psi", 0), -data.dro_size*nomProfit/sqrt(data.N));
            
            if(CSTR_UNC){
                temp.addTermProduct(getVarIndex_1("psi", 1), data.phi[0].size() + (1+CSTR_UNC)*data.N + 1, 1.0);
                temp.addTermX(getVarIndex_1("psi", 1), -data.dro_size*nomCost/sqrt(data.N));
            }
            
            if(USE_PAIR){
                for (int i = 0; i <= data.N-2; ++i)
                {
                    temp.addTermProduct(getVarIndex_1("psi", i + 1 + CSTR_UNC), data.phi[0].size() + (1+CSTR_UNC)*(data.N + 1) + i, 1.0);
                    temp.addTermX(getVarIndex_1("psi", i + 1 + CSTR_UNC), -0.15/2*(data.profit[i]+data.profit[i+1]));
                    // temp.addTermX(getVarIndex_1("psi", i + 1 + CSTR_UNC), -0.15);
                }
                
                if(CSTR_UNC){
                    for (int i = 0; i <= data.N-2; ++i)
                    {
                        temp.addTermProduct(getVarIndex_1("psi", i + 1 + CSTR_UNC - USE_PAIR + data.N) , data.phi[0].size() + (2+CSTR_UNC)*data.N + 1 + CSTR_UNC - USE_PAIR + i, 1.0);
                        temp.addTermX(getVarIndex_1("psi", i + 1 + CSTR_UNC - USE_PAIR + data.N), -0.15/2*(data.cost[i]+data.cost[i+1]));
                        // temp.addTermX(getVarIndex_1("psi", i + 1 + CSTR_UNC - USE_PAIR + data.N), -0.75);
                    }
                }
            }
        }
        
        C_XYQ[k].emplace_back(temp);
        // C_XYQ[k][0].print();

    }
}

//-----------------------------------------------------------------------------------

void KAdaptableInfo_BB::setInstance(const BB& d) {
    data = d;
    hasInteger = 1;
    objectiveUnc = !CSTR_UNC;
    existsFirstStage = 1;
    numFirstStage = 1 + data.N;
    numSecondStage = data.N;
    numPolicies = 1;
    wDetObjOnly = false;
    solfilename = data.solfilename;

    makeUncSet();
    makeVars();
    makeConsX();
    makeConsY(0);
    assert(isConsistentWithDesign());
}

//-----------------------------------------------------------------------------------
