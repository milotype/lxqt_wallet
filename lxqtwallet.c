
/***********************************************************************************
 *   Copyright 2013 by mhogomchungu <mhogomchungu@gmail.com>		           *
 *                                                                                 *
 *                                                                                 *
 *   This library is free software; you can redistribute it and/or                 *
 *   modify it under the terms of the GNU Lesser General Public                    *
 *   License as published by the Free Software Foundation; either                  *
 *   version 2.1 of the License, or (at your option) any later version.            *
 *                                                                                 *
 *   This library is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of                *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU             *
 *   Lesser General Public License for more details.                               *
 *                                                                                 *
 *   You should have received a copy of the GNU Lesser General Public              *
 *   License along with this library.  If not, see <http://www.gnu.org/licenses/>. *
 ***********************************************************************************/

#include "lxqtwallet.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

#define IV_SIZE 16 
#define MAGIC_STRING "lxqtwallet"
#define MAGIC_STRING_SIZE 16
#define PASSWORD_SIZE 16
#define BLOCK_SIZE 16 

struct lxqt_wallet_struct{
	char * application_name ;
	char * wallet_name ;
	char key[ PASSWORD_SIZE ] ;
	struct lxqt_key_value * wallet_data ;
	size_t wallet_data_size ;
};

static char * _wallet_full_path( char * path_buffer,size_t path_buffer_size,const char * wallet_name,const char * application_name ) ;

static void _create_application_wallet_path( const char * application_name ) ;

static void _create_key( char output_key[ PASSWORD_SIZE ],const char * input_key,size_t input_key_length ) ;

static int _get_iv_from_wallet_header( char iv[ IV_SIZE ],const char * wallet_name,const char * application_name ) ;

static int _get_magic_string_from_header( char magic_string[ MAGIC_STRING_SIZE ],const char * wallet_path ) ;

static void _get_iv( char iv[ IV_SIZE ] ) ;

int lxqt_wallet_wallet_size( lxqt_wallet_t wallet ) 
{
	return wallet->wallet_data_size ;
}

const struct lxqt_key_value * lxqt_wallet_read_all_key_values( lxqt_wallet_t wallet ) 
{
	return wallet->wallet_data ;
}

int lxqt_wallet_create( const char * password,size_t password_length,const char * wallet_name,const char * application_name )
{
	int fd ;
	char path[ PATH_MAX ] ;
	char iv[ IV_SIZE ] ;
	char magic_string[ MAGIC_STRING_SIZE ] ;
	char key[ PASSWORD_SIZE ] ;
	
	gcry_error_t r ;
	
	gcry_cipher_hd_t gcry_cipher_handle ;
		
	if( lxqt_wallet_exists( wallet_name,application_name ) == 0 ){
		fprintf( stderr,"ERROR: wallet exists\n" ) ;
		return 1 ;
	}else{
		;
	}
	
	gcry_check_version( NULL ) ;
	gcry_control( GCRYCTL_INITIALIZATION_FINISHED,0 ) ;
	
	r = gcry_cipher_open( &gcry_cipher_handle,GCRY_CIPHER_AES128,GCRY_CIPHER_MODE_CBC,0 ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_open: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		return 1 ;
	}
	
	_create_key( key,password,password_length ) ;
	r = gcry_cipher_setkey( gcry_cipher_handle,key,PASSWORD_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setkey: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return 1 ;
	}
	
	_get_iv( iv ) ;
	
	r = gcry_cipher_setiv( gcry_cipher_handle,iv,IV_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setiv: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return 1 ;
	}
	
	_create_application_wallet_path( application_name ) ;
	
	_wallet_full_path( path,PATH_MAX,wallet_name,application_name ) ;
	
	fd = open( path,O_WRONLY|O_CREAT,0600 ) ;
	
	if( fd == -1 ){
		gcry_cipher_close( gcry_cipher_handle ) ;
		return 1 ;
	}
	
	write( fd,iv,IV_SIZE ) ;
	
	strncpy( magic_string,MAGIC_STRING,MAGIC_STRING_SIZE ) ;
	
	r = gcry_cipher_encrypt( gcry_cipher_handle,magic_string,MAGIC_STRING_SIZE,NULL,0 ) ;
	gcry_cipher_close( gcry_cipher_handle ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		close( fd ) ;
		fprintf( stderr,"gcry_cipher_encrypt: %s/%s\n",gcry_strsource (r),gcry_strerror (r));
		return 1 ;
	}else{
		write( fd,magic_string,MAGIC_STRING_SIZE ) ;
		close( fd ) ;
		return 0 ;
	}
}

