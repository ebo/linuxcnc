#include <list>
#include <tuple>
#include <vector>
#include <fstream>
#include <iostream>
#include <deque>
#include <regex>
#include <complex>

////////////////////////////////////////////////////////////////////////////////
class motion_base {
public:
    virtual void straight_move(std::complex<double> end)=0;
    virtual void straight_rapid(std::complex<double> end)=0;
    virtual void circular_move(int ccw,std::complex<double> center,
	std::complex<double> end)=0;
};

class motion_null:public motion_base {
public:
    void straight_move(std::complex<double> end) override  {}
    void straight_rapid(std::complex<double> end)  override {}
    void circular_move(int ccw,std::complex<double> center,
	std::complex<double> end)  override {}
};

class motion_gcode:public motion_base {
    std::ofstream gcode;
    std::complex<double> current;
    int motion=-1;
public:
    motion_gcode(std::string fname):gcode(fname),current(NAN,NAN)  {}
    void straight_move(std::complex<double> end)  override {
	if(end==current)
	    return;
	if(motion!=1)
	    gcode <<"G1 " << std::fixed;
	gcode.precision(8);
	if(end.imag()!=current.imag())
	    gcode <<"X" << end.imag();
	if(end.real()!=current.real())
	    gcode <<"Z" << end.real();
	gcode << std::endl;
	current=end;
	motion=1;
    }
    void straight_rapid(std::complex<double> end) override {
	if(end==current)
	    return;
	if(motion!=0)
	    gcode <<"G0 ";
	if(end.imag()!=current.imag())
	    gcode <<"X" << end.imag();
	if(end.real()!=current.real())
	    gcode <<"Z" << end.real();
	gcode << std::endl;
	current=end;
	motion=0;
    }
    void circular_move(int ccw, std::complex<double> center,
	std::complex<double> end
    )  override {
	auto d=center-current;
	motion=ccw? 3:2;
	gcode << "G" << motion << " X" << end.imag() << " Z" << end.real()
	    << " K" << d.real() << " I" << d.imag() << std::endl;
	current=end;
    }
    void add(std::string const &line) { gcode << line; }
};

////////////////////////////////////////////////////////////////////////////////
class round_segment;
class straight_segment;

class segment {
protected:
    std::complex<double> start, end;
public:
    segment(double sz,double sx,double ez,double ex):start(sz,sx),end(ez,ex) {}
    segment(std::complex<double> s, std::complex<double> e):start(s),end(e) {}
    typedef std::deque<double> intersections_t;
    virtual void intersection_z(double x, intersections_t &is)=0;
    virtual bool climb(std::complex<double>&, motion_base*)=0;
    virtual bool dive(std::complex<double>&,double, motion_base*,bool)=0;
    virtual void climb_only(std::complex<double>&, motion_base*)=0;
    virtual void draw(motion_base*)=0;
    virtual void offset(double)=0;
    virtual void intersect(segment*)=0;
    virtual std::unique_ptr<segment> dup(void)=0;
    virtual double radius(void)=0;
    virtual bool monotonic(void) { return real(end-start)<=1e-3; }
    std::complex<double> &sp(void) { return start; }
    std::complex<double> &ep(void) { return end; }

    virtual void intersect_end(round_segment *p)=0;
    virtual void intersect_end(straight_segment *p)=0;
    virtual void flip_imag(void) { start=conj(start); end=conj(end); }
    virtual void flip_real(void) { start=-conj(start); end=-conj(end); }
    virtual void rotate(void) {
	std::complex<double> i(0,1);
	start=-start*i;
	end=-end*i;
    }
    virtual void move(std::complex<double> d) { start+=d; end+=d; }
    friend class round_segment;
};

class straight_segment:public segment {
public:
    straight_segment(double sx, double sz,double ex, double ez):
	segment(sz,sx,ez,ex) {}
    straight_segment(std::complex<double> s,std::complex<double> e):
	segment(s,e) {}
    void intersection_z(double x, intersections_t &is) override;
    bool climb(std::complex<double>&,motion_base*) override;
    bool dive(std::complex<double>&,double, motion_base*,bool) override;
    void climb_only(std::complex<double>&,motion_base*) override;
    void draw(motion_base *out) override { out->straight_move(end); }
    void offset(double distance) override {
	std::complex<double> i(0,1);
	std::complex<double> d=i*distance*(start-end)/abs(start-end);
	start+=d;
	end+=d;
    }
    void intersect(segment *p) override;
    void intersect_end(round_segment *p) override;
    void intersect_end(straight_segment *p) override;
    std::unique_ptr<segment> dup(void) {
	return std::make_unique<straight_segment>(*this);
    }
    double radius(void) { return abs(start-end); }
};

