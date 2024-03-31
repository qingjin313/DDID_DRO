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

#ifndef KADAPTABLEINFO_KNP_DD_HPP
#define KADAPTABLEINFO_KNP_DD_HPP

#include "instance_knp.hpp"
#include "problemInfo.hpp"

class KAdaptableInfo_KNP_DD : public KAdaptableInfo {
protected:

	/**
	 * Define 1st-stage and 2nd-stage variables
	 */
	void makeVars() override;

	/**
	 * Define uncertainty set
	 */
	void makeUncSet() override;

	/**
	 * Define constraints involving 1st-stage variables only
	 */
	void makeConsX() override;
	
	/**
	 * Define constraints involving 2nd-stage variables
	 * @param k policy number to make constraints for
	 */
	void makeConsY(unsigned int k = 0) override;

public:
    
    /** Specific instance data */
    KNP data;
    
	/**
	 * Set data of the instance that this object refers to
	 * @param data problem instance
	 */
	void setInstance(const KNP& data);

	void sampleUnc(int n, int seed, std::vector<std::vector<double>>& q) override;

	/**
	 * Virtual (default) constructor
	 */
	inline KAdaptableInfo_KNP_DD* create() const override {
		return new KAdaptableInfo_KNP_DD();
	}

	/**
	 * Virtual (copy) constructor
	 */
	inline KAdaptableInfo_KNP_DD* clone() const override {
		return new KAdaptableInfo_KNP_DD (*this);
	}
	


};

#endif
