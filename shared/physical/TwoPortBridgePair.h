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
    // A mode is an independently moving body coordinate. Positive masses,
    // springs and dampers make the interconnection mechanically passive.
    // 'shape' is a lever sign/ratio at the A and E bridge feet.
    struct BodyMode{
        double mass=0.0,stiffness=0.0,damping=0.0;
        std::array<double,2> couplingStiffness{},couplingDamping{};
        std::array<double,2> shape{1.0,1.0};
    };
    static constexpr int maxBodyModes=2;
    struct Frame{
        std::array<Step,2> string{};
        std::array<ContactSpec,2> spec{};
        double h=0;
    };
    struct Solution{
        std::array<double,2> forceB{},vA{},vB{},bridgeMid{},bodyMid{};
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
        modes_={};
        reset();return true;
    }
    bool setBodyModes(const std::array<BodyMode,maxBodyModes>& modes)noexcept{
        for(const auto& mode:modes){
            if(!std::isfinite(mode.mass)||!std::isfinite(mode.stiffness)
                ||!std::isfinite(mode.damping)||mode.mass<0
                ||mode.stiffness<0||mode.damping<0)return false;
            for(int j=0;j<2;++j){
                if(!(std::isfinite(mode.couplingStiffness[j])
                      &&std::isfinite(mode.couplingDamping[j])
                      &&std::isfinite(mode.shape[j]))
                    ||mode.couplingStiffness[j]<0
                    ||mode.couplingDamping[j]<0)return false;
                if(mode.mass==0.0&&(mode.couplingStiffness[j]!=0.0
                                   ||mode.couplingDamping[j]!=0.0))return false;
            }
        }
        modes_=modes;bodyZ_.fill(0);bodyV_.fill(0);bodyMid_.fill(0);
        return true;
    }
    void reset()noexcept{
        Y_.fill(0.0);V_.fill(0.0);lastMid_.fill(0.0);
        bodyZ_.fill(0.0);bodyV_.fill(0.0);bodyMid_.fill(0.0);
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
        double off=-linkC_-linkK_*h_/2.0;
        // Schur complement of up to two passive corpus oscillators:
        // solve their midstep velocities together with both bridge feet.
        // No sequential/one-sample-delay body-feedback approximation.
        std::array<double,maxBodyModes> modalRhs{},modalDiag{};
        std::array<std::array<double,2>,maxBodyModes> gammaShape{};
        for(int m=0;m<maxBodyModes;++m){
            const auto& mode=modes_[m];
            if(mode.mass<=0)continue;
            double denominator=2*mode.mass/h_+mode.damping+
                               mode.stiffness*h_/2;
            double numerator=2*mode.mass*bodyV_[m]/h_
                             -mode.stiffness*bodyZ_[m];
            for(int i=0;i<2;++i){
                const double gamma=mode.couplingDamping[i]+
                                   mode.couplingStiffness[i]*h_/2;
                const double b=mode.shape[i];
                gammaShape[m][i]=gamma*b;
                denominator+=b*b*gamma;
                numerator+=b*mode.couplingStiffness[i]*
                           (Y_[i]-b*bodyZ_[m]);
            }
            modalDiag[m]=denominator;
            modalRhs[m]=numerator;
            for(int i=0;i<2;++i){
                const double b=mode.shape[i];
                const double gamma=mode.couplingDamping[i]+
                                   mode.couplingStiffness[i]*h_/2;
                rhs[i]+=-mode.couplingStiffness[i]*
                         (Y_[i]-b*bodyZ_[m])
                         +gammaShape[m][i]*numerator/denominator;
                diag[i]+=gamma-
                    gammaShape[m][i]*gammaShape[m][i]/denominator;
            }
            off-=gammaShape[m][0]*gammaShape[m][1]/denominator;
        }
        const double determinant=diag[0]*diag[1]-off*off;
        Solution sol;
        sol.bridgeMid[0]=(diag[1]*rhs[0]-off*rhs[1])/determinant;
        sol.bridgeMid[1]=(diag[0]*rhs[1]-off*rhs[0])/determinant;
        for(int m=0;m<maxBodyModes;++m){
            if(modes_[m].mass>0){
                sol.bodyMid[m]=(modalRhs[m]+
                                gammaShape[m][0]*sol.bridgeMid[0]+
                                gammaShape[m][1]*sol.bridgeMid[1])/
                                modalDiag[m];
            }
        }
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
        for(int m=0;m<maxBodyModes;++m){
            if(modes_[m].mass<=0)continue;
            bodyZ_[m]+=h_*solution.bodyMid[m];
            bodyV_[m]=2*solution.bodyMid[m]-bodyV_[m];
            bodyMid_[m]=solution.bodyMid[m];
        }
    }
    double energy()const noexcept{
        double e=0.5*linkK_*std::pow(Y_[0]-Y_[1],2);
        for(int j=0;j<2;++j){
            e+=strings_[j].energy(Y_[j])
               +0.5*mass_*V_[j]*V_[j]+0.5*groundK_*Y_[j]*Y_[j];
        }
        for(int m=0;m<maxBodyModes;++m){
            const auto& mode=modes_[m];
            if(mode.mass<=0)continue;
            e+=0.5*mode.mass*bodyV_[m]*bodyV_[m]+
               0.5*mode.stiffness*bodyZ_[m]*bodyZ_[m];
            for(int j=0;j<2;++j){
                const double deflection=Y_[j]-mode.shape[j]*bodyZ_[m];
                e+=0.5*mode.couplingStiffness[j]*deflection*deflection;
            }
        }
        return e;
    }
    double dampingWork()const noexcept{
        double loss=-h_*linkC_*std::pow(lastMid_[0]-lastMid_[1],2);
        for(int j=0;j<2;++j)
            loss+=strings_[j].dampingWork()-h_*groundC_*lastMid_[j]*lastMid_[j];
        for(int m=0;m<maxBodyModes;++m){
            const auto& mode=modes_[m];
            if(mode.mass<=0)continue;
            loss-=h_*mode.damping*bodyMid_[m]*bodyMid_[m];
            for(int j=0;j<2;++j){
                const double speed=lastMid_[j]-mode.shape[j]*bodyMid_[m];
                loss-=h_*mode.couplingDamping[j]*speed*speed;
            }
        }
        return loss;
    }
    double bodyModeVelocity(int index)const noexcept{return bodyV_[index];}
    double bridgePosition(int port)const noexcept{return Y_[port];}
    double bridgeVelocity(int port)const noexcept{return V_[port];}
    const String& string(int index)const noexcept{return strings_[index];}
    String& stringForTest(int index)noexcept{return strings_[index];}
private:
    std::array<String,2> strings_{};
    std::array<double,2> Y_{},V_{},lastMid_{};
    std::array<BodyMode,maxBodyModes> modes_{};
    std::array<double,maxBodyModes> bodyZ_{},bodyV_{},bodyMid_{};
    double h_=1.0/48000,mass_=0.001,groundK_=200000,groundC_=5,
           linkK_=200000,linkC_=5;
};
} // namespace vstis::physical
