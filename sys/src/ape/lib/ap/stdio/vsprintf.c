/*
 * pANS stdio -- vsprintf
 */
#include "iolib.h"
int vsprintf(char *buf, const char *fmt, va_list args){
	int n;
	char *v;
	FILE *f=_IO_sopenw();
	if(f==NULL)
		return 0;
	setvbuf(f, buf, _IOFBF, 100000);
	n=vfprintf(f, fmt, args);
	v=_IO_sclose(f);
	return n;
}
