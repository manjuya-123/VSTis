#include "physical/SharedBridgePair.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

using vstis::physical::SharedBridgePair;
bool test(double rate,bool withDamping){
    SharedBridgePair core;
    const double len=0.328,T=48;
    const std::array<double,2> frequencies{440,659.2551138257};
    std::array<SharedBridgePair::StringSpec,2> specs;
    for(int j=0;j<2;++j){
        specs[j].length=len;specs[j].tension=T;
        specs[j].linearDensity=T/std::pow(2.0*len*frequencies[j],2.0);
        specs[j].lossPerSecond=withDamping?0.6:0.0;
    }
    const double cBridge=withDamping?1.2:0.0;
    if(!core.prepare(rate,specs,0.002,150000.0,cBridge))return false;
    std::array<SharedBridgePair::ContactSpec,2> contacts{};
    for(int j=0;j<2;++j){
        contacts[j].a=core.string(j).at(len*0.12);
        contacts[j].b=core.string(j).at(len*0.56);
        contacts[j].spring=withDamping?1200.0:0.0;
        contacts[j].damping=withDamping?0.08:0.0;
    }
    double worstError=0,maxOtherDisplacement=0,peakBridge=0;
    const int n=int(0.4*rate);
    for(int i=0;i<n;++i){
        const auto frame=core.begin(contacts);
        const std::array<double,2> force{
            i<int(rate*0.18)?0.05*std::sin(2*3.14159265358979323846*330*i/rate):0.0,
            0.0
        };
        auto totalEnergy=[&]{
            double result=core.energy();
            for(int j=0;j<2;++j){
                const double q=core.string(j).displacement(contacts[j].b);
                result+=0.5*contacts[j].spring*q*q;
            }
            return result;
        };
        const double before=totalEnergy();
        const auto response=core.resolve(frame,force);
        core.advance(frame,force,response);
        const double after=totalEnergy();
        double work=core.string(0).dt()*force[0]*response.vA[0];
        double loss=core.dampingWork();
        for(int j=0;j<2;++j)
            loss-=core.string(j).dt()*contacts[j].damping*
                  response.vB[j]*response.vB[j];
        worstError=std::max(worstError,std::abs(after-before-work-loss));
        maxOtherDisplacement=std::max(maxOtherDisplacement,
                  std::abs(core.string(1).displacement(contacts[1].a)));
        peakBridge=std::max(peakBridge,std::abs(core.bridgePosition()));
        if(!std::isfinite(after)||after>0.01)return false;
    }
    std::cout<<"shared bridge @"<<rate<<"Hz, damped="<<withDamping
        <<": maximum energy ledger residual="<<worstError
        <<" J; other-string displacement="<<maxOtherDisplacement
        <<" m; bridge motion="<<peakBridge<<" m\n";
    return worstError<2e-11&&maxOtherDisplacement>1e-12&&
           peakBridge>1e-12;
}
int main(){
    const bool success=test(44100,false)&&test(48000,false)
        &&test(44100,true)&&test(48000,true);
    std::cout<<(success?"MULTI-INSTRUMENT BRIDGE PASS\n":
                          "MULTI-INSTRUMENT BRIDGE FAIL\n");
    return success?EXIT_SUCCESS:EXIT_FAILURE;
}
