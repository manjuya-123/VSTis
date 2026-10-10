#pragma once
#include "physical/ImplicitString1D.h"
#include <algorithm>
#include <cmath>

// Deliberately fiddle-specific, separate from shared mechanics and legacy DSP.
// 1D point-bow and bilateral finger-spring are numerical pilot assumptions,
// NOT a calibrated bow-hair/rosin or violin acoustic model.
namespace vstis::fiddle::rebuild {
struct Gesture {
    double bowSpeed=0.0,normalForce=0.0,bowFraction=0.10;
    double fingerX=0.0,fingerLoad=0.0;
};
class BowedStringPilot {
public:
    using String=physical::ImplicitString1D;
    bool prepare(double rate,double openHz,double length=0.328,double tension=48.0)noexcept {
        if(!(openHz>0))return false;
        open_=openHz;length_=length;sticking_=false;\n        bristleDeflection_=0.0;
        const double density=tension/std::pow(2*length*openHz,2);
        return string_.prepare(rate,length,tension,density,0.55);
    }
    void reset()noexcept{string_.reset();sticking_=false;bristleDeflection_=0.0;}
    double fingerPositionForFrequency(double frequency)const noexcept{
        return std::clamp(length_*open_/std::max(frequency,open_),
                          0.02*length_,0.98*length_);
    }
    double step(const Gesture& g)noexcept {
        const auto bow=string_.at(length_*std::clamp(g.bowFraction,0.03,0.30));
        const auto finger=string_.at(std::clamp(g.fingerX,0.02*length_,0.98*length_));
        const auto f=string_.begin(bow,finger);
        const double engagement=std::clamp(g.fingerLoad,0.0,1.0);
        const double stiffness=160000.0*engagement;
        const double c=0.14*engagement+stiffness*f.h/2;
        const double fb=String::sample(f.free,bow);
        const double ff=String::sample(f.free,finger);
        const double gbb=String::sample(f.bow,bow);
        const double gbf=String::sample(f.finger,bow);
        const double gfb=String::sample(f.bow,finger);
        const double gff=String::sample(f.finger,finger);
        const double fingerOffset=-(stiffness*string_.displacement(finger)+c*ff)/(1+c*gff);
        const double fingerSlope=-c*gfb/(1+c*gff);
        const double bowFree=fb+gbf*fingerOffset;
        const double bowMobility=gbb+gbf*fingerSlope;
        // Minimal LuGre/Stribeck bristle interface, solved together with the
        // string's contact-point mobility. No instantaneous static->dynamic
        // friction force jump. Bristle deflection is physical persistent state.
        // This is a pilot tribology law, NOT calibrated real rosin/hair.
        double bowForce=0;sticking_=false;
        const double normal=std::max(0.0,g.normalForce);
        constexpr double bristleStiffnessNPerM=12000.0;
        if(normal>1.0e-9 && bowMobility>0.0){
            const double bound=1.2*normal;
            const auto trialForce=[&](double force)noexcept {
                const double relative=g.bowSpeed-bowFree-bowMobility*force;
                const double scaled=relative/0.045;
                const double grip=normal*(0.34+0.86*std::exp(-scaled*scaled));
                const double yieldDeflection=std::max(1.0e-12,
                    grip/bristleStiffnessNPerM);
                const double zNew=(bristleDeflection_+f.h*relative)/
                    (1.0+f.h*std::abs(relative)/yieldDeflection);
                return bristleStiffnessNPerM*zNew;
            };
            double lo=-bound,hi=bound;
            for(int i=0;i<45;++i){
                const double mid=0.5*(lo+hi);
                if(mid-trialForce(mid)<0.0)lo=mid;else hi=mid;
            }
            bowForce=0.5*(lo+hi);
            bristleDeflection_=bowForce/bristleStiffnessNPerM;
            const double relative=g.bowSpeed-bowFree-bowMobility*bowForce;
            sticking_=std::abs(relative)<0.01;
        } else {
            // Lift gradually unloads hair contact, never the string state.
            bristleDeflection_*=std::exp(-f.h/0.0015);
        }
        string_.advance(f,bowForce,fingerOffset+fingerSlope*bowForce);
        return string_.bridgeForce();
    }
    const String& mechanics()const noexcept{return string_;}
    bool sticking()const noexcept{return sticking_;}
private:
    String string_{};
    double open_=440.0,length_=0.328;
    bool sticking_=false;\n    double bristleDeflection_=0.0;
};
}
