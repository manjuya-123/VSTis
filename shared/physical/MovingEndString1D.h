#pragma once
// A transverse string with its bridge endpoint at a SHARED moving coordinate.
// Implicit-midpoint semi-discrete wave equation; no MIDI or instrument logic.
#include <algorithm>
#include <array>
#include <cmath>
namespace vstis::physical {
class MovingEndString1D {
public:
    static constexpr int segments=80,nodes=segments-1;
    using Vec=std::array<double,nodes>;
    struct Contact{int i=0;double a=1,b=0;};
    struct Step {
        Vec free{},bridge{},bow{},finger{};
        Contact bowAt{},fingerAt{};
        double dt=0;
    };
    bool prepare(double rate,double length,double tension,
                 double density,double loss)noexcept {
        if(!(rate>=8000&&length>0.05&&tension>0&&density>0&&loss>=0))return false;
        h_=1/rate; dx_=length/segments; m_=density*dx_;
        k_=tension/dx_;d_=m_*loss;
        off_=-h_*h_*k_/(4*m_);
        diag_=1+h_*d_/(2*m_)+h_*h_*k_/(2*m_);
        piv_[0]=1/diag_;
        for(int i=1;i<nodes;++i){
            const double p=diag_-off_*off_*piv_[i-1];
            if(!(p>0&&std::isfinite(p)))return false;
            piv_[i]=1/p;
        }
        reset();return true;
    }
    void reset()noexcept{q_.fill(0);v_.fill(0);lastMid_.fill(0);}
    Contact at(double x,double length=0.328)const noexcept {
        const double u=std::clamp(x/(length/segments),1.0,double(segments-1));
        const int grid=std::clamp(int(u),1,segments-1);
        return {grid-1,1.0-(u-grid),u-grid};
    }
    static double sample(const Vec& v,Contact c)noexcept {
        return c.a*v[c.i]+(c.i+1<nodes?c.b*v[c.i+1]:0);
    }
    double displacement(Contact c)const noexcept{return sample(q_,c);}
    double qFirst()const noexcept{return q_[0];}
    double tensionOverDx()const noexcept{return k_;}
    double dt()const noexcept{return h_;}
    Step begin(double yOld,Contact bow,Contact finger)const noexcept {
        Step s;s.bowAt=bow;s.fingerAt=finger;s.dt=h_;
        Vec rhs{},bridge{},unitBow{},unitFinger{};
        const double hkm=h_*k_/(2*m_);
        for(int i=0;i<nodes;++i)
            rhs[i]=v_[i]+hkm*((i?q_[i-1]:yOld)-2*q_[i]
                              +(i+1<nodes?q_[i+1]:0));
        bridge[0]=h_*h_*k_/(4*m_);
        add(unitBow,bow,h_/(2*m_));
        add(unitFinger,finger,h_/(2*m_));
        s.free=solve(rhs);s.bridge=solve(bridge);
        s.bow=solve(unitBow);s.finger=solve(unitFinger);
        return s;
    }
    void advance(const Step& s,double fb,double ff,double bridgeVelocityMid)noexcept {
        for(int i=0;i<nodes;++i){
            const double vm=s.free[i]+s.bow[i]*fb+s.finger[i]*ff
                            +s.bridge[i]*bridgeVelocityMid;
            lastMid_[i]=vm; q_[i]+=h_*vm; v_[i]=2*vm-v_[i];
        }
    }
    double energy(double endpointY)const noexcept{
        double e=0,prev=endpointY;
        for(int i=0;i<nodes;++i){
            const double delta=q_[i]-prev;
            e+=0.5*m_*v_[i]*v_[i]+0.5*k_*delta*delta;
            prev=q_[i];
        }
        return e+0.5*k_*prev*prev;
    }
    double dampingWork()const noexcept{
        double sum=0;for(double v:lastMid_)sum+=v*v;
        return -h_*d_*sum;
    }
private:
    static void add(Vec& v,Contact at,double value)noexcept{
        v[at.i]+=value*at.a;
        if(at.i+1<nodes)v[at.i+1]+=value*at.b;
    }
    Vec solve(const Vec& rhs)const noexcept{
        Vec y{},x{};y[0]=rhs[0]*piv_[0];
        for(int i=1;i<nodes;++i)y[i]=(rhs[i]-off_*y[i-1])*piv_[i];
        x[nodes-1]=y[nodes-1];
        for(int i=nodes-2;i>=0;--i)x[i]=y[i]-off_*piv_[i]*x[i+1];
        return x;
    }
    Vec q_{},v_{},lastMid_{},piv_{};
    double h_=1/48000.0,dx_=0.0041,m_=1,k_=1,d_=0,off_=0,diag_=1;
};
} // namespace vstis::physical
