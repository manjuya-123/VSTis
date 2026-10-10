#pragma once
// Generic two-string / one moving-bridge mechanical junction.
// No MIDI, bow, rosin, or instrument-specific dimensions here.
#include "physical/MovingEndString1D.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace vstis::physical {
class SharedBridgePair {
public:
    using String=MovingEndString1D;
    using Contact=String::Contact;
    using Step=String::Step;
    struct StringSpec{
        double length=0.328,tension=48,linearDensity=0.0005,lossPerSecond=0.55;
    };
    struct ContactSpec {
        Contact a{},b{};
        // Passive bilateral spring/damper attached to ground at contact B.
        // A zero stiffness/damping leaves contact B completely inactive.
        double spring=0,damping=0;
    };
    struct Frame {
        std::array<Step,2> string{};
        std::array<ContactSpec,2> spec{};
        double h=0;
    };
    struct Solution{
        std::array<double,2> forceB{},vA{},vB{};
        double bridgeMid=0;
    };
    bool prepare(double sampleRate,const std::array<StringSpec,2>& specs,
                 double bridgeMass,double bridgeStiffness,double bridgeDamping)noexcept{
        if(!(sampleRate>=8000&&bridgeMass>0&&bridgeStiffness>=0&&bridgeDamping>=0))
            return false;
        h_=1.0/sampleRate;mass_=bridgeMass;
        stiffness_=bridgeStiffness;damping_=bridgeDamping;
        for(int i=0;i<2;++i)
            if(!strings_[i].prepare(sampleRate,specs[i].length,specs[i].tension,
                                    specs[i].linearDensity,specs[i].lossPerSecond))
                return false;
        reset();return true;
    }
    void reset()noexcept{bridgeY_=bridgeV_=0;for(auto& s:strings_)s.reset();}
    Frame begin(const std::array<ContactSpec,2>& cs)const noexcept{
        Frame f;f.spec=cs;f.h=h_;
        for(int i=0;i<2;++i)
            f.string[i]=strings_[i].begin(bridgeY_,cs[i].a,cs[i].b);
        return f;
    }
    Solution resolve(const Frame& f,const std::array<double,2>& forceA)const noexcept{
        // Eliminate two linear ground contacts first, then solve the single
        // moving bridge velocity DOF. This is not a loose sequential coupling.
        std::array<double,2> offset{},slopeA{},slopeBridge{};
        double D=2.0*mass_/h_+damping_+stiffness_*h_/2.0;
        double rhs=2.0*mass_*bridgeV_/h_-stiffness_*bridgeY_;
        for(int i=0;i<2;++i){
            const auto& s=f.string[i];const auto& c=f.spec[i];
            const double k=strings_[i].tensionOverDx();
            const double w=c.spring*h_/2.0+c.damping;
            const double freeB=String::sample(s.free,c.b);
            const double gBB=String::sample(s.finger,c.b);
            const double inverse=1.0/(1.0+w*gBB);
            offset[i]=(-c.spring*strings_[i].displacement(c.b)-w*freeB)*inverse;
            slopeA[i]=-w*String::sample(s.bow,c.b)*inverse;
            slopeBridge[i]=-w*String::sample(s.bridge,c.b)*inverse;
            rhs+=k*(strings_[i].qFirst()-bridgeY_+h_*s.free[0]/2.0);
            rhs+=k*h_/2.0*(s.bow[0]*forceA[i]
                           +s.finger[0]*(offset[i]+slopeA[i]*forceA[i]));
            D+=k*h_/2.0*(1.0-s.bridge[0]-s.finger[0]*slopeBridge[i]);
        }
        Solution result;
        result.bridgeMid=rhs/D;
        for(int i=0;i<2;++i){
            const auto& s=f.string[i];const auto& c=f.spec[i];
            result.forceB[i]=offset[i]+slopeA[i]*forceA[i]
                            +slopeBridge[i]*result.bridgeMid;
            result.vA[i]=String::sample(s.free,c.a)
                         +String::sample(s.bow,c.a)*forceA[i]
                         +String::sample(s.finger,c.a)*result.forceB[i]
                         +String::sample(s.bridge,c.a)*result.bridgeMid;
            result.vB[i]=String::sample(s.free,c.b)
                         +String::sample(s.bow,c.b)*forceA[i]
                         +String::sample(s.finger,c.b)*result.forceB[i]
                         +String::sample(s.bridge,c.b)*result.bridgeMid;
        }
        return result;
    }
    void advance(const Frame& f,const std::array<double,2>& forceA,
                 const Solution& result)noexcept {
        for(int i=0;i<2;++i)
            strings_[i].advance(f.string[i],forceA[i],
                                result.forceB[i],result.bridgeMid);
        bridgeY_+=h_*result.bridgeMid;
        bridgeV_=2.0*result.bridgeMid-bridgeV_;
        lastBridgeMid_=result.bridgeMid;
    }
    double energy()const noexcept {
        double e=0.5*mass_*bridgeV_*bridgeV_+
                 0.5*stiffness_*bridgeY_*bridgeY_;
        for(const auto& s:strings_)e+=s.energy(bridgeY_);
        return e;
    }
    double dampingWork()const noexcept{
        double loss=-h_*damping_*lastBridgeMid_*lastBridgeMid_;
        for(const auto& s:strings_)loss+=s.dampingWork();
        return loss;
    }
    double bridgePosition()const noexcept{return bridgeY_;}
    double bridgeVelocity()const noexcept{return bridgeV_;}
    const String& string(int index)const noexcept{return strings_[index];}
    String& stringForTest(int index)noexcept{return strings_[index];}
private:
    std::array<String,2> strings_{};
    double h_=1.0/48000,mass_=0.002,stiffness_=150000,damping_=1.0;
    double bridgeY_=0,bridgeV_=0,lastBridgeMid_=0;
};
} // namespace vstis::physical
