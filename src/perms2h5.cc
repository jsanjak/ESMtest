//Command line parsing using boost (C++)
#include <boost/program_options.hpp>

//other boost stuff
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

#include <H5Cpp.h>


//Headers to conver chi-squared statistic into chi-squared p-value.  GNU Scientific Library (C language)
#include <gsl/gsl_cdf.h>

//standard C++ headers that we need
#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>

using namespace std;
using namespace boost::program_options;
using namespace H5;

struct options
/*
  This object represents the command-line options
 */
{
  bool strip,convert,verbose;
  string bimfile,ldfile,infile,outfile;
  size_t nrecords;
  options(void);
};

options::options(void) : strip(true),
			 convert(true),
			 verbose(false),
			 bimfile(string()),
			 ldfile(string()),
			 infile(string()),
			 outfile(string()),
			 nrecords(1)
{
}

options process_argv( int argc, char ** argv );
size_t process_bimfile( const options & O, H5File & ofile );
void process_ldfile( const options & O, H5File & ofile );
void process_perms( const options & O, size_t nmarkers, H5File & ofile );

int main( int argc, char ** argv )
{
  options O = process_argv( argc, argv );
  // cerr << O.ldfile << '\n';
  //Create output file
  H5File ofile( O.outfile.c_str() , H5F_ACC_TRUNC );
  size_t nmarkers = process_bimfile( O, ofile );
  process_perms( O, nmarkers, ofile );
  process_ldfile( O, ofile ); 
  ofile.close();
  exit(0);
}

options process_argv( int argc, char ** argv )
{
  options rv;

  options_description desc("Reads PLINK permuted data table from IFP.  Reformats to a HDF5 file.");
  desc.add_options()
    ("help,h", "Produce help message")
    ("nostrip","Do not strip the observed data from the file (default is to strip)")
    ("noconvert","Do not convert input into a p-value.  Default is to assume that the input is a chi^2 statistic with 1 degree of freedom")
    ("bim,b",value<string>(&rv.bimfile)->default_value(string()),"The bim file (map file for binary PLINK data)")
    ("linkage,l",value<string>(&rv.ldfile)->default_value(string()),"The LD file (pairwise r^2 from PLINK)")
    ("infile,i",value<string>(&rv.infile)->default_value(string()),"Input file name containing permutations.  Default is to read from stdin")
    ("outfile,o",value<string>(&rv.outfile)->default_value(string()),"Output file name.  Format is HDF5")
    ("nrecords,n",value<size_t>(&rv.nrecords)->default_value(1),"Number of records to buffer.")
    ("verbose,v","Write process info to STDERR")
    ;

  variables_map vm;
  store( command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm );
  notify(vm);
  
  if ( argc == 1 || vm.count("help") )
    {
      cerr << desc << '\n';
      exit(0);
    }

  bool bad_input = false;

  if (! vm.count("bim") )
    {
      cerr << "Error, no bim file name specified\n";
      bad_input = true;
    }

  if ( bad_input )
    {
      cerr << desc << '\n';
      exit(0);
    }

  if( vm.count("nostrip") )
    {
      rv.strip = false;
    }

  if( vm.count("noconvert") )
    {
      rv.convert = false;
    }

  if( vm.count("verbose") )
    {
      rv.verbose = true;
    }
  return rv;
}

