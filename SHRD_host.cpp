#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "SHRD_host.h"

using namespace Hesper;
#define NUM_MAX_TWRITE_ENTRY 256
#define NUM_MAX_REMAP_ENTRY 32

struct SHRD_TWRITE_HEADER {
	RSP_UINT32 t_addr_start; 		//indicate WAL log start page addr
	RSP_UINT32 io_count; 		//indicate how many writes (page) will be requested
	RSP_UINT32 o_addr[NUM_MAX_TWRITE_ENTRY]; 		//o_addr (or h_addr) for each page (1<<32: padding, 1<<31: invalid(like journal header or journal commit))
};

struct SHRD_REMAP_DATA {
	RSP_UINT32 remap_count;
	RSP_UINT32 t_addr[NUM_MAX_REMAP_ENTRY];
	RSP_UINT32 o_addr[NUM_MAX_REMAP_ENTRY];
};

SHRD_host::SHRD_host(HILWrapper *HIL_) {
	HIL = HIL_;
	
	map_entries = (SHRD_MAP *)malloc(sizeof(SHRD_MAP) * SHRD_RW_LOG_SIZE_IN_PAGE);
	memset(map_entries, 0x00, sizeof(SHRD_MAP) * SHRD_RW_LOG_SIZE_IN_PAGE);
	redirection_map = new map<RSP_UINT32, SHRD_MAP *>;

	rw_log_new_idx = 0;
	rw_log_start_idx = 0;
	srand(0xabcdef);
}

void SHRD_host::do_twrite(RSP_UINT32 max_packed_rw) {

	//Make twrite with random writes
	//I/O size is 1 page at not 
	
	RSP_UINT32 LPN, page_count, log_addr, req_pages = 0, padding = 0, idx = 0, num_packing= 0;
	SHRD_TWRITE_HEADER *twrite_header = (SHRD_TWRITE_HEADER *)malloc(sizeof(SHRD_TWRITE_HEADER));
	RSP_UINT32 *buff = (RSP_UINT32 *)malloc(4096);

	log_addr = rw_log_new_idx;

	num_packing = rand() % SHRD_NUM_MAX_TWRITE_ENTRY;
	
	memset(twrite_header, 0x00, sizeof(SHRD_TWRITE_HEADER));
	twrite_header->t_addr_start = log_addr + SHRD_RW_LOG_START_IN_PAGE;
	
	while (1) {

		LPN = (rand() * rand()) % LPN_RANGE;

		page_count = 1; //it can be varied.
		req_pages += page_count;

		if (req_pages > max_packed_rw) {
			req_pages -= page_count;
			break;
		}

		if (req_pages > num_packing) {
			req_pages -= page_count;
			break;
		}

		if ((log_addr & 0x1) != (LPN & 0x1)) {
			//not aligned, need to pad
			if (req_pages + 1 > max_packed_rw) {
				req_pages -= page_count;
				break;
			}
			else {
				req_pages += 1;
				padding++;
				log_addr++;
				twrite_header->o_addr[idx] = RSP_INVALID_LPN;
				twrite_header->io_count++;
				idx++;
			}
		}

		for (RSP_UINT32 i = 0; i < page_count; i++) {
			twrite_header->o_addr[idx] = LPN + i;
			twrite_header->io_count++;
			idx++;
		}
		log_addr += page_count;
	}

	//send twrite_header
	RSP_UINT32 header_cmd = header_entry_num * SHRD_NUM_CORES + SHRD_CMD_START_IN_PAGE;
	header_entry_num++;
	
	if (header_entry_num == SHRD_TWRITE_ENTRIES)
		header_entry_num = 0;

	memcpy_s(buff, 4096, twrite_header, sizeof(SHRD_TWRITE_HEADER));
	HIL->HIL_WriteLPN(header_cmd, 0xff, buff);
	HIL->HIL_WriteLPN(header_cmd + 1, 0xff, buff);

	//send twrite_data
	//need to manage redirection table in here
	for (RSP_UINT32 i = 0; i < twrite_header->io_count; i++) {
		RSP_UINT32 offset = twrite_header->t_addr_start + i - SHRD_RW_LOG_START_IN_PAGE;

		memset(buff, 0xff, 4096);
		if (twrite_header->o_addr[i] == RSP_INVALID_LPN)
			memset(buff, 0xaa, 4096);
		else if (twrite_header->o_addr[i] % 10 == 0)
			memset(buff, 0xff, 4096);
		else if (twrite_header->o_addr[i] % 10 == 1)
			memset(buff, 0x11, 4096);
		else if (twrite_header->o_addr[i] % 10 == 2)
			memset(buff, 0x22, 4096);
		else if (twrite_header->o_addr[i] % 10 == 3)
			memset(buff, 0x33, 4096);
		else if (twrite_header->o_addr[i] % 10 == 4)
			memset(buff, 0x44, 4096);
		else if (twrite_header->o_addr[i] % 10 == 5)
			memset(buff, 0x55, 4096);
		else if (twrite_header->o_addr[i] % 10 == 6)
			memset(buff, 0x66, 4096);
		else if (twrite_header->o_addr[i] % 10 == 7)
			memset(buff, 0x77, 4096);
		else if (twrite_header->o_addr[i] % 10 == 8)
			memset(buff, 0x88, 4096);
		else if (twrite_header->o_addr[i] % 10 == 9)
			memset(buff, 0x99, 4096);

		HIL->HIL_WriteLPN(twrite_header->t_addr_start + i, 0xff, buff);

		if (twrite_header->o_addr[i] == RSP_INVALID_LPN)
			continue;
		
		map_entries[offset].o_addr = twrite_header->o_addr[i];
		map_entries[offset].t_addr = offset + SHRD_RW_LOG_START_IN_PAGE;

		if (map_entries[offset].flags == SHRD_VALID_MAP) {
			printf("ERROR on packing RW\n");
			getchar();
		}
		map_entries[offset].flags = SHRD_VALID_MAP;

		if (redirection_map->find(map_entries[offset].o_addr) != redirection_map->end()) {
			//delete old map entry
			SHRD_MAP *map = redirection_map->find(map_entries[offset].o_addr)->second;
			redirection_map->erase(map_entries[offset].o_addr);
			memset(map, 0x00, sizeof(SHRD_MAP));
		}
		redirection_map->insert(pair<RSP_UINT32, SHRD_MAP *>(twrite_header->o_addr[i], &map_entries[offset]));
		//if (redirection_map->find(map_entries[offset].o_addr) != redirection_map->end()) {
		//	//delete old map entry
		//	SHRD_MAP *map = redirection_map->find(map_entries[offset].o_addr)->second;
		//	printf("!!");
		//}
	}
	rw_log_new_idx = log_addr;

	write_amount += req_pages;

	free(buff);
	free(twrite_header);
}