static int _free_open( int st,struct lxqt_wallet_struct * w )
{
	if( w ){
		if( w->wallet_name ){
			free( w->wallet_name ) ;
		}
		if( w->application_name ){
			free( w->application_name ) ;
		}
		free( w ) ;
	}
	return st ;
}

/*
 * output:
 * 0 : success
 * 1 : malloc failed
 * 2 : failed to read iv
 * 3 : wrong password
 * 4 : grypt error
 */
int lxqt_wallet_open( lxqt_wallet_t * wallet,const char * password,size_t password_length,
		      const char * wallet_name,const char * application_name )
{
	struct stat st ;
	size_t len ;
	
	gcry_error_t r ;
	gcry_cipher_hd_t gcry_cipher_handle ;
	
	char iv[ IV_SIZE ] ;
	char path[ PATH_MAX ] ;
	char magic_string[ MAGIC_STRING_SIZE ] ;
	char magic_string_1[ MAGIC_STRING_SIZE ] ;
	char buffer[ BLOCK_SIZE ] ;
	
	unsigned char * e ;
	
	int i ;
	int j ;
	int k ;
	int fd;
	
	struct lxqt_wallet_struct * w ;
	
	if( wallet_name == NULL || application_name == NULL || wallet == NULL ){
		return 1 ;
	}
	
	w = malloc( sizeof( struct lxqt_wallet_struct ) ) ;
	
	if( w == NULL ){
		return 1 ;
	}
	
	memset( w,'\0',sizeof( struct lxqt_wallet_struct ) ) ;
	
	len = strlen( wallet_name ) ;
	w->wallet_name = malloc( sizeof( char ) * ( len + 1 ) ) ;
	if( w->wallet_name == NULL ){
		return _free_open( 1,w ) ;
	}else{
		memcpy( w->wallet_name,wallet_name,len + 1 ) ;
	}
	
	len = strlen( application_name ) ;
	w->application_name = malloc( sizeof( char ) * ( len + 1 ) ) ;
	if( w->application_name == NULL ){
		return _free_open( 1,w ) ;
	}else{
		memcpy( w->application_name,application_name,len + 1 ) ;
	}
	
	gcry_check_version( NULL ) ;
	gcry_control( GCRYCTL_INITIALIZATION_FINISHED,0 ) ;
	
	r = gcry_cipher_open( &gcry_cipher_handle,GCRY_CIPHER_AES128,GCRY_CIPHER_MODE_CBC,0 ) ;

	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_open: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		return _free_open( 4,w ) ;
	}
	
	_create_key( w->key,password,password_length ) ;
	
	r = gcry_cipher_setkey( gcry_cipher_handle,w->key,PASSWORD_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setkey: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _free_open( 4,w ) ;
	}
	
	if( _get_iv_from_wallet_header( iv,wallet_name,application_name ) ){
		;
	}else{
		fprintf( stderr,"failed to read iv from wallet header\n" ) ;
		return _free_open( 2,w ) ;
	}
		
	r = gcry_cipher_setiv( gcry_cipher_handle,iv,IV_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setiv: %s/%s\n",gcry_strsource (r),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _free_open( 4,w ) ;
	}
	
	_wallet_full_path( path,PATH_MAX,wallet_name,application_name ) ;
	
	if( _get_magic_string_from_header( magic_string,path ) ){
		;
	}else{
		fprintf( stderr,"failed to read magic string from wallet header\n" ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _free_open( 2,w ) ;
	}
	
	r =  gcry_cipher_decrypt( gcry_cipher_handle,magic_string,MAGIC_STRING_SIZE,NULL,0 ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_decrypt: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _free_open( 4,w ) ;
	}
	
	strncpy( magic_string_1,MAGIC_STRING,MAGIC_STRING_SIZE ) ;
	
	if( memcmp( magic_string,magic_string_1,MAGIC_STRING_SIZE ) == 0 ){
		/*
		 * correct password
		 */
		stat( path,&st ) ;
		if( st.st_size <= IV_SIZE + MAGIC_STRING_SIZE ){
			/*
			 * wallet is empty
			 */
			w->wallet_data_size = 0 ;
			*wallet = w ;
			gcry_cipher_close( gcry_cipher_handle ) ;
			return 0 ;
		}else{
			fd = open( path,O_RDONLY ) ;
			if( fd != -1 ){
				w->wallet_data_size = ( st.st_size - ( IV_SIZE + MAGIC_STRING_SIZE ) ) / sizeof( struct lxqt_key_value ) ;

				j = st.st_size - ( IV_SIZE + MAGIC_STRING_SIZE ) ;
				j = j / BLOCK_SIZE ;
				
				k = 0 ;
				e = NULL ;
				
				lseek( fd, IV_SIZE + MAGIC_STRING_SIZE,SEEK_SET ) ;
				
				for( i = 0 ; i < j ; i++ ){
					e = realloc( e,BLOCK_SIZE * ( i + 1 ) ) ;
					read( fd,buffer,BLOCK_SIZE ) ;
					gcry_cipher_decrypt( gcry_cipher_handle,e + k,BLOCK_SIZE,buffer,BLOCK_SIZE ) ;
					k = k + BLOCK_SIZE ;
				}
			
				w->wallet_data = ( struct lxqt_key_value * ) e ;
				close( fd ) ;
				*wallet = w ;
				gcry_cipher_close( gcry_cipher_handle ) ;
				return 0 ;
			}else{
				gcry_cipher_close( gcry_cipher_handle ) ;
				return _free_open( 2,w ) ;
			}
		}
	}else{
		gcry_cipher_close( gcry_cipher_handle ) ;
		fprintf( stderr,"wrong password\n" ) ;
		return _free_open( 3,w ) ;
	}
}

