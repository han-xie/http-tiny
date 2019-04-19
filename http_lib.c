/*
 *  Http put/get/post mini lib
 *  written by L. Demailly
 *  (c) 2013 Anibal Limon - limon.anibal@gmail.com
 *  (c) 1998 Laurent Demailly - http://www.demailly.com/~dl/
 *  (c) 1996 Observatoire de Paris - Meudon - France
 *  see LICENSE for terms, conditions and DISCLAIMER OF ALL WARRANTIES
 *
 * $Id: http_lib.c,v 3.5 1998/09/23 06:19:15 dl Exp $ 
 *
 * Description : Use http protocol, connects to server to echange data
 *
 * Revision 4.1 2013/10/13 14:15:00 -0500
 * Added function pointer for custom buffer EOF reader.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revision 4.0 2013/10/13 13:15:00 -0500
 * Added functions for multi-thread support httpmt_.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revision 3.6.x 2013/08/23 20:41:42 -0500
 * Added support for read Content without length in GET/POST.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revision 3.6.x 2013/08/07 21:41:42 -0500
 * Added Basic auth support, base64 encoding is provided for external
 * function trought http_set_base64_encoder.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revision 3.6.x 2013/08/07 08:30:42 -0500
 * Removed no used code for OS9, and code functions to access global
 * variables now static instead extern.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revisionq 3.6.x 2013/08/07 00:20:42 -0500
 * Added support for POST
 * Author: alimon <limon.anibal@gmail.com>
 *
 * $Log: http_lib.c,v $
 * Revision 3.5  1998/09/23 06:19:15  dl
 * portability and http 1.x (1.1 and later) compatibility
 *
 * Revision 3.4  1998/09/23 05:44:27  dl
 * added support for HTTP/1.x answers
 *
 * Revision 3.3  1996/04/25 19:07:22  dl
 * using intermediate variable for htons (port) so it does not yell
 * on freebsd  (thx pp for report)
 *
 * Revision 3.2  1996/04/24  13:56:08  dl
 * added proxy support through http_proxy_server & http_proxy_port
 * some httpd *needs* cr+lf so provide them
 * simplification + cleanup
 *
 * Revision 3.1  1996/04/18  13:53:13  dl
 * http-tiny release 1.0
 *
 *
 */

#ifdef _bsd
static char *rcsid="$Id: http_lib.c,v 3.5 1998/09/23 06:19:15 dl Exp $";
#endif

/* http_lib - Http data exchanges mini library.
 */
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "http_lib.h"

#define SERVER_DEFAULT "adonis"
/* beware that filename+type+rest of header must not exceed MAXBUF */
/* so we limit filename to 256 and type to 64 chars in put & get */
#define MAXBUF 512

typedef enum 
{
	CLOSE,  /* Close the socket after the query (for put) */
	KEEP_OPEN /* Keep it open */
} querymode;

static http_retcode http_query(http_ctx *ctx, char *command, char *url,
			 	char *additional_header, querymode mode, 
		 		char *data, int length, int *pfd);
static int http_read_line(int fd, char *buffer, int max);
static int http_read_buffer(int fd, char *buffer, int max);
static int http_read_buffer_eof(int fd, char **buffer, int *length);

/* user agent id string */
static char *http_user_agent="adlib/3 ($Date: 1998/09/23 06:19:15 $)";

static http_ctx _ctx = {
	.server = NULL,
	.port = 5757,
	.proxy_server = NULL,
	.proxy_port = 0,

	.b64_enc = NULL,
	.b64_auth = NULL,

	.reader = NULL
};

/* parses an url : setting the http_server and http_port global variables
 * and returning the filename to pass to http_get/put/...
 * returns a negative error code or 0 if sucessfully parsed.
 * writeable copy of an url 
 *	char *url;  
 * address of a pointer that will be filled with allocated filename
 * the pointer must be equal to NULL before calling or it will be 
 * automatically freed (free(3))
 *	char **pfilename; 
 */
extern http_retcode
http_parse_url(char *url, char **pfilename)
{
	return httpmt_parse_url(&_ctx, url, pfilename);
}

