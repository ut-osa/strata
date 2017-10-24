#include <stdio.h>

/*
#ifdef V2
void __attribute__((weak)) bar()
{
	puts("main: i'm the build in weak bar()");
}
#else
void __attribute__((weak)) bar();
#endif
*/

int main(void)
{
	bar();
	return 0;
}
