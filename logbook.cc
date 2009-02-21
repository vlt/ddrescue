/*  GNU ddrescue - Data recovery tool
    Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>

#include "block.h"
#include "ddrescue.h"


namespace {

int my_fgetc( FILE * f ) throw()
  {
  int ch;
  bool comment = false;

  do {
    ch = std::fgetc( f );
    if( ch == '#' ) comment = true;
    else if( ch == '\n' || ch == EOF ) comment = false;
    }
  while( comment );
  return ch;
  }


const char * my_fgets( FILE * f, int & linenum ) throw()
  {
  const int maxlen = 127;
  static char buf[maxlen+1];
  int ch, len = 1;

  while( len == 1 )
    {
    do { ch = my_fgetc( f ); if( ch == '\n' ) ++linenum; }
    while( std::isspace( ch ) );
    len = 0;
    while( true )
      {
      if( ch == EOF ) { if( len > 0 ) ch = '\n'; else break; }
      if( len < maxlen ) buf[len++] = ch;
      if( ch == '\n' ) { ++linenum; break; }
      ch = my_fgetc( f );
      }
    }
  if( len > 0 ) { buf[len] = 0; return buf; }
  else return 0;
  }


void extend_sblock_vector( std::vector< Sblock > & sblock_vector,
                           const long long isize )
  {
  if( sblock_vector.size() == 0 )
    {
    Sblock sb( 0, (isize > 0) ? isize : -1, Sblock::non_tried );
    sb.fix_size();
    sblock_vector.push_back( sb );
    return;
    }
  Sblock & front = sblock_vector.front();
  if( front.pos() > 0 )
    sblock_vector.insert( sblock_vector.begin(), Sblock( 0, front.pos(), Sblock::non_tried ) );
  Sblock & back = sblock_vector.back();
  const long long end = back.end();
  if( isize > 0 )
    {
    if( back.pos() >= isize )
      {
      if( back.pos() == isize && back.status() == Sblock::non_tried )
        { sblock_vector.pop_back(); return; }
      show_error( "bad logfile; last block begins past end of input file" );
      std::exit( 1 );
      }
    if( end < 0 || end > isize ) back.size( isize - back.pos() );
    else if( end < isize )
      sblock_vector.push_back( Sblock( end, isize - end, Sblock::non_tried ) );
    }
  else if( end >= 0 )
    {
    Sblock sb( end, -1, Sblock::non_tried );
    sb.fix_size();
    if( sb.size() > 0 ) sblock_vector.push_back( sb );
    }
  }


void show_logfile_error( const char * filename, const int linenum )
  {
  char buf[80];
  snprintf( buf, sizeof buf, "error in logfile %s, line %d", filename, linenum );
  show_error( buf );
  }


bool read_logfile( const char * name, std::vector< Sblock > & sblock_vector,
                   long long & current_pos, Logbook::Status & current_status )
  {
  FILE *f = std::fopen( name, "r" );
  if( !f ) return false;
  int linenum = 0;
  sblock_vector.clear();

  const char *line = my_fgets( f, linenum );
  if( line )						// status line
    {
    char ch;
    int n = std::sscanf( line, "%lli %c\n", &current_pos, &ch );
    if( n == 2 && current_pos >= 0 && Logbook::isstatus( ch ) )
      current_status = Logbook::Status( ch );
    else
      {
      show_logfile_error( name, linenum );
      show_error( "Are you using a logfile from ddrescue 1.5 or older?" );
      std::exit( 2 );
      }

    while( true )
      {
      line = my_fgets( f, linenum );
      if( !line ) break;
      long long pos, size;
      n = std::sscanf( line, "%lli %lli %c\n", &pos, &size, &ch );
      if( n == 3 && pos >= 0 && ( size > 0 || size == -1 ) &&
          Sblock::isstatus( ch ) )
        {
        Sblock::Status st = Sblock::Status( ch );
        Sblock sb( pos, size, st ); sb.fix_size();
        if( sblock_vector.size() > 0 && !sb.follows( sblock_vector.back() ) )
          { show_logfile_error( name, linenum ); std::exit( 2 ); }
        sblock_vector.push_back( sb );
        }
      else
        { show_logfile_error( name, linenum ); std::exit( 2 ); }
      }
    }
  std::fclose( f );
  return true;
  }

} // end namespace


Domain::Domain( const char * name, const long long p, const long long s )
  {
  Block b( p, s ); b.fix_size();
  if( !name ) { block_vector.push_back( b ); return; }
  std::vector< Sblock > sblock_vector;
  long long current_pos;			// not used
  Logbook::Status current_status;		// not used
  if( !read_logfile( name, sblock_vector, current_pos, current_status ) )
    {
    char buf[80];
    snprintf( buf, sizeof buf,
              "logfile `%s' does not exist or is not readable", name );
    show_error( buf );
    std::exit( 1 );
    }
  for( unsigned int i = 0; i < sblock_vector.size(); ++i )
    {
    const Sblock & sb = sblock_vector[i];
    if( sb.status() == Sblock::finished ) block_vector.push_back( sb );
    }
  this->crop( b );
  }


void Logbook::split_domain_border_sblocks()
  {
  for( unsigned int i = 0; i < sblock_vector.size(); ++i )
    {
    Sblock & sb = sblock_vector[i];
    const long long pos = _domain.breaks_block_by( sb );
    if( pos > 0 )
      {
      const Sblock head( sb.split( pos ) );
      if( head.size() > 0 ) insert_sblock( i, head );
      else internal_error( "empty block created by split_domain_border_sblocks" );
      }
    }
  }


Logbook::Logbook( const long long ipos, const long long opos, Domain & dom,
                  const long long isize,
                  const char * name, const int cluster, const int hardbs,
                  const bool complete_only )
  : _offset( opos - ipos ), _current_pos( 0 ), _current_status( copying ),
    _domain( dom ), _filename( name ),
    _hardbs( hardbs ), _softbs( cluster * hardbs ),
    _final_msg( 0 ), _final_errno( 0 ), _index( 0 )
  {
  int alignment = sysconf( _SC_PAGESIZE );
  if( alignment < _hardbs || alignment % _hardbs ) alignment = _hardbs;
  if( alignment < 2 || alignment > 65536 ) alignment = 0;
  _iobuf = iobuf_base = new char[ _softbs + alignment ];
  if( alignment > 1 )		// align iobuf for use with raw devices
    {
    const int disp = alignment - ( reinterpret_cast<long> (_iobuf) % alignment );
    if( disp > 0 && disp < alignment ) _iobuf += disp;
    }

  if( !_domain.crop_by_file_size( isize ) ) std::exit( 1 );
  if( _filename )
    read_logfile( _filename, sblock_vector, _current_pos, _current_status );
  if( !complete_only ) extend_sblock_vector( sblock_vector, isize );
  else if( sblock_vector.size() )  // limit domain to blocks read from logfile
    {
    const Block b( sblock_vector.front().pos(),
                   sblock_vector.back().end() - sblock_vector.front().pos() );
    _domain.crop( b );
    }
  compact_sblock_vector();
  split_domain_border_sblocks();
  if( sblock_vector.size() == 0 ) _domain.clear();
  }


bool Logbook::blank() const throw()
  {
  for( unsigned int i = 0; i < sblock_vector.size(); ++i )
    if( sblock_vector[i].status() != Sblock::non_tried )
      return false;
  return true;
  }


void Logbook::compact_sblock_vector()
  {
  for( unsigned int i = sblock_vector.size(); i >= 2; --i )
    if( sblock_vector[i-2].join( sblock_vector[i-1] ) )
      sblock_vector.erase( sblock_vector.begin() + i - 1 );
  }


// Writes periodically the logfile to disc.
// Returns false only if update is attempted and fails.
//
bool Logbook::update_logfile( const int odes, const bool force )
  {
  static time_t t1 = std::time( 0 );

  if( !_filename ) return true;
  const int interval = 30 + std::min( 270, sblocks() / 40 );
  const time_t t2 = std::time( 0 );
  if( !force && t2 - t1 < interval ) return true;
  t1 = t2;
  if( odes >= 0 ) fsync( odes );

  errno = 0;
  FILE *f = std::fopen( _filename, "w" );
  if( !f )
    {
    char buf[80];
    snprintf( buf, sizeof buf, "error opening logfile %s for writing", _filename );
    show_error( buf, errno );
    return false;
    }

  write_logfile_header( f );
  std::fprintf( f, "# current_pos  current_status\n" );
  std::fprintf( f, "0x%08llX     %c\n", _current_pos, _current_status );
  std::fprintf( f, "#      pos        size  status\n" );
  for( unsigned int i = 0; i < sblock_vector.size(); ++i )
    {
    const Sblock & sb = sblock_vector[i];
    std::fprintf( f, "0x%08llX  0x%08llX  %c\n", sb.pos(), sb.size(), sb.status() );
    }

  if( std::fclose( f ) )
    {
    char buf[80];
    snprintf( buf, sizeof buf, "error writing logfile %s", _filename );
    show_error( buf, errno );
    return false;
    }
  return true;
  }


void Logbook::truncate_vector( const long long pos )
  {
  int i = sblocks() - 1;
  while( i >= 0 && sblock_vector[i].pos() >= pos ) --i;
  if( i < 0 )
    {
    sblock_vector.clear();
    sblock_vector.push_back( Sblock( pos, 0, Sblock::finished ) );
    }
  else
    {
    Sblock & sb = sblock_vector[i];
    if( sb.includes( pos ) ) sb.size( pos - sb.pos() );
    sblock_vector.erase( sblock_vector.begin() + i + 1, sblock_vector.end() );
    }
  }


int Logbook::find_index( const long long pos ) const throw()
  {
  if( _index < 0 || _index >= sblocks() ) _index = sblocks() / 2;
  while( _index + 1 < sblocks() && pos >= sblock_vector[_index].end() )
    ++_index;
  while( _index > 0 && pos < sblock_vector[_index].pos() )
    --_index;
  if( !sblock_vector[_index].includes( pos ) ) _index = -1;
  return _index;
  }


// Find chunk from b.pos of size <= b.size and status st.
// if not found, puts b.size to 0.
//
void Logbook::find_chunk( Block & b, const Sblock::Status st ) const
  {
  if( b.size() <= 0 ) return;
  if( b.pos() < sblock_vector.front().pos() )
    b.pos( sblock_vector.front().pos() );
  if( find_index( b.pos() ) < 0 ) { b.size( 0 ); return; }
  int i;
  for( i = _index; i < sblocks(); ++i )
    if( sblock_vector[i].status() == st &&
        _domain.includes( sblock_vector[i] ) )
      { _index = i; break; }
  if( i >= sblocks() ) { b.size( 0 ); return; }
  if( b.pos() < sblock_vector[_index].pos() )
    b.pos( sblock_vector[_index].pos() );
  b.fix_size();
  if( !sblock_vector[_index].includes( b ) )
    b.crop( sblock_vector[_index] );
  if( b.end() != sblock_vector[_index].end() )
    b.align_end( _hardbs );
  }


// Find chunk from b.end backwards of size <= b.size and status st.
// if not found, puts b.size to 0.
//
void Logbook::rfind_chunk( Block & b, const Sblock::Status st ) const
  {
  if( b.size() <= 0 ) return;
  b.fix_size();
  if( sblock_vector.back().end() < b.end() )
    b.end( sblock_vector.back().end() );
  find_index( b.end() - 1 );
  for( ; _index >= 0; --_index )
    if( sblock_vector[_index].status() == st &&
        _domain.includes( sblock_vector[_index] ) )
      break;
  if( _index < 0 ) { b.size( 0 ); return; }
  if( b.end() > sblock_vector[_index].end() )
    b.end( sblock_vector[_index].end() );
  if( !sblock_vector[_index].includes( b ) )
    b.crop( sblock_vector[_index] );
  if( b.pos() != sblock_vector[_index].pos() )
    b.align_pos( _hardbs );
  }


void Logbook::change_chunk_status( const Block & b, const Sblock::Status st )
  {
  if( b.size() <= 0 ) return;
  if( !_domain.includes( b ) || find_index( b.pos() ) < 0 ||
      !_domain.includes( sblock_vector[_index] ) )
    internal_error( "can't change status of chunk not in rescue domain" );
  if( !sblock_vector[_index].includes( b ) )
    internal_error( "can't change status of chunk spread over more than 1 block" );
  if( sblock_vector[_index].status() == st ) return;
  if( sblock_vector[_index].pos() < b.pos() )
    {
    if( sblock_vector[_index].end() == b.end() &&
        _index + 1 < sblocks() && sblock_vector[_index+1].status() == st &&
        _domain.includes( sblock_vector[_index+1] ) )
      {
      sblock_vector[_index].inc_size( -b.size() );
      sblock_vector[_index+1].pos( b.pos() );
      sblock_vector[_index+1].inc_size( b.size() );
      return;
      }
    insert_sblock( _index, sblock_vector[_index].split( b.pos() ) );
    ++_index;
    }
  if( sblock_vector[_index].size() > b.size() )
    {
    sblock_vector[_index].pos( b.end() );
    sblock_vector[_index].inc_size( -b.size() );
    if( _index > 0 && sblock_vector[_index-1].status() == st &&
        _domain.includes( sblock_vector[_index-1] ) )
      sblock_vector[_index-1].inc_size( b.size() );
    else
      insert_sblock( _index, Sblock( b, st ) );
    }
  else
    {
    sblock_vector[_index].status( st );
    if( _index > 0 && sblock_vector[_index-1].status() == st &&
        _domain.includes( sblock_vector[_index-1] ) )
      {
      sblock_vector[_index-1].inc_size( sblock_vector[_index].size() );
      erase_sblock( _index ); --_index;
      }
    if( _index + 1 < sblocks() && sblock_vector[_index+1].status() == st &&
        _domain.includes( sblock_vector[_index+1] ) )
      {
      sblock_vector[_index].inc_size( sblock_vector[_index+1].size() );
      erase_sblock( _index + 1 );
      }
    }
  }
