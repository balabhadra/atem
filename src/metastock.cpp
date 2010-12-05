#include "metastock.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef USE_FTS
	#include <fts.h>
#else 
	#include <dirent.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#include "ms_file.h"
#include "util.h"



#define MAX_FILE_LENGTH (1024*1024)


class FileBuf
{
	public:
		FileBuf();
		~FileBuf();
		
		bool hasName() const;
		const char* constName() const;
		const char* constBuf() const;
		int len() const;
		
		void setName( const char* file_name );
		
		void readFile( int fildes );
		
	private:
		char name[11];
		char *buf;
		int buf_len;
		int buf_size;
};

FileBuf::FileBuf() :
	buf( (char*) malloc( MAX_FILE_LENGTH ) ),
	buf_len(0),
	buf_size(MAX_FILE_LENGTH)
{
	*name = 0;
}

FileBuf::~FileBuf()
{
	free(buf);
}

bool FileBuf::hasName() const
{
	return (*name != 0);
}

const char* FileBuf::constName() const
{
	return name;
}

const char* FileBuf::constBuf() const
{
	return buf;
}

int FileBuf::len() const
{
	return buf_len;
}

void FileBuf::setName( const char* file_name )
{
	strcpy( name, file_name );
}

void FileBuf::readFile( int fildes )
{
	buf_len = read( fildes, buf, MAX_FILE_LENGTH );
}




char Metastock::print_sep = '\t';
unsigned short Metastock::prnt_master_fields = 0;
unsigned char Metastock::prnt_data_fields = 0;
unsigned short Metastock::prnt_data_mr_fields = 0;


Metastock::Metastock() :
	print_date_from(0),
	ms_dir(NULL),
	m_buf( new FileBuf() ),
	e_buf( new FileBuf() ),
	x_buf( new FileBuf() ),
	fdat_buf( new FileBuf() )
{
	error[0] = '\0';
/* dat file numbers are unsigned short only */
#define MAX_DAT_NUM 0xFFFF
	max_dat_num = 0;
	mr_len = 0;
	mr_list = NULL;
	mr_skip_list = NULL;
}



#define SAFE_DELETE( _p_ ) \
	if( _p_ != NULL ) { \
		delete _p_; \
	}


Metastock::~Metastock()
{
	free( mr_skip_list );
	free( mr_list );
	
	delete( fdat_buf );
	delete( x_buf );
	delete( e_buf );
	delete( m_buf );
	free( ms_dir );
}


#ifdef USE_FTS

#define CHECK_MASTER( _file_buf_, _gen_name_ ) \
	if( strcasecmp(_gen_name_, node->fts_name) == 0 ) { \
		assert( !_file_buf_->hasName() ); \
		_file_buf_->setName( node->fts_name ); \
	}

bool Metastock::findFiles()
{
	//TODO error handling!
	char *path_argv[] = { ms_dir, NULL };
	FTS *tree = fts_open( path_argv,
		FTS_NOCHDIR | FTS_LOGICAL | FTS_NOSTAT, NULL );
	if (!tree) {
		perror("fts_open");
		assert(false);
	}
	FTSENT *node;
	while ((node = fts_read(tree))) {
		if( (node->fts_level > 0) && (node->fts_info == FTS_D ) ) {
			fts_set(tree, node, FTS_SKIP);
		} else if( node->fts_info == FTS_F ) {
			if( (*node->fts_name == 'F' || *node->fts_name == 'f') &&
				node->fts_name[1] >= '1' && node->fts_name[1] <= '9') {
				char *c_number = node->fts_name + 1;
				char *end;
				long int number = strtol( c_number, &end, 10 );
				assert( number > 0 && c_number != end );
				if( (strcasecmp(end, ".MWD") == 0 || strcasecmp(end, ".DAT") == 0)
					&& number <= MAX_DAT_NUM ) {
					add_mr_list_datfile( number, node->fts_name );
				}
			} else {
				CHECK_MASTER( m_buf, "MASTER" );
				CHECK_MASTER( e_buf, "EMASTER" );
				CHECK_MASTER( x_buf, "XMASTER" );
			}
		}
	}
	
	if (errno) {
		perror("fts_read");
		assert( false );
	}
	
	if (fts_close(tree)) {
		perror("fts_close");
		assert( false );
	}
	return true;
}

#undef CHECK_MASTER

#else /*USE_FTS*/