void straight_segment::intersection_z(double x, intersections_t &is)
{
    if(std::min(start.imag(),end.imag())>x+1e-6
	|| std::max(start.imag(),end.imag())<x-1e-6
    )
	return;
    if(std::abs(imag(start-end))<1e-6) {
	is.push_back(start.real());
	is.push_back(end.real());
    } else
	is.push_back((x-start.imag())
	    /(end.imag()-start.imag())*(end.real()-start.real())+start.real());
}

bool straight_segment::climb(std::complex<double> &location,
    motion_base *output
) {
    if(end.imag()<start.imag())
	return 1; // not climbing
    if(abs(location-start)>1e-6)
	throw(std::string("How did we get here?"));
    output->straight_move(end);
    location=end;
    return 0;
}

void straight_segment::climb_only(std::complex<double> &location,
    motion_base *output
) {
    if(end.imag()<start.imag())
	return; // not climbing
    intersections_t is;
    intersection_z(location.imag(),is);
    if(!is.size())
	return;

    location.real(is.front());
    output->straight_move(location);
    output->straight_move(end);
    location=end;
    return;
}

bool straight_segment::dive(std::complex<double> &location,
    double x,
    motion_base *output, bool fast)
{
    if(start.imag()<=end.imag())	// Not a diving segment
	return 1;
    if(x<end.imag()) {
	if(fast)
	    output->straight_rapid(end);
	else
	    output->straight_move(end);
	location=end;
	return 0;
    }
    intersections_t is;
    intersection_z(x,is);
    if(!is.size())
	throw(std::string("x too large in straight dive"));
    std::complex<double> ep(is.front(),x);
    if(fast)
	output->straight_rapid(ep);
    else
	output->straight_move(ep);
    location=ep;
    return 0;
}

class round_segment:public segment {
    int ccw;
    std::complex<double> center;
public:
    round_segment(int c, double sx, double sz,
	double cx, double cz, double ex, double ez):
	segment(sz,sx,ez,ex), ccw(c), center(cz,cx)
    {
    }
    round_segment(int cc, std::complex<double> s,
	std::complex<double> c, std::complex<double> e):
	segment(s,e), ccw(cc), center(c)
    {
    }
    void intersection_z(double x,intersections_t &is) override;
    bool climb(std::complex<double>&,motion_base*) override;
    bool dive(std::complex<double>&,double,motion_base*,bool) override;
    void climb_only(std::complex<double>&,motion_base*) override;
    void draw(motion_base *out) override { out->circular_move(ccw,center,end);}
    void offset(double distance) override {
	double factor=(abs(start-center)+(ccw? 1:-1)*distance)
	    /abs(start-center);
	start=factor*(start-center)+center;
	end=factor*(end-center)+center;
    }
    void intersect(segment *p) override;
    void intersect_end(round_segment *p) override;
    void intersect_end(straight_segment *p) override;
    std::complex<double> cp(void) { return center; }
    std::unique_ptr<segment> dup(void) {
	return std::make_unique<round_segment>(*this);
    }
    double radius(void) { return std::min(abs(start-end),abs(start-center)); }
    void flip_imag(void) override { ccw=!ccw; start=conj(start); end=conj(end);
	center=conj(center); }
    void flip_real(void) override { ccw=!ccw; start=-conj(start);
	end=-conj(end); center=-conj(center); }
    virtual void rotate(void) {
	std::complex<double> i(0,1);
	start=-start*i;
	end=-end*i;
	center=-center*i;
    }
    virtual bool monotonic(void) {
	double entry=imag(start-center);
	double exit=imag(end-center);
	double dz=real(end-start);
	/*
	std::cout << "ccw=" << ccw
	    << " entry=" << entry
	    << " exit=" << exit
	    << " dz=" << dz
	    << std::endl;
	*/
	if(ccw)
	    return entry>=-1e-3 && exit>=-1e-3 && dz<=-1e-3;
	else
	    return entry<=1e-3 && exit<=1e-3 && dz<=-1e-3;
    }
    virtual void move(std::complex<double> d) { start+=d; center+=d; end+=d; }
private:
    bool on_segment(std::complex<double> p);
    friend class straight_segment;
};


inline bool round_segment::on_segment(std::complex<double> p)
{
    if(ccw)
	return imag(conj(start-center)*(p-center))>=-1e-6
	    && imag(conj(end-center)*(p-center))<=1e-6;
    else
	return imag(conj(start-center)*(p-center))<=1e-6
	    && imag(conj(end-center)*(p-center))>=-1e-6;
}

