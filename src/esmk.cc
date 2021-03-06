
//Boost headers
#include <boost/program_options.hpp>
#include <boost/bind.hpp>
#include <boost/unordered_map.hpp>

//C headers
#include <H5Cpp.h>
#include <zlib.h>

//Standard c++
#include <iostream>   //let's us print to screen
#include <string>     //strings = containers of characters
#include <fstream>    //let's us write to files
#include <vector>     //vectors = containers of other things
#include <cmath>      //The C++ version of C's math.h (puts the C functions in namespace std)
#include <algorithm>  //find, sort, etc.
#include <set>        //A set is a container, see http://www.cplusplus.com/reference/set/set/
#include <thread>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <utility>
/*
  This is a header that I wrote.

  It contains overly-simple functions
  to read in 1-dimensional data from H5 files
*/
#include <H5util.hpp>
#include <ESMH5type.hpp>

using namespace std;
using namespace boost::program_options;
using namespace H5;

//This is a data type to hold command-line options
struct esm_options
{
  string outfile;
  int winsize,jumpsize,K,nwindows;
  size_t cmarkers,cperms,nperms;
  ESMBASE LDcutoff;
  vector<string> infiles;
};

struct within
/*
  Function object.   
  Returns true if val is within range
  specified by (left,right)
 */
{
  typedef bool result_type;
  inline bool operator()(const int & val,
			 const int & left,
			 const int & right) const
  {
    return (val >= left && val <= right);
  }
};

/*
  get_indexes is passed a list of positions on a chromosome and
  the left and right boundaries of a window.

  The return value is the pair of indexes within pos
  whose values are >= left and <= right.

  If no position exists satisfying one of these criteria,
  the maximum value of a size_t is returned.
 */
pair<size_t,size_t> get_indexes( const vector<int> & pos,
				 const int & left,
				 const int & right );
//Parse command line options
esm_options parseargs( int argc, char ** argv );
//Ask if all the permutation files contain the same marker info
bool permfilesOK( const esm_options & O );
//Runs the esm_k test on the data

void calc_esm( const vector<ESMBASE> * data,
	       const ESMBASE & ESM_obs,
	       const size_t & nperms,
	       const int & nmarkers,
	       const int & K,
	       ESMBASE * ESMP_win,
	       const vector<short> & keep_markers_win);

void run_test( const esm_options & O );

int main( int argc, char ** argv )
{
  esm_options O = parseargs(argc,argv); 
  if( !permfilesOK(O) )
    {
      cerr << "Error with permutation files\n";
      exit(10);
    }
  run_test(O);

  exit(0);
}

