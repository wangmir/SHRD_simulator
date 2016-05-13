#ifndef __SHRD_host_H__
#define __SHRD_host_H__

#include "RSP_header.h"
#include "ATLWrapper.h"
#include "HILWrapper.h"
#include <map>
#include <list>

using namespace std;
using namespace Hesper;

class SHRD_host {

	enum SHRD_MAP_FLAG {
		SHRD_INVALID_MAP = 0,
		SHRD_VALID_MAP,
		SHRD_REMAPPING_MAP, //when the read request is arrived, the corresponding data is in remapping state, then 1) send o_addr read, 2) wait (what is correct?)
	};

	struct SHRD_MAP {
		RSP_UINT32 o_addr;
		RSP_UINT32 t_addr;
		RSP_UINT8 flags;
	};

public:

#define SHRD_NUM_CORES 2

#define SHRD_SECTORS_PER_PAGE 8

#define SHRD_RW_THRESHOLD_IN_SECTOR 32 //under 16KB write requests will be gathered as twrite data
#define SHRD_RW_NUM_ADAPTIVE_PACKING 8

#define SHRD_TWRITE_ENTRIES (32U)
#define SHRD_REMAP_ENTRIES (32U)  // (experimental)
#define SHRD_TREAD_ENTRIES (32U)
#define SHRD_SUBREAD_ENTRIES (SHRD_TREAD_ENTRIES * 128U)

#define SHRD_RW_LOG_SIZE_IN_MB (128U)
#define SHRD_RW_LOG_SIZE_IN_PAGE (SHRD_RW_LOG_SIZE_IN_MB * 256U)

#define SHRD_JN_LOG_SIZE_IN_MB (128U)
#define SHRD_JN_LOG_SIZE_IN_PAGE (SHRD_JN_LOG_SIZE_IN_MB * 256U)

	//110 * 1024 * 256 - SHRD_JN_LOG_SIZE_IN_PAGE - SHRD_RW_LOG_SIZE_IN_PAGE
#define SHRD_TOTAL_LPN (110U * 1024U * 256U) //110GB
	//#define SHRD_TOTAL_LPN (50 * 1024 * 256) 

#define SHRD_LOG_START_IN_PAGE ((SHRD_TOTAL_LPN - SHRD_RW_LOG_SIZE_IN_PAGE - SHRD_JN_LOG_SIZE_IN_PAGE))
#define SHRD_RW_LOG_START_IN_PAGE ((SHRD_JN_LOG_START_IN_PAGE + SHRD_JN_LOG_SIZE_IN_PAGE))
#define SHRD_JN_LOG_START_IN_PAGE (SHRD_LOG_START_IN_PAGE)

#define SHRD_CMD_START_IN_PAGE SHRD_TOTAL_LPN
#define SHRD_TWRITE_CMD_START_IN_PAGE SHRD_CMD_START_IN_PAGE
#define SHRD_REMAP_CMD_START_IN_PAGE (SHRD_CMD_START_IN_PAGE + SHRD_TWRITE_ENTRIES * SHRD_NUM_CORES) //# of ncq * 2 (2 cores)
#define SHRD_COMFRIM_RD_CMD_IN_PAGE (SHRD_REMAP_CMD_START_IN_PAGE + SHRD_REMAP_ENTRIES * SHRD_NUM_CORES)

#define SHRD_MIN_RW_LOGGING_IO_SIZE_IN_PAGE 128

#define SHRD_MAX_TWRITE_IO_SIZE_IN_SECTOR 1024
#define SHRD_MAX_TWRITE_IO_SIZE_IN_PAGE (SHRD_MAX_TWRITE_IO_SIZE_IN_SECTOR / SHRD_SECTORS_PER_PAGE)

#define SHRD_NUM_MAX_TWRITE_ENTRY 128
#define SHRD_NUM_MAX_REMAP_ENTRY 511
#define SHRD_NUM_MAX_SUBREAD_ENTRY 128

#define SHRD_REMAP_DATA_PAGE 1 // 1page (experimental)
#define SHRD_MAX_REMAP_DATA_ENTRIES (SHRD_REMAP_DATA_PAGE * SHRD_NUM_MAX_REMAP_ENTRY)

#define SHRD_RW_REMAP_THRESHOLD_IN_PAGE (SHRD_RW_LOG_SIZE_IN_PAGE >> 2)
#define SHRD_MAX_REMAP_SIZE_IN_PAGE (SHRD_RW_REMAP_THRESHOLD_IN_PAGE >> 2)

#define SHRD_INVALID_LPN 0x7fffffff

#define RANDOM_SEED 0x8fff

#define LPN_RANGE (4 * 1024 * 256)

#define HOST_ASSERT(bCondition) if (!(bCondition)) {printf("ASSERT!!");while(1);}

	HILWrapper *HIL;

	RSP_UINT32 rw_log_start_idx;
	RSP_UINT32 rw_log_new_idx;

	RSP_UINT32 remap_threshold = 20480;
	RSP_UINT32 remap_size = 16384;

	RSP_UINT32 header_entry_num = 0;
	RSP_UINT32 remap_entry_num = 0;

	RSP_UINT32 write_amount = 0;
	RSP_UINT32 read_amount = 0;

	SHRD_MAP *map_entries;
	map< RSP_UINT32, SHRD_MAP *> *redirection_map;

	SHRD_host(HILWrapper *HIL);
	
	//for workload generator
	void do_twrite(RSP_UINT32 max_packed_rw);
	void __do_remap(RSP_UINT32 size);
	RSP_UINT32 do_remap();
	int HOST_gen_random_workload();

	//for realitic host simulation
	void HOST_verify_lpn(RSP_UINT32 lpn);
	void HOST_verify_random_workload();

	RSP_BOOL HOST_Write(RSP_UINT32 SectAddr, RSP_UINT32 SectCount, RSP_UINT32 *buff);
	RSP_BOOL HOST_Read(RSP_UINT32 SectAddr, RSP_UINT32 SectCount, RSP_UINT32 *buff);
	//RSP_BOOL HOST_Flush();
	//RSP_BOOL HOST_Discard();
};

#endif