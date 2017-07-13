#pragma once

#include <string.h>

#define TRUE 1
#define FALSE 0


int isWav(char* fileName) {
	char* c = fileName;
	int stringLength = 0;
	//printf("isWav: %s\n", fileName);
	while (*c != '\0') { // Don't forget to deference!
		c++;
		stringLength++;
	}
	// Just check the last char
	if (fileName[stringLength-1] == 'V' || fileName[stringLength-1] == 'v') {
		//printf("isWav: True\n");
		return TRUE;
	} else {
		//printf("isWav: False\n");
		return FALSE;
	}
}


