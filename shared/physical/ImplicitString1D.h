#pragma once
// JUCE-independent mechanical primitive. No knowledge of instrument or MIDI.
// Implicit midpoint discretisation: dE = dt*(contact_work - damping_loss).
#include <algorithm>
#include <array>
#include <cmath>
namespace vstis::physical {
class ImplicitString1D {
public:
    static constexpr int segments=80,nodes=segments-1;
    using Vec=std::array<double,nodes>;
    struct Contact {int i=0;double a=1.0,b=0.0;};
    struct Frame {Vec free{},bow{},finger{};Contact bc{},fc{};double h=0.0;};
    bool prepare(double rate,double length,double tension,double density,double dampingRate) noexcept {
        if(!(rate>=8000 && length>0.05 && tension>0 && density>0 && dampingRate>=0))return false;
        length_=length;dx_=length/segments; mass_=density*dx_; k_=tension/dx_;
        d_=mass_*dampingRate;h_=1.0/rate;
        off_=-h_*h_*k_/(4*mass_);
        diag_=1+h_*d_/(2*mass_)+h_*h_*k_/(2*mass_);
        piv_[0]=1/diag_;
        for(int i=1;i<nodes;++i) {
            const double p=diag_-off_*off_*piv_[i-1];
            if(!(p>0)||!std::isfinite(p))return false;
            piv_[i]=1/p;
        }
        reset();return true;
    }
    void reset() noexcept {
        q_.fill(0);v_.fill(0);lastMid_.fill(0);
        contactWork_=dampingWork_=change_=0;
    }
    Contact at(double x) const noexcept {
        const double u=std::clamp(x/dx_,1.0,double(segments-1));
        const int i=std::clamp(int(u),1,segments-1);
        return {i-1,1-(u-i),u-i};
    }
    static double sample(const Vec& v,Contact c) noexcept {
        return c.a*v[c.i]+(c.i+1<nodes?c.b*v[c.i+1]:0.0);
    }
    double displacement(Contact c)const noexcept{return sample(q_,c);}
    Frame begin(Contact bc,Contact fc)const noexcept {
        Frame f;f.bc=bc;f.fc=fc;f.h=h_;
        Vec rhs{},unitBow{},unitFinger{};
        for(int i=0;i<nodes;++i)
            rhs[i]=v_[i]+h_*k_/(2*mass_)
                    *((i? q_[i-1]:0)-2*q_[i]+(i+1<nodes?q_[i+1]:0));
        f.free=solve(rhs);
        add(unitBow,bc,h_/(2*mass_));
        add(unitFinger,fc,h_/(2*mass_));
        f.bow=solve(unitBow);f.finger=solve(unitFinger);
        return f;
    }
    void advance(const Frame& f,double forceBow,double forceFinger)noexcept {
        const double before=energy();
        double sumV2=0;
        for(int i=0;i<nodes;++i){
            const double mid=f.free[i]+forceBow*f.bow[i]+forceFinger*f.finger[i];
            lastMid_[i]=mid;q_[i]+=h_*mid;v_[i]=2*mid-v_[i];sumV2+=mid*mid;
        }
        contactWork_=h_*(forceBow*sample(lastMid_,f.bc)+forceFinger*sample(lastMid_,f.fc));
        dampingWork_=-h_*d_*sumV2;
        change_=energy()-before;
    }
    double energy()const noexcept {
        double e=0,prev=0;
        for(int i=0;i<nodes;++i){
            e+=mass_*v_[i]*v_[i]*0.5;
            const double gap=q_[i]-prev;
            e+=k_*gap*gap*0.5;prev=q_[i];
        }
        return e+k_*prev*prev*0.5;
    }
    double ledgerResidual()const noexcept{return change_-contactWork_-dampingWork_;}
    double bridgeForce()const noexcept{return k_*q_[0];}
    void seedStandingWaveForTest(double amplitude)noexcept{
        for(int i=0;i<nodes;++i)
            q_[i]=amplitude*std::sin(3.14159265358979323846*(i+1)/segments);
        v_.fill(0);
    }
private:
    static void add(Vec& out,Contact c,double scale)noexcept {
        out[c.i]+=c.a*scale;
        if(c.i+1<nodes)out[c.i+1]+=c.b*scale;
    }
    Vec solve(const Vec& rhs)const noexcept{
        Vec y{},x{};y[0]=rhs[0]*piv_[0];
        for(int i=1;i<nodes;++i)y[i]=(rhs[i]-off_*y[i-1])*piv_[i];
        x[nodes-1]=y[nodes-1];
        for(int i=nodes-2;i>=0;--i)x[i]=y[i]-off_*piv_[i]*x[i+1];
        return x;
    }
    Vec q_{},v_{},lastMid_{},piv_{};
    double length_=0.328,dx_=0.0041,mass_=1,k_=1,d_=0,h_=1.0/48000,diag_=1,off_=0;
    double change_=0,contactWork_=0,dampingWork_=0;
};
}