void round_segment::intersection_z(double x, intersections_t &is)
{
    std::complex<double> r=start-center;
    double s=-(x-abs(r)-center.imag())*(x+abs(r)-center.imag());
    if(s<-1e-6)
	return;
    if(s<0)
	s=0;
    s=sqrt(s);
    std::complex<double> p1(real(center+s),x);
    if(on_segment(p1))
	is.push_back(real(p1));
    std::complex<double> p2(real(center-s),x);
    if(on_segment(p2))
	is.push_back(real(p2));
}

bool round_segment::climb(std::complex<double> &location,
    motion_base *output
) {
    if(!ccw) { // G2
	if(location.real()>center.real())
	    return 1;
	if(abs(location-end)>1e-3)
	    output->circular_move(ccw,center,end);
	location=end;
	return 0;
    } else {
	if(location.real()<center.real())
	    return 1;
	std::complex<double> ep=end;
	if(end.real()<center.real()) {
	    ep.real(center.real());
	    ep.imag(center.imag()+abs(start-center));
	}
	if(abs(location-ep)>1e-3)
	    output->circular_move(ccw,center,ep);
	location=ep;
	return 1;
    }
}

bool round_segment::dive(std::complex<double> &location,
    double x, motion_base *output, bool
) {
    if(abs(location-end)<1e-3)
	return 0;
    intersections_t is;
    intersection_z(x,is);

    if(ccw) { // G3
	if(location.real()>center.real())
	    return 1;
	if(!is.size() || is.back()>real(center)) {
	    output->circular_move(ccw,center,end);
	    location=end;
	    return 0;
	} else {
	    std::complex<double> ep(is.back(),x);
	    if(abs(location-ep)>1e-3)
		output->circular_move(ccw,center,ep);
	    location=ep;
	    return 0;
	}
    } else {
	if(location.real()<=center.real())
	    return 1; // we're already climbing
	if(!is.size() || is.front()<real(center)) {
	    output->circular_move(ccw,center,end);
	    location=end;
	    return 0;
	} else {
	    std::complex<double> ep(is.front(),x);
	    if(abs(location-ep)>1e-3)
		output->circular_move(ccw,center,ep);
	    location=ep;
	    return 0;
	}
    }
}

void round_segment::climb_only(std::complex<double> &location,
    motion_base *output
) {
    intersections_t is;
    intersection_z(location.imag(),is);
    if(!is.size())
	return;
    if(!ccw) { // G2
	if(is.back()>center.real())
	    return;
	location.real(is.back());
	output->straight_move(location);
	if(abs(location-end)>1e-3)
	    output->circular_move(ccw,center,end);
	location=end;
	return;
    } else {
	if(is.front()<center.real())
	    return;
	location.real(is.front());
	output->straight_move(location);
	std::complex<double> ep=end;
	if(end.real()<center.real()) {
	    ep.real(center.real());
	    ep.imag(center.imag()+abs(start-center));
	}
	if(abs(location-ep)>1e-3)
	    output->circular_move(ccw,center,ep);
	location=ep;
	return;
    }
}


////////////////////////////////////////////////////////////////////////////////
void straight_segment::intersect(segment *p)
{
    p->intersect_end(this);
}

void round_segment::intersect(segment *p)
{
    p->intersect_end(this);
}

void straight_segment::intersect_end(straight_segment *p)
{
    // correct end of p and start of this
    auto rot=conj(start-end)/abs(start-end);
    auto ps=(p->start-end)*rot;
    auto pe=(p->end-end)*rot;
    if(imag(ps-pe)==0) {
	throw(std::string("Cannot intersect parallel lines"));
    }
    auto f=imag(ps)/imag(ps-pe);
    auto is=(ps+f*(pe-ps))/rot+end;
    start=p->end=is;
}

void straight_segment::intersect_end(round_segment *p)
{
    // correct end of p and start of this
    // (arc followed by a straight
    if(abs(start-p->end)<1e-6)
	return;

    auto rot=conj(start-end)/abs(start-end);
    auto pe=(p->end-end)*rot;
    auto pc=(p->center-end)*rot;

    double b=norm(pc-pe)-imag(pc)*imag(pc);
    if(b<0) {
	b=0;
    } else
	b=sqrt(b);

    auto s1=(real(pc)+b)/rot+end;
    auto s2=(real(pc)-b)/rot+end;

    if(abs(start-s1)<abs(start-s2))
	start=p->end=s1;
    else
	start=p->end=s2;
}

