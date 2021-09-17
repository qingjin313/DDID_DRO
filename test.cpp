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


#include "problemInfo_knp.hpp"
#include "problemInfo_spp.hpp"
#include "problemInfo_psp.hpp"
#include "problemInfo_test1.hpp"
#include "problemInfo_knp_dd.hpp"
#include "problemInfo_bb.hpp"
#include "problemInfo_pe.hpp"
#include "robustSolver.hpp"
#include <regex>

int main (int argc, char** argv) {

    assert(argc == 4);
    
    const bool heuristic_mode = true;
	const unsigned int Kmax = 2;
	KAdaptableInfo *pInfo;
        
	try {
        
        BB data;
        KAdaptableInfo_BB bbInfo;
        int size = 10;

        gen_BB(data, size, 0); // set to 'true' to allow option of loans, origional seed is 1, old data seed is 0.
        bbInfo.setInstance(data);
        pInfo = bbInfo.clone();

        KAdaptableSolver S(*pInfo);
        
//        std::vector<bool> w = {0,0,0,0,0,0,1,0,0,1};
//        std::vector<double> x;
//        std::vector<std::vector<double>> q;
//        S.setW(w);
//
//        S.setBestU(+CPX_INFBOUND);
//        S.solve_KAdaptability(1, false, x, q);
        for (uint k = 1; k <= Kmax; k++){
            S.solve_L_Shaped2(k, true, std::cout);
            S.reset(*pInfo);
        }
//
//        std::vector<bool> w = {0,0,0,0,1,0,1,0,0,1};
//        std::vector<double> x;
//        std::vector<std::vector<double>> q;
//        S.setW(w);
////
////        S.setBestU(+CPX_INFBOUND);
//        S.solve_KAdaptability(3, false, x, q);
//        for (uint k = 1; k <= Kmax; k++){
//            S.solve_L_Shaped2(k, true, std::cout);
//            S.reset(*pInfo);
//        }
        
		// Generate the instance data
//		KNP data;
//		KAdaptableInfo_KNP_DD knpInfo;
//        int size(std::stod(std::string(argv[1])));
//        std::string filePath(argv[2]);
//        int seed(std::stod(std::string(argv[3])));
//
////        gen_KNP(data, size, 0); //origianl seed is 1, old data seed is 5.
////        knpInfo.setInstance(data);
////        pInfo = knpInfo.clone();
////
////        // CALL THE SOLVER
////        KAdaptableSolver S(*pInfo);
////
////        S.solve_L_Shaped2(1, true, std::cout);
//
//        // std::cout << size << ", " << filePath;
        // test DRO performance or get RO solution
        //for(int seed = 0; seed < 20; seed++){
//        if(seed >= 0){
//            gen_KNP(data, size, seed); //origianl seed is 1, old data seed is 5.
//            knpInfo.setInstance(data);
//            pInfo = knpInfo.clone();
//
//            // CALL THE SOLVER
//            KAdaptableSolver S(*pInfo);
//
//            for (uint k = 1; k <= Kmax; k++){
//                std::ofstream myfile;
//                if(pInfo->getVarsX().getDefVarTypeSize("psi"))
//                {
//                    if(true)
//                        myfile.open(filePath + "KNP_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv", std::ios_base::app);
//                    else
//                        myfile.open(filePath + "KNP_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
//                }
//
//                else
//                {
//                    if(true)
//                        myfile.open(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv", std::ios_base::app);
//                    else
//                        myfile.open(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
//                }
//                S.solve_L_Shaped2(k, heuristic_mode, std::cout);
//                S.reset(*pInfo);
//                myfile.close();
//            }
//
//            S.sense_ws.clear();
//            S.rhs_ws.clear();
//            S.rmatbeg_ws.clear();
//            S.rmatind_ws.clear();
//            S.rmatval_ws.clear();
//
//            delete pInfo;
//            pInfo = NULL;
//        }
        //test suboptimality of RO solution
//        else{
//            for(uint k = 3; k <= Kmax; k++){
//                std::ofstream myfileOut;
//                myfileOut.open(filePath + "KNP_sub_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv", std::ios_base::app);
//
//                std::ifstream myfile(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
//                std::string line;
//
//                while(getline(myfile, line)){
//                    std::string solstring;
//                    std::vector<double> roData;
//                    std::vector<double> roSol;
//                    // read RO solution from the file
//                    std::stringstream ss(line);
//                    while(std::getline(ss, solstring, ',')){
//                        roData.push_back(std::stod(solstring));
//                    }
//                    seed = roData[0];
//                    // myfileOut << seed << ",";
//                    roSol = std::vector<double>(roData.begin() + 1, roData.end());
//                    gen_KNP(data, size, seed);
//                    knpInfo.setInstance(data);
//                    pInfo = knpInfo.clone();
//                    int numW = pInfo->getVarsX().getDefVarTypeSize("w");
//                    int numX = pInfo->getVarsX().getDefVarTypeSize("x");
//                    int numY = pInfo->getVarsY().getDefVarTypeSize("y");
//
//                    // CALL THE SOLVER
//                    KAdaptableSolver S(*pInfo);
//
//                    std::vector<double> x;
//                    std::vector<std::vector<double>> q;
//
//                    for(uint ki = 0; ki < k; ki++){
//                        x.clear();
//                        q.clear();
//                        std::vector<double> roSolNew;
//                        roSolNew = std::vector<double>(roSol.begin(), roSol.begin() + numW + numX);
//                        roSolNew.insert(roSolNew.end(), roSol.begin() + numW + numX + ki*numY, roSol.end());
//                        if(ki)
//                            roSolNew.insert(roSolNew.end(), roSol.begin() + numW + numX, roSol.begin() + numW + numX + ki*numY);
//                        S.solve_KAdaptability(k, false, x, q, roSolNew);
//                        if(ki < (k - 1)){
//                            myfileOut << x[0] << ",";
//                            S.reset(*pInfo);
//                        }
//                        else{
//                            myfileOut << x[0];
//                            delete pInfo;
//                            pInfo = NULL;
//                        }
//                    }
//                }
//                myfile.close();
//                myfileOut.close();
//            }
//        }
	}
	catch (const int& e) {
		std::cerr << "Program ABORTED: Error number " << e << "\n";
	}


	return 0;
}

