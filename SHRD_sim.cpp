#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "ATLWrapper.h"
#include "VFLWrapper.h"

char working_dir[1024];
char trace_addr[1024];

using namespace Hesper;

static void run_FTL(FILE *fp){
	
}

static void get_cmd(int argc, char *argv []){

	int i;

	for (i = 0; i < argc; i++){

		if (strcmp(argv[i], "--trace") == 0){

			i++;
			strcpy(trace_addr, argv[i]);

		}

		if (strcmp(argv[i], "--dir") == 0){

			i++;
			strcpy(working_dir, argv[i]);
		}

		if (strcmp(argv[i], "--core") == 0){

			i++;
			__COREID__ = atoi(argv[i]);

			if (__COREID__ > 1){
				printf("ERROR: core id is larger than 1\n");
				getchar();
			}

		}
	}
}

void main(int argc, char *argv[]){

	FILE *fp_in;
	
	get_cmd(argc, argv);
	
	VFLWrapper* VFL = new VFLWrapper(working_dir);
	ATLWrapper* ATL = new ATLWrapper(VFL);
	
	fp_in = fopen(trace_addr, "r");

	run_FTL(fp_in);

}