void round_segment::intersect_end(straight_segment *p)
{
    // correct end of p and start of this
    // (straight followed by an arc)
    if(abs(start-p->end)<1e-6)
	return;

    auto rot=conj(p->start-p->end)/abs(p->start-p->end);
    auto pe=(end-p->end)*rot;
    auto pc=(center-p->end)*rot;

    double b=norm(pc-pe)-imag(pc)*imag(pc);
    if(b<0) {
	b=0;
    } else
	b=sqrt(b);

    auto s1=(real(pc)+b)/rot+p->end;
    auto s2=(real(pc)-b)/rot+p->end;

    if(abs(start-s1)<abs(start-s2))
	start=p->end=s1;
    else
	start=p->end=s2;
}

void round_segment::intersect_end(round_segment *p)
{
    // correct end of p and start of this
    if((ccw && !p->ccw) || (!ccw && p->ccw))
	return; // doesn't need correction.
    auto a=abs(start-center);
    auto b=abs(p->start-p->center);
    auto c=abs(center-p->center);
    auto cosB=(c*c+a*a-b*b)/2.0/a/c;
    double cosB2=cosB*cosB;
    if(cosB2>1)
	cosB2=1;
    std::complex<double> rot(cosB,sqrt(1-cosB2));
    auto is=rot*(p->center-center)/c*a+center;
    /*
    std::cout
	<< "a=" << a
	<< " b=" << b
	<< " c=" << c
	<< " cosB=" << cosB
	<< " rot=" << rot
	<< " is=" << is
	<< std::endl;
    */
    p->end=start=is;
}

template <int swap>
class swapped_motion:public motion_base {
    motion_base *orig;
public:
    swapped_motion(motion_base *motion):orig(motion) {}
    virtual void straight_move(std::complex<double> end) override {
	std::complex<double> i(0,1);
	switch(swap) {
	case 0: orig->straight_move(end); break;
	case 1: orig->straight_move(-conj(end)); break;
	case 2: orig->straight_move(conj(end)); break;
	case 3: orig->straight_move(-end); break;
	case 4: orig->straight_move(-i*end); break;
	case 5: orig->straight_move(conj(i*end)); break;
	case 6: orig->straight_move(-conj(i*end)); break;
	case 7: orig->straight_move(i*end); break;
	}
    }
    virtual void straight_rapid(std::complex<double> end) override {
	std::complex<double> i(0,1);
	switch(swap) {
	case 0: orig->straight_rapid(end); break;
	case 1: orig->straight_rapid(-conj(end)); break;
	case 2: orig->straight_rapid(conj(end)); break;
	case 3: orig->straight_rapid(-end); break;
	case 4: orig->straight_rapid(-i*end); break;
	case 5: orig->straight_rapid(conj(i*end)); break;
	case 6: orig->straight_rapid(-conj(i*end)); break;
	case 7: orig->straight_rapid(i*end); break;
	}
    }
    virtual void circular_move(int ccw,std::complex<double> center,
	std::complex<double> end) override
    {
	std::complex<double> i(0,1);
	switch(swap) {
	case 0: orig->circular_move(ccw,center,end); break;
	case 1: orig->circular_move(!ccw,-conj(center),-conj(end)); break;
	case 2: orig->circular_move(!ccw,conj(center),conj(end)); break;
	case 3: orig->circular_move(ccw,-center,-end); break;
	case 4: orig->circular_move(ccw,-i*center,-i*end); break;
	case 5: orig->circular_move(!ccw,conj(i*center),conj(i*end)); break;
	case 6: orig->circular_move(!ccw,-conj(i*center),-conj(i*end)); break;
	case 7: orig->circular_move(ccw,i*center,i*end); break;
	}
    }
};

////////////////////////////////////////////////////////////////////////////////
class g7x:public  std::list<std::unique_ptr<segment>> {
    double delta=0.5;
    std::complex<double> escape{0.3,0.3};
    int flip_state=0;

private:
    void pocket(int cycle, std::complex<double> location, iterator p,
	motion_base *out);
    void add_distance(double distance);

    /* Rotate profile by 90 degrees */
    void rotate(void) {
	for(auto p=begin(); p!=end(); p++)
	    (*p)->rotate();
	flip_state^=4;
    }

    /* Change the direction of the profile to have Z and X decreasing */
    void swap(void) {
	double dir_x=imag(front()->ep()-back()->ep());
	double dir_z=real(front()->ep()-back()->ep());
	if(dir_x>0) {
	    for(auto p=begin(); p!=end(); p++)
		(*p)->flip_imag();
	    flip_state^=2;
	}
	if(dir_z<0) {
	    for(auto p=begin(); p!=end(); p++)
		(*p)->flip_real();
	    flip_state^=1;
	}
    }

