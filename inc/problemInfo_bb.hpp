//
//  problemInfo_bb.hpp
//  Kadaptability
//
//  Created by 靳晴 on 9/6/21.
//

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