extern http_retcode
httpmt_parse_url(http_ctx *ctx, char *url, char **pfilename)
{
	char *pc, c;

	if (ctx == NULL)
		return ERRNULL;

	ctx->port = 80;
	if (ctx->server) {
		free(ctx->server);
		ctx->server = NULL;
	}
	if (*pfilename) {
		free(*pfilename);
		*pfilename = NULL;
	}
	 
	if (strncasecmp("http://", url, 7)) {
#ifdef _DEBUG
		fprintf(stderr,"invalid url (must start with 'http://')\n");
#endif
		return ERRURLH;
	}

	url += 7;
	for (pc = url, c = *pc; (c && c != ':' && c != '/');)
		c = *pc++;
	*(pc - 1) = 0;

	if (c == ':') {
		if (sscanf(pc, "%d", &ctx->port) != 1) {
#ifdef _DEBUG
			fprintf(stderr,"invalid port in url\n");
#endif
			return ERRURLP;
		}

		for (pc++; (*pc && *pc != '/'); pc++);
		if (*pc) pc++;
	}

	ctx->server = strdup(url);
	if (ctx->server == NULL) 
		return ERRMEM;

	*pfilename = strdup(c ? pc : "");
	if (*pfilename == NULL) {
		free(ctx->server);
		ctx->server = NULL;
		return ERRMEM;
	}
	
#ifdef _DEBUG
	fprintf(stderr,"host=(%s), port=%d, filename=(%s)\n",
		ctx->server, ctx->port, *pfilename);
#endif
	return OK0;
}

/*
 * sets proxy to use
 */
extern http_retcode
http_proxy_url(char *proxy)
{
	return httpmt_proxy_url(&_ctx, proxy);
}

extern http_retcode
httpmt_proxy_url(http_ctx *ctx, char *proxy)
{
	http_retcode r = OK0;
	char *filename = NULL;

	if (ctx == NULL)
		return ERRNULL;

	r = httpmt_parse_url(ctx, proxy, &filename);
	if (r < 0)
		return r;

	if (ctx->proxy_server) {
		free(ctx->proxy_server);
		ctx->proxy_server = NULL;
	}

	ctx->proxy_server = ctx->server;
	ctx->server = NULL;
	ctx->proxy_port = ctx->port;

	free(filename);

	return r;
}

/*
 * Put data on the server
 *
 * This function sends data to the http data server.
 * The data will be stored under the ressource name filename.
 * returns a negative error code or a positive code from the server
 *
 * limitations: filename is truncated to first 256 characters 
 *              and type to 64.
 *	char *filename	name of the ressource to create 
 *	char *data	pointer to the data to send
 *	int length	length of the data to send 
 *	int overwrite	flag to request to overwrite the ressource if it
 *			 was already existing 
 *	char *type	type of the data, if NULL default type is used
 */
extern http_retcode
http_put(char *filename, char *data, int length, int overwrite, char *type) 
{
	return httpmt_put(&_ctx, filename, data, length, overwrite, type);
}

extern http_retcode
httpmt_put(http_ctx *ctx, char *filename, char *data, int length, int overwrite, char *type) 
{
	char header[MAXBUF];

	if (ctx == NULL)
		return ERRNULL;

	if (type) 
		sprintf(header,"Content-length: %d\015\012Content-type: %.64s\015\012%s",
			length,
			type  ,
			overwrite ? "Control: overwrite=1\015\012" : ""
			);
	else
		sprintf(header,"Content-length: %d\015\012%s",length,
			overwrite ? "Control: overwrite=1\015\012" : ""
			);

	return http_query(ctx, "PUT", filename, header, CLOSE, data, length, NULL);
}
	
/*
 * Get data from the server
 *
 * This function gets data from the http data server.
 * The data is read from the ressource named filename.
 * Address of new new allocated memory block is filled in pdata
 * whose length is returned via plength.
 * 
 * returns a negative error code or a positive code from the server
 * 
 *	char *filename	name of the ressource to read 
 *	char **pdata	address of a pointer variable which will be set
 *			to point toward allocated memory containing read data.
 *	int  *plength	address of integer variable which will be set to
 *			length of the cead data 
 *	char *typebuf	allocated buffer where the read data type is returned.
 *			If NULL, the type is not returned
 *	 
 *
 * limitations: filename is truncated to first 256 characters
 */
