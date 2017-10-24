#include <stdio.h>

void __attribute__((weak)) bar() 
{
	puts("\t===== i'm the weak bar()");
}
