#include <stdexcept>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "sp3.hpp"
#include "genflags.hpp"

constexpr std::size_t sat_start_idx  {9}; /// In satellite lines, the sat records start at index: 
constexpr std::size_t sat_stop_idx   {60};/// One past-the-end index
constexpr std::size_t sats_per_line  { (sat_stop_idx-sat_start_idx)/3 };
constexpr std::size_t sats_max_num   {85}; //FIXME this has changed in sp3-d
constexpr std::size_t sats_max_lines {5};

/// A bad or absent satellite position in the sp3 file is denoted as:
constexpr double BAD_POS_VALUE { .0e0 };

/// A bad or absent satellite clock correction in the sp3 file is denoted with
/// a value larger or equal to:
constexpr double BAD_CLK_VALUE { 999999.0e0 };

/// The exponent value which denotes that the actual accuracy is unknown
/// or too large to represent, anything larger or equal to:
constexpr int BAD_EXP_VALUE { 99 };

/// In an sp3 header, each declared satellite is recorded in a string of 3
/// chars; starting from column 10 up untill column 60. This function will
/// compute the number of lines we have to read to collect all satellites.
int satellite_lines(int sat_nr) noexcept
{
    return (sat_nr-1) / sats_per_line + 1;
}

/// No header line can have more than 80 chars.
constexpr int MAX_HEADER_CHARS { 82 };

///
typedef ngpt::satellite_state_option_flag state_flag_option;
typedef ngpt::flag<state_flag_option>     state_flag;

///
typedef ngpt::satellite_clock_option_flag clock_flag_option;
typedef ngpt::flag<clock_flag_option>     clock_flag;

ngpt::sp3::sp3(const char* f)
    : _filename(f),
      _istream (f, std::ios::in),
      _end_of_head(0),
      _first_epoch(),
      _last_epoch(),
      _satsys(ngpt::satellite_system::mixed),
      _base_for_pos(0),
      _base_for_clk(0)
{
    std::memset(_coordSys,'\0', 6);
    std::memset(_orbType, '\0', 4);
    
    // stream should be open
    if (!_istream.is_open() ) {
        throw std::runtime_error
            ("sp3::sp3() -> Cannot open sp3 file: " + std::string(f) );
    }

    // (try to) read the header.
    if ( this->read_header() ) {
        throw std::runtime_error
            ("sp3::sp3() -> Cannot read sp3 file header \""+std::string(f)+"\"" );
    }
}

