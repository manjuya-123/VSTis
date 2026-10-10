#include "physical/TwoPortBridgePair.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
using vstis::physical::TwoPortBridgePair;
bool run(double rate,bool damped){
    TwoPortBridgePair core;
    constexpr double length=0.328,tension=48.0,pi=3.14159265358979323846;
    const double pitch[2]={440.0,659.2551138257};
    std::array<TwoPortBridgePair::StringSpec,2> specs{};
    for(int j=0;j<2;++j){
        specs[j].length=length;specs[j].tension=tension;
        specs[j].linearDensity=tension/std::pow(2*length*pitch[j],2.0);
        specs[j].lossPerSecond=damped?0.55:0;
    }
    if(!core.prepare(rate,specs,0.001,200000.0,damped?5:0,
                     200000.0,damped?5:0))return false;
    std::array<TwoPortBridgePair::BodyMode,2> modes{};
    modes[0].mass=0.008;
    modes[0].stiffness=75000;
    modes[0].damping=damped?18:0;
    modes[0].couplingStiffness={27000,27000};
    modes[0].couplingDamping={damped?2.0:0.0,damped?2.0:0.0};
    modes[0].shape={1,1};
    modes[1].mass=0.006;
    modes[1].stiffness=90000;
    modes[1].damping=damped?23:0;
    modes[1].couplingStiffness={36000,36000};
    modes[1].couplingDamping={damped?3.0:0.0,damped?3.0:0.0};
    modes[1].shape={1,-1};
    if(!core.setBodyModes(modes))return false;
    std::array<TwoPortBridgePair::ContactSpec,2> contacts{};
    for(int j=0;j<2;++j){
        contacts[j].a=core.string(j).at(length*0.10);
        contacts[j].b=core.string(j).at(length*0.6);
        contacts[j].spring=damped?1000:0;
        contacts[j].damping=damped?0.06:0;
    }
    double worst=0.0,modalEnergy=0.0,peakMotion=0.0;
    for(int n=0;n<int(rate*0.4);++n){
        const double t=n/rate;
        const std::array<double,2> bow{
            t<0.18?0.06*std::sin(2*pi*340*t):0,
            t>0.18&&t<0.31?0.04*std::sin(2*pi*490*t):0
        };
        auto total=[&]{
            double e=core.energy();
            for(int j=0;j<2;++j){
                const double q=core.string(j).displacement(contacts[j].b);
                e+=0.5*contacts[j].spring*q*q;
            }
            return e;
        };
        const double e0=total();
        const auto frame=core.begin(contacts);
        const auto state=core.resolve(frame,bow);
        core.advance(frame,bow,state);
        const double e1=total();
        double externalWork=0.0,passiveDamping=core.dampingWork();
        for(int j=0;j<2;++j){
            externalWork+=frame.h*bow[j]*state.vA[j];
            passiveDamping-=frame.h*contacts[j].damping*state.vB[j]*state.vB[j];
        }
        worst=std::max(worst,std::abs(e1-e0-externalWork-passiveDamping));
        modalEnergy=std::max(modalEnergy,
            std::abs(core.bodyModeVelocity(0))+std::abs(core.bodyModeVelocity(1)));
        peakMotion=std::max(peakMotion,std::abs(core.bridgePosition(1)));
        if(!std::isfinite(e1)||e1>0.1)return false;
    }
    std::cout<<"body-modes passivity "<<rate<<" Hz damping="<<damped
             <<" worst work ledger "<<worst<<" J"
             <<" peak modal velocity "<<modalEnergy
             <<" coupled bridge port 1 displacement "<<peakMotion<<"\n";
    return worst<1.e-10&&modalEnergy>1.e-12&&peakMotion>1.e-12;
}
int main(){
    bool ok=run(44100,false)&&run(48000,false)
            &&run(44100,true)&&run(48000,true);
    std::cout<<(ok?"MODAL BODY ENERGY PASS\n":"MODAL BODY ENERGY FAIL\n");
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