    void invert(void) {
	for(auto p=begin(); p!=end(); p++) {
	    (*p)->flip_real();
	}
	flip_state^=1;
    }

    std::unique_ptr<motion_base> motion(motion_base *out) {
	switch(flip_state) {
	case 0: return std::make_unique<swapped_motion<0>>(out);
	case 1: return std::make_unique<swapped_motion<1>>(out);
	case 2: return std::make_unique<swapped_motion<2>>(out);
	case 3: return std::make_unique<swapped_motion<3>>(out);
	case 4: return std::make_unique<swapped_motion<4>>(out);
	case 5: return std::make_unique<swapped_motion<5>>(out);
	case 6: return std::make_unique<swapped_motion<6>>(out);
	case 7: return std::make_unique<swapped_motion<7>>(out);
	}
	throw("This can't happen");
    }

    void monotonic(void) {
	if(real(front()->ep()-front()->sp())>0) {
	    std::cerr << front()->ep() << " " << front()->sp() << std::endl;
	    throw(std::string("Initial dive on wrong side of curve"));
	}
	for(auto p=begin(); p!=end(); p++) {
	    if(!(*p)->monotonic())
		throw(std::string("Not monotonic"));
	}
    }

public:
    g7x(void) {}
    g7x(g7x const &other) {
	delta=other.delta;
	escape=other.escape;
	flip_state=other.flip_state;
	for(auto p=other.begin(); p!=other.end(); p++)
	    emplace_back((*p)->dup());
    }

    /*
	x,z	from where the distance is computed, also a rapid to this
		location between each pass.
	d	Starting distance
	e	Ending distance
	p	Number of passes to go from d to e
    */
    void do_g70(motion_base *out, double x, double z, double d, double e,
	double p
    ) {
	std::complex<double> location(z,x);
	for(int pass=p; pass>0; pass--) {
	    double distance=(pass-1)*(d-e)/p+e;
	    g7x path(*this);
	    path.front()->sp()=location;
	    path.add_distance(distance);

	    auto swapped_out=path.motion(out);
	    swapped_out->straight_rapid(path.front()->sp());
	    swapped_out->straight_rapid(path.front()->ep());
	    for(auto p=++path.begin(); p!=--path.end(); p++)
		    (*p)->draw(swapped_out.get());
	    path.back()->draw(swapped_out.get());
	}
    }

    /*
	cycle	0 Complete cut including pockets.
		1 Do not cut any pockets.
		2 Only cut after first pocket (pick up where 1 stopped)
	x,z	From where the cutting is done.
	d	Final distance to profile
	i	Increment of cutting (depth of cut)
	r	Distance to retract
    */
    void do_g71(motion_base *out, int cycle, double x, double z,
	double u, double w,
	double d, double i, double r, bool do_rotate=false
    ) {
	std::complex<double> location(z,x);

	g7x path(*this);
	path.front()->sp()=location;
	path.add_distance(d);
	if(do_rotate)
	    path.rotate();
	path.swap();
	std::complex<double> displacement(w,u);
	for(auto p=path.begin(); p!=path.end(); p++)
	    (*p)->move(displacement);
	auto swapped_out=path.motion(out);

	path.delta=i;
	path.escape=r*std::complex<double>{1,1};

	path.monotonic();

	swapped_out->straight_rapid(path.front()->sp());
	if(imag(path.back()->ep())<imag(path.front()->sp())) {
	    auto ep=path.back()->ep();
	    ep.imag(imag(path.front()->sp()));
	    path.emplace_back(std::make_unique<straight_segment>(
		path.back()->ep(),ep
	    ));
	}
	path.pocket(cycle,path.front()->sp(), path.begin(), swapped_out.get());
    }

    void do_g72(motion_base *out, int cycle, double x, double z,
	double u, double w,
	double d, double i, double r
    ) {
	do_g71(out, cycle, x, z, u, w, d, i, r, true);
    }
};