int
ngpt::sp3::read_header()
{
    char  line [MAX_HEADER_CHARS];
    char* end, *cptr;
    long  lint;
    int   line_nr(0), prev_errno(errno);
    // char  posVelFlag;
    char  dataUsed[6];
    char  agency[5];
    char  ints[4] = "\0"; /* do not modify ints[3] */
    std::vector<ngpt::satellite> sat_vector;

    // The stream should be open by now!
    assert( this->_istream.is_open() );

    // Go to the top of the file.
    _istream.seekg(0);

    // --------------------------------------
    //  Read line #1.
    // --------------------------------------
    ++line_nr;
    if ( !_istream.getline(line, MAX_HEADER_CHARS)
        || (line[1] != 'c') ) 
    {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    // posVelFlag = line[2];
    lint = std::strtol(line+3, &end, 10); // next column is blank, so we're cool
    ngpt::year yr((int)lint);
    lint = std::strtol(line+8, &end, 10);
    ngpt::month mt((int)lint);
    lint = std::strtol(line+11, &end, 10);
    ngpt::day_of_month dm((int)lint);
    lint = std::strtol(line+14, &end, 10);
    ngpt::hours hr((int)lint);
    lint = std::strtol(line+17, &end, 10);
    ngpt::minutes mn((int)lint);
    double decimal_sec (std::strtod(line+20, &end));
    long mls (std::floor(decimal_sec)*1000); // seconds to milliseconds
    lint = std::strtol(line+32, &end, 10);
    _num_of_epochs = (int)lint;
    std::memcpy(dataUsed,  line+40, 5);
    std::memcpy(_coordSys, line+46, 5);
    std::memcpy(_orbType,  line+52, 3);
    std::memcpy(agency,    line+56, 4);
    dataUsed[5] = _coordSys[5] = _orbType[3] = agency[4] = '\0';
    if ( errno == ERANGE ) {
        errno = prev_errno;
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+line_str);
    }
    // Seconds are given as F11.8, i.e. with 8 decimal places. I only want
    // microsecond accuracy though, so check that we are not missing any 
    // digits before creating the starting date
    for (char* p = line+23; p && *p != ' '; ++p) {
        if (*p != '0') {
#ifdef DEBUG
            throw std::runtime_error
            ("sp3::read_header() -> Failed reading starting seconds!! Too much precission");
#endif
            return 1;
        }
    }
    datetime_ms epoch {yr, mt, dm, hr, mn, ngpt::milliseconds(mls)};
    this->_first_epoch = epoch;
    
    // --------------------------------------
    //  Read line #2.
    // --------------------------------------
    ++line_nr;
    if ( !_istream.getline(line, MAX_HEADER_CHARS) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    /*
     * I can resolve the following fields but they are not needed
     * ----------------------------------------------------------
     *
    lint = std::strtol(line+3, &end, 10);
    int gps_w((int)lint);
    double sec_of_week (std::strtod(line+8, &end));
    */
    double eph_interval(std::strtod(line+24, &end));
    // the interval should be an INTEGER amount of milliseconds (or better seconds).
    double f_seconds, i_seconds;
    f_seconds = std::modf(eph_interval, &i_seconds);
    if ( std::abs(f_seconds-.0e0) > 1e-8 ) {
        // std::cout<<"\nfloat seconds="<<float_seconds<<", integral sec="<<eph_interval;
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Interval (in seconds) is fractional! #"+line_str);
    } else {
        ngpt::milliseconds ms__ {(long)i_seconds * 1000L};
        this->_interval = ms__;
    }
    /*
    lint = std::strtol(line+39, &end, 10);
    ngpt::modified_julian_day mjd((int)lint);
    double fract_day(std::strtod(line+45, &end));
    */
    if ( errno == ERANGE ) {
        errno = prev_errno;
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+line_str);
    }

    // --------------------------------------
    //  Lines #3 - at least #7 starting with '+ '.
    //  Read the third line and all subsequent lines that contain satellite
    //  records. These are at least 5 lines. At the end of this block, the first
    //  line starting with '++' should have been read.
    // --------------------------------------
    if ( !line_nr 
        || !_istream.getline(line, MAX_HEADER_CHARS)
        || std::strncmp(line, "+ ", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    lint = std::strtol(line+4, &end, 10);
    std::size_t num_of_sats ((int)lint);
    assert( num_of_sats <= sats_max_num );
    std::size_t sat_lines = satellite_lines(num_of_sats);
    assert( sat_lines <= sats_max_lines );
    sat_vector.reserve(num_of_sats);
    while ( !std::strncmp(line, "+ ", 2) ) {
        if ( sat_vector.size() < num_of_sats ) {
            for (cptr = line+sat_start_idx; cptr<line+sat_stop_idx; cptr+=3) {
                sat_vector.emplace_back(cptr);
                if ( sat_vector.size() == num_of_sats ) {
                    break;
                }
            }
        }
        if ( !(++line_nr) || !_istream.getline(line, MAX_HEADER_CHARS) ) {
#ifdef DEBUG
            std::string line_str = std::to_string(line_nr);
            throw std::runtime_error
                ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
            return 1;
        }
    }
    this->_sat_vec = std::move(sat_vector);
    
    // --------------------------------------
    //  Lines #? - #?
    //  Read all subsequent lines that contain satellite
    //  accuracy records (that is starting with '++'). After this block, we 
    //  will have reached the first line starting the string '%c'
    // --------------------------------------
    if ( std::strncmp(line, "++", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    std::vector<short int> sat_acc;
    sat_acc.reserve(num_of_sats);
    while ( !std::strncmp(line, "++", 2) ) {
        if ( sat_acc.size() < num_of_sats ) {
            for (cptr = line+sat_start_idx; cptr<line+sat_stop_idx; cptr+=3) {
                std::memcpy(ints, cptr, 3);
                lint = std::strtol(ints, &end, 10);
                sat_acc.emplace_back((short int)lint);
                if ( sat_acc.size() == num_of_sats ) {
                    break;
                }
            }
        }
        if ( !(++line_nr) || !_istream.getline(line, MAX_HEADER_CHARS) ) {
#ifdef DEBUG
            std::string line_str = std::to_string(line_nr);
            throw std::runtime_error
                ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
            return 1;
        }
    }
    this->_sat_acc = std::move(sat_acc);

    // --------------------------------------
    //  Read line #?
    // --------------------------------------
    if ( std::strncmp(line, "%c", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    // it might happen that the identifier is actually in column 5 (not 4)!
    {
        bool re_try_sat_sys = false;
        try {
            this->_satsys = ngpt::char_to_satsys( *(line+3) );
        } catch (std::runtime_error& e) {
            re_try_sat_sys = true;
        }
        if ( re_try_sat_sys ) { // if not resolved, let it throw!
            this->_satsys = ngpt::char_to_satsys( *(line+4) );
        }
    }
    // TODO what to do with time system ?
    std::memcpy(ints, line+9, 3);
    
    // --------------------------------------
    //  Read line #?
    // -------------------------------------
    ++line_nr;
    if ( !_istream.getline(line, MAX_HEADER_CHARS) 
        || std::strncmp(line, "%c", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    
    // --------------------------------------
    //  Read line #?
    // -------------------------------------
    ++line_nr;
    if ( !_istream.getline(line, MAX_HEADER_CHARS) 
        || std::strncmp(line, "%f", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    _base_for_pos = std::strtod(line+3, &end);
    _base_for_clk = std::strtod(line+14,&end);
    if ( errno == ERANGE ) {
        errno = prev_errno;
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+line_str);
#endif
        return 1;
    }
    if ( !++line_nr 
        || !_istream.getline(line, MAX_HEADER_CHARS) 
        || std::strncmp(line, "%f", 2) ) {
#ifdef DEBUG
        std::string line_str = std::to_string(line_nr);
        throw std::runtime_error
            ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
        return 1;
    }
    
    // --------------------------------------
    //  Read lines #? - #? starting with '%i'
    // -------------------------------------
    for (int i = 0; i < 2; ++i) {
        if ( !(++line_nr)
            || !_istream.getline(line, MAX_HEADER_CHARS)
            || strncmp(line, "%i", 2) ) {
#ifdef DEBUG
            std::string line_str = std::to_string(line_nr);
            throw std::runtime_error
                ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
            return 1;
        }
    }

    // --------------------------------------
    //  Read lines #? - #? starting with '/*' i.e. comment lines
    // -------------------------------------
    while ( _istream.peek() == '/' ) {
        if ( !(++line_nr)
            || !_istream.getline(line, MAX_HEADER_CHARS)
            || strncmp(line, "/*", 2) ) {
#ifdef DEBUG
            std::string line_str = std::to_string(line_nr);
            throw std::runtime_error
                ("sp3::read_header() -> Failed reading line #"+ line_str);
#endif
            return 1;
        }
    }

    // next line should be a header record (the first)
    this->_end_of_head = _istream.tellg();

    // compute the last epoch in file
    this->_last_epoch = _first_epoch;
    for (int i = 0; i < _num_of_epochs; ++i) {
        _last_epoch.add_seconds( _interval );
    }

    // All done
    return 0;
}

int
ngpt::sp3::get_next_epoch(datetime_ms& epoch, std::vector<satellite> sats,
        std::vector<satellite_state>& states,
        std::vector<satellite_clock>& clocks)
{
    if ( !_istream.good() ) { return -1; }
    
    ngpt::satellite sat;
    ngpt::satellite_state state;
    ngpt::satellite_clock clock;
    int status;

    // read next epoch
    status = this->read_next_epoch_header(epoch);
    std::cout<<"\nNew epoch, status = "<<status<<", epoch: "<<epoch.stringify();
    if ( status ) { return status; }
    
    // read records for the epoch
    char c = _istream.peek();
    while ( c == 'P' ) {
        if ( this->read_next_pos_n_clock(sat, state, clock) ) {
            return 1;
        }
        sats.push_back(sat);
        states.emplace_back(std::move(state));
        clocks.emplace_back(std::move(clock));
        c = _istream.peek();
    }

    return 0;
}

///
/// Exit status:
/// < 0 -> conversion error
/// = 0 -> ok
/// > 0 -> format error
///
int
ngpt::sp3::read_next_pos_n_clock(ngpt::satellite& sat,
                        ngpt::satellite_state& state,
                        ngpt::satellite_clock& clkcor)
{
    // static long lint;
    static char line[MAX_HEADER_CHARS];
    static char num[15];
    std::size_t num_digits = 14;
    int prev_errno = errno;
    state_flag pos_flag {state_flag_option::no_velocity};
    clock_flag clk_flag {clock_flag_option::no_velocity};
    char *chr;

    if ( !_istream.getline(line, MAX_HEADER_CHARS) || *line != 'P' )
    {
        return 1;
    }

    // Resolve the satellite
    ngpt::satellite tmp_sat {line+1};
    sat = std::move(tmp_sat);

    num[14] = '\0';
    // two consecutive numbers (x,y,x) may be recorded with no whitespace
    // between them; so better move them to a temp string and cast that to double
    std::memcpy(num, line+4, num_digits);
    double x (std::strtod(num, &chr));
    std::memcpy(num, line+18, num_digits);
    double y (std::strtod(num, &chr));
    std::memcpy(num, line+32, num_digits);
    double z (std::strtod(num, &chr));
    std::memcpy(num, line+46, num_digits);
    double c (std::strtod(num, &chr));

    if ( errno == ERANGE ) {
        errno = prev_errno;
        return -1;
    }
    
    if (   std::abs(x-BAD_POS_VALUE)>1e-10
        || std::abs(y-BAD_POS_VALUE)>1e-10
        || std::abs(z-BAD_POS_VALUE)>1e-10 )
    {
        pos_flag.set(state_flag_option::bad_or_absent);
    }

    if ( c >= BAD_CLK_VALUE ) { clk_flag.set(clock_flag_option::bad_or_absent); }
    
    long   idev_x = std::strtol(line+61, &chr, 10);
    double sdev_x = std::pow(this->_base_for_pos, (double)idev_x);
    long   idev_y = std::strtol(line+64, &chr, 10);
    double sdev_y = std::pow(this->_base_for_pos, (double)idev_y);
    long   idev_z = std::strtol(line+67, &chr, 10);
    double sdev_z = std::pow(this->_base_for_pos, (double)idev_z);
    long   idev_c = std::strtol(line+70, &chr, 10);
    double sdev_c = std::pow(this->_base_for_clk, (double)idev_c);
    
    if ( errno == ERANGE ) {
        errno = prev_errno;
        return -1;
    }

    if (   idev_x >= BAD_EXP_VALUE
        || idev_y >= BAD_EXP_VALUE 
        || idev_z >= BAD_EXP_VALUE )
    {
        pos_flag.set(state_flag_option::unknown_acc);
    }
    
    if ( idev_c >= BAD_EXP_VALUE ) { clk_flag.set(clock_flag_option::unknown_acc); }

    if ( line[74] == 'E' ) { clk_flag.set(clock_flag_option::discontinuity); }

    if ( line[75] == 'P' ) { clk_flag.set(clock_flag_option::prediction); }
    
    if ( line[78] == 'M' ) { pos_flag.set(state_flag_option::maneuver); }

    if ( line[79] == 'P' ) { pos_flag.set(state_flag_option::prediction); }

    ngpt::satellite_state tmp_state {x, y, z, sdev_x, sdev_y, sdev_z, pos_flag};
    state = std::move(tmp_state);

    ngpt::satellite_clock tmp_clk {c, sdev_c, clk_flag};
    clkcor = std::move(tmp_clk);

    // check what the next line starts with; if it is a correllation
    // or velocity record, read it
    int  status;
    char next[2];
    while ( _istream.get(next, 3) ) { /* basic_istream& get( char_type* s, std::streamsize count ) reads at most count-1 characters */
        _istream.unget(/*next[1]*/);
        _istream.unget(/*next[0]*/);
        if ( next[0] == 'E' &&( next[1] == 'P' || next[1] == 'V') ) {
            if ( (status = this->read_next_corr()) ) {
                return status;
            }
        } else if ( *next == 'V' ) {
            if ( (status = this->read_next_vel(sat, state, clkcor)) ) {
                return status;
            }
        } else {
            break;
        }
    }

    // All done
    return 0;
}

/// Read and ingore an 'E[P|V]' line, i.e. a Position & Clock Correlation
/// information line.
int
ngpt::sp3::read_next_corr()
{
    static char line[MAX_HEADER_CHARS];
    if ( !_istream.getline(line, MAX_HEADER_CHARS)
        || *line != 'E' )
    {
        return 1;
    }
    return 0;
}

int
ngpt::sp3::read_next_vel(const ngpt::satellite s,
    ngpt::satellite_state& state,
    ngpt::satellite_clock& clkcor)
{
    // static long lint;
    static char line[MAX_HEADER_CHARS];
    static char num[15];
    std::size_t num_digits = 14;
    int prev_errno = errno;
    char *chr;

    if ( !_istream.getline(line, MAX_HEADER_CHARS) || *line != 'V' )
    {
        return 1;
    }

    // Resolve the satellit
    ngpt::satellite tmp_sat {line+1};
    if ( s != tmp_sat ) {
        return -2;
    }

    num[14] = '\0';
    // two consecutive numbers (x,y,x) may be recorded with no whitespace
    // between them; so better move them to a temp string and cast that to double
    std::memcpy(num, line+4, num_digits);
    double x (std::strtod(num, &chr));
    std::memcpy(num, line+18, num_digits);
    double y (std::strtod(num, &chr));
    std::memcpy(num, line+32, num_digits);
    double z (std::strtod(num, &chr));
    std::memcpy(num, line+46, num_digits);
    double c (std::strtod(num, &chr));

    if ( errno == ERANGE ) {
        errno = prev_errno;
        return -1;
    }
    
    if (   std::abs(x-BAD_POS_VALUE)>1e-10
        || std::abs(y-BAD_POS_VALUE)>1e-10
        || std::abs(z-BAD_POS_VALUE)>1e-10 )
    {
        state.flag().set(state_flag_option::no_velocity);
    } else {
        state.vx() = x;
        state.vy() = y;
        state.vz() = z;
    }

    if ( c >= BAD_CLK_VALUE ) {
        clkcor.flag().set(clock_flag_option::no_velocity);
    } else {
        clkcor.c() = c;
    }
    
    long   idev_x = std::strtol(line+61, &chr, 10);
    double sdev_x = std::pow(this->_base_for_pos, (double)idev_x);
    long   idev_y = std::strtol(line+64, &chr, 10);
    double sdev_y = std::pow(this->_base_for_pos, (double)idev_y);
    long   idev_z = std::strtol(line+67, &chr, 10);
    double sdev_z = std::pow(this->_base_for_pos, (double)idev_z);
    long   idev_c = std::strtol(line+70, &chr, 10);
    double sdev_c = std::pow(this->_base_for_clk, (double)idev_c);
    
    if ( errno == ERANGE ) {
        errno = prev_errno;
        return -1;
    }

    if (   idev_x >= BAD_EXP_VALUE
        || idev_y >= BAD_EXP_VALUE 
        || idev_z >= BAD_EXP_VALUE )
    {
        state.flag().set(state_flag_option::no_vel_acc);
    } else {
        state.svx() = sdev_x;
        state.svy() = sdev_y;
        state.svz() = sdev_z;
    }
    
    if ( idev_c >= BAD_EXP_VALUE ) {
        clkcor.flag().set(clock_flag_option::no_vel_acc);
    } else {
        clkcor.svc() = sdev_c;
    }

    // All done
    return 0;
}


/// A return value of 999 signals EOF
int
ngpt::sp3::read_next_epoch_header(datetime_ms& date)
{
    static long lint;
    static char line[MAX_HEADER_CHARS];
    int prev_errno = errno;
    char *chr;

    if ( !_istream.getline(line, MAX_HEADER_CHARS) || *line != '*' )
    {
        if ( !std::strncmp(line, "EOF", 3) ) { return 999; }
        return 1;
    }

    lint = std::strtol(line+3, &chr, 10); // next column is blank, so we're cool
    ngpt::year yr((int)lint);
    lint = std::strtol(line+8, &chr, 10);
    ngpt::month mt((int)lint);
    lint = std::strtol(line+11, &chr, 10);
    ngpt::day_of_month dm((int)lint);
    lint = std::strtol(line+14, &chr, 10);
    ngpt::hours hr((int)lint);
    lint = std::strtol(line+17, &chr, 10);
    ngpt::minutes mn((int)lint);
    double decimal_sec (std::strtod(line+20, &chr));
    long mls (std::floor(decimal_sec)*1000); // seconds to milliseconds
    datetime_ms epoch {yr, mt, dm, hr, mn, ngpt::milliseconds(mls)};
    
    if ( errno == ERANGE ) {
        errno = prev_errno;
#ifdef DEBUG
        std::string line_str = std::string(line);
        throw std::runtime_error
            ("sp3::read_next_epoch_header() -> Failed reading line ["+line_str+']');
#endif
        return 1;
    }

    date = epoch;
    return 0;
}