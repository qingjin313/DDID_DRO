/***************************************************************************************/
/*                                                                                     */
/*  Copyright 2018 by Anirudh Subramanyam, Chrysanthos Gounaris and Wolfram Wiesemann  */
/*                                                                                     */
/*  Original Creator:                                                                  */
/*  Anirudh Subramanyam, Chrysanthos Gounaris and Wolfram Wiesemann                    */
/*                                                                                     */
/*  Contributors:                                                                      */
/*  Qing Jin, Angelos Georghiou, Phebe Vayanos and Grani A. Hanasusanto                */
/*                                                                                     */
/*  Additional Contributions by                                                        */
/*  Qing Jin, Angelos Georghiou, Phebe Vayanos and Grani A. Hanasusanto 2024:          */
/*    Add functionality to support DDID                                                */
/*                                                                                     */
/*  Licensed under the FreeBSD License (the "License").                                */
/*  You may not use this file except in compliance with the License.                   */
/*  You may obtain a copy of the License at                                            */
/*                                                                                     */
/*  https://www.freebsd.org/copyright/freebsd-license.html                             */
/*                                                                                     */
/***************************************************************************************/


#include "problemInfo_bb.hpp"
#include "problemInfo_knp_dd.hpp"
#include "robustSolver.hpp"
#include <regex>

int main (int argc, char** argv) {
    
	KAdaptableInfo *pInfo;
        
	try {
		// Generate the instance data
		KNP data;
		KAdaptableInfo_KNP_DD knpInfo;
        int k = 2;
        int size = 5;
        int seed = 0;
            
        gen_KNP(data, size, seed); 

        knpInfo.setInstance(data);
        pInfo = knpInfo.clone();

        // CALL THE SOLVER
        KAdaptableSolver S(*pInfo);

        S.solve_L_Shaped(k, std::cout);
    }
	catch (const int& e) {
		std::cerr << "Program ABORTED: Error number " << e << "\n";
	}

	return 0;
}