void SHRD_host::__do_remap(RSP_UINT32 size) {

	RSP_UINT32 idx = 0, cnt = 0;
	RSP_UINT32 start_idx = rw_log_start_idx, end_idx;

	map<RSP_UINT32, SHRD_MAP *>::iterator iter;

	for (idx = 0; idx < size; idx++) {
		RSP_UINT32 log_idx = idx + start_idx;
		if (map_entries[log_idx].flags == SHRD_VALID_MAP)
			cnt++;
	}
	
	if (cnt == 0) {
		rw_log_start_idx = start_idx + idx + 1;
		if (rw_log_start_idx >= SHRD_RW_LOG_SIZE_IN_PAGE)
			rw_log_start_idx -= SHRD_RW_LOG_SIZE_IN_PAGE;
		return;
	}

	end_idx = start_idx + size - 1;

	idx = 0;

	SHRD_REMAP_DATA *remap_entry = (SHRD_REMAP_DATA *)malloc(sizeof(SHRD_REMAP_DATA));
	RSP_UINT32 *buff = (RSP_UINT32 *)malloc(4096);

	memset(remap_entry, 0x00, sizeof(SHRD_REMAP_DATA));

	for (iter = redirection_map->begin(); iter != redirection_map->end(); ++iter) {

		SHRD_MAP *map = iter->second;

		if (map->flags != SHRD_VALID_MAP)
			continue;

		if ((map->t_addr >= start_idx + SHRD_RW_LOG_START_IN_PAGE) && (map->t_addr <= end_idx + SHRD_RW_LOG_START_IN_PAGE)) {

			if (remap_entry->remap_count == SHRD_NUM_MAX_REMAP_ENTRY) {
				//remap entry is full, send it and get new entries
				RSP_UINT32 remap_cmd = remap_entry_num * SHRD_NUM_CORES + SHRD_REMAP_CMD_START_IN_PAGE;
				remap_entry_num++;

				if (remap_entry_num == SHRD_TWRITE_ENTRIES)
					remap_entry_num = 0;

				memcpy_s(buff, 4096, remap_entry, sizeof(SHRD_REMAP_DATA));
				HIL->HIL_WriteLPN(remap_cmd, 0xff, buff);
				HIL->HIL_WriteLPN(remap_cmd + 1, 0xff, buff);
				memset(buff, 0x00, 4096);

				for (RSP_UINT32 i = 0; i < remap_entry->remap_count; i++) {
					RSP_UINT32 offset = remap_entry->t_addr[i] - SHRD_RW_LOG_START_IN_PAGE;
					redirection_map->erase(remap_entry->o_addr[i]);
					map_entries[offset].flags = SHRD_INVALID_MAP;
				}
				memset(remap_entry, 0x00, sizeof(SHRD_REMAP_DATA));
			}

			remap_entry->o_addr[remap_entry->remap_count] = map->o_addr;
			remap_entry->t_addr[remap_entry->remap_count] = map->t_addr;
			map->flags = SHRD_REMAPPING_MAP;
			remap_entry->remap_count++;
			idx++;
		}

		if (idx == cnt) {
			RSP_UINT32 remap_cmd = remap_entry_num * SHRD_NUM_CORES + SHRD_REMAP_CMD_START_IN_PAGE;
			remap_entry_num++;

			if (remap_entry_num == SHRD_TWRITE_ENTRIES)
				remap_entry_num = 0;

			memcpy_s(buff, 4096, remap_entry, sizeof(SHRD_REMAP_DATA));
			HIL->HIL_WriteLPN(remap_cmd, 0xff, buff);
			HIL->HIL_WriteLPN(remap_cmd + 1, 0xff, buff);
			memset(buff, 0x00, 4096);

			for (RSP_UINT32 i = 0; i < remap_entry->remap_count; i++) {
				RSP_UINT32 offset = remap_entry->t_addr[i] - SHRD_RW_LOG_START_IN_PAGE;
				redirection_map->erase(remap_entry->o_addr[i]);
				map_entries[offset].flags = SHRD_INVALID_MAP;
			}
			break;
		}
	}
	printf("r");

	rw_log_start_idx = end_idx + 1;
	if (rw_log_start_idx >= SHRD_RW_LOG_SIZE_IN_PAGE)
		rw_log_start_idx -= SHRD_RW_LOG_SIZE_IN_PAGE;

	free(remap_entry);
	free(buff);
}

