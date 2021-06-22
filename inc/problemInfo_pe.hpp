//
//  problemInfo_pe.hpp
//  kadaptability
//
//  Created by 靳晴 on 6/21/21.
//

#ifndef problemInfo_pe_hpp
#define problemInfo_pe_hpp

#include "instance_pe.hpp"
#include "problemInfo.hpp"

class KAdaptableInfo_PE : public KAdaptableInfo {
protected:
    /** Specific instance data */
    PE data;

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
    void setInstance(const PE& data);

    /**
     * Virtual (default) constructor
     */
    inline KAdaptableInfo_PE* create() const override {
        return new KAdaptableInfo_PE();
    }

    /**
     * Virtual (copy) constructor
     */
    inline KAdaptableInfo_PE* clone() const override {
        return new KAdaptableInfo_PE (*this);
    }
    


};

#endif /* problemInfo_pe_hpp */
