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

int main (int argc, char** argv) {

    assert(argc == 4);
    
    const bool heuristic_mode = true;
	const unsigned int Kmax = 4;
	KAdaptableInfo *pInfo;
        
	try {
        
//        PE data;
//        KAdaptableInfo_PE peInfo;
//        int size = 8;
//
//        gen_PE(data, size, 5); // set to 'true' to allow option of loans, origianl seed is 1, old data seed is 0.
//        peInfo.setInstance(data);
//        pInfo = peInfo.clone();

        
		// Generate the instance data
		KNP data;
		KAdaptableInfo_KNP_DD knpInfo;
        int size(std::stod(std::string(argv[1])));
        std::string filePath(argv[2]);
        int seed(std::stod(std::string(argv[3])));
        
        // std::cout << size << ", " << filePath;
        // test DRO performance or get RO solution
        //for(int seed = 0; seed < 20; seed++){
            gen_KNP(data, size, seed); //origianl seed is 1, old data seed is 5.
            knpInfo.setInstance(data);
            pInfo = knpInfo.clone();

            // CALL THE SOLVER
            KAdaptableSolver S(*pInfo);
            // std::ostream& out;
//            CPXENVptr envCopy = NULL;
//            CPXLPptr lpCopy = NULL;
            for (uint k = 1; k <= Kmax; k++){
                std::ofstream myfile;
                if(pInfo->getVarsX().getDefVarTypeSize("psi"))
                {
                    if(true)
                        myfile.open(filePath + "KNP_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv", std::ios_base::app);
                    else
                        myfile.open(filePath + "KNP_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
                }

                else
                {
                    if(true)
                        myfile.open(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv", std::ios_base::app);
                    else
                        myfile.open(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
                }
//                S.solve_L_Shaped(k, heuristic_mode, myfile, envCopy, lpCopy);
                S.solve_L_Shaped2(k, heuristic_mode, std::cout);
                S.reset(*pInfo);
                myfile.close();
            }

//            CPXXfreeprob(envCopy, &lpCopy);
//            CPXXcloseCPLEX (&envCopy);
            S.sense_ws.clear();
            S.rhs_ws.clear();
            S.rmatbeg_ws.clear();
            S.rmatind_ws.clear();
            S.rmatval_ws.clear();
            
            delete pInfo;
            pInfo = NULL;
        //}
        //test suboptimality of RO solution
//        std::ofstream myfileOut;
//        myfileOut.open(filePath + "KNP_sub_N=" + std::to_string(size) + ".csv", std::ofstream::out | std::ofstream::trunc);
//        for(uint k = 1; k <= Kmax; k++){
//            std::ifstream myfile(filePath + "KNP_RO_N=" + std::to_string(size) + "_K=" + std::to_string(k) + ".csv");
//            int seed = 0;
//            std::string line;
//
//            myfileOut << "k=" << std::to_string(k);
//            while(getline(myfile, line)){
//                std::string solstring;
//                std::vector<double> data;
//                std::vector<double> roSol;
//                // read RO solution from the file
//                std::stringstream ss(line);
//                while(std::getline(ss, solstring, ',')){
//                    data.push_back(std::stod(solstring));
//                }
//                seed = data[0]
//                roSol = std::vector<double>(data.begin() + 1, data.end());
//                gen_KNP(data, size, seed);
//                knpInfo.setInstance(data);
//                pInfo = knpInfo.clone();
//
//                // CALL THE SOLVER
//                KAdaptableSolver S(*pInfo);
//
//                std::vector<double> x;
//                std::vector<std::vector<double>> q;
//                S.solve_KAdaptability(k, false, x, q, roSol);
//
//                myfileOut << "," << x[0];
//            }
//            myfileOut << "\n";
//            myfile.close();
//        }
//        myfileOut.close();
	}
	catch (const int& e) {
		std::cerr << "Program ABORTED: Error number " << e << "\n";
	}


	return 0;
}

