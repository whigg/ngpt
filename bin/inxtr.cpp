#include <iostream>
#include <map>
#include <cstring>
#include <algorithm>
#include "datetime_v2.hpp"
#include "ionex.hpp"

void help();
void usage();
void epilog();

template<typename T>
struct range {
    T from, to, step;
    range(T f=T(), T t=T(), T s=T())
        : from(f), to(t), step(s)
    {};
    float start() const { return from;}
    bool less(float f) const { return to >= from ? f<= to : f>= from; }
    void increment(float& f) const { f+=step; }
    bool empty() const noexcept
    {
        return from == to && (to == step && step == T());
    }
};

typedef std::pair<float, float>            _point_;
typedef std::map<std::string, std::string> str_str_map;
typedef ngpt::datev2<ngpt::milliseconds>   epoch;

/// Parse command line arguments.
int
cmd_parse(int, char* [], str_str_map&);

int
resolve_geo_range(std::string, range<float>&);

int
resolve_str_date(std::string, epoch&);

int main(int argv, char* argc[])
{
    // a dictionary with any default options
    str_str_map arg_dict;
    arg_dict["list" ] = std::string( "N" );
    arg_dict["diff" ] = std::string( "N" );
    
    // axis/epoch limits
    range<float> lat_range;
    range<float> lon_range;
    range<epoch> epoch_range;
    long time_step; // in seconds

    // get cmd arguments into the dictionary
    int status = cmd_parse(argv, argc, arg_dict);
    if ( status > 0 )      // error
    {
        std::cerr << "\n\nWrong cmds. Stop.\n";
        return 1;
    }
    else if ( status < 0 ) // -h
    {
        std::cout << "\n";
        return 0;
    }
    
    // this may throw in non-debug mode
    auto sit = arg_dict.find("ionex");
    if ( sit == arg_dict.end() )
    {
        std::cerr << "\nMust provide name of ionex file.\n";
        return 1;
    }
    ngpt::ionex inx ( sit->second.c_str() );

    // set the start date
    if ( (sit = arg_dict.find("start")) == arg_dict.end() ) {
        // not provided; set from ionex file
        epoch_range.from = inx.first_epoch();
    } else {
        if ( resolve_str_date(sit->second, epoch_range.from) ) {
            std::cerr << "\nERROR. Failed to resolve start epoch from string:";
            std::cerr << " \"" << sit->second << "\"\n";
            return 1;
        }
    }
    
    // set the stop date
    if ( (sit = arg_dict.find("stop")) == arg_dict.end() ) {
        // not provided; set from ionex file
        epoch_range.to = inx.last_epoch();
    } else {
        if ( resolve_str_date(sit->second, epoch_range.to) ) {
            std::cerr << "\nERROR. Failed to resolve ending epoch from string:";
            std::cerr << " \"" << sit->second << "\"\n";
            return 1;
        }
    }

    // set the interval
    if ( (sit = arg_dict.find("rate")) == arg_dict.end() ) {
        // not provided; set to 0
        time_step = 0L;
    } else {
        try {
            time_step = std::stol( sit->second );
        } catch (std::invalid_argument& e) {
            std::cerr << "\nERROR. Failed to resolve time interval from:";
            std::cerr << " \"" << sit->second <<"\'.";
            return 1;
        }
        if ( time_step < 0 ) {
            std::cerr << "\nERROR. Invalid time interval (<0).";
            return 1;
        }
    }
   
    // set the latitude range
    if ( (sit = arg_dict.find("lat")) == arg_dict.end() ) {
        auto latt = inx.latitude_grid();
        lat_range.from = std::get<0>(latt);
        lat_range.to   = std::get<1>(latt);
        lat_range.step = std::get<2>(latt);
    } else {
        if ( resolve_geo_range(sit->second, lat_range) ) {
            std::cerr << "\nERROR. Failed to resolve latitude range from:";
            std::cerr << " \"" << sit->second << "\"";
            return 1;
        }
    }
   
    // set the longtitude range
    if ( (sit = arg_dict.find("lon")) == arg_dict.end() ) {
        auto lont = inx.longtitude_grid();
        lon_range.from = std::get<0>(lont);
        lon_range.to   = std::get<1>(lont);
        lon_range.step = std::get<2>(lont);
    } else {
        if ( resolve_geo_range(sit->second, lon_range) ) {
            std::cerr << "\nERROR. Failed to resolve longtitude range from:";
            std::cerr << " \"" << sit->second << "\"";
            return 1;
        }
    }
   
    // set (if specified) the latitude interval
    if ( (sit = arg_dict.find("dlat")) != arg_dict.end() ) {
        try {
            lat_range.step = std::stof(sit->second);
        } catch (std::invalid_argument&) {
            std::cerr << "\nERROR. Failed to resolve latitude step from:";
            std::cerr << " \"" << sit->second << "\"";
            return 1;
        }
    }
   
    // set (if specified) the longtitude interval
    if ( (sit = arg_dict.find("dlon")) != arg_dict.end() ) {
        try {
            lon_range.step = std::stof(sit->second);
        } catch (std::invalid_argument&) {
            std::cerr << "\nERROR. Failed to resolve latitude step from:";
            std::cerr << " \"" << sit->second << "\"";
            return 1;
        }
    }

    // construct the vector of points for which we want the TEC values
    std::vector<_point_> points;
    for (float lat = lat_range.from; lat_range.less(lat); lat_range.increment(lat)) {
        for (float lon = lon_range.from; lon_range.less(lon); lon_range.increment(lon)) {
            points.emplace_back( lon, lat );
        }
    }

#ifdef DEBUG
    std::cout<<"\nIONEX file: "<<inx.filename();
    std::cout<<"\nInterpolating in lat: "<<lat_range.from<<"/"<<lat_range.to<<"/"<<lat_range.step;
    std::cout<<"\nInterpolating in lon: "<<lon_range.from<<"/"<<lon_range.to<<"/"<<lon_range.step;
    std::cout<<"\nInterpolating in time "<<epoch_range.from.stringify()<<"/"<<epoch_range.to.stringify()<<"/"<<time_step;
    std::cout<<"\nNumber of points to interpolate at: "<<points.size();
#endif
    


    // let's do this!
    std::vector<epoch> epochs;
    int i_time_step (time_step);
    auto tec_results = inx.interpolate(points, epochs,
                        &epoch_range.from, &epoch_range.to, i_time_step);
    return 0;
}