#define CHECK_MASTER( _file_buf_, _gen_name_ ) \
	if( strcasecmp(_gen_name_, dirp->d_name) == 0 ) { \
		assert( !_file_buf_->hasName() ); \
		_file_buf_->setName( dirp->d_name ); \
	}

bool Metastock::findFiles()
{
	DIR *dirh;
	struct dirent *dirp;
	
	if ((dirh = opendir( ms_dir )) == NULL) {
		setError( ms_dir, strerror(errno) );
		return false;
	}
	
	for (dirp = readdir(dirh); dirp != NULL; dirp = readdir(dirh)) {
		if( ( dirp->d_name[0] == 'F' || dirp->d_name[0] == 'f') &&
			dirp->d_name[1] >= '1' && dirp->d_name[1] <= '9') {
			char *c_number = dirp->d_name + 1;
			char *end;
			long int number = strtol( c_number, &end, 10 );
			assert( number > 0 && c_number != end );
			if( (strcasecmp(end, ".MWD") == 0 || strcasecmp(end, ".DAT") == 0)
					&& number <= MAX_DAT_NUM ) {
				add_mr_list_datfile( number, dirp->d_name );
			}
		} else {
			CHECK_MASTER( m_buf, "MASTER" );
			CHECK_MASTER( e_buf, "EMASTER" );
			CHECK_MASTER( x_buf, "XMASTER" );
		}
	}
	
	closedir( dirh );
	return true;
}

#undef CHECK_MASTER

#endif /*USE_FTS*/


bool Metastock::setDir( const char* d )
{
	// set member ms_dir inclusive trailing '/'
	int dir_len = strlen(d);
	ms_dir = (char*) realloc( ms_dir, dir_len + 2 );
	strcpy( ms_dir, d );
	if( ms_dir[ dir_len - 1] != '/' ) {
		ms_dir[dir_len] = '/';
		ms_dir[dir_len + 1] = '\0';
	}
	
	if( !findFiles() ) {
		return false;
	}
	
	if( !readMasters() ){
		return false;
	}
	if( !parseMasters() ) {
		return false;
	}
	
	return true;
}


bool Metastock::setOutputFormat( char sep, int fmt_data, int fmt_symbols )
{
	print_sep = sep;
	
	
	if( fmt_data < 0 || fmt_symbols < 0 ) {
		setError( "wrong output format" );
		return false;
	}
	
	if( fmt_symbols ) {
		prnt_master_fields = fmt_symbols;
	} else {
		prnt_master_fields = 0xFFFF;
	}
	
	if( fmt_data ) {
		prnt_data_fields = fmt_data;
		prnt_data_mr_fields = fmt_data >> 9;
	} else {
		prnt_data_fields = 0xFF;
		prnt_data_mr_fields = M_SYM;
	}
	
	FDat::initPrinter( sep, prnt_data_fields );
	return true;
}


bool Metastock::readFile( FileBuf *file_buf ) const
{
	// build file name with full path
	char *file_path = (char*) alloca( strlen(ms_dir)
		+ strlen(file_buf->constName()) + 1 );
	strcpy( file_path, ms_dir );
	strcpy( file_path + strlen(ms_dir), file_buf->constName() );
	
	
	int fd = open( file_path, O_RDONLY );
	if( fd < 0 ) {
		setError( file_path, strerror(errno) );
		return false;
	}
	file_buf->readFile( fd );
// 	fprintf( stderr, "read %s: %d bytes\n", file_path, *len);
	close( fd );
//	assert( ba->size() == rb ); //TODO
	
	return true;
}


bool Metastock::parseMasters()
{
	MasterFile mf( m_buf->constBuf(), m_buf->len() );
	EMasterFile emf( e_buf->constBuf(), e_buf->len() );
	XMasterFile xmf( x_buf->constBuf(), x_buf->len() );
	int cntM = mf.countRecords();
	int cntE = emf.countRecords();
	int cntX = xmf.countRecords();
	
	if( cntM <= 0 && cntE <= 0 && cntX <= 0 ) {
		setError( "all *Master files invalid" );
		return false;
	}
	
	if( cntM > 0 ) {
		/* we prefer to use Master because EMaster is often broken */
		for( int i = 1; i<=cntM; i++ ) {
			master_record *mr = &mr_list[ mf.fileNumber(i) ];
			assert( mr->record_number == 0 );
			mf.getRecord( mr, i );
		}
		if( cntE == cntM ) {
			/* EMaster seems to be usable - fill up long names */
			for( int i = 1; i<=cntE; i++ ) {
				master_record *mr = &mr_list[ emf.fileNumber(i) ];
				assert( mr->record_number != 0 );
				emf.getLongName( mr, i );
			}
		}
	} else if ( cntE > 0 ) {
		/* Master is broken - use EMaster */
		for( int i = 1; i<=cntE; i++ ) {
			master_record *mr = &mr_list[ emf.fileNumber(i) ];
			assert( mr->record_number == 0 );
			emf.getRecord( mr, i );
		}
	} /* else neither Master or EMaster is valid */
	
	if( cntX > 0 ) {
		/* XMaster is optional */
		for( int i = 1; i<=cntX; i++ ) {
			master_record *mr = &mr_list[ xmf.fileNumber(i) ];
			assert( mr->record_number == 0 );
			xmf.getRecord( mr, i );
		}
	}
	
	return true;
}