size_t process_bimfile( const options & O, H5File & ofile )
/*
  Write map data into an h5 group called "Markers"
 */
{
  ifstream bimin( O.bimfile.c_str() );
  if (! bimin )
    {
      cerr << "Error, " << O.bimfile
	   << " could not be opened for reading\n";
      exit(10);
    }

  vector<string> markers,chroms;
  vector< const char * > marker_str,chrom_str;
  vector< int > vpos;
  string chrom,marker,dummy,line;
  int pos;
  while( !bimin.eof() )
    {
      bimin >> chrom >> marker >> dummy >> pos >> ws;
      getline( bimin, line );
      bimin >> ws;
      chroms.push_back( chrom );
      markers.push_back( marker );
      vpos.push_back( pos );
    }

  if ( O.verbose )
    {
      cerr << ".bim file contains " << markers.size() << " markers\n";
    }

  for( unsigned i = 0 ; i < markers.size() ; ++i )
    {
      marker_str.push_back( markers[i].c_str() );
      chrom_str.push_back( chroms[i].c_str() );
    }

  ofile.createGroup("/Markers");

  DSetCreatPropList cparms;
  hsize_t chunk_dims[1] = {markers.size()};
  hsize_t maxdims[1] = {markers.size()};

  cparms.setChunk( 1, chunk_dims );
  cparms.setDeflate( 6 ); //compression level makes a big differences in large files!  Default is 0 = uncompressed.
  
  DataSpace dataspace(1,chunk_dims, maxdims);

  H5::StrType datatype(H5::PredType::C_S1, H5T_VARIABLE); 

  DataSet marker_dset = ofile.createDataSet("/Markers/IDs",
					    datatype,
					    dataspace,
					    cparms);

  marker_dset.write(marker_str.data(), datatype );

  DataSet chrom_dset = ofile.createDataSet("/Markers/chr",
					    datatype,
					    dataspace,
					    cparms);

  chrom_dset.write(chrom_str.data(), datatype );

  DataSet pos_dset = ofile.createDataSet("/Markers/pos",
					 PredType::NATIVE_INT,
					 dataspace,
					 cparms);

  pos_dset.write( vpos.data(),
		  PredType::NATIVE_INT );

  return markers.size();
}

void process_perms( const options & O, size_t nmarkers, H5File & ofile )
{
    FILE * ifp = !O.infile.empty() ? fopen( O.infile.c_str(),"r" ) : stdin;

    if ( ifp == NULL )
      {
	cerr << "Error, input stream could not be opened.\n";
	exit(10);
      }

    if ( O.verbose )
      {
	cerr << "Starting to process perms for "
	     << nmarkers << " markers\n";
      }
    //vector< double > data(10*nmarkers);
    // double ** data = new double *[O.nrecords];
    // for( size_t i = 0 ; i < O.nrecords ; ++i )
    //   {
    // 	data[i] = new double[nmarkers];
    //   }
    //double * data = new double(O.nrecords*nmarkers);
    vector<double> data(O.nrecords*nmarkers);
    int repno;
    fscanf(ifp,"%d",&repno);

    char * buffer = new char[100];
    //The first line is the observed data
    for( size_t i = 0 ; i < nmarkers ; ++i )
      {
	int rv = fscanf(ifp,"%lf",&data[i]);
	if(O.convert)
	  {
	    data[i] = (data[i]!=1.) ? -log10(gsl_cdf_chisq_Q(data[i],1.)) : 0.;
	  }
      }

    if ( O.verbose )
      {
	cerr << "Read in observed data.\n" << endl;
      }
    ofile.createGroup("/Perms");

    DSetCreatPropList cparms;
    hsize_t chunk_dims[1] = {nmarkers};
    hsize_t maxdims[1] = {nmarkers};
    
    cparms.setChunk( 1, chunk_dims );
    cparms.setDeflate( 6 ); //compression level makes a big differences in large files!  Default is 0 = uncompressed.

  
    DataSpace * dataspace = new DataSpace(1,chunk_dims, maxdims);

    DataSet * d = new DataSet(ofile.createDataSet("/Perms/observed",
						  PredType::NATIVE_DOUBLE,
						  *dataspace,
						  cparms));

    d->write( data.data(), PredType::NATIVE_DOUBLE );

    delete dataspace;
    delete d;

    //ok, now we write a big matrix of the permuted values
    hsize_t chunk_dims2[2] = {10, nmarkers};
    hsize_t maxdims2[2] = {H5S_UNLIMITED,nmarkers};
    hsize_t datadims[2] = {0,nmarkers};
    hsize_t offsetdims[2] = {0,0};
    hsize_t recorddims[2] = {O.nrecords,nmarkers};
    cparms.setChunk( 2, chunk_dims2 );
    cparms.setDeflate( 6 );

    //dataspace = new DataSpace(2, recorddims, maxdims2);
    DataSpace fspace(2,datadims,maxdims2);
    d = new DataSet(ofile.createDataSet("/Perms/permutations",
					PredType::NATIVE_DOUBLE,
					fspace,
					cparms));

    //DataSpace memspace(2,recorddims);
    size_t RECSREAD = 0;
    while(!feof(ifp))
      {
	int rv = 1;
	size_t I = 0;
	RECSREAD = 0;
	for( size_t i = 0 ; !feof(ifp) && i < O.nrecords ; ++i,++RECSREAD )
	  {
	    if ( O.verbose )
	      {
		cerr << i << ": ";
	      }
	    repno = -1;
	    rv=fscanf(ifp,"%d",&repno);
	    if( rv == 0 || rv == -1 || feof(ifp ) )
	      {
	    	if( O.verbose ) cerr << "return 1\n";
	    	return; //we have hit the end of the file
	      }
	    //for( size_t j = 0 ; j < nmarkers-1 ; ++j,++I )
	    for( size_t j = 0 ; j < nmarkers ; ++j,++I )
	      {
		if(O.verbose) cerr << j << ',' << I << ' ';
		rv = fscanf(ifp,"%lf",&data[I]);
		// if( rv == 0 || !feof(ifp) )
		//   {
		//     if( O.verbose ) cerr << "converting " << repno << ':' << j << ',' << I << "to nan\n";
		//     //rv = chew2ws(ifp);
		//     data[I] = numeric_limits<double>::quiet_NaN();
		//   }
		// if ( rv == -1 )
		// //		if(rv==0||rv==-1)
		//   {
		//     cerr << "Error, input stream ended before expected...\n";
		//     ofile.close();
		//     exit(10);
		//   }
		if(O.convert)
		  {
		    data[I]= (data[I] != 1.) ? -log10(gsl_cdf_chisq_Q(data[I],1.)) : 0.;
		  }
	      }
	    if(O.verbose) cerr<< endl;
	  }
	datadims[0] += RECSREAD;//O.nrecords;
	recorddims[0] = RECSREAD;
	DataSpace memspace(2,recorddims);
	d->extend( datadims );
	DataSpace * dspace = new DataSpace( d->getSpace() );
	dspace->selectHyperslab(H5S_SELECT_SET,recorddims,offsetdims);
	d->write(data.data(), PredType::NATIVE_DOUBLE,memspace,*dspace);
	delete dspace;
	offsetdims[0] += O.nrecords;
      }
}

