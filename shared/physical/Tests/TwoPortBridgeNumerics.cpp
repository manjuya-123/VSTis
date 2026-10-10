#include "physical/TwoPortBridgePair.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
using vstis::physical::TwoPortBridgePair;
bool exercise(double rate,bool damped){
    TwoPortBridgePair model;
    constexpr double L=0.328,T=48;
    std::array<TwoPortBridgePair::StringSpec,2> strings{};
    const double f0[]={440,659.2551138257};
    for(int j=0;j<2;++j){
        strings[j].length=L;strings[j].tension=T;
        strings[j].linearDensity=T/std::pow(2*L*f0[j],2);
        strings[j].lossPerSecond=damped?0.55:0.0;
    }
    if(!model.prepare(rate,strings,0.001,200000,
                      damped?5.0:0.0,200000,damped?5.0:0.0))return false;
    std::array<TwoPortBridgePair::ContactSpec,2> cs{};
    for(int j=0;j<2;++j){
        cs[j].a=model.string(j).at(L*0.12);
        cs[j].b=model.string(j).at(L*0.56);
        cs[j].spring=damped?1200.0:0;
        cs[j].damping=damped?0.08:0;
    }
    double maxResidual=0,bMotion=0,aMotion=0;
    for(int n=0;n<int(rate*0.4);++n){
        const std::array<double,2> external{
            n<int(rate*0.18)?0.05*std::sin(2*3.141592653589793*330*n/rate):0.0,
            0.0
        };
        const auto total=[&]{
            double e=model.energy();
            for(int j=0;j<2;++j){
                const double q=model.string(j).displacement(cs[j].b);
                e+=0.5*cs[j].spring*q*q;
            }
            return e;
        };
        const double before=total();
        const auto frame=model.begin(cs);
        const auto solution=model.resolve(frame,external);
        model.advance(frame,external,solution);
        const double after=total();
        double expected=frame.h*external[0]*solution.vA[0]+model.dampingWork();
        for(int j=0;j<2;++j)
            expected-=frame.h*cs[j].damping*solution.vB[j]*solution.vB[j];
        maxResidual=std::max(maxResidual,std::abs(after-before-expected));
        aMotion=std::max(aMotion,std::abs(model.bridgePosition(0)));
        bMotion=std::max(bMotion,std::abs(model.bridgePosition(1)));
        if(!std::isfinite(after)||after>0.1)return false;
    }
    std::cout<<"two-port "<<rate<<" Hz loss="<<damped
       <<" ledger "<<maxResidual<<" J; portA="<<aMotion
       <<" m; coupled portE="<<bMotion<<" m\n";
    return maxResidual<1e-10&&aMotion>1e-12&&bMotion>1e-12;
}
int main(){
    const bool ok=exercise(44100,false)&&exercise(48000,false)
        &&exercise(44100,true)&&exercise(48000,true);
    std::cout<<(ok?"TWO-PORT BRIDGE PASS\n":"TWO-PORT BRIDGE FAIL\n");
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