RSP_UINT32 SHRD_host::do_remap() {
	
	RSP_UINT32 start_idx = rw_log_start_idx, new_idx = rw_log_new_idx;
	RSP_UINT32 size;

	(new_idx >= start_idx) ? (size = new_idx - start_idx) : (size = SHRD_RW_LOG_SIZE_IN_PAGE - start_idx + new_idx);

	if (size > remap_threshold) {
		//need to remap
		if (new_idx < start_idx)
			size = SHRD_RW_LOG_SIZE_IN_PAGE - start_idx;
			//to protect reversing of circular q at single remap command.
		if (size > remap_size)
			size = remap_size;

		__do_remap(size);
		return 0;
	}
	else
		//no need for the remap
		return 1;

}

int SHRD_host::HOST_gen_random_workload() {

	RSP_UINT32 max_packed_rw;

	max_packed_rw = SHRD_MAX_TWRITE_IO_SIZE_IN_PAGE;

	//make twrites
	if (rw_log_new_idx >= rw_log_start_idx) {
		if (SHRD_RW_LOG_SIZE_IN_PAGE - rw_log_new_idx >= SHRD_MIN_RW_LOGGING_IO_SIZE_IN_PAGE){

			max_packed_rw = SHRD_RW_LOG_SIZE_IN_PAGE - rw_log_new_idx; //page to sector
			if (max_packed_rw > SHRD_MAX_TWRITE_IO_SIZE_IN_SECTOR)
				max_packed_rw = SHRD_MAX_TWRITE_IO_SIZE_IN_SECTOR;
		}
		else {
			if (rw_log_start_idx >= SHRD_MAX_TWRITE_IO_SIZE_IN_PAGE) {
				rw_log_new_idx = 0; //goto log end because enoulgh slot is presented
			}
			else {
				//need remap
				RSP_UINT32 rtn = do_remap();
				if (rtn)
					//should do remap, but didn't
					getchar();
				return 0;
			}
		}
	}
	else {
		if ((rw_log_start_idx - rw_log_new_idx) >= SHRD_MIN_RW_LOGGING_IO_SIZE_IN_PAGE) {
			max_packed_rw = rw_log_start_idx - rw_log_new_idx; //page to sector
			if (max_packed_rw > SHRD_MAX_TWRITE_IO_SIZE_IN_PAGE)
				max_packed_rw = SHRD_MAX_TWRITE_IO_SIZE_IN_PAGE;
		}
		else {
			// need remap
			RSP_UINT32 rtn = do_remap();
			if (rtn)
				//should do remap, but didn't
				getchar();
			return 0;
		}
	}

	do_twrite(max_packed_rw);

	return 0;
}