esm_options parseargs( int argc, char ** argv )
{
  esm_options rv;

  options_description desc("Calculate ESM_K p-values in sliding window");
  desc.add_options()
    ("help,h", "Produce help message")
    ("outfile,o",value<string>(&rv.outfile),"Output file name.  Format is gzipped")
    ("winsize,w",value<int>(&rv.winsize),"Window size (bp)")
    ("jumpsize,j",value<int>(&rv.jumpsize),"Window jump size (bp)")
    ("K,k",value<int>(&rv.K),"Number of markers to use for ESM_k stat in a window.  Must be > 0.")
    ("nwindows,n",value<int> (&rv.nwindows),"Number of windows to bring in at a time")
    ("LDcutoff,r",value<ESMBASE> (&rv.LDcutoff), "The R^2 cutoff for LD between SNPs")
    ("cmarkers,m",value<size_t>(&rv.cmarkers)->default_value(50),"Raw data chunk size in markers, default = 50")
    ("cperms,c",value<size_t>(&rv.cperms)->default_value(10000),"Raw data chunk size in perms, default = 10000")
    ("nperms,p",value<size_t>(&rv.nperms)->default_value(2000000),"Number of perms, default = 2000000")
    ;

  variables_map vm;
  store( command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm );
  notify(vm);

  if( vm.count("help") || argc == 1 )
    {
      cerr << desc << '\n';
      exit(0);
    }
  
  parsed_options parsed = command_line_parser(argc, argv).options(desc).allow_unregistered().run(); 
  
  if ( argc == 1 || vm.count("help") )
    {
      cerr << desc << '\n';
      exit(0);
    }

  if (!vm.count("outfile") || !vm.count("winsize") || !vm.count("jumpsize") || !vm.count("K") || !vm.count("nwindows"))
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

//make sure each perm file contains the same markers, etc.
bool permfilesOK( const esm_options & O )
{
  if( O.infiles.empty() ) { return false; }


  vector<string> chroms_0 = read_strings(O.infiles[0].c_str(),"/Markers/chr");
 
  set<string> sc_0(chroms_0.begin(),chroms_0.end());
  
  if( sc_0.size() > 1 ) { return false; }
 
  vector<string> markers_0 = read_strings(O.infiles[0].c_str(),"/Markers/IDs");
  vector<int> pos_0 = read_ints(O.infiles[0].c_str(),"/Markers/pos");  
  vector<string> snpA_0 = read_strings(O.infiles[0].c_str(),"/LD/snpA");
  vector<string> snpB_0 = read_strings(O.infiles[0].c_str(),"/LD/snpB");
  
  for ( size_t i = 1 ; i < O.infiles.size() ; ++i )
    {
      vector<string> chroms_i = read_strings(O.infiles[i].c_str(),"/Markers/chr");
      set<string> sc_i(chroms_i.begin(),chroms_i.end());
      if( sc_i.size() > 1 ) { return false; }
      vector<string> markers_i = read_strings(O.infiles[i].c_str(),"/Markers/IDs");
      vector<int> pos_i = read_ints(O.infiles[i].c_str(),"/Markers/pos");
      vector<string> snpA_i = read_strings(O.infiles[i].c_str(),"/LD/snpA");
      vector<string> snpB_i = read_strings(O.infiles[i].c_str(),"/LD/snpB");
if( markers_0 != markers_i)
	{
	  cerr <<"markers are not equal"<<"\n";
	  return false;
	}
 if(  sc_0 != sc_i )
	{
	  cerr <<"chroms are not equal"<<"\n";
	  return false;
	}
 if(pos_0 != pos_i )
	{
	  cerr <<"positions not equal"<<"\n";
	  return false;
	}
 if(  snpA_0 != snpA_i )
	{
	  cerr <<"snpA not equal"<<"\n";
	  return false;
	}
  if(  snpB_0 != snpB_i )
	{
	  cerr <<"snpA not equal"<<"\n";
	  return false;
	}  
      
      if( markers_0 != markers_i || sc_0 != sc_i || pos_0 != pos_i || snpA_0 != snpA_i || snpB_0 != snpB_i)
	{
	  cerr <<"things are not equal"<<"\n";
	  return false;
	}  
    }
 
  return true;
}

pair<size_t,size_t> get_indexes( const vector<int> & pos,
				 const int & left,
				 const int & right )
{
  vector<int>::const_iterator ci1 = find_if(pos.begin(),pos.end(),
					    boost::bind( within(),_1,left,right));
  vector<int>::const_reverse_iterator ci2 = find_if(pos.rbegin(),pos.rend(),
						    boost::bind(within(),_1,left,right) );

  if( ci1 == pos.end() && ci2 == pos.rend() )
    {
      //no snps are in window
      return make_pair( numeric_limits<size_t>::max(),
			numeric_limits<size_t>::max() );
    }
  return make_pair( ci1-pos.begin(), pos.rend()-ci2-1 );
}

void calc_esm( const vector<ESMBASE> * data,
	       const ESMBASE & ESM_obs,
	       const size_t & nperms,
	       const int & nmarkers,
	       const int & K,
	       ESMBASE * ESMP_win,
	       const vector<short> & keep_markers_win)
{
	      //for each perm in the data
  vector<ESMBASE> ESM_perm ( nperms );
  for ( size_t j = 0; j< nperms; ++j)
    {
    
      ESMBASE ESM = 0;
      /*calculate the ESM
	Which is:
		    
	ESM = SUM_k_M(Y_k + log10(k/M))
	
	where Y_k is the kth most significant chisq value and M is the number
	of markers considered.
      */  
	     	   
      // need to go from 0 to min(markers_used, nmarkers)
      vector<ESMBASE> temp;
      for ( int k =0 ; k < nmarkers ; ++k)
	{
	  temp.push_back((*data)[nmarkers*j + k]*keep_markers_win[k]);
	}
      //sort the markers for this range ( this sorts in ascending order)
      sort( temp.begin(),temp.end(),boost::bind(greater<ESMBASE>(),_1,_2));
      for ( int k =0 ; k < min(K,nmarkers) ; ++k )
	{
	  ESM += temp[k] + log10(((ESMBASE) k + 1) / (ESMBASE) nmarkers);
	}
		  
      ESM_perm[j] = ESM;
      
    } 
    ESMBASE PVAL_win = 0 ;
    for ( size_t i = 0; i < ESM_perm.size(); ++i)//for each perm
    {
      if (ESM_perm[i]>=ESM_obs)//if the ESM is larger than observed
	{
	  PVAL_win += 1 ;
	}
    }
    PVAL_win /= (ESMBASE)ESM_perm.size();//divide by number of perms
    *ESMP_win = PVAL_win;

}

				 
void run_test( const esm_options & O )
{
  //Step 1: read in the marker data from the first file in 0.infiles:
  //1a: the chrom labels
  
  vector<string> chroms_0 = read_strings(O.infiles[0].c_str(),"/Markers/chr");
  
  set<string> sc_0(chroms_0.begin(),chroms_0.end());
  chroms_0.clear();
  //1b: the rsID for the markers
  
  vector<string> markers_0 = read_strings(O.infiles[0].c_str(),"/Markers/IDs");
  
  //get the observed chisqs:

  vector<ESMBASE> chisq_obs = read_doubles(O.infiles[0].c_str(),"/Perms/observed");
  
  //1c: the marker positions

  vector<int> pos_0 = read_ints(O.infiles[0].c_str(),"/Markers/pos");  
  
  //get the LD lists:

  vector<string>snpA = read_strings(O.infiles[0].c_str(),"/LD/snpA");
  vector<string>snpB = read_strings(O.infiles[0].c_str(),"/LD/snpB");
  vector<ESMBASE>rsq = read_doubles(O.infiles[0].c_str(),"/LD/rsq");
  
  //LD map takes a pair of snps and returns an R squared value
  typedef pair<string, string> snp_pair;
  typedef map<snp_pair, ESMBASE> LDMap;

  LDMap myld;
  snp_pair snps;
  
  for (size_t i=0; i < snpA.size();++i)
    {
      snps = make_pair(snpA[i],snpB[i]);
      myld.insert(make_pair(snps,rsq[i])); 
    }
  
  
  //Step 2: establish the left and right boundaries of the first set of windows
  int left = 1, right = O.winsize  + O.jumpsize*(O.nwindows-1) + 1;

  //Step 3: go over the current data and make sure that our helper functions are working
  
   const int LPOS = *(pos_0.end()-1); //This is the last position in pos_0.  Equivalent to pos[pos.size()-1], but I guess I like to complicate things.
  
  //declare vectors for the final PVALUES, the midpoint of associated window and chromosome(dumbway):
  vector<ESMBASE> p_values;
  vector<ESMBASE> midpoints;

  
  //While there is at least one valid window in the set
  while( (LPOS - left)>= O.winsize )
    {
      //get the indexes in pos_0 corresponding to left- and right- most SNPs in in the set of windows
      pair<size_t,size_t> indexes_set = get_indexes(pos_0,left,right);
      size_t nmarkers_set = (indexes_set.second - indexes_set.first + 1);
      
      if( indexes_set.first != numeric_limits<size_t>::max() ) //If there are SNPs in the window set 
	{
	  //the set is either full with n = O.nwindows or it is smaller
	  // such that LPOS is the real right endpoint and 
	  int nwin_set = min(O.nwindows,( ((LPOS-left)-O.winsize)/O.jumpsize) + 1);
	  vector< pair<size_t,size_t> > indexes_win;
	  vector<size_t> nmarkers_win;
	  vector<size_t> loci_mid;
	  for ( int m = 0 ; m < nwin_set; ++m )
	    {      
	      size_t nmarkers_set = (indexes_set.second - indexes_set.first + 1);
	      int izqui = left + m*O.jumpsize  ;
	      int derech = izqui + O.winsize;
	      indexes_win.push_back(get_indexes(pos_0,izqui, derech));
	      nmarkers_win.push_back(indexes_win[m].second - indexes_win[m].first + 1);
	      loci_mid.push_back( (derech + izqui)/2 );
	    }

	  vector<ESMBASE> ESMP_win ( nwin_set ) ;
	  vector<ESMBASE> ESM_obs_win ( nwin_set );
	  vector<ESMBASE> newdata  ;   
	  size_t markers_used = O.K;

      	 	 
	  for( size_t i = 0 ; i < O.infiles.size() ; ++i ) 
	    {
	      //Get a the slab in vector form from the file
	      vector<ESMBASE> fresh = read_doubles_slab(O.infiles[i].c_str(),"/Perms/permutations",indexes_set.first, nmarkers_set,O.cmarkers,O.cperms,O.nperms) ;
	      for ( size_t b = 0; b< fresh.size(); ++b)
		{
		  newdata.push_back(fresh[b]);
		}
	    }
	  //this should be the same, but may as well determine it here
	  size_t nperms_tot = newdata.size()/nmarkers_set;
	  
	  //prepare the vector of esm values  for the new values from the file
	  //we have one ESM value for each perm
	  //vector of vectors to contain data for all perms in each window
	  vector< vector<ESMBASE> > data ( nwin_set );
	  vector< vector<short> > keep_markers_win ( nwin_set );
	  for ( int m = 0 ; m < nwin_set; ++m)
	    {
	      if( indexes_win[m].first != numeric_limits<size_t>::max() ){
		//using the constructor to copy chisq_obs; probably the same as using equals, but I wasn't 100% sure
		//used to reset the sorting that occurs
		
		vector<ESMBASE> chisq_win( chisq_obs ) ;
		vector<string> markers_win ( markers_0 ) ;
		vector<short> keep ( nmarkers_win[m], 1 );
		keep_markers_win[m] = keep;
		snp_pair AB;
		ESMBASE LD_AB;
		size_t a = 0;
		//Go through markers in the window and filter by LD
		//If two markers are in too much LD, then keep the one to the left, i.e. the first one
		for (size_t q = indexes_win[m].first; q < indexes_win[m].first + nmarkers_win[m]-1;++q,++a)
		  {
		    size_t b = a + 1;
		    for (size_t qq = q + 1; qq < indexes_win[m].first + nmarkers_win[m]; ++qq,++b)
		      {
			if (keep_markers_win[m][b])
			  {
			    snp_pair AB = make_pair(markers_0[q],markers_0[qq]);
			    LD_AB = myld[AB];
			    if (LD_AB >O.LDcutoff)
			      {
				
				keep_markers_win[m][b] = 0;
				
				chisq_win[qq] = chisq_win[qq]*0;
				
			      }
			  }
					
		      }		    
		  }
		
		sort( chisq_win.begin() + indexes_win[m].first, 
		      chisq_win.begin() + indexes_win[m].second,
		      boost::bind(greater<ESMBASE>(),_1,_2)
		      );
		
		ESMBASE ESM_obs = 0;
		size_t n;    
		for ( size_t q = indexes_win[m].first; q < indexes_win[m].first + min(markers_used, nmarkers_win[m]); ++q )
		  {
		    
		    n  = q - indexes_win[m].first + 1;
		    //critical that the denominator be nmarkers in the window NOT markers_used
		    ESM_obs += chisq_win[q] + log10((ESMBASE) n / (ESMBASE) nmarkers_win[m]);
		      
		  }
		
		ESM_obs_win[m] = ESM_obs;
		for ( size_t w = 0 ; w < nperms_tot; ++w )
		  {
		    for ( size_t z = 0; z < nmarkers_win[m]; ++z )
		      { 
			//each element in data is a vector of p values collated by marker then perm
			//THIS is where the problem is; you are moving forward by too much, you need to caluclate the number of markers
			//that you skip during the jump.
			data[m].push_back(newdata[w*nmarkers_set + indexes_win[m].first - indexes_set.first + z]) ;
		      }
		  }
	      }
	    }//end for m in nwin set
	  
	  //ESTABLISH THREADS
	  vector<thread> t ( nwin_set );
	  
	  
	  for ( unsigned h = 0 ; h < t.size(); ++h)
	    {
	      //throw the vector in data[h] to the function calc_esm(), with some params,and output to ESMP_win[h]
	      if( indexes_win[h].first != numeric_limits<size_t>::max() ){
		t[h] = thread(calc_esm, &data[h],ESM_obs_win[h],nperms_tot,nmarkers_win[h],markers_used,&ESMP_win[h],keep_markers_win[h]);
	      }
	    }
	  
	  for ( unsigned h = 0 ; h < t.size(); ++h)
	    {
	      //Join means come on back to main thread; will wait until all are done.
	      if( indexes_win[h].first != numeric_limits<size_t>::max() ){
		t[h].join();
	      }
	    }
	  for ( size_t h = 0 ; h < ESMP_win.size(); ++h)
	    { 
	      if ( indexes_win[h].first != numeric_limits<size_t>::max()){
		midpoints.push_back(loci_mid[h]);
		p_values.push_back(ESMP_win[h]);
	      }
	    }
	  
	}//end if there are SNPs in the set
      //jump forward to the next set of windows
      left += O.jumpsize*O.nwindows;
      right += O.jumpsize*O.nwindows;
      
    }//end do while loop
 
  
  ofstream output;
  output.open(O.outfile.c_str());
  output << "p.values"<<' '<<"loci.midpoint"<<'\n';
  for ( size_t i = 0; i< p_values.size(); ++i)
    { 
      output<<p_values[i]<<' '<<midpoints[i]<<'\n';
    }
  output.close();
}
