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


#ifndef BB_INSTANCE_HPP
#define BB_INSTANCE_HPP


#include <vector>
#include <cmath>
#include <cassert>
#include <string>
#include <algorithm> //std::sort, std::random_shuffle
#include <random>
#include <array>

#define BB_NFACTORS 4

/**
 * Class represesnting an instance of the Knapsack Problem
 * (as defined in the K-Adaptability paper)
 * 
 * Note: All arrays and matrices are 1-indexed
 */
struct BB {
	/** # of items */
	int N;

	/** Cost of items */
	std::vector<double> cost;

	/** Budget (size of knapsack) */
	double B;

	/** Profit of items */
	std::vector<double> profit;

	/** Risk factor coefficients for costs */
	std::vector<std::array<double, 1 + BB_NFACTORS> > phi;

	/** Risk factor coefficients for profits */
	std::vector<std::array<double, 1 + BB_NFACTORS> > ksi;

	/** Solution file name */
	std::string solfilename;

};




/**
 * Construct an instance of the Best Box Problem
 * @param data          (reference to) instance of Best Box Problem
 * @param n             number of items
 * @param seed          seed that is unique to this instance
 * @param mixed_integer true if mixed-integer knapsack problem, otherwise pure integer
 */
static inline void gen_BB(BB& data, unsigned int n, int seed = 1) {

	if (n < 5) {
		fprintf(stderr, "warning: N < 5 in Best Box Problem. Setting N = 5.\n");
		n = 5;
	}

	// seed for re-producibility
	std::default_random_engine gen (1111 + seed);
	std::uniform_real_distribution<double> interval_1 (-1.0, 1.0);
	std::uniform_real_distribution<double> interval_10 (0.0, 10.0);

	// # of items
	data.N = n;
	data.B = 0.0;
    
    // Cost of items
    data.cost.assign(data.N, 0.0);
    for (n = 0; (int)n <= data.N-1; n++) {
        data.cost[n] = interval_10(gen);
        data.B += data.cost[n];
    }

    // Budget (size of knapsack)
    data.B /= 2.0;


    // Profit of items
    data.profit.assign(data.N, 0.0);
    for (n = 0; (int)n <= data.N-1; n++) {
        data.profit[n] = data.cost[n] / 5.0;
    }


    // Risk factor coefficients
    data.phi.resize(data.N);
    data.ksi.resize(data.N);
    
    for (n = 0; (int)n <= data.N-1; n++) {
        bool reSample = true;
        while(reSample){
            double total = 0.0;
            // generate F numbers between -1 and 1
            for (size_t f = 1; f <= BB_NFACTORS; f++) {
                data.ksi[n][f] = interval_1(gen);
                total += std::abs(data.ksi[n][f]);
            }
            if (total <= 2.0)
                reSample = false;
        }
    }
    
    for (n = 0; (int)n <= data.N-1; n++) {
        bool reSample = true;
        while(reSample){
            double total = 0.0;
            // generate F numbers between -1 and 1
            for (size_t f = 1; f <= BB_NFACTORS; f++) {
                data.phi[n][f] = interval_1(gen);
                total += std::abs(data.phi[n][f]);
            }
            if (total <= 2.0)
                reSample = false;
        }
    }
    
	// set solution file name
	data.solfilename = "bb-n" + std::to_string(data.N) + "-s" + std::to_string(seed) + "-t" + ".opt";
}
#undef KNP_NFACTORS
#endif
