/*** ms_file.cpp -- parsing metastock files
 *
 * Copyright (C) 2010-2017 Ruediger Meier
 * Author:  Ruediger Meier <sweet_f_a@gmx.de>
 * License: BSD 3-Clause, see LICENSE file
 *
 ***/

#include "ms_file.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>


#include "util.h"
#include "boobs.h"
#include "config.h"



/**
 * copy src to dst string, return strlen
 */
static inline int strcpy_len( char *dest, const char *src )
{
	int len = strlen( src );
	memcpy( dest, src, len );
	return len;
}

/**
 * copy a char to dst string, return strlen
 */
static inline int cpychar( char *dest, char c )
{
	*dest = c;
	return 1;
}


#define RETURN_IF_COLUMN( _field_ ) \
	do { \
		if( strcasecmp(column, STR_##_field_) == 0 ) { \
			return _field_; \
		} \
	} while(0)

unsigned int str_to_master_field( const char* column )
{
	RETURN_IF_COLUMN( M_SYM );
	RETURN_IF_COLUMN( M_NAM );
	RETURN_IF_COLUMN( M_PER );
	RETURN_IF_COLUMN( M_DT1 );
	RETURN_IF_COLUMN( M_DT2 );
	RETURN_IF_COLUMN( M_FNO );
	RETURN_IF_COLUMN( M_FIL );
	RETURN_IF_COLUMN( M_FLD );
	RETURN_IF_COLUMN( M_RNO );
	RETURN_IF_COLUMN( M_KND );
	return 0;
}

unsigned int str_to_data_field( const char* column )
{
	RETURN_IF_COLUMN( D_DAT );
	RETURN_IF_COLUMN( D_HIG );
	RETURN_IF_COLUMN( D_LOW );
	RETURN_IF_COLUMN( D_CLO );
	RETURN_IF_COLUMN( D_VOL );
	RETURN_IF_COLUMN( D_OPE );
	RETURN_IF_COLUMN( D_OPI );
	RETURN_IF_COLUMN( D_TIM );
	return 0;
}

#undef RETURN_IF_COLUMN


#define PRINT_FIELD( _func_, _field_, _var_ ) \
	if( prnt_master_fields & _field_) { \
		cp += _func_( cp, _var_ ); \
		*cp++ = sep; \
	}


int mr_record_to_string( char *dest, const struct master_record* mr,
	unsigned short prnt_master_fields, char sep )
{
	char * cp = dest;

	PRINT_FIELD( strcpy_len, M_SYM, mr->c_symbol );
	PRINT_FIELD( strcpy_len, M_NAM, mr->c_long_name );
	PRINT_FIELD( cpychar, M_PER, mr->barsize );
	PRINT_FIELD( itodatestr, M_DT1, mr->from_date );
	PRINT_FIELD( itodatestr, M_DT2, mr->to_date );
	PRINT_FIELD( itoa, M_FNO, mr->file_number );
	PRINT_FIELD( strcpy_len, M_FIL, mr->file_name );
	PRINT_FIELD( itoa, M_FLD, mr->field_bitset );
	PRINT_FIELD( itoa, M_RNO, mr->record_number );
	PRINT_FIELD( cpychar, M_KND, mr->kind );

	// remove last separator if exists
	if( cp != dest ) {
		*--cp = '\0';
	} else {
		*cp = '\0';
	}
	assert( (cp - dest) < MAX_SIZE_MR_STRING );
	return cp - dest;
}


int mr_header_to_string( char *dest,
	unsigned short prnt_master_fields, char sep )
{
	char * cp = dest;

	PRINT_FIELD( strcpy_len, M_SYM, STR_M_SYM );
	PRINT_FIELD( strcpy_len, M_NAM, STR_M_NAM );
	PRINT_FIELD( strcpy_len, M_PER, STR_M_PER );
	PRINT_FIELD( strcpy_len, M_DT1, STR_M_DT1 );
	PRINT_FIELD( strcpy_len, M_DT2, STR_M_DT2 );
	PRINT_FIELD( strcpy_len, M_FNO, STR_M_FNO );
	PRINT_FIELD( strcpy_len, M_FIL, STR_M_FIL );
	PRINT_FIELD( strcpy_len, M_FLD, STR_M_FLD );
	PRINT_FIELD( strcpy_len, M_RNO, STR_M_RNO );
	PRINT_FIELD( strcpy_len, M_KND, STR_M_KND );

	// remove last separator if exists
	if( cp != dest ) {
		*--cp = '\0';
	} else {
		*cp = '\0';
	}
	assert( (cp - dest) < MAX_SIZE_MR_STRING );
	return cp - dest;
}

#undef PRINT_FIELD


static inline char
readChar( const char *c, int offset )
{
	return (char)(c[offset]);
}

static inline unsigned char
readUnsignedChar( const char *c, int offset )
{
	return (unsigned char) c[offset];
}

static inline uint16_t
read_uint16( const char *c, int offset )
{
	union {
		uint16_t L;
		char c[2];
	} x;

	memcpy(x.c, c + offset, 2);
	return le16toh(x.L);
}

static inline uint32_t
read_uint32( const char *c, int offset )
{
	union {
		uint32_t L;
		char c[4];
	} x;

	memcpy(x.c, c + offset, 4);
	return le32toh(x.L);
}

static inline int32_t
read_int32( const char *c, int offset )
{
	return (int32_t)read_uint32(c, offset);
}

static inline float
readFloat_IEEE(const char *c, int offset)
{
	union {
		uint32_t L;
		float F;
	} x;

	x.L = read_uint32(c, offset);
	return x.F;
}

static inline float
readFloat(const char *c, int offset)
{
	union {
		uint32_t L;
		float F;
	} x;

	x.L = read_uint32(c, offset);

	/* regardless of endianness, that's how these floats look like
	  MBF:  eeeeeeeeSmmmmmmmmmmmmmmmmmmmmmmm
	  IEE:  Seeeeeeeemmmmmmmmmmmmmmmmmmmmmmm

	  "MBF is bias 128 and IEEE is bias 127. ALSO, MBF places the decimal
	  point before the assumed bit, while IEEE places the decimal point
	  after the assumed bit"
	  -> so ieee_exp = ms_exp - 2 */
	const uint32_t ms_e = 0xff000000 & x.L;
	if( ms_e == 0x00000000 ) {
		/* "any msbin w/ exponent of zero = zero" */
		return 0.0;
	}

	uint32_t ieee_s = (0x00800000 & x.L) << 8;

	/* Adding -2 to MS exponent. We set zero when ms_e is 1 because it would
	   overflow. The orignal MS code lets overflow it (type unsigned char!)
	   i.e. _probably_ they set exponent to 0xFF which is an IEEE NaN or INF
	   dependent on mantissa.
	   Note when ms_e is 2 the resulting IEEE mantissa is subnormal - don't
	   know if MS and IEEE mantissa are compatible in this case. */
	uint32_t ieee_e = ( (ms_e - 0x02000000) & 0xff000000) >> 1;
	uint32_t ieee_m = 0x007fffff & x.L;

	x.L = ieee_e | ieee_s | ieee_m;
	return x.F;
}


static inline int
floatToIntDate_YYY( float d )
{
	int i = (int)d;
	return i + 19000000;
}








MasterFile::MasterFile( const char *_buf, int _size ) :
	buf( _buf ),
	size( _size )
{
}


bool MasterFile::check() const
{
	checkHeader();
	checkRecords();
	return true;
}


bool MasterFile::checkHeader() const
{
	assert( size % record_length == 0 );
	assert( countRecords() == (size / record_length - 1) );

	printHeader();

	//  #0,  1b, unsigned char, count records (dat files)
	//           must be >0 (error #1002)
	//  #1,  1b, char, always '\0' (~ error #1002)
	//  #2,  1b, unsigned char, max record number (dat file number)
	//           must be >0 (error #1003)
	//  #3,  1b, char, always '\0' (~ error #1003)
	//  #4, 45b, char*, always '\0' ?
	// #49,  4b, int, serial number

	unsigned char cntRec = readUnsignedChar(buf, 0);
	assert( cntRec == countRecords() && cntRec > 0 );
	assert( buf[1] == '\0' );
	unsigned char maxRec = readUnsignedChar(buf, 2);
	assert( maxRec >= cntRec && maxRec > 0 );
	assert( buf[3] == '\0' );
	for( int i=4; i<49; i++ ) {
		assert( buf[i] == '\0' );
	}

	return true;
}


void MasterFile::printHeader() const
{
	fprintf( stdout, "MASTER:\t%d\t%d\t%X\n",
		readUnsignedChar(buf, 0), // count records (stored in master?)
		readUnsignedChar(buf, 2), // count records (existing dat files?)
		read_int32(buf, 49) // unknown - just print as hex
		);
}


bool MasterFile::checkRecords() const
{
	for( int i = 1; i <= countRecords(); i++ ) {
		bool ok = checkRecord( i );
		if( !ok ) {
			return false;
		}
	}
	return true;
}


bool MasterFile::checkRecord( unsigned char r ) const
{
	assert( r > 0 );
	const char *record = buf + (record_length * r);
	printRecord( record );

	//  #0,  1b, unsigned char, dat file number
	//  #1,  2b, short, file type, always 101 (error #1005)
	//  #3,  1b, unsigned char, record length
	//           must be 4 times record count (error #1006)
	//  #4,  1b, unsigned char, record count
	//           must be 4, 5, 6, 7 or 8 (error #1007)
	//  #5,  1b: char, always '\0' (error #1008)
	//  #6,  1b: char, always '\0' (error #1009)
	//  #7, 16b: char*, security name
	//           only alphanumeric characters (error #1010)
	// #23,  2b: short, always 0 (error #1011)
	// #25,  4b: float(ms basic), first date, valid (error #1012)
	// #29,  4b: float(ms basic), last date, valid (error #1013)
	// #33,  1b: char, periodicity, must be 'I', 'D', 'W', 'M' (error #1014)
	// #34,  2b: unsigned short, intraday time frame between 0 and 60 minutes
	//           (error #1015)
	// #36, 14b: char*, symbol, space padded,
	//           not always (or never?) zero terminated
	//           only alphanumeric characters (error #1016)
	// #50,  1b: char, always a space ' ' (error #1017)
	//           note, premium data sets '\0'
	// #51,  1b: char, chart flag, always ' ' or '*' (error #1018)
	//           note, premium data sets '\0'
	// #52,  1b: char, always '\0' (error #1019)

	assert( read_uint16( record, 1 ) == 101 );
	assert( record[3] == 4 * record[4] );
	assert( record[4] >= 5 && record[4] <= 8 );
	assert( record[5] == '\0' );
	assert( record[6] == '\0' );

	assert( read_uint16( record, 23 ) == 0 );
	int date1 = floatToIntDate_YYY( readFloat( record, 25 ) );
	int date2 = floatToIntDate_YYY( readFloat( record, 29 ) );
	assert( date1 <= date2 );
	assert( record[33] == 'D' || record[33] == 'I' );
	unsigned short intrTimeFrame = read_uint16( record, 34 );
	assert( intrTimeFrame == 0
		|| (record[33] == 'I' && intrTimeFrame > 0 && intrTimeFrame <= 60) );

	assert( record[50] == ' ' || record[50] == '\0' );
	assert( record[51] == ' ' || record[51] == '*' || record[51] == '\0' );
	assert( record[52] == '\0' );

	return true;
}


void MasterFile::printRecord( const char *record ) const
{
	fprintf( stdout, "F%4d.dat\t%d\t%d\t%d\t%d\t'%s'\t'%s'\n",
		readUnsignedChar( record, 0 ), // F#.dat
		readChar( record, 3 ), // dat record length in bytes
		readChar( record, 4 ), // dat fields count per record
		floatToIntDate_YYY( readFloat( record, 25 ) ),
		floatToIntDate_YYY( readFloat( record, 29 ) ),
		record + 36, // symbol
		record + 7 // name
		);
}


int MasterFile::countRecords() const
{
	if( size < record_length ) {
		return -1;
	}

	int cnt = readUnsignedChar( buf, 0 );

	if( (cnt + 1) * record_length > size ) {
		return -1;
	}

	return cnt;
}


int MasterFile::getRecord( master_record *mr, unsigned short rnum ) const
{
	const char *record = buf + (record_length * rnum);
	mr->record_number = rnum;
	mr->kind = 'M';
	mr->file_number = readUnsignedChar( record, 0 );
	mr->barsize = readChar( record, 33 );

	/* MASTER file has no field bitset available, create it from field count */
	unsigned char field_count = readUnsignedChar( record, 4);
	mr->field_bitset = 0;
	if( mr->barsize == 'I' && field_count > 0 ) {
		mr->field_bitset |= D_TIM;
		--field_count;
	}
	mr->field_bitset |= (unsigned char)0xff >> (8 - field_count);
	assert( count_bits(mr->field_bitset) == readChar( record, 4 ) );

	trim_end( mr->c_symbol, record + 36, 14);
	trim_end( mr->c_long_name, record + 7, 16);

	mr->from_date = floatToIntDate_YYY(readFloat(record, 25));
	mr->to_date = floatToIntDate_YYY(readFloat(record, 29));
	if( mr->from_date > mr->to_date ) {
		//HACK premium data have year like 128 (+1900) but should be 28 (+1900)
		mr->from_date -= 1000000;
	}
	return 0;
}


int MasterFile::fileNumber( int r ) const
{
	const char *record = buf + (record_length * r);
	int fileNumber = readUnsignedChar( record, 0);

	assert( fileNumber > 0 && fileNumber <= 255 );
	return fileNumber;
}


int MasterFile::dataLength( int r ) const
{
	const char *record = buf + (record_length * r);
	return readUnsignedChar( record, 3 );
}








EMasterFile::EMasterFile( const char *_buf, int _size ) :
	buf( _buf ),
	size( _size )
{
}


bool EMasterFile::check() const
{
	checkHeader();
	checkRecords();
	return true;
}


bool EMasterFile::checkHeader() const
{
	assert( size % record_length == 0 );
	assert( countRecords() == (size / record_length - 1) );

	printHeader();

	// note, first 53 bytes seems to be identical to MASTER
	//  #0,  1b, unsigned char, count records (dat files)
	//           must be >0 (error #1023)
	//  #1,  1b, char, always '\0' (~ error #1023)
	//  #2,  1b, unsigned char, max record number (dat file number)
	//           must be >0 (error #1024)
	//  #3,  1b, char, always '\0' (~ error #1024)
	//  #4, 45b, char*, always '\0' ?
	// #49,  4b, int, serial number
	// #53,139b, char*, seems to be vendor specific

	unsigned char cntRec = readUnsignedChar(buf, 0);
	assert( cntRec == countRecords() && cntRec > 0 );
	assert( buf[1] == '\0' );
	unsigned char maxRec = readUnsignedChar(buf, 2);
	assert( maxRec >= cntRec && maxRec > 0 );
	assert( buf[3] == '\0' );
	for( int i=4; i<49; i++ ) {
		assert( buf[i] == '\0' );
	}

	return true;
}


void EMasterFile::printHeader() const
{
	fprintf( stdout, "EMASTER:\t%d\t%d\t%X\t'%s'\n",
		readUnsignedChar(buf, 0), // count records (stored in master?)
		readUnsignedChar(buf, 2), // count records (existing dat files?)
		read_int32(buf, 49), // unknown - just print as hex
		buf + 53 // unknown, equis sends a string
		);
}


bool EMasterFile::checkRecords() const
{
	for( int i = 1; i <= countRecords(); i++ ) {
		bool ok = checkRecord( i );
		if( !ok ) {
			return false;
		}
	}
	return true;
}


bool EMasterFile::checkRecord( unsigned char r ) const
{
	assert( r > 0 );
	const char *record = buf + (record_length * r);
	printRecord( record );

	//   #0,  2b: unsigned short, version number, valid (error #1026)
	//   #2,  1b: unsigned char, dat file number, valid (error #1027)
	//   #3,  1b: char, security type always '\0' (error #1028)
	//   #4,  1b: char, security type always '\0' (~error #1028)
	//   #5,  1b: char, security type always '\0' (~error #1028)
	//   #6,  1b: unsigned char, data field count
	//            must be 4, 5, 6, 7 or 8 (error #1029)
	//   #7,  1b: unsigned char, data field bitset
	//   #8,  1b: char, data fields always '\0' (error #1030)
	//   #9,  1b: char, chart flag, always ' ' or '*' (error #1031)
	//  #10,  1b: char, vendor code always '\0' (error #1032)
	//  #11, 14b: char*, symbol, always zero terminated?
	//            only alphanumeric characters (error #1033)
	//  #25,  6b: char*, always zero? (error #1034)
	//  #32, 16b: char*, short name, always zero terminated? (error #1034)
	//  #48, 12b: char*, always zero? (error #1036 and error #1037)
	//  #60,  1b: char, periodicity, must be 'I', 'D', 'W', 'M' (error #1038)
	//  #61,  1b: char, always zero (error #1039)
	//  #62,  2b: short, intraday time frame between 0 and 60 minutes (error
	//            #1040), note we have only times when intraday time == 'I'
	//  #64,  4b: float(ieee), first date, valid (error #1041)
	//  #68,  4b: float(ieee), first time, valid (error #1042)
	//  #72,  4b: float(ieee), last date, valid (error #1043)
	//  #76,  4b: float(ieee), last time, valid (error #1044)
	//  #80,  4b: float(ieee), start trade time, not always set,
	//            valid (error #1045)
	//  #84,  4b: float(ieee), end trade time, not always set,
	//            valid (error #1046)
	//  #88, 38b: char*, assume always zero but should something about composite
	//            ticker stuff, see error #1047 - #1054
	// #126,  4b: int, first date again as integer, equal to #64 or invalid
	//            19000101
	// #130,  9b: char* assume always zero
	// #139, 52b: char*, long name, when set it should start like short name
	// #191,  1b: char, last byte zero

	unsigned short version = read_uint16( record, 0 );
	assert( version == 0 || version == 0x3636 );
	assert( record[3]== '\0' && record[4]== '\0' && record[5]== '\0' );
	assert( record[6] >=5 && record[6] <= 8);
	assert( record[6] == count_bits(readUnsignedChar(record, 7)) );
	assert( record[8] == '\0' );
	assert( record[9] == ' ' || record[9] == '*' );
	assert( record[10] == '\0' );
	// if byte 25 is always zero then strlen(symbol) <= 14
	for( int i = 25; i<32; i++ ) {
		assert( record[i] == '\0' );
	}
	// if byte 48 is always zero then strlen(name) <= 16
	for( int i = 48; i<60; i++ ) {
		assert( record[i] == '\0' );
	}
	assert( record[60] == 'D' || record[60] == 'I' );
	assert( record[61] == '\0' );
	unsigned short intrTimeFrame = read_uint16( record, 62 );
	assert( intrTimeFrame == 0
		|| (record[60] == 'I' && intrTimeFrame > 0 && intrTimeFrame <= 60) );

	int date1 = floatToIntDate_YYY( readFloat_IEEE( record, 64 ) );
	int time1 = readFloat_IEEE( record, 68 );
	int date2 = floatToIntDate_YYY( readFloat_IEEE( record, 72 ) );
	int time2 = readFloat_IEEE( record, 76 );
	int timeA = readFloat_IEEE( record, 80 );
	int timeB = readFloat_IEEE( record, 84 );
	for( int i = 88; i<126; i++ ) {
		assert( record[i] == '\0' );
	}
	int dateL = read_int32( record, 126 );

	if( date1 > date2 ) {
		//HACK premium data have year like 128 (+1900) but should be 28 (+1900)
		date1 -= 1000000;
	}
	if( dateL > date2 ) {
		//HACK premium data have year like 128 (+1900) but should be 28 (+1900)
		dateL -= 1000000;
	}
	assert( date1 >0 && date2>0 );
	assert((record[60] != 'I'
	        && time1 == 0 && time2 == 0 && timeA == 0 && timeB == 0)
	    || (record[60] == 'I'
	        && time1 > 0 && time2 > 0 && timeA >= 0 && timeB >= 0));
	assert( (long) date1*1000000+time1 <= (long) date2*1000000+time2 );

	assert( date1 == dateL || dateL == 19000101 );

	for( int i = 130; i<=138; i++ ) {
		assert( record[i] == '\0' );
	}

	if( strlen(record + 139) > 0 ) {
		assert( strncmp(record + 32, record + 139, strlen(record + 32)) == 0 );
	}

	assert( record[191] == '\0' );

	return true;
}


void EMasterFile::printRecord( const char *record ) const
{
// 	fprintf( stdout, "F%d.dat\t%d\t%d\t%d\t%d\t'%s'\t'%s'\n",
	fprintf( stdout, "F%4d.dat\t%d\t%d\t%c\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%X\t%X\t'%s'\t'%s'\t'%s'\n",
		readUnsignedChar( record, 2),  // F#.dat
		readChar( record, 7 ), // fields bit set
		readChar( record, 6 ), // dat fields count per record
		readChar( record, 60 ), // time frame 'D'
		// never saw these floats
		floatToIntDate_YYY(readFloat_IEEE( record, 64 )), // first date YYY but seems to be wrong
		(int)readFloat_IEEE( record, 68 ),
		floatToIntDate_YYY(readFloat_IEEE( record, 72 )), // last date YYY
		(int)readFloat_IEEE( record, 76 ),
		(int)readFloat_IEEE( record, 80 ),
		(int)readFloat_IEEE( record, 84 ),
		read_int32( record, 126 ), // first date YYYY
// 		readFloat( record, 131 ),
// 		readFloat( record, 135 ),
		readUnsignedChar( record, 0 ), // unknown, just print hex
		readUnsignedChar( record, 1 ), // unknown, just print hex
		record + 11, // symbol
		record + 32, // name
		record + 139 ); // name when "too long"
}


int EMasterFile::countRecords() const
{
	if( size < record_length ) {
		return -1;
	}

	int cnt = readUnsignedChar( buf, 0 );

	if( (cnt + 1) * record_length > size ) {
		return -1;
	}

	return cnt;
}


int EMasterFile::getLongName( master_record *mr, unsigned short rnum ) const
{
	const char *record = buf + (record_length * rnum);
	assert( mr->record_number == rnum );

	char lname[ MAX_LEN_MR_LNAME + 1 ];
	int len_lname = trim_end( lname, record + 139, MAX_LEN_MR_LNAME );
	if( len_lname > 0 ) {
		assert( strncmp(mr->c_long_name, lname, strlen(mr->c_long_name)) == 0 );
		strcpy( mr->c_long_name, lname );
		mr->kind = 'E';
	}

	return 0;
}


int EMasterFile::getRecord( master_record *mr, unsigned short rnum ) const
{
	const char *record = buf + (record_length * rnum);
	mr->record_number = rnum;
	mr->kind = 'E';
	mr->file_number = readUnsignedChar( record, 2 );
	mr->field_bitset= readUnsignedChar( record, 7 );
	assert( count_bits(mr->field_bitset) == readUnsignedChar( record, 6 ) );
	mr->barsize= readChar( record, 60 );
	trim_end( mr->c_symbol, record + 11, MAX_LEN_MR_SYMBOL );

	if( trim_end( mr->c_long_name, record + 139, MAX_LEN_MR_LNAME ) == 0 ) {
		// long name is empty - using short name
		trim_end( mr->c_long_name, record + 32, 16 );
	}

	mr->from_date = floatToIntDate_YYY(readFloat_IEEE(record, 64));
	mr->to_date = floatToIntDate_YYY(readFloat_IEEE(record, 72));
	if( mr->from_date > mr->to_date ) {
		//HACK premium data have year like 128 (+1900) but should be 28 (+1900)
		mr->from_date -= 1000000;
	}
	return 0;
}


int EMasterFile::fileNumber( int r ) const
{
	const char *record = buf + (record_length * r);
	int fileNumber = readUnsignedChar( record, 2);
	assert( fileNumber > 0 && fileNumber <= 255 );
	return fileNumber;

}


int EMasterFile::dataLength( int r ) const
{
	const char *record = buf + (record_length * r);
	return readUnsignedChar( record, 6 ) * 4;
}








XMasterFile::XMasterFile( const char *_buf, int _size ) :
	buf( _buf ),
	size( _size )
{
}


bool XMasterFile::check() const
{
	checkHeader();
	checkRecords();
	return true;
}


bool XMasterFile::checkHeader() const
{
	assert( size % record_length == 0 );
	assert( countRecords() == (size / record_length - 1) );

	printHeader();

	assert( readChar(buf, 0) == '\x5d' );
	assert( readChar(buf, 1) == '\xFE' );
	assert( readChar(buf, 2) == 'X' );
	assert( readChar(buf, 3) == 'M' );
	// char 4 - 9 unknown
	assert( read_uint16(buf, 10) ==  countRecords() );
	assert( readChar( buf, 12 ) == '\x00' );
	assert( readChar( buf, 13 ) == '\x00' );
	assert( read_uint16(buf, 14) ==  countRecords() );
	assert( readChar( buf, 16 ) == '\x00' );
	assert( readChar( buf, 17 ) == '\x00' );

	// last used + 1 !?
	assert( read_uint16(buf, 18) > countRecords() );
	assert( readChar( buf, 20 ) == '\x00' );
	assert( readChar( buf, 21 ) == '\x00' );

	// char 22 -191 unknown

	return true;
}


void XMasterFile::printHeader() const
{
	fprintf( stdout, "XMASTER:\t%d\t%d\t%d\t'%s'\n",
		read_uint16(buf, 10), // count records (stored in master?)
		read_uint16(buf, 14), // count records (the same?)
		read_uint16(buf, 18), // last used record
		buf + 22 //  // unknown, equis sends a string
		);
}


bool XMasterFile::checkRecords() const
{
	for( int i = 1; i <= countRecords(); i++ ) {
		bool ok = checkRecord( i );
		if( !ok ) {
			return false;
		}
	}
	return true;
}


bool XMasterFile::checkRecord( int r ) const
{
	assert( r > 0 );
	const char *record = buf + (record_length * r);
	printRecord( record );

	//   #0,  1b: char, record type always '\x01' (error #1107)
	//   #1, 14b: char*, symbol, always zero terminated?
	//            only alphanumeric characters (error #1108)
	//  #15,  1b: char, always zero?
	//  #16, 45b: char*, name, always zero terminated?
	//            only alphanumeric characters (error #1109)
	//  #61,  1b: char, always zero (error #1110)
	//  #62,  1b: char, periodicity, must be 'I', 'D', 'W', 'M' (error #1111)
	//  #63,  2b: short, intraday time frame between 0 and 60 minutes
	//            (error #1112)
	//  #65,  2b: unsigned short, dat file number, <=2000 (error #1113)
	//  #67,  3b: char[3], ??, always '0' ?
	//  #70,  1b: unsigned char, data field bitset
	//  #71,  9b: char[9], ??, always '0' ?
	//  #80,  4b: ??
	//            equis/premium  looks like int 19800317, 19990430 or 0
	//  #84,  4b: ??
	//            equis/premium first 3 bytes equal #80
	//  #87, 17b: char[17], ??, always '0' ?
	// #104,  4b: int, collection date ??, valid (error #1121)
	// #108   4b: int, first date, valid (error #1122)
	// #112,  4b: ??, first time, valid (error #1123)
	// #116   4b: int, last date, valid (error #1124)
	// #120,  4b: ??, last time, valid (error #1125)
	// #124, 26b: char[29], ??, always '0' ?
	//
	// note, the unknown fields from #71 on may contain "composite stuff",
	// whatever it is

	assert( readChar( record, 0) == '\x01' );
	assert( readChar( record, 15) == '\x00' );
	assert( readChar( record, 61) == '\x00' );

	char per = record[62];
	assert( per == 'D' || per == 'I' );
	unsigned short intrTimeFrame = read_uint16( record, 63 );
	assert( intrTimeFrame == 0
		|| (per == 'I' && intrTimeFrame > 0 && intrTimeFrame <= 60) );

	for( int i = 67; i<70; i++ ) {
		assert( readChar( record, i ) == '\0' );
	}
	for( int i = 71; i<80; i++ ) {
		assert( readChar( record, i ) == '\0' );
	}
	for( int i = 87; i<104; i++ ) {
		assert( readChar( record, i ) == '\0' );
	}
	for( int i = 112; i<116; i++ ) {
		assert( readChar( record, i ) == '\0' );
	}
	for( int i = 124; i<150; i++ ) {
		assert( readChar( record, i ) == '\0' );
	}

	return true;
}


void XMasterFile::printRecord( const char *record ) const
{
	fprintf( stdout, "F%4d.mwd\t%c\t%d\t%d\t%d\t%d\t%d\t'%s'\t'%s'\n",
		read_uint16( record, 65 ), // F#.mwd
		readChar( record, 62 ), // time frame 'D'
		readUnsignedChar( record, 70 ), // fields bitset
		read_int32( record, 80 ), // some date ?
// 		readInt( record, 84 ), // stupid date? forst 3 bytes equal
		read_int32( record, 104 ), // some date ?
		read_int32( record, 108 ), // looks like first date ?
		read_int32( record, 116 ), // looks like last date ?
		record + 1, // symbol
		record + 16 // name
		);
}


int XMasterFile::countRecords() const
{
	if( size < record_length ) {
		return -1;
	}

	int cnt = read_uint16( buf, 10 );

	if( (cnt + 1) * record_length > size ) {
		return -1;
	}

	return cnt;
}


int XMasterFile::getRecord( master_record *mr, unsigned short rnum ) const
{
	const char *record = buf + (record_length * rnum);
	mr->record_number = rnum;
	mr->kind = 'X';
	mr->file_number = read_uint16( record, 65 );
	mr->field_bitset = readUnsignedChar( record, 70 );
	mr->barsize = readChar( record, 62 );
	trim_end( mr->c_symbol, record + 1, MAX_LEN_MR_SYMBOL );
	trim_end( mr->c_long_name, record + 16, MAX_LEN_MR_LNAME );
	mr->from_date = read_int32( record, 108 );
	mr->to_date = read_int32( record, 116 );
	return 0;
}


int XMasterFile::fileNumber( int r ) const
{
	const char *record = buf + (record_length * r);
	int fileNumber = read_uint16( record, 65 );

	assert( fileNumber > 255 );
	return fileNumber;
}


int XMasterFile::dataLength( int r ) const
{
	const char *record = buf + (record_length * r);
	unsigned char v = readUnsignedChar( record, 70 );
	unsigned char c; // c accumulates the total bits set in v
	for (c = 0; v; v >>= 1) {
		c += v & 1;
	}
	return 4 * c;
}








FDat::FDat( const char *_buf, int _size, unsigned char fields ) :
	field_bitset( fields ),
	record_length( count_bits(fields) * 4 ),
	buf( _buf ),
	size( _size )
{
}


void* FDat::out = stdout;
char FDat::print_sep = '\t';
unsigned int FDat::print_bitset = 0xff;
int FDat::print_date_from = 0;

ftoa_func FDat::prc_ftoa = ftoa;
ftoa_func FDat::vol_ftoa = ftoa_prec_f0;
ftoa_func FDat::opi_ftoa = ftoa_prec_f0;


void FDat::set_outfile( void *file )
{
	out = file;
}


void FDat::initPrinter( char sep, unsigned int bitset )
{
	print_sep = sep;
	print_bitset = bitset;
}


void FDat::setPrintDateFrom( int date )
{
	print_date_from = date;
}

void FDat::setForceFloat( ms_data_field fld )
{
	switch(fld) {
	case D_OPI:
		opi_ftoa = prc_ftoa;
		break;
	case D_VOL:
		vol_ftoa = prc_ftoa;
		break;
	default:
		/* maybe extend this switch if ever needed */
		assert(false);
	}
}


bool FDat::checkHeader() const
{
	assert( size % record_length == 0 );
	assert( countRecords() == (size / record_length) - 1 );

	return true;
}


int FDat::print( const char* header ) const
{
	const char *record = buf + record_length;
	const char *end = buf + ((countRecords() + 1) * record_length);
	assert( end - buf <= size );
	char buf[512];
	char *buf_p = buf;

	int h_size = strlen( header );
	memcpy( buf, header, h_size );
	buf_p += h_size;

	int err = 0;
	while( record < end ) {
		int len = record_to_string( record, buf_p );
		record += record_length;
		if( len < 0) {
			continue;
		}
		buf_p[len++] = '\n';
		buf_p[len] = '\0';

		/* We don't check errors every loop to be fast. Main reason to check
		   errors at all is because there is no SIGPIPE on WIN32. */
		err = fputs( buf, (FILE*)out );
	}

	fflush( (FILE*)out );
	return err;
}


void FDat::print_header( const char* symbol_header )
{
	char buf[512];
	char *buf_p = buf;

	int h_size = strlen( symbol_header );
	memcpy( buf, symbol_header, h_size );
	buf_p += h_size;

	int len = header_to_string( buf_p );
	buf_p[len++] = '\n';
	buf_p[len] = '\0';

	fputs( buf, (FILE*)out );
}


// to be printed when field does not exist
#define DEFAULT_FLOAT -0.0

#define READ_FIELD( _dst_, _field_) \
	if( field_bitset & _field_ ) { \
		 _dst_ = readFloat(record, offset); \
		offset += 4; \
	}

#define PRINT_FIELD( _func_, _field_, _var_ ) \
	if( print_bitset & _field_) { \
		s += _func_( s, _var_ ); \
		*s++ = print_sep; \
	}


int FDat::record_to_string( const char *record, char *s ) const
{
	int offset = 0;
	char *begin = s;

	int date, time;
	float open, high , low, close, volume, openint;
	date = time = 0;
	open = high = low = close = volume = openint = DEFAULT_FLOAT;

	if( field_bitset & D_DAT ) {
		date = floatToIntDate_YYY(readFloat(record, offset));
		if( date < print_date_from ) {
			return -1;
		}
		offset += 4;
	}

	READ_FIELD( time, D_TIM );
	READ_FIELD( open, D_OPE );
	READ_FIELD( high, D_HIG );
	READ_FIELD( low, D_LOW );
	READ_FIELD( close, D_CLO );
	READ_FIELD( volume, D_VOL );
	READ_FIELD( openint, D_OPI );

	PRINT_FIELD( itodatestr, D_DAT, date );
	PRINT_FIELD( itotimestr, D_TIM, time );
	PRINT_FIELD( prc_ftoa, D_OPE, open );
	PRINT_FIELD( prc_ftoa, D_HIG, high );
	PRINT_FIELD( prc_ftoa, D_LOW, low );
	PRINT_FIELD( prc_ftoa, D_CLO, close );
	PRINT_FIELD( vol_ftoa, D_VOL, volume );
	PRINT_FIELD( opi_ftoa, D_OPI, openint );

	if( s != begin ) {
		*(--s) = '\0';
	} else {
		*s = '\0';
	}

	return s - begin;
}

#undef DEFAULT_FLOAT
#undef READ_FIELD

int FDat::header_to_string( char *s )
{
	char *begin = s;

	PRINT_FIELD( strcpy_len, D_DAT, STR_D_DAT );
	PRINT_FIELD( strcpy_len, D_TIM, STR_D_TIM );
	PRINT_FIELD( strcpy_len, D_OPE, STR_D_OPE );
	PRINT_FIELD( strcpy_len, D_HIG, STR_D_HIG );
	PRINT_FIELD( strcpy_len, D_LOW, STR_D_LOW );
	PRINT_FIELD( strcpy_len, D_CLO, STR_D_CLO );
	PRINT_FIELD( strcpy_len, D_VOL, STR_D_VOL );
	PRINT_FIELD( strcpy_len, D_OPI, STR_D_OPI );

	if( s != begin ) {
		*(--s) = '\0';
	} else {
		*s = '\0';
	}

	return s - begin;
}


int FDat::countRecords() const
{
	if( size < record_length ) {
		return -1;
	}

	int cnt = read_uint16( buf, 2 ) - 1;

	if( (cnt + 1) * record_length > size ) {
		return -1;
	}

	return cnt;
}
