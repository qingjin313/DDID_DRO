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
#include "problemInfo_pe.hpp"
#include "robustSolver.hpp"
#include <regex>

int main (int, char*[]) {

	const bool heuristic_mode = true;
	const unsigned int Kmax = 2;
	KAdaptableInfo *pInfo;
        
	try {
        
//        PE data;
//        KAdaptableInfo_PE peInfo;
//        int size = 8;
//
//        gen_PE(data, size, 5); // set to 'true' to allow option of loans, origianl seed is 1, old data seed is 0.
//        peInfo.setInstance(data);
//        pInfo = peInfo.clone();

        
        // --------- Qing comment out
		// Generate the instance data
		KNP data;
		KAdaptableInfo_KNP_DD knpInfo;
        int size = 5;
        
        // test DRO performance or get RO solution
//        for(int seed = 0; seed < 20; seed++){
//            gen_KNP(data, size, seed); //origianl seed is 1, old data seed is 5.
//            knpInfo.setInstance(data);
//            pInfo = knpInfo.clone();
//
//            // CALL THE SOLVER
//            KAdaptableSolver S(*pInfo);
//
//    //        std::vector<bool> wInput(size, 0);
//    //        std::vector<double> xInput;
//    //        std::vector<double> yInput(size, 0);
//    //        yInput[2] = 1;
//    //
//    //        S.setRobSolx(wInput, xInput);
//    //        S.setRobSoly(yInput);
//            // std::ostream& out;
//            CPXENVptr envCopy = NULL;
//            CPXLPptr lpCopy = NULL;
//            for (uint k = 1; k <= Kmax; k++){
//                std::ofstream myfile("/Users/lynn/Desktop/research/DRO/figures/KNP_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
//                S.solve_L_Shaped(k, heuristic_mode, myfile, envCopy, lpCopy);
//                S.reset(*pInfo);
//                myfile.close();
//            }
//
//            CPXXfreeprob(envCopy, &lpCopy);
//            CPXXcloseCPLEX (&envCopy);
//
//            delete pInfo;
//            pInfo = NULL;
//        }
        //test suboptimality of RO solution
        std::ofstream myfileOut("/Users/lynn/Desktop/research/DRO/figures/KNP_sub_N=" + std::to_string(size) + ".csv");
        for(uint k = 2; k <= Kmax; k++){
            std::ifstream myfile("/Users/lynn/Desktop/research/DRO/figures/KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
            int seed = 0;
            std::string line;
            
            myfileOut << "sol";
            while(getline(myfile, line)){
                std::string solstring;
                std::vector<double> roSol;
                // read RO solution from the file
                std::stringstream ss(line);
                while(std::getline(ss, solstring, ',')){
                    roSol.push_back(std::stod(solstring));
                }

                gen_KNP(data, size, seed); //origianl seed is 1, old data seed is 5.
                knpInfo.setInstance(data);
                pInfo = knpInfo.clone();

                // CALL THE SOLVER
                KAdaptableSolver S(*pInfo);

                // get solution of variable w, x and y
                int sizeW(pInfo->getVarsX().getDefVarTypeSize("w"));
                int sizeX(pInfo->getVarsX().getDefVarTypeSize("x"));
                int sizeY(pInfo->getNumSecondStage());

                std::vector<bool> wSol;
                wSol.resize(sizeW);
                std::transform(roSol.begin(), roSol.begin()+sizeW, wSol.begin(), [](double x) { return abs(x) > 1.E-5;});
                std::vector<double> xSol(roSol.begin()+sizeW, roSol.begin()+sizeW+sizeX);
                std::vector<double> ySol(roSol.begin()+sizeW+sizeX, roSol.end());
                assert(uint(ySol.size()) == sizeY*k);
                
                S.setRobSolx(wSol, xSol);
                std::vector<double> x;
                std::vector<std::vector<double>> q;
                S.solve_KAdaptability(k, false, x, q, ySol);

                myfileOut << "," << x[0];
                seed++;
            }
            myfileOut << "\n";
            myfile.close();
        }
        myfileOut.close();
	}
	catch (const int& e) {
		std::cerr << "Program ABORTED: Error number " << e << "\n";
	}


	return 0;
}

