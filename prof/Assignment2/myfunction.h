#ifndef MYFUNCTION_H
#define MYFUNCTION_H

#include <ctype.h>

void convertToUpperCase(char *s, int count) {
  for(char *c = s; c != s + count; c++) {
    *c = toupper(*c);
  }
}

size_t countStrLen(char * s) {
	return strlen(s) + 1;
}

void printData(char *s, size_t count) {
	for(int i = 0; i < count; i++) {
		putchar(s[i]);
	}
	putchar('\n');
}

#endif