char * lxqt_wallet_read_key_value( lxqt_wallet_t wallet,const char * key )
{
	struct lxqt_key_value * i = wallet->wallet_data ;
	struct lxqt_key_value * k ;
	struct lxqt_key_value * j = i + wallet->wallet_data_size ;
	
	size_t len ;
	char * value ;
	
	if( key == NULL ){
		return NULL ;
	}
	
	len = strlen( key ) ;
	
	while( i != j ){
		k = i ;
		i++ ;
		if( strncmp( key,k->key,len ) == 0 ){
			value = malloc( k->value_size ) ;
			memcpy( value,k->value,k->value_size ) ;
			return value ;
		}
	}
	
	return NULL ;
}

int lxqt_wallet_add_key( lxqt_wallet_t wallet,const char * key,const char * value,size_t key_value_length ) 
{
	struct lxqt_key_value * key_value ;
	struct lxqt_key_value * key_value_1 ;
	
	if( key == NULL || value == NULL ){
		return 1 ;
	}
	
	if( key_value_length == 0 || key_value_length >= VALUE_SIZE ){
		return 1 ;
	}
	
	if( wallet->wallet_data_size == 0 ){
		key_value = malloc( sizeof( struct lxqt_key_value ) ) ;
		if( key_value != NULL ){
			strncpy( ( char *)&key_value->key,key,KEY_SIZE ) ;
			memset( &key_value->value,'\0',VALUE_SIZE ) ;
			memcpy( &key_value->value,value,key_value_length ) ;
			key_value->value_size = key_value_length + 1 ;
			wallet->wallet_data = key_value ;
			wallet->wallet_data_size++ ;
			return 0 ;
		}else{
			return 1 ;
		}
	}else{
		key_value = realloc( wallet->wallet_data,sizeof( struct lxqt_key_value ) * ( wallet->wallet_data_size + 1 ) ) ;
		if( key_value != NULL ){
			key_value_1 = key_value + wallet->wallet_data_size ;
			strncpy( ( char *)&key_value_1->key,key,KEY_SIZE ) ;
			memset( &key_value_1->value,'\0',VALUE_SIZE ) ;
			memcpy( &key_value_1->value,key_value,key_value_length ) ;
			key_value_1->value_size = key_value_length + 1 ;
			wallet->wallet_data = key_value ;
			wallet->wallet_data_size++ ;
			return 0 ;
		}else{
			return 1 ;
		}
	}
}

