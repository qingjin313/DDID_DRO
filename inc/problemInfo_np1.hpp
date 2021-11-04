//
//  problemInfo_np1.hpp
//  Kadaptability
//
//  Created by 靳晴 on 10/31/21.
//

#ifndef problemInfo_np1_hpp
#define problemInfo_np1_hpp

#include <stdio.h>
#include "problemInfo.hpp"

class KAdaptableInfo_np1 : public KAdaptableInfo {
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

    /**
     * Virtual (default) constructor
     */
    inline KAdaptableInfo_np1* create() const override {
        return new KAdaptableInfo_np1();
    }

    /**
     * Virtual (copy) constructor
     */
    inline KAdaptableInfo_np1* clone() const override {
        return new KAdaptableInfo_np1 (*this);
    }
    
    /**
     * Set data of the instance that this object refers to
     */
    void setInstance();

};

#endif /* problemInfo_np1_hpp */