void g7x::pocket(int cycle, std::complex<double> location, iterator p,
    motion_base *out
) {
    double initial_x=imag(front()->sp());
    double x=initial_x;

    if(cycle==2) {
	// This skips the initial roughing pass
	for(; p!=end(); p++) {
	    if((*p)->dive(location,-1e9,out,p==begin()))
		break;
	}
	cycle=0;
    }

    while(p!=end()) {
	while(x>imag(location))
	    x-=delta;
	if((*p)->dive(location,x,out,p==begin())) {
	    if(cycle==1) {
		/* After the initial roughing pass, move along the final
		   contour and we're done
		*/
		for(; p!=end(); p++)
		    (*p)->climb_only(location,out);
	    } else {
		/* Move along the final contour until a pocket is found,
		   then start cutting that pocket
		*/
		for(; p!=end(); p++) {
		    if((*p)->climb(location,out)) {
			pocket(0, location,p,out);
			return;
		    }
		}
	    }
	    return;
	}
	if(imag(location)>x || std::abs(location.imag()-x)>1e-6) {
	    /* Our x coordinate iis beyond the current segment, move onto
	       the next
	    */
	    p++;
	    continue;
	}

	for(auto ip=p; ip!=end(); ip++) {
	    segment::intersections_t is;
	    (*ip)->intersection_z(location.imag(),is);
	    if(!is.size())
		continue;

	    /* We can hit the diving curve, if its a circle */
	    double destination_z=ip!=p? is.front():is.back();
	    double distance=std::abs(destination_z-real(location));
	    if(distance<1e-6) {
		if(p==ip)
		    // Hitting the diving curve at the starting point.
		    continue;
		else {
		    // Another curve, at the entry point, move to that curve
		    p++;
		    break;
		}
	    }

	    std::complex<double> dest=location;
	    dest.real(destination_z);
	    out->straight_move(dest);
	    // If the travelled distance is too small for the entire escape
	    double escape_scale=std::min(1.0,distance/2/real(escape));
	    dest+=escape_scale*escape;
	    out->straight_rapid(dest);
	    dest=location-conj(escape_scale*escape);
	    out->straight_rapid(dest);
	    //if(real((*p)->sp())==real((*p)->ep()))
	    if(p==begin())
		out->straight_rapid(location);
	    else
		out->straight_move(location);
	    x-=delta;
	    break;
	}
    }
}

void g7x::add_distance(double distance) {
    auto backup(*this);
    swap();
    auto v1=front()->ep()-front()->sp();
    auto v2=back()->sp()-front()->sp();
    auto angle=v1/v2;
    if(imag(angle)<0)
	distance=-distance;
    auto of(std::move(front()));
    pop_front();
    double current_distance=0;
    while(current_distance!=distance) {
	std::cout << current_distance << " -------\n";

	double max_distance=1e9;
	for(auto p=begin(); p!=end(); p++)
	    max_distance=std::min(max_distance,(*p)->radius()/2);
	max_distance=std::min(max_distance,std::abs(distance-current_distance));
	if(distance<0)
	    max_distance=-max_distance;
	//max_distance=std::min(max_distance,1e-2);
	std::cout << "max_distance=" << max_distance << std::endl;

	for(auto p=begin(); p!=end(); p++) {
	    (*p)->offset(max_distance);
	    if((*p)->radius()<1e-3)
		erase(p--);
	}

	for(auto p=begin(); p!=--end(); p++) {
	    auto n=p; ++n;
	    if(real((*p)->ep()-(*n)->sp())>1e-2) {
		// insert connecting arc
		auto s((*p)->dup());
		auto e((*n)->dup());
		s->offset(-max_distance);
		e->offset(-max_distance);
		auto center=(s->ep()+e->sp())/2.0;
		/*
		std::cout
		    << "correcting=" << real((*p)->ep()-(*n)->sp())
		    << " max_distance=" << max_distance
		    << " current_distance=" << current_distance
		    << " start=" << s->ep()
		    << " end=" << e->sp()
		    << " center=" << center
		    << std::endl;
		*/
		emplace(n, std::make_unique<round_segment>(
		    distance>0,(*p)->ep(),center,(*n)->sp()));
		p++;
	    }
	}

	for(auto p=begin(); p!=--end(); p++) {
	    if(!(*p)->monotonic()) {
		std::cout << "Oops " << (*p)->sp()-(*p)->ep() << std::endl;
		auto pp=p; --pp;
		if((*p)->radius()<(*pp)->radius())
		    erase(p);
		else
		    erase(pp);
		p=begin();
	    }
	    auto n=p; ++n;
	    if((*p)->radius()<1e-3) {
		erase(p);
		p=begin();
	    } else
		(*p)->intersect(n->get());
	}
	current_distance+=max_distance;
    }
    for(auto p=begin(); p!=--end(); p++) {
	auto n=p; ++n;
	auto mid=((*p)->ep()+(*n)->sp())/2.0;
	(*p)->ep()=(*n)->sp()=mid;
    }


    of->ep()=front()->sp();
    push_front(std::move(of));
}