int lxqt_wallet_delete_key( lxqt_wallet_t wallet,const char * key )
{
	size_t size = wallet->wallet_data_size ;
	size_t k = 0 ;
	struct lxqt_key_value * start = wallet->wallet_data ;
	struct lxqt_key_value * end = start + size ;
	struct lxqt_key_value * it = start ;
	
	if( key == NULL ){
		return 1 ;
	}
	
	for( it = start ; it != end ; it++ ){
		k++ ;
		if( strcmp( key,it->key ) == 0 ){
			if( size == 1 ){
				wallet->wallet_data_size = 0 ;
				free( wallet->wallet_data ) ;
				wallet->wallet_data = NULL ;
			}else if( k == size ){
				wallet->wallet_data_size-- ;
				wallet->wallet_data = realloc( wallet->wallet_data,wallet->wallet_data_size * sizeof( struct lxqt_key_value ) ) ;
			}else{
				memmove( it,it + 1,( size - 1 - k ) *  wallet->wallet_data_size * sizeof( struct lxqt_key_value ) ) ;
				wallet->wallet_data_size-- ;					
				wallet->wallet_data = realloc( wallet->wallet_data,wallet->wallet_data_size * sizeof( struct lxqt_key_value ) ) ;
			}
			return 0 ;
		}
	}
		
	return 1 ;
}

int lxqt_wallet_delete_wallet( const char * wallet_name,const char * application_name ) 
{
	char path[ PATH_MAX ] ;
	_wallet_full_path( path,PATH_MAX,wallet_name,application_name ) ;
	unlink( path ) ;
	return 0 ;
}

static int _close_exit( lxqt_wallet_t wallet )
{
	free( wallet->wallet_data ) ;
	free( wallet->wallet_name ) ;
	free( wallet->application_name ) ;
	free( wallet ) ;
	return 0 ;
}

int lxqt_wallet_close( lxqt_wallet_t wallet ) 
{
	gcry_cipher_hd_t gcry_cipher_handle ;
	int fd ;
	char iv[ IV_SIZE ] ;
	char path[ PATH_MAX ] ;
	char path_1[ PATH_MAX ] ;
	char magic_string[ MAGIC_STRING_SIZE ] ;
	
	gcry_error_t r = gcry_cipher_open( &gcry_cipher_handle,GCRY_CIPHER_AES128,GCRY_CIPHER_MODE_CBC,0 ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_open: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		return _close_exit( wallet ) ;
	}
	
	r = gcry_cipher_setkey( gcry_cipher_handle,wallet->key,PASSWORD_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setkey: %s/%s\n",gcry_strsource( r ),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _close_exit( wallet ) ;
	}
	
	_get_iv( iv ) ;
	
	r = gcry_cipher_setiv( gcry_cipher_handle,iv,IV_SIZE ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"gcry_cipher_setiv: %s/%s\n",gcry_strsource (r),gcry_strerror( r ) ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _close_exit( wallet ) ;
	}
	
	_wallet_full_path( path,PATH_MAX,wallet->wallet_name,wallet->application_name ) ;
	
	snprintf( path_1,PATH_MAX,"%s.tmp",path ) ;
	
	fd = open( path_1,O_WRONLY|O_CREAT,0600 ) ;
	
	if( fd == -1 ){
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _close_exit( wallet ) ;
	}
	
	write( fd,iv,IV_SIZE ) ;
	
	strncpy( magic_string,MAGIC_STRING,MAGIC_STRING_SIZE ) ;
	
	r = gcry_cipher_encrypt( gcry_cipher_handle,magic_string,MAGIC_STRING_SIZE,NULL,0 ) ;
		
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"1-gcry_cipher_encrypt: %s/%s\n",gcry_strsource (r),gcry_strerror( r ) ) ;
		unlink( path_1 ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _close_exit( wallet ) ;
	}
	
	write( fd,magic_string,MAGIC_STRING_SIZE ) ;
	
	r = gcry_cipher_encrypt( gcry_cipher_handle,wallet->wallet_data,wallet->wallet_data_size * sizeof( struct lxqt_key_value ),NULL,0 ) ;
	
	if( r != GPG_ERR_NO_ERROR ){
		fprintf( stderr,"2-gcry_cipher_encrypt: %s/%s\n",gcry_strsource (r),gcry_strerror( r ) ) ;
		unlink( path_1 ) ;
		gcry_cipher_close( gcry_cipher_handle ) ;
		return _close_exit( wallet ) ;
	}
	
	write( fd,wallet->wallet_data,wallet->wallet_data_size * sizeof( struct lxqt_key_value ) ) ;
	
	close( fd ) ;
		
	gcry_cipher_close( gcry_cipher_handle ) ;
	
	rename( path_1,path ) ;
	return _close_exit( wallet ) ;
}

