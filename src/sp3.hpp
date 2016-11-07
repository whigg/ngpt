#ifndef __SP3__HPP__
#define __SP3__HPP__

#include <fstream>
#include <vector>
#include "datetime_v2.hpp"
#include "satellite.hpp"

namespace ngpt
{

class sp3
{
/// Let's not write this more than once.
typedef std::ifstream::pos_type pos_type;

public:
    /// This is the datetime resolution for sp3
    typedef ngpt::datev2<ngpt::milliseconds> datetime_ms;

public:
    /// Constructor from filename.
    explicit sp3(const char*);
    /// Destructor
    ~sp3() noexcept = default;
    /// Copy not allowed.
    sp3(const sp3&) = delete;
    /// Assignment not allowed.
    sp3& operator=(const sp3&) = delete;

    int         num_of_epochs() const noexcept { return _num_of_epochs; }
    std::size_t num_of_sats()   const noexcept { return _sat_vec.size(); }

    /// get next epoch off from the sp3 file
    int get_next_epoch(datetime_ms&, std::vector<satellite>,
        std::vector<satellite_state>&,
        std::vector<satellite_clock>&);

private:
    /// Read sp3 header.
    int read_header();

    ///
    int read_next_pos_n_clock(ngpt::satellite&, ngpt::satellite_state&,
        ngpt::satellite_clock&);

    ///
    int read_next_epoch_header(datetime_ms&);

    ///
    int read_next_corr();

    ///
    int read_next_vel(const ngpt::satellite, ngpt::satellite_state&,
        ngpt::satellite_clock&);

    std::string     _filename;
    std::ifstream   _istream;
    pos_type        _end_of_head;
    datetime_ms     _first_epoch;
    datetime_ms     _last_epoch;
    int             _num_of_epochs;
    ngpt::satellite_system _satsys;
    char            _coordSys[6];
    char            _orbType[4];
    std::vector<ngpt::satellite> _sat_vec;
    std::vector<short int> _sat_acc;
    double _base_for_pos;
    double _base_for_clk;
    ngpt::milliseconds _interval;
     
}; // sp3

} // ngpt

#endif