// Resolve a lat/lon interval of type "from/to/step"
int
resolve_geo_range(std::string str, range<float>& rng)
{
    // tokenize the string using '/' as delimeter
    float  lary[3] = {-9999, -9999, -9999};
    int j = 0;
    
    std::size_t i = 0;
    std::string::size_type pos = str.find_first_of('/', i);
    try
    {
        while (pos != std::string::npos)
        {
            lary[j] = std::stod( str.substr(i, pos-i) );
            ++j;
            i = pos + 1;
            pos = str.find_first_of('/', i);
        }
        lary[j] = std::stod( str.substr(i, pos-i) );
    }
    catch (std::invalid_argument& e)
    {
        return 1;
    }

    // see that all values and no more are assigned.
    if ( j!=2 ) { return 1; }

    rng.from = lary[0];
    rng.to   = lary[1];
    rng.step = lary[2];

    return 0;   
}

int
cmd_parse(int argv, char* argc[], str_str_map& smap)
{
    if ( argv == 1 ) {
            help();
            std::cout << "\n";
            usage();
            std::cout << "\n";
            epilog();
            return 1;
    }

    for (int i = 1; i < argv; i++) {
        if (   !std::strcmp(argc[i], "-h") 
            || !std::strcmp(argc[i], "--help") )
        {
            help();
            std::cout << "\n";
            usage();
            std::cout << "\n";
            epilog();
            return -1;
        }
        else if ( !std::strcmp(argc[i], "-l")
               || !std::strcmp(argc[i], "--list") )
        {
            smap["list"] = std::string("Y");
        } 
        else if ( !std::strcmp(argc[i], "-diff") )
        {
            smap["diff"] = std::string("Y");
        }
        else if ( !std::strcmp(argc[i], "-i") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["ionex"] = std::string( argc[i+1] );
            ++i;
        } 
        else if ( !std::strcmp(argc[i], "-start") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["start"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-stop") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["stop"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-interval") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["rate"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-lat") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["lat"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-lon") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["lon"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-dlat") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["dlat"] = std::string( argc[i+1] );
            ++i;
        }
        else if ( !std::strcmp(argc[i], "-dlon") )
        {
            if ( i+1 >= argv ) { return 1; }
            smap["dlon"] = std::string( argc[i+1] );
            ++i;
        }
        else
        {
            std::cerr << "\nIrrelevant cmd: " << argc[i];
        }
    }
    return 0;
}

// Resolve Datetime or Time. This function can resolve a epoch string of type:
// YYYY/MM/DDTHH:MM:SS , or
// HH:MM:SS
// In the second case, the MJDay of the epoch will be set to 0.
int
resolve_str_date(std::string str, epoch& eph)
{
    int num_tokens = 0;
    const char* p = str.c_str();
    char* end;
    std::vector<long> vec_tokens;

    auto it = std::find(std::begin(str), std::end(str), '/');
    if ( it == std::end(str) ) {
    // only time (2nd case)
        for (long i = std::strtol(p, &end, 10);
             p != end;
             ++p, i = std::strtol(p, &end, 10))
        {
            p = end;
            if (errno == ERANGE || num_tokens++ > 2) { return 1; }
            vec_tokens.emplace_back( i );
        }
        eph = epoch( ngpt::modified_julian_day(0),
                     ngpt::hours((int)vec_tokens[0]),
                     ngpt::minutes((int)vec_tokens[1]),
                     ngpt::milliseconds(vec_tokens[2]*1000L)
                   );
        return 0;
    } else {
    // normal datetime (1st case)
        for (long i = std::strtol(p, &end, 10);
             p != end;
             ++p, i = std::strtol(p, &end, 10))
        {
            p = end;
            if (errno == ERANGE || num_tokens++ > 5) { return 1; }
            vec_tokens.emplace_back( i );
        }
        eph = epoch ( ngpt::year{(int)vec_tokens[0]},
                      ngpt::month{(int)vec_tokens[1]},
                      ngpt::day_of_month{(int)vec_tokens[2]},
                      ngpt::hours{(int)vec_tokens[3]},
                      ngpt::minutes{(int)vec_tokens[4]},
                      ngpt::milliseconds{vec_tokens[5]*1000L}
                     );
        return 0;
    }
}

void
help()
{
    std::cout << "\n"
    "Program inxtr\n"
    "This program will read IONEX files and interpolate and report TEC maps\n"
    "and/or values for selected regions and time intervals. All output is directed\n"
    "to \'stdout\'\n"
    "References: IONEX: The IONosphere Map EXchange Format Version 1,\n"
    "S. Schaer, W. Gurtner, J. Feltens,\n"
    "https://igscb.jpl.nasa.gov/igscb/data/format/ionex1.pdf";
    return;
}

void
usage()
{
    std::cout << "\n"
    "Usage:\n"
    " inxtr -i IONEX [-start HH:MM:SS [-stop HH:MM:SS [-lat <from/to/step> -lon <from/to/step>] ] ]\n"
    "\n"
    " -h or --help\n"
    "\tDisplay (this) help message and exit.\n"
    " -i [IONEX]\n"
    "\tSpecify the input IONEX file.\n"
    " -azi [from/to/step]\n"
    "\tSpecify the range for the azimouth axis. Azimouth\n"
    "\tgrid will span the interval [from,to) with a step\n"
    "\tsize of step degrees. Default is [0,360,1]. This\n"
    "\twill automatically fall back to [0,0,0] if no\n"
    "\tazimouth-dependent pcv values are available. Note\n"
    "\tthat this option will overwrite the \'-dazi\' option.\n"
    " -zen [from/to/step]\n"
    "\tSpecify the range for the zenith ditance axis. The\n"
    "\tgrid will span the interval [from,to) with a step\n"
    "\tsize of step degrees. Default is [0,90,1]. Note\n"
    "\tthat this option will overwrite the \'-dzen\' option.\n"
    " -diff\n"
    "\tInstead of printing each antenna's pcv corrections,\n"
    "\tprint the diffrences between pcv values. The first\n"
    "\tantenna in the list is considered as \'reference\' and\n"
    "\tfor each antenna in the specified list the respective\n"
    "\tdiscrepancies are computed.";

    std ::cout << "Example usage:\n"
    "atxtr -a igs08.atx -m \"TRM41249.00     TZGD,LEIATX1230+GNSS NONE\"";
    return;
}

void
epilog()
{
    std::cout << "\n"
    "Copyright 2015 National Technical University of Athens.\n\n"
    "This work is free. You can redistribute it and/or modify it under the\n"
    "terms of the Do What The Fuck You Want To Public License, Version 2,\n"
    "as published by Sam Hocevar. See http://www.wtfpl.net/ for more details.\n"
    "\nSend bugs to: \nxanthos[AT]mail.ntua.gr, \ndemanast[AT]mail.ntua.gr \nvanzach[AT]survey.ntua.gr";
    return;
}
