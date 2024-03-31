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

#ifndef problemInfo_bb_hpp
#define problemInfo_bb_hpp

#include "instance_bb.hpp"
#include "problemInfo.hpp"

class KAdaptableInfo_BB : public KAdaptableInfo {
protected:
    /** Specific instance data */
    BB data;

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

    /**
     * Set data of the instance that this object refers to
     * @param data problem instance
     */
    void setInstance(const BB& data);

    /**
     * Virtual (default) constructor
     */
    inline KAdaptableInfo_BB* create() const override {
        return new KAdaptableInfo_BB();
    }

    /**
     * Virtual (copy) constructor
     */
    inline KAdaptableInfo_BB* clone() const override {
        return new KAdaptableInfo_BB (*this);
    }
    


};

#endif /* problemInfo_bb_hpp */
