#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "ATLWrapper.h"
#include "VFLWrapper.h"
#include "HILWrapper.h"
#include "SHRD_host.h"

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

enum CMD_LINE_TYPE{

	NONE,
	REQUEST,  //SHRD_TRACE	W	230652744	184
	NORMAL_FRONT, //SHRD_TRACE	NORMAL_REQUEST
	TWRITE_HDR_FRONT, //SHRD_TRACE	TWRITE_HDR	28831732	23
	TWRITE_HDR_OADDR, //SHRD_TRACE TWRITE_HDR	O_ADDR	192718	
	REMAP_DATA_FRONT, //SHRD_TRACE	REMAP_DATA	28827648	28828389	510
	REMAP_DATA_ADDR //SHRD_TRACE	REMAP_DATA	T_ADDR 28827648	O_ADDR	28827648	
};

struct CMD_LINE{
	enum CMD_LINE_TYPE type;
	RSP_UINT8 RW;
	RSP_UINT32 SectorAddress;
	RSP_UINT32 SectorCount;
	RSP_UINT32 O_ADDR;
	RSP_UINT32 T_ADDR;
	RSP_UINT32 start;
	RSP_UINT32 end;
	RSP_UINT32 count;
};

struct SHRD_TWRITE_HEADER{
	RSP_UINT32 t_addr_start; 		//indicate WAL log start page addr
	RSP_UINT32 io_count; 		//indicate how many writes (page) will be requested
	RSP_UINT32 o_addr[NUM_MAX_TWRITE_ENTRY]; 		//o_addr (or h_addr) for each page (1<<32: padding, 1<<31: invalid(like journal header or journal commit))
};

struct SHRD_REMAP_DATA{
	RSP_UINT32 remap_count;
	RSP_UINT32 t_addr[NUM_MAX_REMAP_ENTRY];
	RSP_UINT32 o_addr[NUM_MAX_REMAP_ENTRY];
};

static CMD_LINE get_CMD_LINE(char *buff){

	char seps [] = " \t\n";
	char *tr;
	CMD_LINE cmdline;

	tr = strtok(buff, seps);

	while (1){
		tr = strtok(NULL, seps);
		if (tr == NULL){
			cmdline.type = NONE;
			return cmdline;
		}
		if (strcmp(tr, "SHRD_TRACE") == 0){
			break;
		}
	}

	tr = strtok(NULL, seps);
	if (strcmp(tr, "W") == 0){
		//REQUEST type cmdline, and write
		//SHRD_TRACE	W	230652744	184
		cmdline.type = REQUEST;
		cmdline.RW = WRITE;

		tr = strtok(NULL, seps);
		cmdline.SectorAddress = atoi(tr);
		tr = strtok(NULL, seps);
		cmdline.SectorCount = atoi(tr);

	}
	else if (strcmp(tr, "R") == 0){
		//REQUEST type cmdline with read 
		cmdline.type = REQUEST;
		cmdline.RW = READ;

		tr = strtok(NULL, seps);
		cmdline.SectorAddress = atoi(tr);
		tr = strtok(NULL, seps);
		cmdline.SectorCount = atoi(tr);
	}
	else if (strcmp(tr, "NORMAL_REQUEST") == 0){
		cmdline.type = NORMAL_FRONT;
	}
	else if (strcmp(tr, "TWRITE_HDR") == 0){
		
		tr = strtok(NULL, seps);
		if (strcmp(tr, "O_ADDR") == 0){
			cmdline.type = TWRITE_HDR_OADDR;

			tr = strtok(NULL, seps);
			cmdline.O_ADDR = atoi(tr);
		}
		else {
			cmdline.type = TWRITE_HDR_FRONT;
			cmdline.start = atoi(tr);
			tr = strtok(NULL, seps);
			cmdline.count = atoi(tr);
		}
	}
	else if (strcmp(tr, "REMAP_DATA") == 0){

		tr = strtok(NULL, seps);
		if (strcmp(tr, "T_ADDR") == 0){
			cmdline.type = REMAP_DATA_ADDR;
			tr = strtok(NULL, seps);
			cmdline.T_ADDR = atoi(tr);
			tr = strtok(NULL, seps);
			tr = strtok(NULL, seps);
			cmdline.O_ADDR = atoi(tr);
		}
		else{
			cmdline.type = REMAP_DATA_FRONT;
			cmdline.count = atoi(tr);
		}

	}
	else{
		printf("ERROR: trace is not completed\n");
		getchar();
	}

	return cmdline;
}