void SHRD_host::HOST_verify_lpn(RSP_UINT32 lpn) {

	RSP_UINT32 *buff = (RSP_UINT32 *)malloc(4096);

	if (lpn % 2 == 0)
		HIL->pATLWrapper[0]->RSP_ReadPage(lpn, lpn / 2, 0xff, buff);
	else
		HIL->pATLWrapper[1]->RSP_ReadPage(lpn, lpn / 2, 0xff, buff);

	if (*buff == 0x00000000 || *buff == 0xcdcdcdcd)
		return;

	switch (lpn % 10) {
	case 0:
		if (*buff != 0xffffffff)
			HOST_ASSERT(0);
		break;
	case 1:
		if (*buff != 0x11111111)
			HOST_ASSERT(0);
		break;
	case 2:
		if (*buff != 0x22222222)
			HOST_ASSERT(0);
		break;
	case 3:
		if (*buff != 0x33333333)
			HOST_ASSERT(0);
		break;
	case 4:
		if (*buff != 0x44444444)
			HOST_ASSERT(0);
		break;
	case 5:
		if (*buff != 0x55555555)
			HOST_ASSERT(0);
		break;
	case 6:
		if (*buff != 0x66666666)
			HOST_ASSERT(0);
		break;
	case 7:
		if (*buff != 0x77777777)
			HOST_ASSERT(0);
		break;
	case 8:
		if (*buff != 0x88888888)
			HOST_ASSERT(0);
		break;
	case 9:
		if (*buff != 0x99999999)
			HOST_ASSERT(0);
		break;
	}
}

void SHRD_host::HOST_verify_random_workload() {

	RSP_UINT32 *buff = (RSP_UINT32 *)malloc(4096);
	printf("\nVerifying\n");

	for (RSP_UINT32 i = 0; i < LPN_RANGE; i++) {

		memset(buff, 0x00, 4096);

		//HIL->HIL_ReadLPN(i, i, 0xff, buff);
		if (i % 2 == 0)
			HIL->pATLWrapper[0]->RSP_ReadPage(i, i / 2, 0xff, buff);
		else
			HIL->pATLWrapper[1]->RSP_ReadPage(i, i / 2, 0xff, buff);

		if (i % 10000 == 0)
			printf("-");
		if (*buff == 0x00000000 || *buff == 0xcdcdcdcd)
			continue;

		switch (i % 10) {
		case 0: 
			if (*buff != 0xffffffff)
				HOST_ASSERT(0);
			break;
		case 1:
			if (*buff != 0x11111111)
				HOST_ASSERT(0);
			break;
		case 2: 
			if (*buff != 0x22222222)
				HOST_ASSERT(0);
			break;
		case 3:
			if (*buff != 0x33333333)
				HOST_ASSERT(0);
			break;
		case 4:
			if (*buff != 0x44444444)
				HOST_ASSERT(0);
			break;
		case 5:
			if (*buff != 0x55555555)
				HOST_ASSERT(0);
			break;
		case 6:
			if (*buff != 0x66666666)
				HOST_ASSERT(0);
			break;
		case 7:
			if (*buff != 0x77777777)
				HOST_ASSERT(0);
			break;
		case 8:
			if (*buff != 0x88888888)
				HOST_ASSERT(0);
			break;
		case 9:
			if (*buff != 0x99999999)
				HOST_ASSERT(0);
			break;
		}
	}
	//free(buff);
	printf("verify end\n");
}

RSP_BOOL SHRD_host::HOST_Write(RSP_UINT32 SectAddr, RSP_UINT32 SectCount, RSP_UINT32 *buff) {
	return 0;
}

RSP_BOOL SHRD_host::HOST_Read(RSP_UINT32 SectAddr, RSP_UINT32 SectCount, RSP_UINT32 *buff) {
	return 0;
}