/*
 *  Http put/get/post standalone program using Http put mini lib
 *  written by L. Demailly
 *  (c) 2013 Anibal Limon - limon.anibal@gmail.com
 *  (c) 1998 Laurent Demailly - http://www.demailly.com/~dl/
 *  (c) 1996 Observatoire de Paris - Meudon - France
 *  see LICENSE for terms, conditions and DISCLAIMER OF ALL WARRANTIES
 *
 * $Id: http.c,v 1.4 1998/09/23 06:11:55 dl Exp $
 *
 * $Log: http.c,v $
 * 
 * Revision 1.5.x 2013/08/07 08:30:42 -0500
 * Removed no used code for OS9, and code functions to access global
 * variables now static instead extern.
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revisionq 1.5.x 2013/08/07 00:20:42 -0500
 * Added support for POST
 * Author: alimon <limon.anibal@gmail.com>
 *
 * Revision 1.4  1998/09/23 06:11:55  dl
 * one more lint
 *
 * Revision 1.3  1998/09/23 06:03:45  dl
 * proxy support (old change which was not checked in back in 96)
 * contact, etc.. infos update
 *
 * Revision 1.2  1996/04/18  13:52:14  dl
 * strings.h->string.h
 *
 * Revision 1.1  1996/04/18  12:17:25  dl
 * Initial revision
 *
 */


#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_lib.h"

int main(int argc,char* argv[]) 
{
	int  ret,lg,blocksize,r,i;
	char typebuf[70];
	char *data=NULL,*filename=NULL,*proxy=NULL;
	int data_len = 0;
	char *type = NULL;
	enum {
		ERR,
		DOPUT,
		DOGET,
		DODEL,
		DOHEA,
		DOPOST
	} todo=ERR;
	
	if (argc!=3) {
		fprintf(stderr,"usage: http <cmd> <url>\n\tby <L@Demailly.com>\n");
		return 1;
	}

	i=1;
	 
	if (!strcasecmp(argv[i],"put")) {
		todo=DOPUT;
	} else if (!strcasecmp(argv[i],"get")) {
		todo=DOGET;
	} else if (!strcasecmp(argv[i],"delete")) {
		todo=DODEL;
	} else if (!strcasecmp(argv[i],"head")) {
		todo=DOHEA;
	} else if (!strcasecmp(argv[i],"post")) {
		todo=DOPOST;
	}

	if (todo==ERR) {
		fprintf(stderr,
			"Invalid <cmd> '%s',\nmust be "
			"'put', 'get', 'post', 'delete', or 'head'\n",
			argv[i]
		);
		return 2;
	}

	i++;
	
	if ((proxy=getenv("http_proxy"))) {
		ret=http_proxy_url(proxy);
		if (ret<0) { 
			return ret;
		}
	}
	
	ret=http_parse_url(argv[i],&filename);
	if (ret<0) {
		return ret;
	}
	
	switch (todo) {
	/* *** PUT  *** */
	case DOPUT:
		fprintf(stderr,"reading stdin...\n");
		/* read stdin into memory */
		blocksize=16384;
		lg=0;  

		if (!(data=(char *) malloc(blocksize))) {
			return 3;
		}

		while (1) {
			r=read(0,data+lg,blocksize-lg);
			if (r<=0) break; 
			lg+=r;
			if ((3*lg/2)>blocksize) {
				blocksize *= 4;
				fprintf(stderr,
				"read to date: %9d bytes, reallocating buffer to %9d\n",
				lg,blocksize);	
				if (!(data=(char *) realloc(data,blocksize))) {
					return 4;
				}
			}
		}

		fprintf(stderr,"read %d bytes\n",lg);
		ret=http_put(filename,data,lg,0,NULL);
		fprintf(stderr,"res=%d\n",ret);
		break;
	/* *** GET  *** */
	case DOGET:
		ret=http_get(filename,&data,&lg,typebuf);
		fprintf(stderr,"res=%d,type='%s',lg=%d\n",ret,typebuf,lg);
		fwrite(data,lg,1,stdout);
		fprintf(stderr, "%s\n", data);
		break;
	/* *** HEAD  *** */
	case DOHEA:
		ret=http_head(filename,&lg,typebuf);
		fprintf(stderr,"res=%d,type='%s',lg=%d\n",ret,typebuf,lg);
		break;
	/* *** DELETE  *** */
	case DODEL:
		ret=http_delete(filename);
		fprintf(stderr,"res=%d\n",ret);
		break;
	case DOPOST:
		ret = http_post(filename, "your_name=1", 11, NULL, &data, &data_len, &type);
		fprintf(stderr,"res=%d\n",ret);
		fprintf(stderr,"%s\n", type);
		fprintf(stderr,"data: %s\n", data);
		break;
	/* impossible... */
	default:
		fprintf(stderr,"impossible todo value=%d\n",todo);
		return 5;
	}
	
	if (type) {
		free(type);
	}

	if (data) {
		 free(data);
	}

	free(filename);
	 
	return ( (ret==201) || (ret==200) ) ? 0 : ret;
}