#if 0
int main(int argc, char **argv)
{
    double old_x, old_z, x,z,i,k,g,o,d,p,q,r,u,w,e;

    std::vector<std::tuple<char,double*>> codes{
	{'X',&x},{'Z',&z},
	{'I',&i},{'K',&k},
	{'G',&g},{'O',&o},
	{'D',&d},{'P',&p},
	{'Q',&q},{'R',&r},
	{'U',&u},{'W',&w},
	{'E',&e}
    };

    std::map<int,g7x> path;
    int current_path=0;

    std::string line;
    motion_gcode gcode("cutting.ngc");
    while(getline(std::cin,line)) {
	for(auto &c: line)
	    c=toupper(c);
	if(line=="M2")
	    break;
	for(auto x: codes) {
	    auto pos=line.find(std::get<0>(x));
	    if(pos==std::string::npos)
		continue;
	    *std::get<1>(x)=std::stod(line.substr(pos+1));
	    if(!std::isnan(o))
		break;
	}

	if(!std::isnan(o))
	    current_path=o;
	size_t pos=line.find("ENDSUB");
	if(pos!=std::string::npos) {
	    //path[current_path].pop_front();
	    //path[current_path].pop_back();
	    current_path=0;
	}
	bool used=0;
	if(current_path)
	    switch(int(round(g))) {
	    case 0:
		path[current_path].emplace_back(
		    std::make_unique<straight_segment>(old_x,old_z,x,z));
		break;
	    case 1:
		path[current_path].emplace_back(
		    std::make_unique<straight_segment>(old_x,old_z,x,z));
		break;
	    case 2:
	    case 3:
		path[current_path].emplace_back(
		    std::make_unique<round_segment>(
		    round(g)-2,old_x,old_z,old_x+i,old_z+k,x,z));
		break;
	    }
	else {
	    int cycle=int(round(g));
	    int subcycle=int(round(10*(g-cycle)));

	    try {
		switch(cycle) {
		case 70: path[q].do_g70(&gcode,x,z,d,e,p); used=1; break;
		case 71: path[q].do_g71(&gcode,subcycle,x,z,u,w,d,i,r); used=1;
		    break;
		case 72: path[q].do_g72(&gcode,subcycle,x,z,u,w,d,i,r); used=1;
		    break;
		}
	    } catch(const char *error) {
		std::cerr << line << " *** IGNORED *** " << error << std::endl;
		used=1;
	    }
	    g=NAN;
	}

	if(!used)
	    gcode.add(line + "\n");
	old_x=x;
	old_z=z;
	o=NAN;
    }
    gcode.add("M2\n");
    return 0;
}

#else
////////////////////////////////////////////////////////////////////////////////
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include "rtapi_math.h"
#include "rs274ngc.hh"
#include "rs274ngc_return.hh"
#include "rs274ngc_interp.hh"
#include "interp_internal.hh"
#include "interp_queue.hh"
#include "interp_parameter_def.hh"

#include "units.h"
#include <iostream>


class motion_machine:public motion_base {
    Interp *interp;
    setup_pointer settings;
    block_pointer block;
public:
    motion_machine(Interp *i, setup_pointer s, block_pointer b):
	interp(i), settings(s), block(b) {}

    void straight_move(std::complex<double> end) {
	std::cerr << "straight_move ("
	    << settings->current_z << "," << settings->current_x  << ") "
	    << end
	    << std::endl;
	block->x_flag=1;
	block->x_number=imag(end);
	block->z_flag=1;
	block->z_number=real(end);
	int r=interp->convert_straight(G_1, block, settings);
	if(r!=INTERP_OK)
	    throw(r);
    }

    void straight_rapid(std::complex<double> end)  {
	std::cerr << "straight_rapid ("
	    << settings->current_z << "," << settings->current_x  << ") "
	    << end
	    << std::endl;
	block->x_flag=1;
	block->x_number=imag(end);
	block->z_flag=1;
	block->z_number=real(end);
	int r=interp->convert_straight(G_0, block, settings);
	if(r!=INTERP_OK)
	    throw(r);
    }
    void circular_move(int ccw,std::complex<double> center,
	std::complex<double> end
    ) {
	std::cerr << "circular_move " << (ccw? "G3 (":"G2 (")
	    << settings->current_z << "," << settings->current_x  << ") "
	    << center << " "
	    << end
	    << std::endl;
	block->x_flag=1;
	block->x_number=imag(end);
	block->z_flag=1;
	block->z_number=real(end);
	block->i_flag=1;
	block->i_number=imag(center)-settings->current_x;
	block->k_flag=1;
	block->k_number=real(center)-settings->current_z;
	int r=interp->convert_arc(ccw? G_3:G_2, block, settings);
	if(r!=INTERP_OK)
	    throw(r);
    }
};


