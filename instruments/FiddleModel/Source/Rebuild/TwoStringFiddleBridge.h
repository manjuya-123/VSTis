#pragma once
#include "physical/TwoPortBridgePair.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Fiddle-only exploration of continuous A/E crossing.
// Geometry, normal-force allocation and rosin law are pilot assumptions:
// NO actual hair ribbon, violin body, multi-mode bridge or physical radiativity.
namespace vstis::fiddle::rebuild {
class TwoStringFiddleBridge {
public:
    using Core=physical::TwoPortBridgePair;
    static constexpr double length=0.328;
    struct Gesture{
        double bowSpeed=0.22,normalForce=0.30,crossing=0.0;
        double fingerAHz=440.0,fingerEHz=659.2551138257;
        double fingerLoadA=0.0,fingerLoadE=0.0;
    };
    // Explicit exploratory bridge impedance parameters. They are not
    // instrument-fit values; defaults reproduce the current failed tonal gate.
    struct BridgeImpedance {
        double portMassKg=0.001;
        double groundingStiffnessNpm=200000.0;
        double groundingDampingNspm=5.0;
        double rockingStiffnessNpm=200000.0;
        double rockingDampingNspm=5.0;
        double stringBulkDampingPerSecond=0.55;
    };
    bool prepare(double sampleRate)noexcept {return prepare(sampleRate,BridgeImpedance{});}
    bool prepare(double sampleRate,const BridgeImpedance& impedance)noexcept{
        std::array<Core::StringSpec,2> specs{};
        const std::array<double,2> open{440.0,659.2551138257};
        for(int i=0;i<2;++i){
            specs[i].length=length;
            specs[i].tension=48.0;
            specs[i].linearDensity=48.0/std::pow(2.0*length*open[i],2.0);
            // Small intrinsic string loss only. Overall observed decay must
            // emerge from a fitted bridge/body radiation model, rather than
            // being imposed as uniform bulk-string damping.
            specs[i].lossPerSecond=impedance.stringBulkDampingPerSecond;
        }
        // Two distinct bowing-side bridge contact coordinates, with a passive
        // elastic rocker between them. This is not a fitted violin body.
        if(!core_.prepare(sampleRate,specs,
                          impedance.portMassKg,
                          impedance.groundingStiffnessNpm,
                          impedance.groundingDampingNspm,
                          impedance.rockingStiffnessNpm,
                          impedance.rockingDampingNspm))return false;
        h_=1.0/sampleRate;reset();return true;
    }
    void reset()noexcept{
        core_.reset();bristle_.fill(0.0);
        lastResidual_=0;lastHairDissipation_=0;lastRootResidual_=0;
        lastForce_.fill(0.0);lastVString_.fill(0.0);
    }
    double step(const Gesture& g)noexcept{
        const auto beforeBristle=bristle_;
        std::array<Core::ContactSpec,2> specs{};
        constexpr std::array<double,2> open{440.0,659.2551138257};
        const std::array<double,2> freq{g.fingerAHz,g.fingerEHz};
        const std::array<double,2> fingerLoad{
            std::clamp(g.fingerLoadA,0.0,1.0),
            std::clamp(g.fingerLoadE,0.0,1.0)
        };
        for(int i=0;i<2;++i){
            specs[i].a=core_.string(i).at(length*0.105);
            specs[i].b=core_.string(i).at(
                std::clamp(length*open[i]/std::max(open[i],freq[i]),
                           length*0.02,length*0.98));
            specs[i].spring=160000.0*fingerLoad[i];
            specs[i].damping=0.14*fingerLoad[i];
        }
        const auto frame=core_.begin(specs);
        auto storedFinger=[&]{
            double e=0.0;
            for(int i=0;i<2;++i){
                const double q=core_.string(i).displacement(specs[i].b);
                e+=0.5*specs[i].spring*q*q;
            }
            return e;
        };
        const double before=core_.energy()+storedFinger();
        const double u=std::clamp(g.crossing,0.0,1.0);
        const double angle=1.57079632679489661923*u;
        const double ca=std::cos(angle),ce=std::sin(angle);
        const std::array<double,2> normal{
            std::max(0.0,g.normalForce)*ca*ca,
            std::max(0.0,g.normalForce)*ce*ce
        };
        // Unloading a bow contact shrinks its admissible bristle deflection.
        // Any released elastic energy is dissipated in hair/rosin rather than
        // injected into the string as an unbounded last contact impulse.
        constexpr double hairStiffness=12000.0;
        std::array<double,2> bristleSeed{};
        for(int i=0;i<2;++i){
            const double bound=1.2*normal[i]/hairStiffness;
            bristleSeed[i]=std::clamp(bristle_[i],-bound,bound);
        }
        // Both strings are resolved against the SAME bridge DOF. The coupled
        // LuGre implicit roots are solved by deterministic block Gauss-Seidel,
        // with exact linear bridge + finger elimination at each trial.
        std::array<double,2> forces{};
        for(int i=0;i<2;++i)
            forces[i]=(normal[i]>1.e-10)?std::clamp(
                12000.0*bristleSeed[i],-1.2*normal[i],1.2*normal[i]):0.0;
        const auto contactForce=[&](int i,
                                   double trial,
                                   std::array<double,2> other)noexcept{
            other[i]=trial;
            const auto state=core_.resolve(frame,other);
            const double slip=g.bowSpeed-state.vA[i];
            const double scaled=slip/0.045;
            const double grip=normal[i]*(0.34+0.86*std::exp(-scaled*scaled));
            const double yield=std::max(1.0e-12,grip/hairStiffness);
            const double next=(bristleSeed[i]+h_*slip)/
                (1.0+h_*std::abs(slip)/yield);
            return hairStiffness*next;
        };
        for(int sweep=0;sweep<12;++sweep){
            double biggest=0.0;
            for(int i=0;i<2;++i){
                if(normal[i]<=1.e-10){forces[i]=0;continue;}
                const double bound=1.2*normal[i];
                double lo=-bound,hi=bound;
                for(int iter=0;iter<40;++iter){
                    const double trial=0.5*(lo+hi);
                    if(trial-contactForce(i,trial,forces)<0.0)lo=trial;
                    else hi=trial;
                }
                const double next=0.5*(lo+hi);
                biggest=std::max(biggest,std::abs(next-forces[i]));
                forces[i]=next;
            }
            if(biggest<1.e-11)break;
        }
        const auto response=core_.resolve(frame,forces);
        lastRootResidual_=0;
        for(int i=0;i<2;++i){
            if(normal[i]>1.e-10){
                lastRootResidual_=std::max(lastRootResidual_,
                    std::abs(forces[i]-contactForce(i,forces[i],forces)));
                bristle_[i]=forces[i]/hairStiffness;
            }else{
                bristle_[i]*=std::exp(-h_/0.0015);
            }
        }
        core_.advance(frame,forces,response);
        const double after=core_.energy()+storedFinger();
        double inputWork=0.0,fingerDamping=0.0,bristleDiss=0.0;
        for(int i=0;i<2;++i){
            inputWork+=h_*forces[i]*response.vA[i];
            fingerDamping-=h_*specs[i].damping*response.vB[i]*response.vB[i];
            const double changeHair=0.5*hairStiffness*
                 (bristle_[i]*bristle_[i]-beforeBristle[i]*beforeBristle[i]);
            const double hairDiss=h_*forces[i]*
                 (g.bowSpeed-response.vA[i])-changeHair;
            bristleDiss+=hairDiss;
        }
        // Track mechanical ledger + bristle stored energy against the bow
        // actuator work. When finger position/load changes between samples,
        // externally supplied finger-control work is NOT yet tracked.
        lastResidual_=(after-before)-(inputWork+core_.dampingWork()+fingerDamping);
        lastHairDissipation_=bristleDiss;
        lastForce_=forces;lastVString_=response.vA;
        return 0.5*(response.bridgeMid[0]+response.bridgeMid[1]); // center velocity, NOT sound radiation
    }
    const Core& mechanics()const noexcept{return core_;}
    double energyResidual()const noexcept{return lastResidual_;}
    double hairDissipation()const noexcept{return lastHairDissipation_;}
    double rootResidual()const noexcept{return lastRootResidual_;}
    std::array<double,2> bowForces()const noexcept{return lastForce_;}
    std::array<double,2> stringBowVelocities()const noexcept{return lastVString_;}
private:
    Core core_{};
    std::array<double,2> bristle_{},lastForce_{},lastVString_{};
    double h_=1.0/48000,lastResidual_=0,lastHairDissipation_=0,
           lastRootResidual_=0;
};
} // namespace vstis::fiddle::rebuild
