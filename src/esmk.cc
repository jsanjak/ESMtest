//libhdf5
#include <H5Cpp.h>

//Command line parsing using boost (C++)
#include <boost/program_options.hpp>

//other boost stuff
#include <boost/bind.hpp>

//zlib
#include <zlib.h>

//Standard c++
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace std;
using namespace boost::program_options;
using namespace H5;

struct esm_options
{
  string outfile;
  unsigned winsize,jumpsize,K;
  vector<string> infiles;
};

esm_options parseargs( int argc, char ** argv );

int main( int argc, char ** argv )
{
  esm_options O = parseargs(argc,argv); 
}

esm_options parseargs( int argc, char ** argv )
{
  esm_options rv;

  options_description desc("Calculate ESM_K p-values in sliding window");
  desc.add_options()
    ("help,h", "Produce help message")
    ("outfile,o",value<string>(&rv.outfile),"Output file name.  Format is gzipped")
    ("winsize,w",value<unsigned>(&rv.winsize),"Window size (bp)")
    ("jumpsize,j",value<unsigned>(&rv.jumpsize),"Window jump size (bp)")
    ("K,k",value<unsigned>(&rv.K),"Number of markers to use for ESM_k stat in a window")
    ;

  variables_map vm;
  store( command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm );
  notify(vm);

  if( vm.count("help") || argc == 1 )
    {
      cerr << desc << '\n';
      exit(0);
    }
  
  parsed_options parsed = 
    command_line_parser(argc, argv).options(desc).allow_unregistered().run(); 
  
  if ( argc == 1 || vm.count("help") )
    {
      cerr << desc << '\n';
      exit(0);
    }

  if (!vm.count("outfile") || !vm.count("winsize") || !vm.count("jumpsize") || !vm.count("K") )
    {
      cerr << "Too few options given.\n"
	   << desc << '\n';
      exit(10);
    }
  rv.infiles = collect_unrecognized(parsed.options, include_positional);
  if(rv.infiles.empty())
    {
      cerr << "Error: no permutation files passed to program.\n"
	   << desc << '\n';
      exit(0);
    }
  return rv;
}