class switch_settings {
    setup_pointer settings;
    std::string filename;
    long offset;
    long sequence_number;
public:
    switch_settings(setup_pointer s, offset_struct &op):
	settings(s), filename(s->filename),
	sequence_number(s->sequence_number)
    {
	offset=ftell(settings->file_pointer);
	if(settings->file_pointer)
	    fclose(settings->file_pointer);
	settings->file_pointer=fopen(op.filename,"r");
	if(!settings->file_pointer)
	    throw(std::string("cannot open") + op.filename);
	fseek(settings->file_pointer, op.offset, SEEK_SET);
	settings->sequence_number=op.sequence_number;
    }
    ~switch_settings(void) {
	std::cerr << "back to " << filename << " line " << sequence_number
	    << "@" << offset << std::endl;
	if(settings->file_pointer)
	    fclose(settings->file_pointer);
	settings->file_pointer=fopen(filename.c_str(),"r");
	if(!settings->file_pointer)
	    return;
	fseek(settings->file_pointer, offset, SEEK_SET);
	settings->sequence_number=sequence_number;
    }
};



offset_map_type *ofp;

int Interp::convert_g7x(int mode,
      block_pointer block,     //!< pointer to a block of RS274 instructions
      setup_pointer settings)  //!< pointer to machine settings
{
    if(!block->q_flag)
    	ERS("G7x.x  requires a Q word");
    auto name=std::to_string(lround(block->q_number));
    ofp=&settings->offset_map;
    auto call=settings->offset_map.find(name.c_str());
    if(call==settings->offset_map.end())
	ERS("Q routine not found");

    switch_settings old(settings,call->second);

    auto original_block=*block;

    CHP(enter_context(settings, block));
    if(settings->call_level<=0)
	ERS("G7X error: call_level too small");

    auto frame = &settings->sub_context[settings->call_level];
    auto previous_frame=&settings->sub_context[settings->call_level-1];
    frame->named_params=previous_frame->named_params;

    parameter_map named_params;

    double x=settings->current_x;
    double z=settings->current_z;
    if(block->x_flag)
	x=block->x_number;
    if(block->z_flag)
	z=block->z_number;

    original_block.x_number=x;
    original_block.z_number=z;

    double old_x=x;
    double old_z=z;
    g7x path;

    do {
	CHP(read()); // FIXME Overwrites current block
	std::cout << "g_modes = ";
	for(auto g: block->g_modes) {
	    std::cout << " " << g;
	}
	std::cout << std::endl;

	x=old_x;
	z=old_z;
	double i=0, k=0;

	if(block->x_flag) x=block->x_number;
	if(block->z_flag) z=block->z_number;
	if(block->i_flag) i=block->i_number;
	if(block->k_flag) k=block->k_number;

	int motion=settings->motion_mode;
	if(block->g_modes[1]!=-1) {
	    motion=block->g_modes[1];
	    settings->motion_mode=block->g_modes[1];
	}
	if(x==old_x && z==old_z)
	    continue;
	switch(motion) {
	case 0:
	case 10:
	    path.emplace_back(std::make_unique<straight_segment>(
		old_x,old_z,
		x,z
	    ));
	    break;
	case 20:
	case 30:
	    path.emplace_back(std::make_unique<round_segment>(
		block->g_modes[1]==30,
		old_x,old_z,
		old_x+i,old_z+k,
		x,z
	    ));
	    break;
	}
	old_x=x;
	old_z=z;
    } while(block->o_type!=O_endsub);

    leave_context(settings);

    int cycle=original_block.g_modes[1];
    int subcycle=cycle%10;
    cycle/=10;

    double d=0, e=0, i=1, p=1, r=0.5, u=0, w=0;
    if(original_block.d_flag) d=original_block.d_number_float;
    if(original_block.e_flag) e=original_block.e_number;
    if(original_block.i_flag) i=original_block.i_number;
    if(original_block.p_flag) p=original_block.p_number;
    if(original_block.r_flag) r=original_block.r_number;
    if(original_block.u_flag) u=original_block.u_number;
    if(original_block.w_flag) w=original_block.w_number;
    if(original_block.x_flag) x=original_block.x_number;
    if(original_block.z_flag) z=original_block.z_number;

    // motion_gcode motion("cutting.ngc");
    motion_machine motion(this, settings, block);
    try {
	switch(cycle) {
	case 70: path.do_g70(&motion,x,z,d,e,p); break;
	case 71: path.do_g71(&motion,subcycle,x,z,u,w,d,i,r); break;
	case 72: path.do_g72(&motion,subcycle,x,z,u,w,d,i,r); break;
	}
    } catch(std::string &s) {
	ERS("G7X error: %s", s.c_str());
	return INTERP_ERROR;
    } catch(int i) {
	return i;
    }

    return INTERP_OK;
}
#endif