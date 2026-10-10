#pragma once
// General-purpose symmetric two-port passive mechanical bridge.
// Each string has its OWN mobile termination; the bridge ports are
// coupled by a positive spring/damper and can rock relative to each other.
// The two nodal coordinates are simultaneously integrated (midpoint).
#include "physical/MovingEndString1D.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace vstis::physical {
class TwoPortBridgePair {
public:
    using String=MovingEndString1D;
    using Contact=String::Contact;
    using Step=String::Step;
    struct StringSpec{
        double length=0.328,tension=48,linearDensity=0.0005,lossPerSecond=0.55;
    };
    struct ContactSpec{
        Contact a{},b{};
        double spring=0,damping=0;
    };
    struct Frame{
        std::array<Step,2> string{};
        std::array<ContactSpec,2> spec{};
        double h=0;
    };
    struct Solution{
        std::array<double,2> forceB{},vA{},vB{},bridgeMid{};
    };
    bool prepare(double sampleRate,const std::array<StringSpec,2>& specs,
                 double portMass,double groundStiffness,double groundDamping,
                 double linkStiffness,double linkDamping)noexcept{
        if(!(sampleRate>=8000&&portMass>0&&groundStiffness>=0&&groundDamping>=0
          &&linkStiffness>=0&&linkDamping>=0))return false;
        h_=1/sampleRate;mass_=portMass;groundK_=groundStiffness;
        groundC_=groundDamping;linkK_=linkStiffness;linkC_=linkDamping;
        for(int i=0;i<2;++i)
            if(!strings_[i].prepare(sampleRate,specs[i].length,specs[i].tension,
                  specs[i].linearDensity,specs[i].lossPerSecond))return false;
        reset();return true;
    }
    void reset()noexcept{
        Y_.fill(0.0);V_.fill(0.0);lastMid_.fill(0.0);
        for(auto& s:strings_)s.reset();
    }
    Frame begin(const std::array<ContactSpec,2>& cs)const noexcept{
        Frame f;f.spec=cs;f.h=h_;
        for(int i=0;i<2;++i)
            f.string[i]=strings_[i].begin(Y_[i],cs[i].a,cs[i].b);
        return f;
    }
    Solution resolve(const Frame& frame,
                     const std::array<double,2>& forceA)const noexcept{
        std::array<double,2> offFinger{},slopeA{},slopePort{},rhs{},diag{};
        for(int j=0;j<2;++j){
            const auto& f=frame.string[j];
            const auto& c=frame.spec[j];
            const double springK=strings_[j].tensionOverDx();
            const double w=c.spring*h_/2.0+c.damping;
            const double freeB=String::sample(f.free,c.b);
            const double inverse=1.0/(1.0+w*String::sample(f.finger,c.b));
            offFinger[j]=(-c.spring*strings_[j].displacement(c.b)-w*freeB)*inverse;
            slopeA[j]=-w*String::sample(f.bow,c.b)*inverse;
            slopePort[j]=-w*String::sample(f.bridge,c.b)*inverse;
            rhs[j]=2.0*mass_*V_[j]/h_-groundK_*Y_[j]
                   -linkK_*(Y_[j]-Y_[1-j])
                   +springK*(strings_[j].qFirst()-Y_[j]+h_*f.free[0]/2.0)
                   +springK*h_/2.0*(f.bow[0]*forceA[j]
                     +f.finger[0]*(offFinger[j]+slopeA[j]*forceA[j]));
            diag[j]=2.0*mass_/h_+groundC_+groundK_*h_/2.0
                    +linkC_+linkK_*h_/2.0
                    +springK*h_/2.0*(1.0-f.bridge[0]
                                     -f.finger[0]*slopePort[j]);
        }
        const double off=-linkC_-linkK_*h_/2.0;
        const double determinant=diag[0]*diag[1]-off*off;
        Solution sol;
        sol.bridgeMid[0]=(diag[1]*rhs[0]-off*rhs[1])/determinant;
        sol.bridgeMid[1]=(diag[0]*rhs[1]-off*rhs[0])/determinant;
        for(int j=0;j<2;++j){
            const auto& f=frame.string[j];
            const auto& c=frame.spec[j];
            sol.forceB[j]=offFinger[j]+slopeA[j]*forceA[j]
                         +slopePort[j]*sol.bridgeMid[j];
            sol.vA[j]=String::sample(f.free,c.a)
                       +String::sample(f.bow,c.a)*forceA[j]
                       +String::sample(f.finger,c.a)*sol.forceB[j]
                       +String::sample(f.bridge,c.a)*sol.bridgeMid[j];
            sol.vB[j]=String::sample(f.free,c.b)
                       +String::sample(f.bow,c.b)*forceA[j]
                       +String::sample(f.finger,c.b)*sol.forceB[j]
                       +String::sample(f.bridge,c.b)*sol.bridgeMid[j];
        }
        return sol;
    }
    void advance(const Frame& frame,const std::array<double,2>& forceA,
                 const Solution& solution)noexcept{
        for(int j=0;j<2;++j){
            strings_[j].advance(frame.string[j],forceA[j],solution.forceB[j],
                                solution.bridgeMid[j]);
            Y_[j]+=h_*solution.bridgeMid[j];
            V_[j]=2.0*solution.bridgeMid[j]-V_[j];
            lastMid_[j]=solution.bridgeMid[j];
        }
    }
    double energy()const noexcept{
        double e=0.5*linkK_*std::pow(Y_[0]-Y_[1],2);
        for(int j=0;j<2;++j){
            e+=strings_[j].energy(Y_[j])
               +0.5*mass_*V_[j]*V_[j]+0.5*groundK_*Y_[j]*Y_[j];
        }
        return e;
    }
    double dampingWork()const noexcept{
        double loss=-h_*linkC_*std::pow(lastMid_[0]-lastMid_[1],2);
        for(int j=0;j<2;++j)
            loss+=strings_[j].dampingWork()-h_*groundC_*lastMid_[j]*lastMid_[j];
        return loss;
    }
    double bridgePosition(int port)const noexcept{return Y_[port];}
    double bridgeVelocity(int port)const noexcept{return V_[port];}
    const String& string(int index)const noexcept{return strings_[index];}
    String& stringForTest(int index)noexcept{return strings_[index];}
private:
    std::array<String,2> strings_{};
    std::array<double,2> Y_{},V_{},lastMid_{};
    double h_=1.0/48000,mass_=0.001,groundK_=200000,groundC_=5,
           linkK_=200000,linkC_=5;
};
} // namespace vstis::physical