extern http_retcode
http_get(char *filename, char **pdata, int *plength, char *typebuf) 
{
	return httpmt_get(&_ctx, filename, pdata, plength, typebuf);
}

extern http_retcode
httpmt_get(http_ctx *ctx, char *filename, char **pdata, int *plength, char *typebuf) 
{
	http_retcode ret;
	 
	char header[MAXBUF];
	char *pc;
	int fd;
	int n, length = -1;

	if (ctx == NULL)
		return ERRNULL;
	
	if (!pdata)
		return ERRNULL;
	else
		*pdata = NULL;

	if (plength) *plength = 0;
	if (typebuf) *typebuf = '\0';
	
	ret = http_query(ctx, "GET", filename, "", KEEP_OPEN, NULL, 0, &fd);
	if (ret == OK200) {
		while (1) {
			n = http_read_line(fd, header , MAXBUF - 1);
#ifdef _DEBUG
			fputs(header, stderr);
			putc('\n', stderr);
#endif	
			if (n <= 0) {
				close(fd);
				return ERRRDHD;
			}
			/* empty line ? (=> end of header) */
			if (n > 0 && (*header) == '\0') break;
			/* try to parse some keywords : */
			/* convert to lower case 'till a : is found or end of string */
			for (pc = header; (*pc != ':' && *pc); pc++)
				*pc = tolower(*pc);
			sscanf(header, "content-length: %d", &length);
			if (typebuf)
				sscanf(header, "content-type: %s", typebuf);
		}

		if (length <= 0) {
			if (ctx->reader) {
				(*ctx->reader)(fd);
			} else {
				if (http_read_buffer_eof(fd, pdata, plength) == -1)
					ret = ERRNOLG;
			}
			close(fd);
		} else {
			*plength = length;
			if (!(*pdata = (char *) malloc(length))) {
				close(fd);
				return ERRMEM;
			}
			n = http_read_buffer(fd, *pdata, length);
			close(fd);
			if (n != length)
				ret = ERRRDDT;
		}
	} else if (ret >= OK0) {
		close(fd);
	}

	return ret;
}
	
/*
* Request the header
*
* This function outputs the header of thehttp data server.
* The header is from the ressource named filename.
* The length and type of data is eventually returned (like for http_get(3))
*
* returns a negative error code or a positive code from the server
* 
*	char *filename	name of the ressource to read 
*	int  *plength	address of integer variable which will be set to
*			length of the data
*	char *typebuf	allocated buffer where the data type is returned.
*			If NULL, the type is not returned 
* limitations: filename is truncated to first 256 characters
*/
extern http_retcode
http_head(char *filename, int *plength, char *typebuf) 
{
	return httpmt_head(&_ctx, filename, plength, typebuf);
}

extern http_retcode
httpmt_head(http_ctx *ctx, char *filename, int *plength, char *typebuf) 
{
/* mostly copied from http_get : */
	http_retcode ret;
	 
	char header[MAXBUF];
	char *pc;
	int fd;
	int n, length=-1;

	if (ctx == NULL)
		return ERRNULL;
	
	if (plength)
		*plength = 0;
	if (typebuf)
		*typebuf = '\0';
	
	ret = http_query(ctx, "HEAD", filename, "", KEEP_OPEN, NULL, 0, &fd);

	if (ret == OK200) {
		while (1) {
			n = http_read_line(fd, header, MAXBUF - 1);
#ifdef _DEBUG
			fputs(header, stderr);
			putc('\n', stderr);
#endif	
			if (n <= 0) {
				close(fd);
				return ERRRDHD;
			}
			/* empty line ? (=> end of header) */
			if (n > 0 && (*header) == '\0')
				break;
			/* try to parse some keywords : */
			/* convert to lower case 'till a : is found or end of string */
			for (pc = header; (*pc != ':' && *pc); pc++)
				*pc = tolower(*pc);
			sscanf(header, "content-length: %d", &length);
			if (typebuf)
				sscanf(header, "content-type: %s", typebuf);
		}
		if (plength) 
			*plength = length;
		close(fd);
	} else if (ret >= OK0) {
		close(fd);
	}

	return ret;
}
	
