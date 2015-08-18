#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "ATLWrapper.h"
#include "VFLWrapper.h"
#include "HILWrapper.h"

char working_dir[1024];
char trace_addr[1024];

using namespace Hesper;

struct CMD{
	RSP_UINT8 RW; //read (1) or write (0), if RW > 1, then error
	RSP_UINT32 RID;
	RSP_UINT32 LPN; 
	RSP_UINT16 SectorBitmap;
	RSP_UINT32 *BufferAddress;
};

static void parse_CMD(char *buff, CMD *command){

	char seps [] = " \t";
	char *tr;

	command->RW = 0;
	command->LPN = rand() % (100 * 1024 * 256);

	memset(command->BufferAddress, 0xff, 4096);

	command->SectorBitmap = 0xff;
	
}

static void run_FTL(FILE *fp_in){

	char buff[1024];
	RSP_UINT32 i = 0;

	VFLWrapper* VFL_0 = new VFLWrapper(working_dir, 0);
	ATLWrapper* ATL_0 = new ATLWrapper(VFL_0, 1);

	VFLWrapper* VFL_1 = new VFLWrapper(working_dir, 1);
	ATLWrapper* ATL_1 = new ATLWrapper(VFL_1, 2);

	HILWrapper* HIL = new HILWrapper(ATL_0, ATL_1);

	CMD command;
	command.BufferAddress = (RSP_UINT32 *) malloc(4096);

	printf("\n==RUN FTL simulation==\n");

	ATL_0->RSP_Open();
	ATL_1->RSP_Open();

	//while (fgets(buff, 1024, fp_in) != NULL){
	while (1){

		i++;
		if (i % 500 == 0)
			printf("-");

		parse_CMD(buff, &command);

		if (command.RW){

			HIL->HIL_ReadLPN(command.RID, command.LPN, command.SectorBitmap, command.BufferAddress);

		}
		else{

			HIL->HIL_WriteLPN(command.LPN, command.SectorBitmap, command.BufferAddress);
		}

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
	
	fp_in = fopen(trace_addr, "r");

	run_FTL(fp_in);

}