int lxqt_wallet_exists( const char * wallet_name,const char * application_name ) 
{
	char path[ PATH_MAX ] ;
	struct stat st ;
	_wallet_full_path( path,PATH_MAX,wallet_name,application_name ) ;
	return stat( path,&st ) ;
}

void lxqt_wallet_application_wallet_path( char * path,size_t path_buffer_size,const char * application_name )
{
	struct passwd * pass = getpwuid( getuid() ) ;
	
	char path_1[ PATH_MAX ] ;
	
	snprintf( path,path_buffer_size,"%s/.config",pass->pw_dir ) ;
	
	snprintf( path_1,path_buffer_size,"%s/%s",path,application_name )  ;
	
	snprintf( path,path_buffer_size,"%s/wallets",path_1 ) ;
}

char * _wallet_full_path( char * path_buffer,size_t path_buffer_size,const char * wallet_name,const char * application_name )
{
	struct passwd * pass = getpwuid( getuid() ) ;
	snprintf( path_buffer,path_buffer_size,"%s/.config/%s/wallets/%s",pass->pw_dir,application_name,wallet_name ) ;
	return path_buffer ;
}

static void _create_application_wallet_path( const char * application_name )
{
	struct passwd * pass = getpwuid( getuid() ) ;
	
	char path_1[ PATH_MAX ] ;
	char path_2[ PATH_MAX ] ;
	
	snprintf( path_1,PATH_MAX,"%s/.config",pass->pw_dir ) ;
	mkdir( path_1,0755 ) ;
	
	snprintf( path_2,PATH_MAX,"%s/%s",path_1,application_name )  ;
	mkdir( path_2,0755 ) ;
	
	snprintf( path_1,PATH_MAX,"%s/wallets",path_2 ) ;
	mkdir( path_1,0755 ) ;
}

static void _create_key( char output_key[ PASSWORD_SIZE ],const char * input_key,size_t input_key_length )
{
	if( input_key_length ){;}
	/*
	 * TODO: hash the password before using it
	 */
	strncpy( output_key,input_key,PASSWORD_SIZE ) ;
}

static int _get_iv_from_wallet_header( char iv[ IV_SIZE ],const char * wallet_name,const char * application_name ) 
{
	int fd ;
	char path[ PATH_MAX ] ;
	if( lxqt_wallet_exists( wallet_name,application_name ) == 0 ){
		_wallet_full_path( path,PATH_MAX,wallet_name,application_name ) ;
		fd = open( path,O_RDONLY ) ;
		if( fd == -1 ){
			return 0 ;
		}else{
			read( fd,iv,IV_SIZE ) ;
			return 1 ;
		}
	}else{
		return 0 ;
	}
}

int _get_magic_string_from_header( char magic_string[ MAGIC_STRING_SIZE ],const char * wallet_path ) 
{
	int fd = open( wallet_path,O_RDONLY ) ;
	if( fd == -1 ){
		return 0 ;
	}else{
		lseek( fd,IV_SIZE,SEEK_SET ) ;
		read( fd,magic_string,MAGIC_STRING_SIZE ) ;
		close( fd ) ;
		return 1 ;
	}
}

static void _get_iv( char iv[ IV_SIZE ] )
{
	int fd ;
	fd = open( "/dev/urandom",O_RDONLY ) ;
	read( fd,iv,IV_SIZE ) ;
	close( fd ) ;
}