/*
 * Delete data on the server
 *
 * This function request a DELETE on the http data server.
 *
 * returns a negative error code or a positive code from the server
 *
 *	char *filename	name of the ressource to create
 * limitations: filename is truncated to first 256 characters 
 */
extern http_retcode
http_delete(char *filename) 
{
	return httpmt_delete(&_ctx, filename);
}

extern http_retcode
httpmt_delete(http_ctx *ctx, char *filename) 
{
	if (ctx == NULL)
		return ERRNULL;
	else
		return http_query(ctx, "DELETE", filename, "", CLOSE, NULL, 0, NULL);
}
	
/*
* post data
*/
extern http_retcode
http_post(char *filename, char *data, int length, char *type, char **pdata,
		int *plength, char **ptype)
{
	return httpmt_post(&_ctx, filename, data, length, type, pdata, plength,
				ptype);
}

extern http_retcode
httpmt_post(http_ctx *ctx, char *filename, char *data, int length, char *type,
			char **pdata, int *plength, char **ptype)
{
	int fd;
	char *pc;
	int n;
	char header[MAXBUF];
	char typebuf[MAXBUF];
	http_retcode ret;

	if (ctx == NULL)
		return ERRNULL;

	if (data == NULL || length <= 0 || pdata == NULL || plength == NULL)
		return ERRNULL;
	
	*pdata = NULL;
	*plength = 0;

	header[0] = '\0';	
	typebuf[0] = '\0';
	
	if (type) 
		sprintf(header, "Content-length: %d\015\012Content-type: %.64s\015\012",
			length, type);
	else
		sprintf(header, "Content-length: %d\015\012", length);
	
	ret = http_query(ctx, "POST", filename, header, KEEP_OPEN, data, length, &fd);
	
	if (ret==OK200) { 
		while (1) {
			n = http_read_line(fd, header, MAXBUF - 1);
#ifdef _DEBUG
			fputs(header, stderr);
			putc('\n', stderr);
#endif	
			if (n <= 0) {
				close(fd);
				return ERRRDHD;
			}
	
			/* empty line ? (=> end of header) */
			if (n > 0 && (*header) =='\0')
				break;
	
			/* try to parse some keywords : */
			/* convert to lower case 'till a : is found or end of string */
			for (pc = header; (*pc != ':' && *pc); pc++)
				*pc = tolower(*pc);
			sscanf(header, "content-length: %d", plength);
			sscanf(header, "content-type: %s", typebuf);
		}
	
		if (ptype)
			*ptype = strdup(typebuf);
		
		if (*plength <= 0) {
			if (ctx->reader) {
				(*ctx->reader)(fd);
			} else {
				if (http_read_buffer_eof(fd, pdata, plength) == -1) {
					ret = ERRNOLG;
					if (ptype) {
						free(*ptype);
						*ptype = NULL;
					}
            			}
			}

			close(fd);
		} else {
			if (!(*pdata = (char *) malloc(*plength))) {
				close(fd);
				if (ptype) {
					free(*ptype);
					*ptype = NULL;
				}
				return ERRMEM;
			}
	
			n = http_read_buffer(fd, *pdata, *plength);
			close(fd);
	
			if (n != *plength) {
				free(*pdata);
				*pdata = NULL;
				if (ptype) {
					free(*ptype);
					*ptype = NULL;
				}
				ret = ERRRDDT;
			}
		}
	} else if (ret >= OK0) {
		close(fd);
	}
	
	return ret;
}

/**
 * set external base64 encoder for basic auth
 */
extern void
http_set_base64_encoder(http_base64_encoder enc)
{
	httpmt_set_base64_encoder(&_ctx, enc);
}