void process_ldfile( const options & O, H5File & ofile )
{
  
  ifstream ldf (O.ldfile.c_str());
  string line;
  vector<string> lineVector, snpA, snpB;
  vector<double> rsq;
  double r2;
  string::size_type sz;
  if (ldf.is_open())
    {
      while(getline(ldf,line))
	{
	  
	  boost::split(lineVector,line,boost::is_any_of(" "),boost::token_compress_on);
	  snpA.push_back(lineVector.at(3));
	  snpB.push_back(lineVector.at(6));
	  r2 = ::atof( lineVector.at(7).c_str());
	  //cerr << r2;
	  //may have to make the rsq's a double
	  //r2 = stod (lineVector.at(7)); 
	  rsq.push_back(r2);
	  
	}
      ldf.close();
    }
  else cerr <<"Unable to read LD file"<< '\n';
  
  ofile.createGroup("/LD");
  
  DSetCreatPropList cparms;
  hsize_t chunk_dims[1] = {snpA.size()};
  hsize_t maxdims[1] = {snpA.size()};

  cparms.setChunk( 1, chunk_dims );
  cparms.setDeflate( 6 ); //compression level makes a big differences in large files!  Default is 0 = uncompressed.
  
  DataSpace dataspace(1,chunk_dims, maxdims);

  H5::StrType datatype(H5::PredType::C_S1, H5T_VARIABLE); 

  DataSet snpA_dset = ofile.createDataSet("/LD/snpA",
					    datatype,
					    dataspace,
					    cparms);

  snpA_dset.write(snpA.data(), datatype );
  
  DataSet snpB_dset = ofile.createDataSet("/LD/snpB",
					    datatype,
					    dataspace,
					    cparms);

  snpB_dset.write(snpB.data(), datatype );
  /*for (vector<double>::const_iterator i = rsq.begin();i!=rsq.end();++i)
    {
      cerr << *i<< ' ';
      }*/
  DataSet rsq_dset = ofile.createDataSet("/LD/rsq",
					 H5::PredType::NATIVE_DOUBLE,
					 dataspace,
					 cparms);


  rsq_dset.write( rsq.data(),H5::PredType::NATIVE_DOUBLE );

}
