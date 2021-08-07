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
        int size = 8;

		gen_KNP(data, size, 2); //origianl seed is 1, old data seed is 5.
		knpInfo.setInstance(data);
        pInfo = knpInfo.clone();

		// Other examples from the paper
		/*
		SPP data;
		KAdaptableInfo_SPP sppInfo;
		gen_SPP(data, 20, 0);
		sppInfo.setInstance(data);
		pInfo = sppInfo.clone();
		*/
		/*
		PSP data;
		KAdaptableInfo_PSP pspInfo;
		gen_PSP(data, 3, false); // set to 'true' for piecewise affine decision rules
		pspInfo.setInstance(data);
		pInfo = pspInfo.clone();
		*/

		// CALL THE SOLVER
		KAdaptableSolver S(*pInfo);
        
        std::vector<double> sol;
        sol.emplace_back(0.0);
        CPXENVptr envCopy = NULL;
        CPXLPptr lpCopy = NULL;
        for (uint k = 1; k <= Kmax; k++){
            S.solve_L_Shaped(k, heuristic_mode, sol, envCopy, lpCopy);
            S.reset(*pInfo);
        }
        
        CPXXfreeprob(envCopy, &lpCopy);
        CPXXcloseCPLEX (&envCopy);
		// Uncomment to also solve without warm-start -- only in exact mode
		// S.solve_KAdaptability(K, false, x);

		delete pInfo;
		pInfo = NULL;
	}
	catch (const int& e) {
		std::cerr << "Program ABORTED: Error number " << e << "\n";
	}


	return 0;
}