static int get_CMD(FILE *fp, HILWrapper *HIL){

	char buff[1024];
	CMD_LINE cmdline;
	SHRD_TWRITE_HEADER *twrite_hdr = NULL;
	SHRD_REMAP_DATA *remap_data = NULL;
	int i = 0, arr = 0;
	RSP_UINT32 LPN_start, LPN_cnt;
	CMD command;
	command.BufferAddress = (RSP_UINT32 *) malloc(4096);

	memset(command.BufferAddress, 0x00, 4096);

	while (1){

		if (fgets(buff, 1024, fp) == 0)
			return 0;
		//NONE,
		//REQUEST,  //SHRD_TRACE	W	230652744	184
		//NORMAL_FRONT, //SHRD_TRACE	NORMAL_REQUEST
		//TWRITE_HDR_FRONT, //SHRD_TRACE	TWRITE_HDR	28831732	23
		//TWRITE_HDR_OADDR, //SHRD_TRACE TWRITE_HDR	O_ADDR	192718	
		//REMAP_DATA_FRONT, //SHRD_TRACE	REMAP_DATA	28827648	28828389	510
		//REMAP_DATA_ADDR //SHRD_TRACE	REMAP_DATA	T_ADDR 28827648	O_ADDR	28827648

		cmdline = get_CMD_LINE(buff);
		if (cmdline.type == NONE)
			continue;

		if (cmdline.type == REQUEST){
			LPN_start = cmdline.SectorAddress / RSP_SECTOR_PER_LPN;
			LPN_cnt = cmdline.SectorCount / RSP_SECTOR_PER_LPN;
			command.RW = cmdline.RW;
		}

		else if (cmdline.type == NORMAL_FRONT){
			memset(command.BufferAddress, 0xff, 4096);
			break;
		}

		else if (cmdline.type == TWRITE_HDR_FRONT){
			if (remap_data || twrite_hdr){
				printf("ERROR: cmdline error, already there are data structure to send\n");
				getchar();
			}
			twrite_hdr = (SHRD_TWRITE_HEADER *) command.BufferAddress;
			twrite_hdr->t_addr_start = cmdline.start;
			twrite_hdr->io_count = cmdline.count;
		}
		else if (cmdline.type == TWRITE_HDR_OADDR){

			if (remap_data){
				printf("ERROR: cmdline is mixed, this is twrite header cmdline but the previous remap handling is not over\n");
				getchar();
			}

			twrite_hdr->o_addr[arr] = cmdline.O_ADDR;
			arr++;
			if (arr == NUM_MAX_TWRITE_ENTRY)
				break;
		}
		else if (cmdline.type == REMAP_DATA_FRONT){
			if (remap_data || twrite_hdr){
				printf("ERROR: cmdline error, already there are data structure to send\n");
				getchar();
			}
			remap_data = (SHRD_REMAP_DATA *) command.BufferAddress;
			remap_data->remap_count = cmdline.count;
		}
		else if (cmdline.type == REMAP_DATA_ADDR){
			if (twrite_hdr){
				printf("ERROR: cmdline is mixed, this is remap data cmdline but the previous twrite handling is not over\n");
				getchar();
			}
			remap_data->o_addr[arr] = cmdline.O_ADDR;
			remap_data->t_addr[arr] = cmdline.T_ADDR;
			arr++;
			if (arr == NUM_MAX_REMAP_ENTRY)
				break;
		}
		i++;
	}

	command.SectorBitmap = 0xff;

	for (RSP_UINT32 iter = 0; iter < LPN_cnt; iter++){

		command.RID = 0;
		command.LPN = LPN_start + iter;

		if (command.LPN == 14406865 * 2)
			RSP_UINT32 err = 3;

		if (command.LPN == 28835870){
			if (twrite_hdr->t_addr_start == 28813720)
				RSP_UINT32 err = 3;
		}

		if (command.RW){
			HIL->HIL_ReadLPN(command.RID, command.LPN, command.SectorBitmap, command.BufferAddress);
		}
		else{
			HIL->HIL_WriteLPN(command.LPN, command.SectorBitmap, command.BufferAddress);
		}
	}
	return 1;
	
}

static void run_trace(FILE *fp_in){

	RSP_UINT32 i = 0;

	VFLWrapper* VFL_0 = new VFLWrapper(working_dir, 0);
	ATLWrapper* ATL_0 = new ATLWrapper(VFL_0, 1);

	VFLWrapper* VFL_1 = new VFLWrapper(working_dir, 1);
	ATLWrapper* ATL_1 = new ATLWrapper(VFL_1, 2);

	HILWrapper* HIL = new HILWrapper(ATL_0, ATL_1);

	VFL_0->HIL_ptr(HIL);
	VFL_1->HIL_ptr(HIL);

	printf("\n==RUN FTL simulation with trace==\n");

	if (fp_in == NULL){

		printf("ERROR:: trace file is empty or the path is incorrect\n");
		getchar();
	}

	ATL_0->RSP_Open();
	ATL_1->RSP_Open();

	while (1){
		int ret = 0;

		i++;
		if (i % 100 == 0)
			printf("-");
		if (i == 13147)
			RSP_UINT32 err = 3;
		ret = get_CMD(fp_in, HIL);
		if (ret == 0)
			break;
	}

	RSP_UINT32 *profile0 = VFL_0->profile;
	RSP_UINT32 *profile1 = VFL_1->profile;
}

static void run_workload() {

	RSP_UINT32 i = 0;

	VFLWrapper* VFL_0 = new VFLWrapper(working_dir, 0);
	ATLWrapper* ATL_0 = new ATLWrapper(VFL_0, 1);

	VFLWrapper* VFL_1 = new VFLWrapper(working_dir, 1);
	ATLWrapper* ATL_1 = new ATLWrapper(VFL_1, 2);

	HILWrapper* HIL = new HILWrapper(ATL_0, ATL_1);

	VFL_0->HIL_ptr(HIL);
	VFL_1->HIL_ptr(HIL);

	printf("\n==RUN FTL simulation with workload generator==\n");

	ATL_0->RSP_Open();
	ATL_1->RSP_Open();

	//HIL->HIL_ReadLPN(command.RID, command.LPN, command.SectorBitmap, command.BufferAddress);
	//HIL->HIL_WriteLPN(command.LPN, command.SectorBitmap, command.BufferAddress);

	SHRD_host *HOST = new SHRD_host(HIL);

	RSP_UINT32 total_write_pages = 110 * 262144; //110G

	while (1) {
		if(i % 100 == 0)
			printf("-");

		HOST->HOST_gen_random_workload();
		if (HOST->write_amount > total_write_pages)
			break;
		i++;
	}

	RSP_UINT32 *profile0 = VFL_0->profile;
	RSP_UINT32 *profile1 = VFL_1->profile;
}

static void get_arg(int argc, char *argv []){

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
	
	get_arg(argc, argv);
	
	fp_in = fopen(trace_addr, "r");

	//run_trace(fp_in);
	run_workload();
}