bool Metastock::readMasters()
{
	if( !m_buf->hasName() && !e_buf->hasName() && !x_buf->hasName() ) {
		setError( "no *Master files found" );
		return false;
	}
	
	if( m_buf->hasName() ) {
		if( !readFile( m_buf ) ) {
			return false;
		}
	} else {
		printWarn("Master file not found");
	}
	
	if( e_buf->hasName() ) {
		if( !readFile( e_buf ) ) {
			return false;
		}
	}else {
		printWarn("EMaster file not found");
	}
	
	if( x_buf->hasName() ) {
		if( !readFile( x_buf ) ) {
			return false;
		}
	} else {
		// xmaster is optional, but we could check for existing f#.mwd >255
		printWarn("XMaster file not found");
	}
	
	return true;
}


const char* Metastock::lastError() const
{
	return error;
}


void Metastock::printWarn( const char* e1, const char* e2 ) const
{
	if( e2 == NULL || *e2 == '\0' ) {
		fprintf( stderr, "warning: %s\n", e1);
	} else {
		fprintf( stderr, "warning: %s: %s\n", e1, e2 );
	}
}


void Metastock::setError( const char* e1, const char* e2 ) const
{
	if( e2 == NULL || *e2 == '\0' ) {
		snprintf( error, ERROR_LENGTH, "%s", e1);
	} else {
		snprintf( error, ERROR_LENGTH, "%s: %s", e1, e2 );
	}
}


void Metastock::dumpMaster() const
{
	MasterFile mf( m_buf->constBuf(), m_buf->len() );
	mf.check();
}


void Metastock::dumpEMaster() const
{
	EMasterFile emf( e_buf->constBuf(), e_buf->len() );
	emf.check();
}


void Metastock::dumpXMaster() const
{
	XMasterFile xmf( x_buf->constBuf(), x_buf->len() );
	xmf.check();
}


bool Metastock::incudeFile( unsigned short f ) const
{
	for( int i = 1; i< mr_len; i++ ) {
			mr_skip_list[i] = true;
	}
	
	if( f > 0 && f < mr_len && mr_list[f].record_number != 0 ) {
		mr_skip_list[f] = false;
		return true;
	} else {
		setError("data file not referenced by master files");
		return false;
	}
}


time_t str2time( const char* s)
{
	struct tm dt;
	time_t dt_t;
	bzero( &dt, sizeof(tm) );
	
	int ret = sscanf( s, "%d-%d-%d %d:%d:%d", &dt.tm_year,
		&dt.tm_mon, &dt.tm_mday, &dt.tm_hour, &dt.tm_min, &dt.tm_sec );
	
	if( ret < 0 ) {
		return -1;
	} else if( ret != 6  && ret != 3 ) {
		return -1;
	}
	
	
	dt.tm_year -= 1900;
	dt.tm_mon -= 1;
	dt.tm_isdst = -1;
	
	dt_t = mktime( &dt );
	
	return dt_t;
}


int str2date( const char* s)
{
	int y, m, d;
	y = m = d = 0;
	
	int ret = sscanf( s, "%d-%d-%d", &y, &m, &d );
	
	if( ret != 3 ) {
		return -1;
	}
	
	if( !(y>=0 && y<=9999) ||  !(m>=1 && m<=12 ) || !(d>=1 && d<=31) ) {
		return -1;
	}
	
	return 10000 * y + 100 * m + d;
}


bool Metastock::setPrintDateFrom( const char *date )
{
	int dt = str2date( date );
	if( dt < 0 ) {
		setError("parsing date time");
		return false;
	}
	print_date_from = dt;
	return true;
}


