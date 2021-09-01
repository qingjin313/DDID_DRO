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
#define USE_GAMMA 1

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
	std::pair<bool, double> gamma;

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
	std::uniform_real_distribution<double> interval_1 (0.0, 1.0);

	// # of items
	data.N = n;
	data.Q = n/2;
    
    data.phi.resize(n);
    for (n = 0; (int)n < data.N; n++) {
        // generate F-1 numbers between 0 and 1
        std::array<double, PE_NFETURES - 1> xn;
        for (size_t f = 0; f < xn.size(); f++) {
            xn[f] = interval_1(gen);
        }

        // sort these numbers
        std::sort(xn.begin(), xn.end());

        // generate F numbers such that their sum = 1
        data.phi[n][0] = 1.0;
        data.phi[n][1] = xn[0];
        for (size_t f = 2; f < PE_NFETURES; f++) {
            data.phi[n][f] = xn[f-1] - xn[f-2];
        }
        data.phi[n][PE_NFETURES] = 1 - xn[PE_NFETURES-2];

        // randomize in order to eliminate bias
        std::mt19937 g(static_cast<uint32_t>(1));
        std::shuffle(data.phi[n].begin() + 1, data.phi[n].end(), g);

        // check (just to be safe)
        #ifndef NDEBUG
        double check_phi = 0.0;
        for (size_t f = 1; f <= PE_NFETURES; f++) {
            check_phi += data.phi[n][f];
        }
        assert(std::abs(1 - check_phi) <= 1.E-12);
        #endif
    }
    
    data.gamma.first = USE_GAMMA;
    data.gamma.second = USE_GAMMA*0.05;
    
	// set solution file name
	data.solfilename = "pre-n" + std::to_string(data.N) + "-s" + std::to_string(seed) + ".opt";
}
#undef PE_NFETURES
#endif