extern void
httpmt_set_base64_encoder(http_ctx *ctx, http_base64_encoder enc)
{
	if (ctx != NULL)
		ctx->b64_enc = enc;
}

/**
 * set basic auth for use in all requests, calls external
 * base64 encoder
 */
extern http_retcode
http_set_basic_auth(char *user, char *pass)
{
	return httpmt_set_basic_auth(&_ctx, user, pass);
}

extern http_retcode
httpmt_set_basic_auth(http_ctx *ctx, char *user, char *pass)
{
	http_retcode r = OK0;
	char userpass[MAXBUF];
	char *b64;

	if (ctx == NULL)
		return ERRNULL;

	if (ctx->b64_enc == NULL || user == NULL || pass == NULL)
		return ERRNULL;

	snprintf(userpass, MAXBUF, "%s:%s", user, pass);
	if ((*ctx->b64_enc)(userpass, &b64) == -1)
		return ERRMEM;

	if (ctx->b64_auth)
		free(ctx->b64_auth);

	ctx->b64_auth = b64;

	return r;
}
/**
 * set custom buffer reader
 */
extern void
http_set_buffer_eof_reader(http_buffer_eof_reader reader)
{
	return httpmt_set_buffer_eof_reader(&_ctx, reader);
}

extern void
httpmt_set_buffer_eof_reader(http_ctx *ctx, http_buffer_eof_reader reader)
{
	if (ctx != NULL)
		ctx->reader = reader;
}
	
/*
 * Pseudo general http query
 *
 * send a command and additional headers to the http server.
 * optionally through the proxy (if http_proxy_server and http_proxy_port are
 * set).
 *
 * Limitations: the url is truncated to first 256 chars and
 * the server name to 128 in case of proxy request.
 *
 * char *command		Command to send
 * char *url;			url / filename queried
 * char *additional_header	Additional header 
 * querymode mode; 		Type of query
 * char *data			Data to send after header. 
 *				If NULL, not data is sent 
 * int length			size of data
 * int *pfd			pointer to variable where to 
 *				set file descriptor value
 */
static http_retcode
http_query(http_ctx *ctx, char *command, char *url, char *additional_header, 
	querymode mode, char *data, int length, int *pfd) 
{
	int s;
	struct hostent *hp;
	struct sockaddr_in server;
	char header[MAXBUF];
	int hlg;
	http_retcode ret;
	int proxy; 
	int port;

	proxy = (ctx->proxy_server != NULL && ctx->proxy_port != 0);
	port = proxy ? ctx->proxy_port : ctx->port;
	 
	if (pfd) *pfd=-1;
	
	/* get host info by name :*/
	if ((hp = gethostbyname( proxy ? ctx->proxy_server 
		: (ctx->server ? ctx->server : SERVER_DEFAULT)))) {
		memset((char *) &server,0, sizeof(server));
		memmove((char *) &server.sin_addr, hp->h_addr, hp->h_length);
		server.sin_family = hp->h_addrtype;
		server.sin_port = (unsigned short) htons( port );
	} else {
		return ERRHOST;
	}
	
	/* create socket */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return ERRSOCK;
	setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, 0, 0);
	
	/* connect to server */
	if (connect(s, (const struct sockaddr *) &server, sizeof(server)) < 0) {
		ret = ERRCONN;
	} else {
		if (pfd) *pfd = s;
	 
		/* create header */
		if (proxy) {
			if (ctx->b64_auth) {
				sprintf(header,
					"%s http://%.128s:%d/%.256s HTTP/1.0\015\012User-Agent: %s\015\012"
					"Authorization: Basic %s\015\012%s\015\012",
					command,
					ctx->server,
					ctx->port,
					url,
					http_user_agent,
					ctx->b64_auth,
					additional_header
					);
			} else {
				sprintf(header,
					"%s http://%.128s:%d/%.256s HTTP/1.0\015\012"
					"User-Agent: %s\015\012%s\015\012",
					command,
					ctx->server,
					ctx->port,
					url,
					http_user_agent,
					additional_header
				       );
			}
		} else {
			if (ctx->b64_auth) {
				sprintf(header,
					"%s /%.256s HTTP/1.0\015\012User-Agent: %s\015\012"
					"Authorization: Basic %s\015\012%s\015\012",
					command,
					url,
					http_user_agent,
					ctx->b64_auth,
					additional_header
					);
			} else {
				sprintf(header,
					"%s /%.256s HTTP/1.0\015\012User-Agent: %s\015\012%s\015\012",
					command,
					url,
					http_user_agent,
					additional_header
					);
			}
		}
	
		hlg = strlen(header);
		
#ifdef _DEBUG
		fputs(header, stderr);
		putc('\n', stderr);
#endif	
	
		/* send header */
		if (write(s, header, hlg) != hlg) {
			ret = ERRWRHD;
			/* send data */
		} else if (length && data && (write(s, data, length) != length)) {
			ret = ERRWRDT;
		} else {
			/* read result & check */
			ret = (http_retcode) http_read_line(s, header, MAXBUF - 1);

			if (ret <= 0) 
				ret = ERRRDHD;
			else if (sscanf(header, "HTTP/1.%*d %03d", (int*)&ret) != 1) 
				ret = ERRPAHD;
			else if (mode == KEEP_OPEN)
				return ret;
		}
	}

	/* close socket */
	close(s);
	return ret;
}