bool Metastock::excludeFiles( const char *stamp ) const
{
	bool revert = false;
	if( *stamp == '-' ) {
		stamp++;
		revert = true;
	}
	time_t oldest_t = str2time( stamp );
	if( oldest_t < 0 ) {
		setError("parsing date time");
		return false;
	}
	
	for( int i = 1; i<mr_len; i++ ) {
		if( *mr_list[i].file_name == '\0' || mr_skip_list[i] ) {
			continue;
		}
		assert( mr_list[i].file_number == i );
		
		char *file_path = (char*) alloca( strlen(ms_dir) + strlen( mr_list[i].file_name) + 1 );
		strcpy( file_path, ms_dir );
		strcpy( file_path + strlen(ms_dir), mr_list[i].file_name );
		
		struct stat s;
		int tmp = stat( file_path, &s );
		if( tmp < 0 ) {
			setError( file_path,  strerror(errno) );
			return false;
		}
		if( !revert ) {
			if( oldest_t > s.st_mtime ) {
				mr_skip_list[i] = true;
			}
		} else {
			if( oldest_t <= s.st_mtime ) {
				mr_skip_list[i] = true;
			}
		}
	}
	return true;
}


bool Metastock::dumpSymbolInfo() const
{
	char buf[MAX_SIZE_MR_STRING + 1];
	
	for( int i = 1; i<mr_len; i++ ) {
		if( mr_list[i].record_number != 0 && !mr_skip_list[i] ) {
			assert( mr_list[i].file_number == i );
			int len = mr_record_to_string( buf, &mr_list[i],
				prnt_master_fields, print_sep );
			buf[len++] = '\n';
			buf[len] = '\0';
			fputs( buf, stdout );
		}
	}
	return true;
}


void Metastock::resize_mr_list( int new_len )
{
	mr_list = (master_record*) realloc( mr_list,
		new_len * sizeof(master_record) );
	mr_skip_list = (bool*) realloc( mr_skip_list,
		new_len * sizeof(bool) );
	
	memset( mr_list + mr_len, '\0',
		(new_len - mr_len) * sizeof(master_record) );
	memset( mr_skip_list + mr_len, '\0',
		(new_len - mr_len) * sizeof(bool) );
	
	mr_len = new_len;
}


void Metastock::add_mr_list_datfile(  int datnum, const char* datname )
{
	if( datnum > max_dat_num ) {
		max_dat_num = datnum;
	}
	if( mr_len <= datnum ) {
		/* increase by 128 instead of 1 to avoid some reallocs */
		resize_mr_list(datnum + 128);
	}
	strcpy( mr_list[datnum].file_name, datname );
}


int Metastock::build_mr_string( char *dst, const master_record *mr ) const
{
	char *cp = dst;
	int tmp = 0;
	
	if( prnt_data_mr_fields & M_SYM ) {
		tmp = strlen(mr->c_symbol);
		memcpy( cp, mr->c_symbol, tmp );
		cp += tmp;
		*cp++ = print_sep;
	}
	
	if( prnt_data_mr_fields & M_NAM ) {
		tmp = strlen(mr->c_long_name);
		memcpy( cp, mr->c_long_name, tmp );
		cp += tmp;
		*cp++ = print_sep;
	}
	
	*cp = '\0';
	return cp - dst;
}


bool Metastock::dumpData() const
{
	char buf[256];
	
	for( int i = 1; i<mr_len; i++ ) {
		if( mr_list[i].record_number != 0 && !mr_skip_list[i] ) {
			assert( mr_list[i].file_number == i );
			int len = mr_record_to_string( buf, &mr_list[i],
				prnt_data_mr_fields, print_sep );
			if( len > 0 ) {
				buf[len++] = print_sep;
				buf[len] = '\0';
			}
			if( !dumpData( i, mr_list[i].field_bitset, buf ) ) {
				return false;
			}
		}
	}
	return true;
}



bool Metastock::dumpData( unsigned short n, unsigned char fields, const char *pfx ) const
{
	fdat_buf->setName( mr_list[n].file_name );
	
	if( !fdat_buf->hasName() ) {
		setError( "no fdat found" );
		return false;
	}
	
	if( ! readFile( fdat_buf ) ) {
		return false;
	}
	
	FDat datfile( fdat_buf->constBuf(), fdat_buf->len(), fields );
// 	fprintf( stderr, "#%d: %d x %d bytes\n",
// 		n, datfile.countRecords(), count_bits(fields) * 4 );
	
	datfile.checkHeader();
	datfile.print( pfx );
	
	return true;
}


bool Metastock::hasXMaster() const
{
	return( x_buf->hasName() );
}
