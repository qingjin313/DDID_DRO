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


#ifndef PE_INSTANCE_HPP
#define PE_INSTANCE_HPP


#include <vector>
#include <cmath>
#include <cassert>
#include <string>
#include <algorithm> //std::sort, std::random_shuffle
#include <random>
#include <array>

#define PE_NFETURES 4

/**
 * Class represesnting an instance of the Preference Elicitation
 * 
 * Note: All arrays and matrices are 1-indexed
 */
struct PE {
	/** # of items */
	int N;

	/** # of questions */
	int Q;

	/** budget of uncertainty*/
	// double Gamma;

	/** Risk factor coefficients for features */
    /** The first entry store the l-1 norm of the features*/
	std::vector<std::array<double, 1 + PE_NFETURES> > phi;
    
    /** name  of the solution file*/
    std::string solfilename;
};




/**
 * Construct an instance of the Knapsack Problem
 * @param data          (reference to) instance of Knapsack Problem
 * @param n             number of items
 * @param seed          seed that is unique to this instance
 */
static inline void gen_PE(PE& data, unsigned int n, int seed = 1) {

	if (n < 5) {
		fprintf(stderr, "warning: N < 5 in Knapsack Problem. Setting N = 5.\n");
		n = 5;
	}

	// seed for re-producibility
	std::default_random_engine gen (1111 + seed);
	std::uniform_real_distribution<double> interval_1 (0.0, 10.0);

	// # of items
	data.N = n;
	data.Q = n/2;
    
    data.phi.resize(n);
    for (n = 0; (int)n < data.N; n++) {
        data.phi[n][0] = 0.0;
        for (uint i = 1; i <= PE_NFETURES; i++){
            data.phi[n][i] = interval_1(gen);
            data.phi[n][0] += data.phi[n][i];
        }
    }

        
	// set solution file name
	data.solfilename = "PE-n" + std::to_string(data.N) + "-s" + std::to_string(seed) + ".opt";
}
#undef PE_NFETURES
#endif
