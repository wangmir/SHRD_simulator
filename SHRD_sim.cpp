#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "ATLWrapper.h"
#include "VFLWrapper.h"

char working_dir[1024];
char trace_addr[1024];

using namespace Hesper;

struct CMD{
	RSP_UINT8 RW; //read (1) or write (0), if RW > 1, then error
	RSP_UINT32 LPN[2]; //if the request is read, then the LPN[0] refers request ID and LPN[1] shows actual LPN
	RSP_UINT16 SectorBitmap;
	RSP_UINT32 BufferAddress[1024];
};

static CMD parse_CMD(char *buff){

	CMD command = { 0 };
	char seps [] = " \t";
	char *tr;



	return  command;
}

static void run_FTL(FILE *fp_in){

	char buff[1024];
	RSP_UINT32 i = 0;

	VFLWrapper* VFL_0 = new VFLWrapper(working_dir, 0);
	ATLWrapper* ATL_0 = new ATLWrapper(VFL_0, 1);

	VFLWrapper* VFL_1 = new VFLWrapper(working_dir, 1);
	ATLWrapper* ATL_1 = new ATLWrapper(VFL_1, 2);

	CMD command = parse_CMD(buff);

	ATL_0->RSP_Open();
	ATL_1->RSP_Open();

	while (fgets(buff, 1024, fp_in) != NULL){

		command = parse_CMD(buff);



	}
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
	}
}

void main(int argc, char *argv[]){

	FILE *fp_in = NULL;
	
	get_cmd(argc, argv);
	
	//fp_in = fopen(trace_addr, "r");

	run_FTL(fp_in);

}