/*
 * read a line from file descriptor
 * returns the number of bytes read. negative if a read error occured
 * before the end of line or the max.
 * cariage returns (CR) are ignored.
 * 	int fd		File descriptor to read from
 * 	char *buffer	Placeholder for data
 *	int max		Max number of bytes to read
 */
static int http_read_line (int fd, char *buffer, int max) 
{ 
	/* not efficient on long lines (multiple unbuffered 1 char reads) */
	int n=0;
	while (n<max) {
		if (read(fd,buffer,1)!=1) {
			n= -n;
			break;
		}
		n++;
		if (*buffer=='\015') continue; /* ignore CR */
		if (*buffer=='\012') break;    /* LF is the separator */
		buffer++;
	}
	*buffer=0;
	return n;
}

/*
 * read data from file descriptor
 * retries reading until the number of bytes requested is read.
 * returns the number of bytes read. negative if a read error (EOF) occured
 * before the requested length.
 *
 *	int fd		file descriptor to read from
 *	char *buffer	placeholder for data
 *	int length	number of bytes to read
 */
static int 
http_read_buffer(int fd, char *buffer, int length) 
{
	int n,r;
	for (n=0; n<length; n+=r) {
		r=read(fd,buffer,length-n);
		if (r<=0) return -n;
		buffer+=r;
	}
	return n;
}

/*
 * read data from file descriptor
 * retries reading until the number of bytes requested is read.
 * returns the number of bytes read or -1 if fails
 *
 *	int fd		file descriptor to read from
 *	char **pbuffer	placeholder for return data
 *	int *plength	number of bytes read
 */
static int 
http_read_buffer_eof(int fd, char **pbuffer, int *plength) 
{
	int r = 0;
	static int page_size = 0;
	int to_read;
	char *data;
	int size = 0;

	if (page_size == 0) 
		page_size = sysconf(_SC_PAGESIZE);

#ifdef _DEBUG
	printf("page_size: %d\n", page_size);
#endif

	*pbuffer = NULL;
	*plength = 0;

	do {
		if (*plength >= size) {
			size += page_size;
			data = (char *) realloc(*pbuffer, size);
			if (data == NULL) {
				r = -1;
				free(*pbuffer);
				*plength = 0;
				break;
			} else {
				*pbuffer = data;
			}
		}

		to_read = -1 * ((*plength % page_size) - page_size);
		r = read(fd, *pbuffer + *plength, to_read);

		if (r == -1) {
			if (errno == ECONNRESET) {
				r = 0;
			} else {
				free(*pbuffer);
				*plength = 0;
			}
			break;
		} else if (r == 0) {
			break;	
		} else {
			*plength += r;
		}
	} while (1);

	return r;
}
