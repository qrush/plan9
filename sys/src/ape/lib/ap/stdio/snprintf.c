/*
 * pANS stdio -- sprintf
 */
#include "iolib.h"
int snprintf(char *buf, int nbuf, const char *fmt, ...){
	int n;
	va_list args;
	FILE *f=_IO_sopenw();
	if(f==NULL)
		return 0;
	setvbuf(f, buf, _IOFBF, nbuf);
	va_start(args, fmt);
	n=vfprintf(f, fmt, args);
	va_end(args);
	_IO_sclose(f);
	return n;
}
