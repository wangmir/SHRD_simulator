#include <cstddef>
#include <new>
//#define __NAND_virtual
#ifndef __NAND_virtual
#include "RSP_Header.h"

#include "RSP_OSAL.h"

#include "VFLWrapper.h"
#include "ATLWrapper.h"
#else
#include "ATLWrapper.h"
#endif 
#include <stdlib.h>



RSP_UINT32 ATLTESTVALUE0;
RSP_UINT32 ATLTESTVALUE1;
RSP_UINT32 ATLTESTVALUE2;

namespace Hesper
{
	RSP_UINT32 NUM_LBLK;
	RSP_UINT32 NUM_PBLK;
	//RSP_UINT32 CMT_size = 1024 * KB; //2MB
	RSP_UINT32 CMT_size = 256*KB; //2MB
	RSP_UINT8 Flush_method = META_FLUSH;
	RSP_UINT32 *DB_GC;

	static RSP_BOOL BG_IC_enabled = false;//true;
#ifdef __NAND_virtual
	//for test
	ATLWrapper::ATLWrapper(VFLWrapper* pVFL, RSP_UINT8 core_id)
	{
		m_pVFLWrapper = pVFL;
		__COREID__ = core_id;
	}
#endif
	//overloading function for simulator
	ATLWrapper::ATLWrapper(VFLWrapper* pVFL, RSP_UINT32 CORE_ID) {

		m_pVFLWrapper = pVFL;
		_COREID_ = CORE_ID;
	}

	ATLWrapper::ATLWrapper(VFLWrapper* pVFL)
	{
		m_pVFLWrapper = pVFL;
	}

	ATLWrapper::~ATLWrapper(RSP_VOID)
	{

	}
//#define VC_debug_JBD
#ifdef VC_debug_JBD
	
	static special_command* debug_sc;
	static RSP_UINT32 debug_sc_counter = 0;
	
	static RSP_UINT32 last_remap_address_debug = 0;

	static RSP_UINT32 debug_ppn[400];
	static RSP_UINT32 debug_lpn[400];
	static RSP_UINT32 debug_counter = 0;

	#define JBD_COUNTER (131071)
	#define JBD_START	(2113665)
	#define JBD_END		(2244735)
	static RSP_UINT32 *JBD_state;
#endif
	// map read
	RSP_VOID ATLWrapper::map_read(RSP_UINT32 map_offset, RSP_UINT32 cache_offset, RSP_UINT8 type, RSP_UINT32 cache_type)
	{
		RSP_UINT32 ppn, channel, bank, plane, block_offset;
		RSPReadOp RSP_read_op;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(map_offset < NUM_MAP_ENTRY(cache_type));
#endif
		ppn = MAP_MAPPING_TABLE[cache_type][map_offset];
		if (ppn == (RSP_UINT32)VC_MAX)
		{
			RSPOSAL::RSP_MemSet((RSP_UINT32 *)add_addr(CACHE_ADDR[cache_type], BYTES_PER_SUPER_PAGE * cache_offset), 0xff, BYTES_PER_SUPER_PAGE);
			return;
		}
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(ppn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK(cache_type));
#endif
		bank = map_offset / NUM_MAP_ENTRY_PER_BANK(cache_type);
		channel = bank % NAND_NUM_CHANNELS;
		bank = bank / NAND_NUM_CHANNELS;

		block_offset = ppn / PAGES_PER_BLK;
		//READ Request
		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_read_op.pData = (RSP_UINT32 *)add_addr(CACHE_ADDR[cache_type], BYTES_PER_SUPER_PAGE * cache_offset + plane * RSP_BYTES_PER_PAGE);
			RSP_read_op.nReqID = RSP_INVALID_RID;
			//RSP_read_op.pSpareData = NULL;
			RSP_read_op.nChannel = channel;
			RSP_read_op.nBank = bank;
			RSP_read_op.nBlock = NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][block_offset] * PLANES_PER_BANK + plane;
			RSP_read_op.nPage = ppn % PAGES_PER_BLK;
			RSP_read_op.bmpTargetSector = 0xffff;
			RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, 0);
			RSP_read_op.m_nLPN = RSP_INVALID_LPN;
			m_pVFLWrapper->INC_READPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 2);
			m_pVFLWrapper->MetaIssue(RSP_read_op);
			

			//Read Complete
		}
		m_pVFLWrapper->WAIT_READPENDING();
		//Check_cache_slot((RSP_UINT32*)add_addr(CACHE_ADDR, cache_offset * BYTES_PER_SUPER_PAGE));

		if(cache_type == L2P)
		{
			if(type == Prof_Read)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Read_Map_load, 1);
			else if(type == Prof_Write)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Write_Map_load, 1);
			else if(type == Prof_FGC)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_FGC_Map_load, 1);
			else if(type == Prof_BGC)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_BGC_Map_load, 1);
			else if(type == Prof_Remap)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Map_load, 1);
			else if (type == Prof_Trim)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Trim_Map_load, 1);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_Total_load, 1);
		}
		else
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_Total_load, 1);
		return;
	}

	RSP_VOID ATLWrapper::map_write(RSP_UINT32 map_offset, RSP_UINT32 cache_offset, RSP_UINT8 type, RSP_UINT32 cache_type)
	{
		RSP_UINT32 channel, plane, old_ppn, victim_blk = 0, block_offset, i, bank;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];

		bank = map_offset / NUM_MAP_ENTRY_PER_BANK(cache_type);
		channel = bank % NAND_NUM_CHANNELS;
		bank = bank / NAND_NUM_CHANNELS;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(map_offset < NUM_MAP_ENTRY(cache_type));
#endif
		if(cache_type == P2L && P2L_VALID_COUNT[map_offset]== 0)
		{
			old_ppn = MAP_MAPPING_TABLE[cache_type][map_offset];
			block_offset = old_ppn / PAGES_PER_BLK;
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(old_ppn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK(cache_type) || old_ppn == (RSP_UINT32)VC_MAX);
#endif
			if (old_ppn != (RSP_UINT32)VC_MAX)
				set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) - 1, cache_type);

			MAP_MAPPING_TABLE[cache_type][map_offset] = VC_MAX;
			return;
		}
		if (NAND_bank_state[channel][bank].map_blk_list[cache_type].count == 0 && NAND_bank_state[channel][bank].cur_map_ppn[cache_type] % PAGES_PER_BLK == PAGES_PER_BLK - 1)
		{
			while(map_incremental_garbage_collection( channel, bank, cache_type) != 2);
		}
		if(NAND_bank_state[channel][bank].cur_map_ppn[cache_type] % PAGES_PER_BLK == PAGES_PER_BLK - 1)
		{
			RSP_UINT32 temp_blk;
			temp_blk = get_free_blk(&NAND_bank_state[channel][bank].map_blk_list[cache_type]);
			for(i=0;i< MAP_ENTRY_BLK_PER_BANK(cache_type);i++)
				if(NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][i] == temp_blk)
				{
					victim_blk = i;
					break;
				}
			MAP_VALID_COUNT[cache_type][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) + victim_blk] = 0;
			NAND_bank_state[channel][bank].cur_map_ppn[cache_type] = victim_blk * PAGES_PER_BLK;
			//map write 	
			old_ppn = MAP_MAPPING_TABLE[cache_type][map_offset];
			block_offset = old_ppn / PAGES_PER_BLK;
		
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(old_ppn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK(cache_type) || old_ppn == (RSP_UINT32)VC_MAX);
#endif
			if (old_ppn != (RSP_UINT32)VC_MAX)
				set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) - 1, cache_type);
			block_offset = NAND_bank_state[channel][bank].cur_map_ppn[cache_type] / PAGES_PER_BLK;
			set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) + 1, cache_type);

			MAP_MAPPING_TABLE[cache_type][map_offset] = NAND_bank_state[channel][bank].cur_map_ppn[cache_type];
			old_ppn = NAND_bank_state[channel][bank].cur_map_ppn[cache_type];
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_ppn[cache_type] < TOTAL_MAP_BLK(cache_type) * PAGES_PER_BLK);
#endif
			MAPP2L[cache_type][((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_ppn[cache_type]] = map_offset;

		}
		else
		{
			//map write
			old_ppn = MAP_MAPPING_TABLE[cache_type][map_offset];
			block_offset = old_ppn / PAGES_PER_BLK;
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(old_ppn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK(cache_type) || old_ppn == (RSP_UINT32)VC_MAX);
#endif
			if (old_ppn != (RSP_UINT32)VC_MAX)
				set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) - 1, cache_type);

			if (NAND_bank_state[channel][bank].map_start[cache_type])
				NAND_bank_state[channel][bank].cur_map_ppn[cache_type]++;
			else
				NAND_bank_state[channel][bank].map_start[cache_type] = true;

			block_offset = NAND_bank_state[channel][bank].cur_map_ppn[cache_type] / PAGES_PER_BLK;
			set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) + 1, cache_type);

			MAP_MAPPING_TABLE[cache_type][map_offset] = NAND_bank_state[channel][bank].cur_map_ppn[cache_type];
			old_ppn = NAND_bank_state[channel][bank].cur_map_ppn[cache_type];
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_ppn[cache_type] < TOTAL_MAP_BLK(cache_type) * PAGES_PER_BLK);
#endif
			MAPP2L[cache_type][((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_ppn[cache_type]] = map_offset;


		}


		//WRITE REQUEST

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(CACHE_ADDR[cache_type], BYTES_PER_SUPER_PAGE * cache_offset + plane * RSP_BYTES_PER_PAGE);
			RSP_write_ops[plane].pSpareData = NULL_SPARE;
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].nBlock = NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][block_offset] * PLANES_PER_BANK + plane;
			RSP_write_ops[plane].nPage = old_ppn % PAGES_PER_BLK;
			RSP_write_ops[plane].bmpTargetSector = 0xffff;
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
		}

		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, PLANES_PER_BANK * LPAGE_PER_PPAGE);
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif

		if(cache_type == L2P)
		{
			if (type == Prof_Read)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Read_Map_log, 1);
			else if (type == Prof_Write)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Write_Map_log, 1);
			else if (type == Prof_FGC)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_FGC_Map_log, 1);
			else if (type == Prof_BGC)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_BGC_Map_log, 1);
			else if (type == Prof_Remap)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Map_log, 1);
			else if (type == Prof_Trim)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Trim_Map_log, 1);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_Total_log, 1);
		}
		else
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_Total_log, 1);

		return;
	}

	RSP_UINT32 ATLWrapper::map_incremental_garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 cache_type)
	{
		RSP_UINT32 plane, vt_block, free_ppn, src_page, old_ppn;
		RSP_UINT32 src_lpn, i, temp_blk;
		RSP_UINT32 vcount = VC_MAX, block_offset;
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSP_BOOL copy_one_block = false;
		RSP_UINT8 return_val = 1;

		if(NAND_bank_state[channel][bank].MAP_GC_victim_blk[cache_type] == VC_MAX)
		{
			for (i = 0; i < MAP_ENTRY_BLK_PER_BANK(cache_type); i++)
			{
				if(i == NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] || i == NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type] / PAGES_PER_BLK)
					continue;
				if(i == NAND_bank_state[channel][bank].cur_map_ppn[cache_type] / PAGES_PER_BLK)
					continue;
			
				if (vcount > get_map_vcount(channel, bank, i, cache_type))
				{
					vcount = get_map_vcount(channel, bank, i, cache_type);
					vt_block = i;
				}
			}
			
			NAND_bank_state[channel][bank].MAP_GC_victim_blk[cache_type] = vt_block;
			if(NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type]  == VC_MAX)
			{
				NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type] = NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] * PAGES_PER_BLK;
				NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] = VC_MAX;
			}
			NAND_bank_state[channel][bank].MAP_GC_src_page[cache_type] = 0;
		}
		else
		{
			vt_block = NAND_bank_state[channel][bank].MAP_GC_victim_blk[cache_type];
		}
		free_ppn = NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type];
		src_page = NAND_bank_state[channel][bank].MAP_GC_src_page[cache_type];


		for(;!copy_one_block && src_page != PAGES_PER_BLK;src_page++)
		{
			//page copy
			src_lpn = MAPP2L[cache_type][((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + vt_block * PAGES_PER_BLK + src_page];

			if (src_lpn == (RSP_UINT32)VC_MAX)
					continue;

			old_ppn = MAP_MAPPING_TABLE[cache_type][src_lpn];

			if (old_ppn == (RSP_UINT32)VC_MAX)
				continue;


			if (old_ppn == vt_block * PAGES_PER_BLK + src_page)
			{
				copy_one_block = true;
				MAP_MAPPING_TABLE[cache_type][src_lpn] = free_ppn;
				MAPP2L[cache_type][((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) * PAGES_PER_BLK) + free_ppn++] = src_lpn;
					
				//READ_REQUEST
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].cpybuf_addr[1], plane * RSP_BYTES_PER_PAGE);
					RSP_read_op.nReqID = RSP_INVALID_RID;
					//RSP_read_op.pSpareData = NULL;
					RSP_read_op.nChannel = channel;
					RSP_read_op.nBank = bank;
					RSP_read_op.nBlock = get_block(NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block] * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + old_ppn % PAGES_PER_BLK);
					RSP_read_op.nPage = old_ppn % PAGES_PER_BLK;
					RSP_read_op.bmpTargetSector = 0xffff;
					RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, 0);
					RSP_read_op.m_nLPN = RSP_INVALID_LPN;
					m_pVFLWrapper->INC_READPENDING();
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 2);
					if(cache_type == L2P)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_GC_read, 2);
					else
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_GC_read, 2);
					m_pVFLWrapper->MetaIssue(RSP_read_op);
					
					//Read Complete
				}
				m_pVFLWrapper->WAIT_READPENDING();
				//WRITE_REQUEST
				block_offset = MAP_MAPPING_TABLE[cache_type][src_lpn] / PAGES_PER_BLK;
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].cpybuf_addr[1], plane * RSP_BYTES_PER_PAGE);
					RSP_write_ops[plane].pSpareData = NULL_SPARE;
					RSP_write_ops[plane].nChannel = channel;
					RSP_write_ops[plane].nBank = bank;
					RSP_write_ops[plane].nBlock = get_block(NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][block_offset] * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + MAP_MAPPING_TABLE[cache_type][src_lpn] % PAGES_PER_BLK);
					RSP_write_ops[plane].nPage = MAP_MAPPING_TABLE[cache_type][src_lpn] % PAGES_PER_BLK;
					RSP_write_ops[plane].bmpTargetSector = 0xffff;
					RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
					RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
					RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
					RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
				}
				m_pVFLWrapper->INC_PROGRAMPENDING();
				
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				if(cache_type == L2P)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_GC_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_GC_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				m_pVFLWrapper->MetaIssue(RSP_write_ops);
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif
				set_map_vcount(channel, bank, block_offset, get_map_vcount(channel, bank, block_offset, cache_type) + 1, cache_type);
			}
		}


		if(free_ppn / PAGES_PER_BLK !=  NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type] / PAGES_PER_BLK)
		{
			free_ppn = NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] * PAGES_PER_BLK;
			NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] = VC_MAX;
			NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type] = free_ppn;
		}

		if(src_page == PAGES_PER_BLK)
		{
			NAND_bank_state[channel][bank].MAP_GC_victim_blk[cache_type] = VC_MAX;
			
			if(NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] != VC_MAX)
			{//block change
				return_val = 2;
				temp_blk = get_free_blk(&NAND_bank_state[channel][bank].free_blk_list);
			
				if(temp_blk != VC_MAX)
				{
					add_free_blk(&NAND_bank_state[channel][bank].free_blk_list, &NAND_bank_state[channel][bank].blk_list[NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block]]);
					NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block] = temp_blk;
					erase_wrapper(channel, bank, temp_blk);
				}
				else
					erase_wrapper(channel, bank, NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block]);
				
				set_map_vcount(channel, bank, vt_block, (RSP_UINT32)VC_MAX, cache_type);
				add_free_blk(&NAND_bank_state[channel][bank].map_blk_list[cache_type], &NAND_bank_state[channel][bank].blk_list[NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block]]);
			}
			else
			{
				erase_wrapper(channel, bank, NAND_bank_state[channel][bank].MAP_blk_offset[cache_type][vt_block]);
	
				NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type] = vt_block;
				set_map_vcount(channel, bank, NAND_bank_state[channel][bank].MAP_GC_BLK[cache_type], 0, cache_type);
			}
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
			if(cache_type == L2P)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_erase, 1);
			else
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_erase, 1);

		}
		NAND_bank_state[channel][bank].MAP_GC_src_page[cache_type] = src_page;
		NAND_bank_state[channel][bank].MAP_GC_free_page[cache_type] = free_ppn;
		return return_val;

	}


	//vcount management
	RSP_UINT32 ATLWrapper::get_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 cache_type)
	{
		RSP_UINT32 vcount = MAP_VALID_COUNT[cache_type][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) + block];
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) + block < TOTAL_MAP_BLK(cache_type));
		RSP_ASSERT(vcount <= PAGES_PER_BLK || vcount == (RSP_UINT32)VC_MAX);
#endif
		return vcount;
	}
	RSP_VOID ATLWrapper::set_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount, RSP_UINT32 cache_type)
	{
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) + block < TOTAL_MAP_BLK(cache_type));
		RSP_ASSERT(vcount <= PAGES_PER_BLK || vcount == (RSP_UINT32)VC_MAX);
#endif
		MAP_VALID_COUNT[cache_type][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(cache_type) + block] = vcount;
		return;
	}
	///////////////////////////////////////////////////////////////////////////////

	RSP_VOID ATLWrapper::ATL_initialize_manage(RSP_UINT8 L2P_size, RSP_UINT8 P2L_size, RSP_UINT8 flush_type, RSP_UINT8 init_type, RSP_BOOL BG_IC)
	{
		RSP_UINT32 new_CMT_size, loop, input_map_size[2] = {L2P_size, P2L_size};
		flush_bank_start = false;
		if(init_type == SEQ80)
			initialize_handler(8,0,0);
		else if(init_type == SEQ80_RAND50_30)
			initialize_handler(8,5,3);			
		else if(init_type == SEQ50_RAND50_80)
			initialize_handler(5,5,8);
		else if(init_type == RAND_50_110)
			initialize_handler(0,5,11);
		else if(init_type == RAND_80_110)
			initialize_handler(0,8,11);
		flush_bank_start = true;

		for(loop =1; loop < 2; loop ++)
		{
			new_CMT_size = input_map_size[loop] * 256 * KB;

			map_flush();
			num_cached[loop] = 0;
			CACHED_MAP_HEAD[loop] = NULL;
			RSPOSAL::RSP_MemSet(CACHE_ADDR[loop], 0xff, new_CMT_size);
			NUM_CACHED_MAP[loop] = new_CMT_size / (BYTES_PER_SUPER_PAGE);
		}

		Flush_method = flush_type;


		BG_IC_enabled = BG_IC;
		if(SM_value != NULL)
			SM_value->value[51] = BG_IC;

		for(loop = 0; loop < Prof_total_num;loop++)
			m_pVFLWrapper->RSP_SetProfileData(loop,0);
	}
	RSP_VOID ATLWrapper::initialize_handler(RSP_UINT32 SEQ_TH, RSP_UINT32 RAND_TH, RSP_UINT32 RAND_COUNT)
	{
		RSP_UINT32 iner_iter = 0, iter = 0, lpn, lpn_th, start_lpn = 1024, rand_max_iter;
		//SEQ
		lpn_th = SPECIAL_COMMAND_PAGE * SEQ_TH / 10;
		for(lpn = start_lpn; lpn < lpn_th; lpn++)
			meta_write_page(lpn, NULL, false, true);
		//RAND
		rand_max_iter = SPECIAL_COMMAND_PAGE / 1000;
		lpn_th = SPECIAL_COMMAND_PAGE * RAND_TH / 10;
		srand(0);
		for(iter = 0; iter < RAND_COUNT * 100; iter++)
		{
			lpn = rand();
			lpn %= (lpn_th - start_lpn - rand_max_iter);
			lpn += start_lpn;
			for(iner_iter = 0; iner_iter < rand_max_iter; iner_iter++)
			{
				meta_write_page(lpn + iner_iter, NULL, false, true);
			}
		}
		return;
		
	}
	RSP_BOOL remap_start;
	//VC-VM
	RSP_VOID ATLWrapper::Special_command_handler(RSP_UINT32 *BufferAddress)
	{
		RSP_UINT32 iter = 0;
		special_command* temp_sc;
#ifdef VC_debug_JBD

		static RSP_UINT32 JBD_last_index = 0;
		RSP_UINT32 last_iter = ((sc->command_entry[sc->command_count-1].src_LPN - THIS_CORE) / 2 + 1 - JBD_START);
		RSPOSAL::RSP_MemCpy((RSP_UINT32*)&debug_sc[debug_sc_counter], (RSP_UINT32*)sc, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		debug_sc_counter = (debug_sc_counter + 1 ) % 5000;
		for(;JBD_last_index != last_iter;JBD_last_index++)
		{
			if(JBD_last_index > (JBD_END - JBD_START))
			{
				JBD_last_index = 0;
				if(last_iter == 0)
					break;
			}
			
			RSP_ASSERT(JBD_state[JBD_last_index] == 1);
			JBD_state[JBD_last_index] = 0;
		}
#endif
		remap_start = true;
		dbg4++;
		SM_value->value[27 + THIS_CORE] = 1;
		SM_value->value[52 + THIS_CORE]++;
		while(SM_value->value[52] != SM_value->value[53])
			Check_other_core_read();
		//CORE1 make read mode
		if (THIS_CORE == 0)
		{
			SM_value->value[35] = 1;
			SM_value->value[25] = 1;
			
		}
		else
		{
			SM_value->value[36] = 1;
			SM_value->value[26] = 1;
		}

		real_copy_count = 0;
		sc_other->command_count = 0;
		if(sc->command_count < MAX_COMMAND)
		{
		sc->command_entry[sc->command_count].src_LPN = 0;
		sc->command_entry[sc->command_count].dst_LPN = 0;
		}
		for (iter = 0; iter < sc->command_count; iter++)
		{ 
		
			if (sc->command_entry[iter].src_LPN % NUM_FTL_CORE != THIS_CORE)  //
				continue;


			if (sc->command_entry[iter].dst_LPN % NUM_FTL_CORE != THIS_CORE)
			{//for other core
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_VC_Intercore_count, 1);
				sc_other->command_entry[sc_other->command_count].dst_LPN = _FTL_ReadData2Buffer(sc->command_entry[iter].src_LPN / NUM_FTL_CORE, sc->command_type[iter]);
				sc_other->command_type[sc_other->command_count] = sc->command_type[iter];
				sc_other->command_entry[sc_other->command_count++].src_LPN = sc->command_entry[iter].dst_LPN;

			}
			else
			{
				if (sc->command_type[iter])
				{
					Virtual_copy(sc->command_entry[iter].dst_LPN / NUM_FTL_CORE, sc->command_entry[iter].src_LPN / NUM_FTL_CORE);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_VC_count, 1);
				}
				else
				{
					Virtual_move(sc->command_entry[iter].dst_LPN / NUM_FTL_CORE, sc->command_entry[iter].src_LPN / NUM_FTL_CORE);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_VM_count, 1);
				}
			}
		}
		
		//SM_value->value[4] = 0;
		if (THIS_CORE == 0)
		{
			while (SM_value->value[26] == 0 && SM_value->value[36] == 0)
			{
				Check_other_core_read();
			}
			SM_value->value[26] = 0;
			SM_value->value[36] = 0;
			while (SM_value->value[25] == 1 && SM_value->value[35] == 1);

			temp_sc = (special_command*)SM_value->value[4];
		}
		else
		{
			while (SM_value->value[25] == 0 && SM_value->value[35] == 0)
			{
				Check_other_core_read();
			}
			SM_value->value[25] = 0;
			SM_value->value[35] = 0;
			while (SM_value->value[26] == 1 && SM_value->value[36] == 1);
			
			temp_sc = (special_command*)SM_value->value[3];
		}

		//IC handling
		for (iter = 0; iter < temp_sc->command_count; iter++)
		{
				if(temp_sc->command_type[iter] == 2)
				{
					RSP_UINT32 channel, bank, block, temp_ppn;
					temp_ppn = temp_sc->command_entry[iter].dst_LPN;
					channel = get_channel_from_ppn(temp_ppn);
					bank = get_bank_from_ppn(temp_ppn);
					block = get_super_block(temp_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
					set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);
				}
				else
					remap_inter_core(temp_sc->command_entry[iter].src_LPN, temp_sc->command_entry[iter].dst_LPN);
		}

		
		
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_command_count, 1);
		sc->command_count = real_copy_count;
		if(real_copy_count)
		{
			if(pending_VC_count == 0)
			{
				cur_VC_lpn = 0;
				free_VC_lpn = 0;
				RSPOSAL::RSP_MemCpy((RSP_UINT32*)cur_VC_struct, (RSP_UINT32*)sc, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
			}
			else
			{
				if(pending_VC_count == MAX_PENDDING_VC)
				{
					//cur 처리  && read_new_VC
					complete_cur_VC();
					SM_value->value[18 + THIS_CORE]--;
					pending_VC_count--;
					SM_value->value[45 + THIS_CORE] = 0;
					SM_value->value[9] = 1;
				}
				free_VC_lpn = (free_VC_lpn +1) % MAX_PENDDING_VC;
			}
			SM_value->value[18 + THIS_CORE]++;
			pending_VC_count++;
		
			meta_write_page(SPECIAL_COMMAND_PAGE + 2 + free_VC_lpn, (RSP_UINT32*)sc, true, false);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Struct_write, 1);
			meta_buffer_flush();
		}
		//FLUSH
		if (sc->flush_enable)
				flush_Remap();
		SM_value->value[27 + THIS_CORE] = 0;
	}

	RSP_VOID ATLWrapper::dec_valid_count(RSP_UINT32 input_lpn)
	{
		RSP_UINT32 channel, bank, block, old_ppn, temp_ppn;
		RSP_BOOL dec_valid = false;
		old_ppn = get_ppn(input_lpn, Prof_Remap);

		if (is_in_virtual(old_ppn) && old_ppn != VC_MAX)
		{//virtual list
			old_ppn ^= VIRTUAL_BIT;
			temp_ppn = get_P2L(old_ppn, Prof_Remap);
			if (is_in_virtual(temp_ppn) || !del_list(old_ppn, input_lpn))
			{
				if (is_in_virtual(temp_ppn))
					set_P2L(old_ppn, VC_MAX, Prof_Remap);
				channel = get_channel_from_ppn(old_ppn);
				bank = get_bank_from_ppn(old_ppn);
				block = get_super_block(old_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
				dec_valid = true;
			}

		}
		else if (old_ppn != VC_MAX)
		{
			channel = get_channel_from_ppn(old_ppn);
			bank = get_bank_from_ppn(old_ppn);
			block = get_super_block(old_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));

			if (!is_in_realcopy(old_ppn))
				dec_valid = true;
		}
#ifdef ATL_ASSERTION_TEST
		else if (is_in_write_buffer(old_ppn) && old_ppn != VC_MAX)
		{//in_buffer
			RSP_ASSERT(0);
		}
#endif
		if(dec_valid)
				{
					set_vcount(channel, bank, block, get_vcount(channel, bank, block) - 1);
					clear_valid(channel, bank, (old_ppn) % (PAGES_PER_BANK * LPAGE_PER_PPAGE));
				}

	}
	RSP_VOID ATLWrapper::set_realcopy(RSP_UINT32 src_lpn, RSP_UINT32 dst_lpn)
	{
		if(is_in_realcopy(dst_lpn) && sc->command_type[real_copy_count])
			{
				RSP_ASSERT(0);
			}
		sc->command_entry[real_copy_count].src_LPN = src_lpn;
		sc->command_entry[real_copy_count++].dst_LPN = dst_lpn;
	}
	RSP_VOID ATLWrapper::remap_inter_core(RSP_UINT32 dst_LPN, RSP_UINT32 input_ppn)
	{
		RSP_UINT32 temp_ppn;
		dec_valid_count(dst_LPN / NUM_FTL_CORE);

		temp_ppn = input_ppn;
		if (temp_ppn == VC_MAX)
		{
			set_ppn(dst_LPN / LPAGE_PER_PPAGE, VC_MAX, Prof_Remap);
		}
		else if(is_in_realcopy(temp_ppn))
		{
			RSP_UINT32 channel, bank, block;
			temp_ppn ^= REALCOPY_BIT;
			channel = get_channel_from_ppn(temp_ppn);
			bank = get_bank_from_ppn(temp_ppn);
			block = get_super_block(temp_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
			set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);

			set_ppn(dst_LPN / LPAGE_PER_PPAGE, temp_ppn, Prof_Remap);
			sc->command_type[real_copy_count] = 1;
			set_realcopy(dst_LPN, temp_ppn);
		}
		else
		{
			temp_ppn ^= REALCOPY_BIT;
			set_ppn(dst_LPN / LPAGE_PER_PPAGE, temp_ppn, Prof_Remap);
			sc->command_type[real_copy_count] = 0;
			set_realcopy(dst_LPN, temp_ppn);
		}
	}

	RSP_VOID ATLWrapper::Virtual_copy(RSP_UINT32 dst_LPN, RSP_UINT32 src_LPN)
	{
		RSP_UINT32 old_ppn, temp_ppn;
		RSP_BOOL  alloc = false;

		dec_valid_count(dst_LPN);

		//V2P table 
		old_ppn = get_ppn(src_LPN, Prof_Remap);

		if (old_ppn == VC_MAX)
		{
			set_ppn(dst_LPN, VC_MAX, Prof_Remap);
		}
		else if (is_in_write_buffer(old_ppn))
		{
			RSP_ASSERT(0);
		}
		else if (is_in_virtual(old_ppn))
		{//lpn list

			if (free_list_count < 2)
			{//real copy same core
				RSP_UINT32 channel, bank, block;
				channel = get_channel_from_ppn(old_ppn ^ VIRTUAL_BIT);
				bank = get_bank_from_ppn(old_ppn ^ VIRTUAL_BIT);
				block = get_super_block((old_ppn ^ VIRTUAL_BIT) % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
				set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);
				
				set_ppn(dst_LPN, old_ppn ^ VIRTUAL_BIT, Prof_Remap);
				sc->command_type[real_copy_count] = 1;
				set_realcopy(dst_LPN * NUM_FTL_CORE + THIS_CORE, old_ppn ^ VIRTUAL_BIT);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_VC_Realcopy_count, 1);
			}
			else
			{
				old_ppn ^= VIRTUAL_BIT;
				add_list(old_ppn, dst_LPN);
				set_ppn(dst_LPN, old_ppn ^ VIRTUAL_BIT, Prof_Remap);
			}

		}
		else if (is_in_realcopy(old_ppn) && old_ppn != (RSP_UINT32)VC_MAX)
		{
			sc->command_type[real_copy_count] = 0;
			set_realcopy(dst_LPN * NUM_FTL_CORE + THIS_CORE, old_ppn);
			set_ppn(dst_LPN, old_ppn, Prof_Remap);
			
			sc_other->command_entry[sc_other->command_count].dst_LPN = old_ppn ^ REALCOPY_BIT;
			sc_other->command_type[sc_other->command_count] = 2;
			sc_other->command_entry[sc_other->command_count++].src_LPN = dst_LPN * NUM_FTL_CORE + THIS_CORE;

		}
		else
			alloc = true;

		if (alloc)
		{//alloc
			if (free_list_count < 3 )
			{//real copy same core
				RSP_UINT32 channel, bank, block;
				set_ppn(dst_LPN, old_ppn, Prof_Remap);	
				channel = get_channel_from_ppn(old_ppn);
				bank = get_bank_from_ppn(old_ppn);
				block = get_super_block(old_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
				set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);
				sc->command_type[real_copy_count] = 1;
				set_realcopy(dst_LPN * NUM_FTL_CORE + THIS_CORE, old_ppn);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_VC_Realcopy_count, 1);
			}
			else
			{//list
				add_list(old_ppn, dst_LPN);
				add_list(old_ppn, src_LPN);
				set_ppn(dst_LPN, VIRTUAL_BIT ^ old_ppn, Prof_Remap);
				set_ppn(src_LPN, VIRTUAL_BIT ^ old_ppn, Prof_Remap);

			}

		}



	}
	RSP_VOID ATLWrapper::Virtual_move(RSP_UINT32 dst_LPN, RSP_UINT32 src_LPN)
	{
		RSP_UINT32 old_ppn, temp_ppn;
		RSP_UINT32 low_high;
		
		dec_valid_count(dst_LPN);

		//V2P table
		old_ppn = get_ppn(src_LPN, Prof_Remap);
		if (old_ppn == VC_MAX)
		{
			set_ppn(dst_LPN, VC_MAX, Prof_Remap);

		}
		else if (is_in_write_buffer(old_ppn))
		{
			RSP_ASSERT(0);
		}
		else if (is_in_virtual(old_ppn))
		{//lpn list 
			
			old_ppn ^= VIRTUAL_BIT;
			temp_ppn = get_P2L(old_ppn, Prof_Remap);
			if (is_in_virtual(temp_ppn))
			{
				set_P2L(old_ppn, dst_LPN ^ VIRTUAL_BIT, Prof_Remap);

			}
			else
			{
				del_list(old_ppn, src_LPN);
				add_list(old_ppn, dst_LPN);
			}
			set_ppn(src_LPN, VC_MAX, Prof_Remap);
			set_ppn(dst_LPN, VIRTUAL_BIT ^ old_ppn, Prof_Remap);

			
		}
		else if (is_in_realcopy(old_ppn) && old_ppn != (RSP_UINT32)VC_MAX)
		{
			set_realcopy(dst_LPN * NUM_FTL_CORE + THIS_CORE, old_ppn);
			set_ppn(dst_LPN, old_ppn, Prof_Remap);
			set_ppn(src_LPN, VC_MAX, Prof_Remap);

			
			sc_other->command_entry[sc_other->command_count].dst_LPN = old_ppn ^ REALCOPY_BIT;
			sc_other->command_type[sc_other->command_count] = 2;
			sc_other->command_entry[sc_other->command_count++].src_LPN = dst_LPN * NUM_FTL_CORE + THIS_CORE;
		}
		else
		{//alloc			
			set_ppn(src_LPN, VC_MAX, Prof_Remap);
			set_ppn(dst_LPN, VIRTUAL_BIT ^ old_ppn, Prof_Remap);

			add_list(old_ppn, dst_LPN);		
		}
	}


	RSP_VOID ATLWrapper::add_list(RSP_UINT32 ppn, RSP_UINT32 lpn)
	{
		LPN_list* temp;
		LPN_list* cur_list, *head;
		RSP_UINT32 offset, temp_ppn;
		if(get_P2L(ppn, Prof_Remap) == VC_MAX)
		{
			P2L_VALID_COUNT[get_map_offset_by_ppn(ppn)]++;
			set_P2L(ppn, lpn ^ VIRTUAL_BIT, Prof_Remap);
			return;
		}
		offset = alloc_free_list();
		temp = &LPN_ADDR[offset];
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(offset < L2V_MAX_ENTRY);
#endif
		temp_ppn = get_P2L(ppn, Prof_Remap);
		if(is_in_virtual(temp_ppn))
		{
			RSP_UINT32 lpn2 = temp_ppn ^ VIRTUAL_BIT;
			temp->lpn = lpn2;
			free_list_count--;
			head = temp;

			offset = alloc_free_list();
			temp = &LPN_ADDR[offset];
			head->next = temp->offset;
			set_P2L(ppn, head->offset, Prof_Remap);
		}
		else
		{
			cur_list = &LPN_ADDR[temp_ppn];
			for(;cur_list->next != VC_MAX;cur_list = &LPN_ADDR[cur_list->next]);
			cur_list->next = temp->offset;
		}

		temp->lpn = lpn;
		temp->next = VC_MAX;
		free_list_count--;
		if(!BG_IC_enabled && free_list_count < L2V_MAX_ENTRY * 0.3)
			SM_value->value[51] = 1;

	}

	RSP_BOOL ATLWrapper::del_list(RSP_UINT32 ppn, RSP_UINT32 lpn)
	{
		LPN_list* cur_list, *temp;
		RSP_UINT32 temp_ppn = get_P2L(ppn, Prof_Remap);
		LPN_list* head;
		RSP_BOOL ret_value;

		if(temp_ppn == VC_MAX)
			return false;
		head = &LPN_ADDR[temp_ppn];
		if (head->next == VC_MAX)
		{
			RSP_ASSERT(0);
			RSP_ASSERT(head->offset < L2V_MAX_ENTRY);
			LPN_ADDR[head->offset].next = LPN_LIST_HEAD.head;
			LPN_LIST_HEAD.head = head->offset;
			LPN_ADDR[LPN_LIST_HEAD.head].lpn = 0;
			LPN_LIST_HEAD.count++;
			free_list_count++;
			set_P2L(ppn, VC_MAX, Prof_Remap);
			ret_value = false;
		}
		else if (LPN_ADDR[head->next].next == VC_MAX)
		{	
			if (head->lpn == lpn)
			{
				set_P2L(ppn, LPN_ADDR[head->next].lpn ^ VIRTUAL_BIT, Prof_Remap);
			}
			else
			{
				set_P2L(ppn, head->lpn ^ VIRTUAL_BIT, Prof_Remap);
			}
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(LPN_ADDR[head->next].offset < L2V_MAX_ENTRY);
#endif
			LPN_ADDR[head->next].next = LPN_LIST_HEAD.head;
			LPN_LIST_HEAD.head = head->next;
			LPN_ADDR[LPN_LIST_HEAD.head].lpn = 0;
			LPN_LIST_HEAD.count++;
			free_list_count++;

#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(head->offset < L2V_MAX_ENTRY);
#endif
			LPN_ADDR[head->offset].next = LPN_LIST_HEAD.head;
			LPN_LIST_HEAD.head = head->offset;
			LPN_ADDR[LPN_LIST_HEAD.head].lpn = 0;
			LPN_LIST_HEAD.count++;
			free_list_count++;
			ret_value = true;
		}
		else
		{
			temp = head;
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(temp != NULL);
#endif
			cur_list = head;
			for (; temp->lpn != lpn; )
			{
				cur_list = temp;
				temp = &LPN_ADDR[temp->next];
			}

			if (temp == head)
			{
				head = &LPN_ADDR[temp->next];
				set_P2L(ppn, head->offset, Prof_Remap);
			}
			else
				cur_list->next = temp->next;
			temp->next = VC_MAX;
			cur_list = NULL;
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(temp->offset < L2V_MAX_ENTRY);
#endif
			
			temp->next = LPN_LIST_HEAD.head;
			LPN_LIST_HEAD.head = temp->offset;
			temp->lpn = 0;
			LPN_LIST_HEAD.count++;
			free_list_count++;

			ret_value = true;
		}
		
		if(!BG_IC_enabled && free_list_count >= L2V_MAX_ENTRY * 0.3)
			SM_value->value[51] = 0;

		return ret_value;
	}

	RSP_UINT32 ATLWrapper::alloc_free_list()
	{
		RSP_UINT32 ret_val;
		LPN_list* temp;
#ifdef ATL_ASSERTION_TEST
		if (LPN_LIST_HEAD.count == 0)
			RSP_ASSERT(0);
#endif

		ret_val = LPN_ADDR[LPN_LIST_HEAD.head].offset;
		temp = &LPN_ADDR[LPN_LIST_HEAD.head];
		LPN_LIST_HEAD.head = LPN_ADDR[LPN_LIST_HEAD.head].next;
		LPN_LIST_HEAD.count--;
		temp->next = VC_MAX;
		return ret_val;
	}

	///////////////////////////////////////////////////////////////////////////////



	RSP_UINT32 ATLWrapper::RSP_ReadPage(RSP_UINT32 request_ID, RSP_LPN LPN, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress)
	{
		RSP_UINT32 lpn, ret_value;
		RSP_UINT32 bank, ppn, channel, plane, buf_offset;
		RSP_SECTOR_BITMAP read_bitmap;
		RSPReadOp RSP_read_op;
		void **temp;
 
		lpn = LPN;

		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Host_read, 1);

		ppn = get_ppn(lpn, Prof_Read);

#ifdef VC_debug_JBD

		debug_ppn[debug_counter] = ppn;
		debug_lpn[debug_counter] = lpn * 2 + THIS_CORE;
		debug_counter = (debug_counter + 1) % 400;
#endif

		
		if (is_in_virtual(ppn) && ppn != (RSP_UINT32)VC_MAX)
			ppn ^= VIRTUAL_BIT;
		else if(is_in_realcopy(ppn) && ppn != (RSP_UINT32)VC_MAX)
		{
			if(THIS_CORE == 0)
			{
				while(!check_realcopy_done());
				SM_value->value[12] = ppn ^ REALCOPY_BIT;
				temp = (void**)&SM_value->value[14];
                *temp = (void*)BufferAddress;
				SM_value->value[16] = 0xff;
				SM_value->value[10] = 1;
				SM_value->value[33] = 1;
				realcopy_read();
				meta_write_page(LPN, BufferAddress, true, false);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);
				
			}
			else
			{
				while(!check_realcopy_done());
				SM_value->value[13] = ppn ^ REALCOPY_BIT;
				temp = (void**)&SM_value->value[15];
                *temp = (void*)BufferAddress;
				SM_value->value[17] = 0xff;
				SM_value->value[11] = 1;
				SM_value->value[34] = 1;
                realcopy_read();
				meta_write_page(LPN, BufferAddress, true, false);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);
			}
			Check_other_core_read();
			return ReadWriteBuffer;
		}

#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || ppn == VC_MAX || is_in_write_buffer(ppn));
#endif
		if (ppn != (RSP_UINT32)VC_MAX)
		{

			if (is_in_write_buffer(ppn))
			{
				ppn ^= (WRITE_BUFFER_BIT);

				if(ppn >= LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL * RSP_NUM_CHANNEL)
				{//Meta Write buffer
					ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				
					plane = ppn / LPAGE_PER_PPAGE;
					buf_offset = ppn % LPAGE_PER_PPAGE;

					RSPOSAL::RSP_MemSet(BufferAddress, 0xff, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					RSP_BufferCopy(BufferAddress, 
						(RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + (plane * LPAGE_PER_PPAGE + buf_offset) * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), SectorBitmap);
				}
				else
				{//Write buffer
					channel = ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
					ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
					bank = ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK);
					ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
					plane = ppn / LPAGE_PER_PPAGE;
					buf_offset = ppn % LPAGE_PER_PPAGE;
#ifdef ATL_ASSERTION_TEST
					RSP_ASSERT(plane < PLANES_PER_BANK && buf_offset < LPAGE_PER_PPAGE);
#endif
					RSPOSAL::RSP_MemSet(BufferAddress, 0xFF, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);

					RSP_BufferCopy(BufferAddress, (RSP_UINT32 *)add_addr(writebuf_addr[plane], buf_offset * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), SectorBitmap);
				}
				ret_value = ReadWriteBuffer;
			}
			else
			{
				channel = get_channel_from_ppn(ppn);
				bank = get_bank_from_ppn(ppn);
				if (ppn % LPAGE_PER_PPAGE)
				{
					read_bitmap = 0xff00;
				}
				else
				{
					read_bitmap = 0xff;
				}
				ppn = (ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK;
				plane = get_plane(ppn);

				//RSP_read_op.pSpareData = NULL;
				RSP_read_op.nReqID = request_ID;
				RSP_read_op.nChannel = channel;
				RSP_read_op.nBank = bank;
				RSP_read_op.nBlock = get_block(ppn);
				RSP_read_op.nPage = get_page_offset(ppn);
				RSP_read_op.bmpTargetSector = read_bitmap;

				if (read_bitmap == 0xff00){
					RSP_read_op.pData = (RSP_UINT32 *)sub_addr(BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, 1);
				}
				else{
					RSP_read_op.pData = (RSP_UINT32 *)BufferAddress;
					RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, 0);
				}

				RSP_read_op.m_nLPN = lpn;
				m_pVFLWrapper->INC_READPENDING();
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 1);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Read_read, 1);
				m_pVFLWrapper->Issue(RSP_read_op);
#ifdef WAIT_TEST
				m_pVFLWrapper->WAIT_READPENDING();
#endif
				//m_pVFLWrapper->WAIT_READPENDING();

				ret_value = ReadNand;

			}
		}
		else
		{
			RSPOSAL::RSP_MemSet(BufferAddress, 0x00, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
			ret_value = ReadError;

		}
		Check_other_core_read();
		return ret_value;
	}

	//NAND write: handling write request
	//NAND_request: cur request
	//isEPF: this req is issued by EPF
	//iscb: callback function called

	RSP_BOOL ATLWrapper::RSP_WritePage(RSP_LPN LPN[2], RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress)
	{
		RSP_BOOL ret = true, remap_enable = false; 
		if(LPN[0] >= SPECIAL_COMMAND_PAGE && LPN[0] < MAX_SPECIAL_COMMAND_PAGE)
		{
			if(THIS_CORE == 0)
				SM_value->value[54] = LPN[0] << 1;
#ifdef VC_debug_JBD		
			if(last_remap_address_debug  != 0)
			{
				if((last_remap_address_debug + 1) % SPECIAL_COMMAND_RANGE != LPN[0] % SPECIAL_COMMAND_RANGE)
				{
					RSP_ASSERT(0);
				}
			}
			last_remap_address_debug  = LPN[0];
#endif


			
			write_page(SPECIAL_COMMAND_PAGE, SectorBitmap & 0xff, BufferAddress, ((SectorBitmap & 0xff00) == 0) ? true : false);
			remap_enable = true;

			RSPOSAL::RSP_MemCpy((RSP_UINT32 *)sc, BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);

			
		}
		else if(LPN[0] == CMT_SIZE_COMMAND_PAGE)
		{
			ATL_initialize_manage((RSP_UINT8)*BufferAddress, (RSP_UINT8)BufferAddress[1], (RSP_UINT8)BufferAddress[2], (RSP_UINT8)BufferAddress[3], (RSP_UINT8)BufferAddress[4]);
			write_page(SPECIAL_COMMAND_PAGE, SectorBitmap & 0xff, BufferAddress, ((SectorBitmap & 0xff00) == 0) ? true : false);
		}
		else if(LPN[0] < RSP_INVALID_LPN)
		{
			
			write_page(LPN[0], SectorBitmap & 0xff, BufferAddress, ((SectorBitmap & 0xff00) == 0) ? true : false);
		}

		if(LPN[1] >= SPECIAL_COMMAND_PAGE && LPN[1] < MAX_SPECIAL_COMMAND_PAGE)
		{
			if(THIS_CORE == 0)
				SM_value->value[54] = LPN[1] << 1;
#ifdef VC_debug_JBD		
			if(last_remap_address_debug  != 0)
			{
				if((last_remap_address_debug + 1) % SPECIAL_COMMAND_RANGE != LPN[1] % SPECIAL_COMMAND_RANGE)
				{
					RSP_ASSERT(0);
				}
			}
			last_remap_address_debug  = LPN[1];
#endif

			write_page(SPECIAL_COMMAND_PAGE, SectorBitmap & 0xff00, BufferAddress, true);
			remap_enable = true;
			RSPOSAL::RSP_MemCpy((RSP_UINT32 *)sc, (RSP_UINT32*)add_addr(BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		}
		else if(LPN[1] == CMT_SIZE_COMMAND_PAGE)
		{
			ATL_initialize_manage((RSP_UINT8)*BufferAddress, (RSP_UINT8)BufferAddress[1], (RSP_UINT8)BufferAddress[2], (RSP_UINT8)BufferAddress[3], (RSP_UINT8)BufferAddress[4]);
			write_page(SPECIAL_COMMAND_PAGE, SectorBitmap & 0xff, BufferAddress, ((SectorBitmap & 0xff00) == 0) ? true : false);
		}
		else if(LPN[1] < RSP_INVALID_LPN)
		{
			write_page(LPN[1], SectorBitmap & 0xff00, BufferAddress, true);
		}
		if(remap_enable)
		{
			buffer_flush(get_channel(ATL_cur_write_bank), get_bank(ATL_cur_write_bank));
			Special_command_handler(NULL);
		}

		
		/*if (pHILK2L == NULL)
		{
			if (THIS_CORE == 0)
			{
				void **temp;
				pHILK2L = (RSP_UINT32*)RSPSharedMem::FTL_GetHILK2L();
				pHILK2L[0] = 1;
				temp = (void**)&pHILK2L[1];
				*temp = (void*)SM_value;
				temp = (void**)&SM_value->value[3];
				*temp = sc_other;

				temp = (void**)&SM_value->value[5];
				*temp = cur_VC_struct;
			}
			else
			{
				void **temp;
				pHILK2L = (RSP_UINT32*)RSPSharedMem::FTL_GetHILK2L();
				if (pHILK2L[1] != 0)
				{
					SM_value = (struct SM_struct*)pHILK2L[1];
					temp = (void**)&SM_value->value[4];
					*temp = sc_other;

					temp = (void**)&SM_value->value[6];
					*temp = cur_VC_struct;
				}
				else
					pHILK2L = NULL;
			}

		}*/
		Check_other_core_read();
		return ret;
	}


	RSP_UINT32 test_buffer_flush_value = 0;

	RSP_BOOL ATLWrapper::write_page(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap_input, RSP_UINT32* BufferAddress, RSP_BOOL end_io)
	{
		RSP_UINT32 channel, bank, old_ppn, new_ppn, block = 0, ppn;
		RSP_UINT32 plane, buf_offset, plane_ppn[LPAGE_PER_PPAGE], super_blk;
		RSP_UINT32 vpage_offset = 0, rpage_offset;
		static RSP_UINT32 first_JBD = 0;
		RSP_SECTOR_BITMAP SectorBitmap = SectorBitmap_input;
		RSP_BOOL is_in_virtual_page = false;
		RSP_BOOL ret = true;  //when the buffer is not used, then switch into false
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];

#ifdef VC_debug_JBD

		if(lpn >= JBD_START && lpn <= JBD_END)
		{
			first_JBD++;
			
			if(remap_start)
				RSP_ASSERT(JBD_state[lpn - JBD_START] == 0);
			JBD_state[lpn - JBD_START] = 1;

			if(first_JBD == JBD_COUNTER)
				RSPOSAL::RSP_MemSet(JBD_state, 0x00, 131072 * sizeof(RSP_UINT32));
		}
#endif
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Host_write, 1);

		old_ppn = get_ppn(lpn, Prof_Write);
		if (old_ppn != VC_MAX && is_in_virtual(old_ppn))
		{
			old_ppn ^= VIRTUAL_BIT;
			ppn = get_P2L(old_ppn, Prof_Write);
			if (is_in_virtual(ppn))
			{
				set_P2L(old_ppn, VC_MAX, Prof_Write);
			}
			else if (del_list(old_ppn, lpn))
					is_in_virtual_page = true;
#ifdef ATL_ASSERTION_TEST
			if (is_in_write_buffer(old_ppn))
			{
				RSP_ASSERT(0);
			}
#endif
			
		}
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(old_ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || old_ppn == VC_MAX || is_in_write_buffer(old_ppn) || is_in_realcopy(old_ppn));
#endif
		if (is_in_write_buffer(old_ppn) && old_ppn != VC_MAX)
		{//in_buffer
			
			old_ppn ^= (WRITE_BUFFER_BIT);

			if(old_ppn >= LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL * RSP_NUM_CHANNEL)
			{//Meta write_buffer
				old_ppn = old_ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				plane = old_ppn / LPAGE_PER_PPAGE;
				buf_offset = old_ppn % LPAGE_PER_PPAGE;
				
				if (!(SectorBitmap == 0xff) && !(SectorBitmap == 0xff00))
                { //Read Modify
                    if (SectorBitmap >> SECTORS_PER_LPN)
                    {
                            RSP_BufferCopy((RSP_UINT32 *)add_addr(BufferAddress, BYTES_PER_SECTOR * SECTORS_PER_LPN),
								(RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + (plane * LPAGE_PER_PPAGE + buf_offset) * BYTES_PER_SECTOR * SECTORS_PER_LPN),
                            (SectorBitmap >> SECTORS_PER_LPN) ^ 0xff);
                    }
                    else
                    {
                            RSP_BufferCopy(BufferAddress,
								(RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + (plane * LPAGE_PER_PPAGE + buf_offset) * BYTES_PER_SECTOR * SECTORS_PER_LPN),
                            SectorBitmap ^ 0xff);
                    }
                }
				meta_write_lpn[plane][buf_offset][0] = RSP_INVALID_LPN;
			}
			else
			{//Write buffer
				channel = old_ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
				old_ppn = old_ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
				bank = old_ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				old_ppn = old_ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				plane = old_ppn / LPAGE_PER_PPAGE;
				buf_offset = old_ppn % LPAGE_PER_PPAGE;
				if (!(SectorBitmap == 0xff) && !(SectorBitmap == 0xff00))
				{ //Read Modify
					if (SectorBitmap >> SECTORS_PER_LPN)
					{
						RSP_BufferCopy((RSP_UINT32 *)add_addr(BufferAddress, BYTES_PER_SECTOR * SECTORS_PER_LPN),
						(RSP_UINT32 *)add_addr(writebuf_addr[plane], buf_offset * BYTES_PER_SECTOR * SECTORS_PER_LPN),
						(SectorBitmap >> SECTORS_PER_LPN) ^ 0xff);
						SectorBitmap |= writebuf_data_bitmap[plane] & 0xff00;	
					}
					else
					{
						RSP_BufferCopy(BufferAddress,
						(RSP_UINT32 *)add_addr(writebuf_addr[plane], buf_offset * BYTES_PER_SECTOR * SECTORS_PER_LPN),
						SectorBitmap ^ 0xff);
						SectorBitmap |= writebuf_data_bitmap[plane] & 0xff;
					}
				}
				
				if (buf_offset)
				{
					writebuf_data_bitmap[plane] ^= 0xff00;
					writebuf_lpn[plane][1][0] = RSP_INVALID_LPN;
					writebuf_bitmap &= (1 << ((plane)* LPAGE_PER_PPAGE + buf_offset)) ^ 0xffff;
				}
				else
				{
					writebuf_data_bitmap[plane] ^= 0xff;
					writebuf_lpn[plane][0][0] = RSP_INVALID_LPN;
					writebuf_bitmap &= (1 << ((plane)* LPAGE_PER_PPAGE + buf_offset)) ^ 0xffff;
				}
			}
		}
		else if(old_ppn != VC_MAX)
		{
			if(is_in_realcopy(old_ppn))
			{
				if(!(SectorBitmap == 0xff) && !(SectorBitmap == 0xff00))
				{
					void **temp;
					if(THIS_CORE == 0)
					{
						while (!check_realcopy_done());
						SM_value->value[12] = old_ppn ^ REALCOPY_BIT;
						temp = (void**)&SM_value->value[14];
		                                *temp = (void*)BufferAddress;
						if(SectorBitmap >> RSP_SECTOR_PER_LPN)
							SM_value->value[16] = ((~SectorBitmap) >> RSP_SECTOR_PER_LPN) & 0xff;
						else
							SM_value->value[16] = (~SectorBitmap) & 0xff;
						SM_value->value[10] = 1;
						SM_value->value[33] = 1;
		                                realcopy_read();
					}
					else
					{
						while (!check_realcopy_done());
						SM_value->value[13] = old_ppn ^ REALCOPY_BIT;
						temp = (void**)&SM_value->value[15];
                        *temp = (void*)BufferAddress;
                        if(SectorBitmap >> RSP_SECTOR_PER_LPN)
							SM_value->value[17] = ((~SectorBitmap) >> RSP_SECTOR_PER_LPN) & 0xff;
                        else
							SM_value->value[17] = (~SectorBitmap) & 0xff;
						SM_value->value[11] = 1;
						SM_value->value[34] = 1;
                        realcopy_read();	
					}
				}
			}
			else
			{
				channel = get_channel_from_ppn(old_ppn);
				bank = get_bank_from_ppn(old_ppn);
				plane = get_plane((old_ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK);
				block = get_super_block(old_ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));


				if (old_ppn % LPAGE_PER_PPAGE)
					vpage_offset = 1;
				old_ppn = (old_ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK; //8KB page in bank
				if (!(SectorBitmap == 0xff) && !(SectorBitmap == 0xff00))
				{ //Read Modify
					RSP_SECTOR_BITMAP tempSectorBitmap;
					//metaIssue is 8KB read operation
					//to modify
					if (SectorBitmap & 0xff)
						rpage_offset = 0;
					else
						rpage_offset = 1;

					if (vpage_offset)
						RSP_read_op.pData = (RSP_UINT32 *)sub_addr(NAND_bank_state[channel][bank].cpybuf_addr[0], vpage_offset * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					else
						RSP_read_op.pData = (RSP_UINT32 *)NAND_bank_state[channel][bank].cpybuf_addr[0];

					//RSP_read_op.pSpareData = NULL;
					RSP_read_op.nReqID = RSP_INVALID_RID;
					RSP_read_op.nChannel = channel;
					RSP_read_op.nBank = bank;
					RSP_read_op.nBlock = get_block(old_ppn);
					RSP_read_op.nPage = get_page_offset(old_ppn);
					if (SectorBitmap >> RSP_SECTOR_PER_LPN){
						if (vpage_offset)
							RSP_read_op.bmpTargetSector = 0xff00;
						else
							RSP_read_op.bmpTargetSector = 0xff;
						tempSectorBitmap = ((~SectorBitmap) >> RSP_SECTOR_PER_LPN) & 0xff;
						SectorBitmap = 0xff00;
					}
					else{
						if (vpage_offset)
							RSP_read_op.bmpTargetSector = 0xff00;
						else
							RSP_read_op.bmpTargetSector = 0xff;
						tempSectorBitmap = (~SectorBitmap) & 0xff;
						SectorBitmap = 0xff;
					}
					RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, vpage_offset);
					RSP_read_op.m_nLPN = lpn;
	
					m_pVFLWrapper->INC_READPENDING();
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 1);
					m_pVFLWrapper->Issue(RSP_read_op);
	
					//Read Complete
					m_pVFLWrapper->WAIT_READPENDING();
					RSP_BufferCopy((RSP_UINT32 *)add_addr(BufferAddress, rpage_offset * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), NAND_bank_state[channel][bank].cpybuf_addr[0], tempSectorBitmap);
					
				}
				if (!is_in_virtual_page && get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + vpage_offset))
				{
					//if it is virtual, it already has beend invalidated
#ifdef ATL_ASSERTION_TEST
					RSP_ASSERT(get_vcount(channel, bank, block) != 0);
#endif

						set_vcount(channel, bank, block, get_vcount(channel, bank, block) - 1);
						clear_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + vpage_offset);
				}
			}
		}
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(block < BLKS_PER_PLANE);
#endif
		channel = get_channel(ATL_cur_write_bank);
		bank = get_bank(ATL_cur_write_bank);


		//buffer bitmap:: 00 00 00 00
		//write_buf_idx:: 3  2  1  0
		//write_buf_off:: 10 10 10 10
		RSP_UINT32 idx = writebuf_index;
		if (idx == 0){
			//first buff access after write
			//new buff ptr
			writebuf_addr[idx] = BufferAddress;
//			writebuf_data_bitmap[idx] |= 0xff;
			writebuf_data_bitmap[idx] |= SectorBitmap;
			writebuf_orig_data_bitmap[idx] |= SectorBitmap;
			if (SectorBitmap >> SECTORS_PER_LPN){
				writebuf_bitmap |= 1 << (idx * LPAGE_PER_PPAGE + 1); //buffoffset 1
				writebuf_lpn[idx][1][0] = lpn;
				writebuf_orig_lpn[idx][1] = lpn;
				set_ppn(lpn, WRITE_BUFFER_BIT ^ ((channel * BANKS_PER_CHANNEL + bank) * LPAGE_PER_PPAGE * PLANES_PER_BANK + idx * LPAGE_PER_PPAGE + 1), Prof_Write);

			}
			else{
				writebuf_bitmap |= 1 << (idx * LPAGE_PER_PPAGE); //buffoffset 0
				writebuf_lpn[idx][0][0] = lpn;
				writebuf_orig_lpn[idx][0] = lpn;
				set_ppn(lpn, WRITE_BUFFER_BIT ^ ((channel * BANKS_PER_CHANNEL + bank) * LPAGE_PER_PPAGE * PLANES_PER_BANK + idx * LPAGE_PER_PPAGE), Prof_Write);

			}
			writebuf_index++;
		}
		else if ((SectorBitmap >> SECTORS_PER_LPN) && (BufferAddress == writebuf_addr[idx - 1])){
			//buff tail	
			writebuf_lpn[idx - 1][1][0] = lpn;
			writebuf_orig_lpn[idx - 1][1] = lpn;
			writebuf_bitmap |= 1 << ((idx - 1) * LPAGE_PER_PPAGE + 1);
			set_ppn(lpn, WRITE_BUFFER_BIT ^ ((channel * BANKS_PER_CHANNEL + bank) * LPAGE_PER_PPAGE * PLANES_PER_BANK + (idx - 1) * LPAGE_PER_PPAGE + 1), Prof_Write);


			//writebuf_data_bitmap[idx - 1] |= 0xff00;
			writebuf_data_bitmap[idx - 1] |= SectorBitmap;
			writebuf_orig_data_bitmap[idx - 1] |= SectorBitmap;
		}
		else if (idx < PLANES_PER_BANK){
			//new buff ptr
			writebuf_addr[idx] = BufferAddress;
//			writebuf_data_bitmap[idx] |= 0xff;
			writebuf_data_bitmap[idx] |= SectorBitmap;
			writebuf_orig_data_bitmap[idx] |= SectorBitmap;

			if (SectorBitmap >> SECTORS_PER_LPN){
				writebuf_bitmap |= 1 << (idx * LPAGE_PER_PPAGE + 1); //buffoffset 1
				writebuf_lpn[idx][1][0] = lpn;
				writebuf_orig_lpn[idx][1] = lpn;
				set_ppn(lpn, WRITE_BUFFER_BIT ^ ((channel * BANKS_PER_CHANNEL + bank) * LPAGE_PER_PPAGE * PLANES_PER_BANK + idx * LPAGE_PER_PPAGE + 1), Prof_Write);
				
			}
			else{
				writebuf_bitmap |= 1 << (idx * LPAGE_PER_PPAGE); //buffoffset 0
				writebuf_lpn[idx][0][0] = lpn;
				writebuf_orig_lpn[idx][0] = lpn;
				set_ppn(lpn, WRITE_BUFFER_BIT ^ ((channel * BANKS_PER_CHANNEL + bank) * LPAGE_PER_PPAGE * PLANES_PER_BANK + idx * LPAGE_PER_PPAGE), Prof_Write);
				
			}
			writebuf_index++;

		}
#ifdef ATL_ASSERTION_TEST
		else
		{
			channel = idx;
			bank = writebuf_index;
			RSP_ASSERT(0);
		}
#endif
		if (writebuf_index == PLANES_PER_BANK && end_io) //buff full
		{
			RSP_UINT32 valid_count = 0; 
			new_ppn = assign_new_write_ppn(channel, bank); //super page size
#ifdef ATL_ASSERTION_TEST
			RSP_ASSERT(new_ppn < BLKS_PER_BANK * PAGES_PER_BLK);
#endif
			super_blk = new_ppn / PAGES_PER_BLK;
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
				{
					plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;

					if (writebuf_lpn[plane][buf_offset][0] == RSP_INVALID_LPN)
					{
						continue;
					}
					else if (is_in_virtual(writebuf_lpn[plane][buf_offset][0]))
					{
						writebuf_lpn[plane][buf_offset][0] ^= VIRTUAL_BIT;
						continue;
					}
					if(writebuf_bitmap & ((1 << ((plane)* LPAGE_PER_PPAGE + buf_offset)) ^ 0xffff));
						valid_count++;
					old_ppn = get_ppn(writebuf_lpn[plane][buf_offset][0], Prof_Write);
#ifdef ATL_ASSERTION_TEST
					if (is_in_virtual(old_ppn) && old_ppn != VC_MAX)
					{//VC/VM in write buffer

						RSP_ASSERT(0);
					}
					else
#endif
						set_ppn(writebuf_lpn[plane][buf_offset][0], plane_ppn[buf_offset], Prof_Write);
					set_valid(channel, bank, (plane_ppn[buf_offset]) % (PAGES_PER_BANK * LPAGE_PER_PPAGE));

#ifdef ATL_ASSERTION_TEST
					RSP_ASSERT(writebuf_lpn[plane][buf_offset][0] < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
					RSP_ASSERT(plane_ppn[buf_offset] < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || plane_ppn[buf_offset] == VC_MAX || is_in_write_buffer(plane_ppn[buf_offset]));
#endif

				}

				RSP_write_ops[plane].pData = writebuf_addr[plane];
				RSP_write_ops[plane].pSpareData = &writebuf_lpn[plane][0][0];

				RSP_write_ops[plane].bmpTargetSector = 0xffff;
				RSP_write_ops[plane].nChannel = channel;
				RSP_write_ops[plane].nBank = bank;
				RSP_write_ops[plane].nBlock = get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);

				RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
				RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
				RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
				RSP_write_ops[plane].m_anLPN[1] = writebuf_orig_lpn[plane][1];
				RSP_write_ops[plane].m_anLPN[0] = writebuf_orig_lpn[plane][0];
				

			}

			m_pVFLWrapper->INC_PROGRAMPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, valid_count);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Write_write, valid_count);
			m_pVFLWrapper->Issue(RSP_write_ops);
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif

			block = new_ppn / PAGES_PER_BLK;
			writebuf_index = 0;
			writebuf_bitmap = 0;
			for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++){
				writebuf_addr[iter] = NULL;
				writebuf_data_bitmap[iter] = 0;
				writebuf_orig_data_bitmap[iter] = 0;
				for (RSP_UINT8 ineriter = 0; ineriter < LPAGE_PER_PPAGE; ineriter++)
				{
					writebuf_lpn[iter][ineriter][0] = RSP_INVALID_LPN;
					writebuf_lpn[iter][ineriter][1] = RSP_INVALID_LPN;
					writebuf_lpn[iter][ineriter][2] = RSP_INVALID_LPN;
					writebuf_orig_lpn[iter][ineriter] = RSP_INVALID_LPN;
				}
			}
			

			set_vcount(channel, bank, block, get_vcount(channel, bank, block) + valid_count);
			channel = channel ^ 1;

			if(NAND_bank_state[channel][bank].free_blk_list.count <= 5)
				for(int iter = 0; iter < 3; iter++)
					while(incremental_garbage_collection(channel, bank, Prof_FGC) == 0);
			if (NAND_bank_state[channel][bank].free_blk_list.count == 0)
				while (incremental_garbage_collection(channel, bank, Prof_FGC) != 2);
			if (NAND_bank_state[channel][bank].GCbuf_index != 0)
				dbg1++;
			//test_buffer_flush_value = 0;

		}
		return ret;
	}

	RSP_BOOL ATLWrapper::meta_write_page(RSP_LPN lpn, RSP_UINT32* BufferAddress, RSP_BOOL need_mem_copy, RSP_BOOL need_unmap)
	{
		if (need_unmap)
		{
			RSP_UINT32 old_ppn;
			old_ppn = get_ppn(lpn, Prof_Write);
			unmap(lpn, old_ppn);
		}
		if(need_mem_copy == true)
			RSPOSAL::RSP_MemCpy((RSP_UINT32*)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + meta_write_count * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		meta_write_lpn[meta_write_count / LPAGE_PER_PPAGE][meta_write_count % LPAGE_PER_PPAGE][0] = lpn;
	
		set_ppn(lpn, WRITE_BUFFER_BIT ^ (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL * LPAGE_PER_PPAGE * PLANES_PER_BANK + meta_write_count++), Prof_Write);

		if(meta_write_count == LPAGE_PER_PPAGE * PLANES_PER_BANK)
		{
			meta_buffer_flush();
		}
		return true;
	}


	//assign_new_write_ppn: it return new page can write
	//bank: bank number
	//assign_latency: latency is inserted this value
	RSP_UINT32 ATLWrapper::assign_new_write_ppn(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 write_ppn;
		RSP_UINT32 block;
		RSP_BOOL do_flush = false;
		write_ppn = get_cur_write_ppn(channel, bank);
		block = write_ppn / PAGES_PER_BLK;

		if ((write_ppn % PAGES_PER_BLK) == (PAGES_PER_BLK - 1))
		{
			//GC occured
			block = get_free_blk(&NAND_bank_state[channel][bank].free_blk_list);
			if (block == VC_MAX)
			{
				RSP_ASSERT(0);
			}
			do_flush = true;

		}
		if (block != (write_ppn / PAGES_PER_BLK))
		{
			//new block
			write_ppn = block * PAGES_PER_BLK;
			erase_wrapper(channel, bank, block);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);

			set_vcount(channel, bank, block, 0);
		}
		else if (NAND_bank_state[channel][bank].write_start)
			write_ppn++;
		else
			NAND_bank_state[channel][bank].write_start = true;
		//bank satus update
		set_new_write_ppn(channel, bank, write_ppn);
		ATL_cur_write_bank = (ATL_cur_write_bank + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
		if(do_flush && ATL_cur_write_bank == NAND_NUM_CHANNELS * BANKS_PER_CHANNEL - 1)
			{
				flush_bank_counter++;
				//dummy_buffer_flush();
			}
		


		return write_ppn;
	}
	//L2P management
	///////////////////////////////////////////////////////////////////////////////
	//get_ppn: return L2P table value
	//lpn: logical page number
	//map_latency latency is added this value
	RSP_UINT32 ATLWrapper::get_ppn(RSP_UINT32 lpn, RSP_UINT8 type)
	{
		RSP_UINT32 cache_slot, map_page_offset, return_val;
		map_page_offset = lpn % NUM_PAGES_PER_MAP;


		RSPOSAL::RSP_MemCpy(&return_val, (RSP_UINT32 *)add_addr(CACHE_ADDR[L2P], lpn * sizeof_u32), sizeof_u32);

#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(return_val < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || return_val == VC_MAX || is_in_write_buffer(return_val) || is_in_realcopy(return_val) || is_in_virtual(return_val));
#endif
		return return_val;
	}
	//set_ppn: set L2P table value
	//lpn: logical page number
	//map_latency latency is added this value

	RSP_VOID ATLWrapper::set_ppn(RSP_UINT32 lpn, RSP_UINT32 ppn, RSP_UINT8 type)
	{
	
		RSP_UINT32 cache_slot = lpn / 8192;
		if (_COREID_ == 1 && lpn == 60480)
			dbg1++;
		if (_COREID_ == 1 && ppn == 81216 && lpn == 60480)
			dbg1++;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || ppn == VC_MAX || is_in_write_buffer(ppn) || is_in_realcopy(ppn) || is_in_virtual(ppn));
		RSP_ASSERT(cache_slot < NUM_CACHED_MAP[L2P]);
#endif
		RSPOSAL::RSP_MemCpy((RSP_UINT32 *)add_addr(CACHE_ADDR[L2P], lpn * sizeof_u32), &ppn, sizeof_u32);

		
		CACHE_MAP_DIRTY_TABLE[L2P][cache_slot] = true;
		return;
	}



	RSP_UINT32 ATLWrapper::get_P2L(RSP_UINT32 ppn, RSP_UINT8 type)
	{
		RSP_UINT32 cache_slot, map_page_offset, return_val, mod_ppn;

		mod_ppn = get_modppn_by_ppn(ppn);
		map_page_offset = mod_ppn % NUM_PAGES_PER_MAP;
		CMT_manage(mod_ppn, &cache_slot, type, P2L);

		RSPOSAL::RSP_MemCpy(&return_val, (RSP_UINT32 *)add_addr(CACHE_ADDR[P2L], (cache_slot * BYTES_PER_SUPER_PAGE) + map_page_offset * sizeof_u32), sizeof_u32);

		
		return return_val;
	}


	RSP_VOID ATLWrapper::set_P2L(RSP_UINT32 ppn, RSP_UINT32 lpn, RSP_UINT8 type)
	{
		RSP_UINT32 cache_slot, map_page_offset, return_val, mod_ppn;
		RSP_UINT32 *value;
		mod_ppn = get_modppn_by_ppn(ppn);
		map_page_offset = mod_ppn % NUM_PAGES_PER_MAP;
		CMT_manage(mod_ppn, &cache_slot, type, P2L);
		value = (RSP_UINT32 *)add_addr(CACHE_ADDR[P2L], (cache_slot * BYTES_PER_SUPER_PAGE) + map_page_offset * sizeof_u32);
			
		RSPOSAL::RSP_MemCpy(value, &lpn, sizeof_u32);
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(cache_slot < NUM_CACHED_MAP[P2L]);
#endif
		
		if(lpn == VC_MAX)
			P2L_VALID_COUNT[get_map_offset_by_ppn(ppn)]--;
		else
			CACHE_MAP_DIRTY_TABLE[P2L][cache_slot] = true;
		return;
	}


	RSP_VOID ATLWrapper::CMT_manage(RSP_UINT32 lpn, RSP_UINT32 *cache_slot, RSP_UINT8 type, RSP_UINT32 cache_type)
	{
		RSP_UINT32 map_page, loop;
		cached_map_list* temp = CACHED_MAP_HEAD[cache_type], *old_temp = CACHED_MAP_HEAD[cache_type];
		map_page = lpn / NUM_PAGES_PER_MAP;
#ifdef ATL_ASSERTION_TEST
		if(cache_type == L2P)
		{
			RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		}
		else
		{
			RSP_ASSERT(lpn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		}
		RSP_ASSERT(map_page < NUM_MAP_ENTRY(cache_type) * LPAGE_PER_PPAGE);
#endif
		//check this map_page is in SRAM
		for (loop = 0; loop < num_cached[cache_type]; loop++)
		{

			if (temp->map_page == map_page)
				break;
			if (temp->next != NULL)
			{
				old_temp = temp;
				temp = temp->next;
			}
		}

		if (loop == NUM_CACHED_MAP[cache_type] || loop == num_cached[cache_type])
		{
			//cache miss
			if (num_cached[cache_type] != NUM_CACHED_MAP[cache_type])
			{
				//cache have empty slot
				map_read(map_page, num_cached[cache_type], type, cache_type);

				temp = &CACHE_MAPPING_TABLE[cache_type][num_cached[cache_type]];
				temp->next = CACHED_MAP_HEAD[cache_type];
				CACHED_MAP_HEAD[cache_type] = temp;
				//meta update
				CACHE_MAPPING_TABLE[cache_type][num_cached[cache_type]].map_page = map_page;

				*cache_slot = num_cached[cache_type];
				num_cached[cache_type]++;
			}
			else
			{
				if (temp->map_page != (RSP_UINT32)VC_MAX)
				{
					//write victim 
					*cache_slot = temp->offset;
					if (CACHE_MAP_DIRTY_TABLE[cache_type][*cache_slot] == true)
					{
						map_write(temp->map_page, *cache_slot, type, cache_type); //sync
						m_pVFLWrapper->WAIT_PROGRAMPENDING();
						CACHE_MAP_DIRTY_TABLE[cache_type][*cache_slot] = false;
					}
					temp->map_page = (RSP_UINT32)VC_MAX;
				}

				map_read(map_page, *cache_slot, type, cache_type);

				temp->map_page = map_page;

				old_temp->next = temp->next;
				temp->next = CACHED_MAP_HEAD[cache_type];
				CACHED_MAP_HEAD[cache_type] = temp;

			}
			if(cache_type == L2P)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_MISS, 1);
			else
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_MISS, 1);
		}
		else
		{
			//cache hit
			//update LRU value
			if (temp != CACHED_MAP_HEAD[cache_type])
			{
				old_temp->next = temp->next;
				temp->next = CACHED_MAP_HEAD[cache_type];
				CACHED_MAP_HEAD[cache_type] = temp;
			}
			*cache_slot = temp->offset;
			if(cache_type == L2P)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_HIT, 1);
			else
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_HIT, 1);
			//latency is 0
		}
	}

	//vcount management
	////////////////////////////////////////////////////////////////////////////
	//get_vcount: return vcount of block
	//set_vcount  set vcount of input block
	RSP_UINT32 ATLWrapper::get_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
	{
		RSP_UINT32 vcount = VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block];
		if (vcount == (RSP_UINT32)VC_MAX)
			return vcount;
		else
			vcount &= VCOUNT_MASK;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block < NUM_PBLK);
		RSP_ASSERT(vcount <= PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE || vcount == (RSP_UINT32)VC_MAX);
#endif
		return vcount;
	}
	RSP_VOID ATLWrapper::set_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount)
	{
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(vcount <= PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE || vcount == (RSP_UINT32)VC_MAX);
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block < NUM_PBLK);
#endif
		if (vcount == (RSP_UINT32)VC_MAX)
			VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] = vcount;
		else if(vcount == 0)
			VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] = vcount;
		else
		{
			VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] &= REFCOUNT_MASK;
			VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] += vcount;
		}
		return;
	}

	//refcount management
	////////////////////////////////////////////////////////////////////////////
	//getrefcount: return refcount of block
	//set_refcount  set refcount of input block
	RSP_UINT32 ATLWrapper::get_refcount(RSP_UINT32 channel, RSP_UINT32 bank,  RSP_UINT32 block)
	{
		RSP_UINT32 refcount = VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block];
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block < NUM_PBLK);
#endif
		if (refcount == (RSP_UINT32)VC_MAX)
			return 0;
		else
			refcount &= REFCOUNT_MASK;
		refcount = refcount >> REFCOUNT_BIT_OFFSET;
		return refcount;
	}
	RSP_VOID ATLWrapper::set_refcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 refcount)
	{
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT((channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block < NUM_PBLK);
		RSP_ASSERT(VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] != VC_MAX);
#endif
		VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] &= VCOUNT_MASK;
		VCOUNT[(channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_PLANE + block] += refcount << REFCOUNT_BIT_OFFSET;

		return;
	}

	//Valid bitmap
	RSP_VOID ATLWrapper::set_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn)
	{
		RSP_UINT32 index, offset;
		index = (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK * LPAGE_PER_PPAGE + ppn;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(index < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
#endif
		if (_COREID_ == 1 && channel == 0 && bank == 0 && ppn == 85134)
			dbg1++;
		if (_COREID_ == 1 && channel == 0 && bank == 0 && ppn == 81216)
			dbg1++;
		offset = index % BIT_PER_RSP_UINT32;
		index = index / BIT_PER_RSP_UINT32;
		VALID_DIRTY[index / BYTES_PER_SUPER_PAGE * sizeof(RSP_UINT32)] = true;
		RSP_ASSERT(!get_valid(channel, bank, ppn));
		VALID[index] |= 1 << offset;
	}
	RSP_BOOL ATLWrapper::get_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn)
	{
		RSP_UINT32 index, offset;
		index = (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK * LPAGE_PER_PPAGE + ppn;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(index < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
#endif
		offset = index % BIT_PER_RSP_UINT32;
		index = index / BIT_PER_RSP_UINT32;

		if(VALID[index] & (1 << offset))
			return true;
		else
			return false;
	}
	RSP_VOID ATLWrapper::clear_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn)
	{
		RSP_UINT32 index, offset;
		index = (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK * LPAGE_PER_PPAGE + ppn;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(index < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
#endif
		if (_COREID_ == 1 && channel == 0 && bank == 0 && ppn == 85134)
			dbg1++;
		if (_COREID_ == 1 && channel == 0 && bank == 0 && ppn == 81216)
			dbg1++;
		offset = index % BIT_PER_RSP_UINT32;
		index = index / BIT_PER_RSP_UINT32;
		VALID_DIRTY[index / BYTES_PER_SUPER_PAGE * sizeof(RSP_UINT32)] = true;
		RSP_ASSERT(get_valid(channel, bank, ppn));
		VALID[index] ^= (1 << offset);
	}

	//GC
	/////////////////////////////////////////////////////////////////////////////
	RSP_VOID ATLWrapper::garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_ASSERT(0);
		/*
		RSP_UINT8 value = 0;
		RSP_UINT32 cpy_count = 0;
		RSP_UINT32 vcount = 0;
		while(value != 2)
		{
			if(NAND_bank_state[channel][bank].GC_victim_blk == 0)
			{
				NAND_bank_state[channel][bank].GC_victim_blk = get_vt_vblock(channel, bank);
				if(NAND_bank_state[channel][bank].GC_free_page  == 0)
				{
					if(NAND_bank_state[channel][bank].GC_BLK == 0)
						RSP_ASSERT(0);
					NAND_bank_state[channel][bank].GC_free_page = NAND_bank_state[channel][bank].GC_BLK * PAGES_PER_BLK;
				}
				NAND_bank_state[channel][bank].GC_src_page = 0;
				NAND_bank_state[channel][bank].GC_plane = 0;
			}
			vcount= get_vcount(channel, bank, NAND_bank_state[channel][bank].GC_victim_blk);
			value = incremental_garbage_collection(channel, bank);

		}
		test_count(vcount);
		test_count(cpy_count);*/
	}
	int MAX_GC_vcount = 0;
	RSP_UINT8 ATLWrapper::incremental_garbage_collection(RSP_UINT32 channel,RSP_UINT32 bank, RSP_UINT8 flag)
	{
		RSP_UINT32 plane, high_low, vt_block, gc_block, free_ppn, src_page, spare_area[LPAGE_PER_PPAGE][SPARE_LPNS], old_ppn, buf_offset, new_ppn = NULL;
		RSP_UINT32 read_ppn, write_plane, spare_lpn, ppn;
		RSP_UINT32 temp_ppn;
		RSPReadOp RSP_read_op;
		RSP_BOOL copy_one_block = false;
		RSP_UINT8 return_val = 0;
		RSP_BOOL high_valid, low_valid;
		dbg8++;
		if(NAND_bank_state[channel][bank].GC_victim_blk == VC_MAX)
		{
			vt_block = get_vt_vblock(channel, bank);
			if (MAX_GC_vcount <= get_vcount(channel, bank, vt_block))
				MAX_GC_vcount = get_vcount(channel, bank, vt_block);
			if(vt_block == VC_MAX)
				return 1;
			NAND_bank_state[channel][bank].GC_victim_blk = vt_block;
			if(NAND_bank_state[channel][bank].GC_free_page  == VC_MAX)
			{
#ifdef ATL_ASSERTION_TEST
				if(NAND_bank_state[channel][bank].GC_BLK == VC_MAX)
					RSP_ASSERT(0);
#endif
				NAND_bank_state[channel][bank].GC_free_page = NAND_bank_state[channel][bank].GC_BLK * PAGES_PER_BLK;
				set_gc_block(channel, bank, (RSP_UINT32)VC_MAX);
			}
			NAND_bank_state[channel][bank].GC_src_page = 0;
			NAND_bank_state[channel][bank].GC_plane = 0;
		}
		else
		{
			vt_block = NAND_bank_state[channel][bank].GC_victim_blk;
		}
		free_ppn = NAND_bank_state[channel][bank].GC_free_page;
		gc_block = get_gc_block(channel, bank);
		src_page = NAND_bank_state[channel][bank].GC_src_page;
		plane = NAND_bank_state[channel][bank].GC_plane;

#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(vt_block < BLKS_PER_PLANE);
		RSP_ASSERT(gc_block < BLKS_PER_PLANE || gc_block == VC_MAX);
		RSP_ASSERT(vt_block != gc_block);
#endif

		for(;src_page < PAGES_PER_BLK && copy_one_block == false; src_page++ )
		{
			if(plane == PLANES_PER_BANK)
				plane = 0;
			for(;plane < PLANES_PER_BANK && copy_one_block == false; plane++)
			{

				

				old_ppn = (vt_block * PLANES_PER_BANK * PAGES_PER_BLK) + (plane * PAGES_PER_BLK) + (src_page % PAGES_PER_BLK);

				high_valid = get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE);
				low_valid = get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + 1);
				if (high_valid == false && low_valid == false)
					continue;
				copy_one_block = true;

				RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
				RSP_read_op.nReqID = RSP_INVALID_RID;
				RSP_read_op.nChannel = channel;
				RSP_read_op.nBank = bank;
				RSP_read_op.nBlock = get_block(old_ppn);
				RSP_read_op.nPage = get_page_offset(old_ppn);
				RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, 0);
				RSP_read_op.bmpTargetSector = 0xffff;

				RSP_read_op.m_nLPN = RSP_INVALID_LPN;

				m_pVFLWrapper->INC_READPENDING();
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 2);
				if(flag == Prof_FGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_FGC_read, 2);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_BGC_read, 2);
				m_pVFLWrapper->MetaIssue(RSP_read_op);

				m_pVFLWrapper->WAIT_READPENDING();

				m_pVFLWrapper->_GetSpareData(spare_area[0]);



				if (high_valid != false && low_valid != false)
				{
					RSP_BOOL copy_after_write = false;
					clear_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE);
					clear_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + 1);

					high_low = 0;
					spare_lpn = spare_area[high_low][0];
					if (spare_lpn == RSP_INVALID_LPN) // check later
						continue;
					read_ppn = get_ppn(spare_lpn, flag);
#ifdef ATL_ASSERTION_TEST
					RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
					RSP_ASSERT(read_ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_ppn == VC_MAX || is_in_write_buffer(read_ppn) || is_in_virtual(read_ppn) || is_in_realcopy(read_ppn));
#endif

					temp_ppn = ((channel * BANKS_PER_CHANNEL + bank) * (PAGES_PER_BANK));
					temp_ppn += old_ppn;
					//Read complete
					if (!((temp_ppn * LPAGE_PER_PPAGE + high_low) == read_ppn))	//if L2P equals to P2L //valid bitmap is true, but PPN is invalid
					{
						ppn = get_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, flag);
						if (is_in_virtual(ppn))
						{
							spare_lpn = ppn ^ VIRTUAL_BIT;
							set_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, VC_MAX, flag);
						}
						else
						{
							if (ppn == VC_MAX)
								RSP_ASSERT(0);
							if (LPN_ADDR[ppn].next == VC_MAX)
							{
								RSP_ASSERT(0);
								//spare_lpn = ((LPN_list*)ppn)->lpn;
								//del_list((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, spare_lpn);

							}
							else
								spare_lpn = (temp_ppn * LPAGE_PER_PPAGE + high_low) ^ VIRTUAL_BIT;
						}
					}

					write_plane = NAND_bank_state[channel][bank].GCbuf_index / 2;
					buf_offset = NAND_bank_state[channel][bank].GCbuf_index % 2;

					NAND_bank_state[channel][bank].GCbuf_index++;
					NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][0] = spare_lpn;

					if (NAND_bank_state[channel][bank].GCbuf_index == PLANES_PER_BANK * LPAGE_PER_PPAGE)
					{
						copy_after_write = true;
					}
					else
					{
						high_low = 1;
						spare_lpn = spare_area[high_low][0];
						if (spare_lpn == RSP_INVALID_LPN) // check later
							continue;
						read_ppn = get_ppn(spare_lpn, flag);
#ifdef ATL_ASSERTION_TEST

						RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
						RSP_ASSERT(read_ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_ppn == VC_MAX || is_in_write_buffer(read_ppn) || is_in_virtual(read_ppn) || is_in_realcopy(read_ppn));
#endif

						temp_ppn = ((channel * BANKS_PER_CHANNEL + bank) * (PAGES_PER_BANK));
						temp_ppn += old_ppn;
						//Read complete
						if (!((temp_ppn * LPAGE_PER_PPAGE + high_low) == read_ppn))	//if L2P equals to P2L //valid bitmap is true, but PPN is invalid
						{
							ppn = get_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, flag);
							if (is_in_virtual(ppn))
							{
								spare_lpn = ppn ^ VIRTUAL_BIT;
								set_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, VC_MAX, flag);
							}
							else
							{
								if (ppn == VC_MAX)
									RSP_ASSERT(0);
								if (LPN_ADDR[ppn].next == VC_MAX)
								{
									RSP_ASSERT(0);
									//spare_lpn = ((LPN_list*)ppn)->lpn;
									//del_list((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, spare_lpn);

								}
								else
									spare_lpn = (temp_ppn * LPAGE_PER_PPAGE + high_low) ^ VIRTUAL_BIT;
							}
						}

						write_plane = NAND_bank_state[channel][bank].GCbuf_index / 2;
						buf_offset = NAND_bank_state[channel][bank].GCbuf_index % 2;

						NAND_bank_state[channel][bank].GCbuf_index++;
						NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][0] = spare_lpn;
					}

					if (NAND_bank_state[channel][bank].GCbuf_index == PLANES_PER_BANK * LPAGE_PER_PPAGE)
					{
						new_ppn = free_ppn++;
						GC_write_buffer(channel, bank, new_ppn, flag);
						if(!copy_after_write)
							return_val = 1;
						
					}


					if (copy_after_write)
					{
						RSPOSAL::RSP_MemCpy((RSP_UINT32*)NAND_bank_state[channel][bank].GCbuf_addr,
							(RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, (PLANES_PER_BANK * LPAGE_PER_PPAGE) * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE),
							RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
						high_low = 1;
						spare_lpn = spare_area[high_low][0];
						if (spare_lpn == RSP_INVALID_LPN) // check later
							continue;
						read_ppn = get_ppn(spare_lpn, flag);
#ifdef ATL_ASSERTION_TEST
						RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
						RSP_ASSERT(read_ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_ppn == VC_MAX || is_in_write_buffer(read_ppn) || is_in_virtual(read_ppn) || is_in_realcopy(read_ppn));
#endif

						temp_ppn = ((channel * BANKS_PER_CHANNEL + bank) * (PAGES_PER_BANK));
						temp_ppn += old_ppn;
						//Read complete
						if (!((temp_ppn * LPAGE_PER_PPAGE + high_low) == read_ppn))	//if L2P equals to P2L //valid bitmap is true, but PPN is invalid
						{
							ppn = get_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, flag);
							if (is_in_virtual(ppn))
							{
								spare_lpn = ppn ^ VIRTUAL_BIT;
								set_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, VC_MAX, flag);
							}
							else
							{
								if (ppn == VC_MAX)
									RSP_ASSERT(0);
								if (LPN_ADDR[ppn].next == VC_MAX)
								{
									RSP_ASSERT(0);
									//spare_lpn = ((LPN_list*)ppn)->lpn;
									//del_list((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, spare_lpn);

								}
								else
									spare_lpn = (temp_ppn * LPAGE_PER_PPAGE + high_low) ^ VIRTUAL_BIT;
							}
						}

						write_plane = NAND_bank_state[channel][bank].GCbuf_index / 2;
						buf_offset = NAND_bank_state[channel][bank].GCbuf_index % 2;

						NAND_bank_state[channel][bank].GCbuf_index++;
						NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][0] = spare_lpn;

					}

				}
				else
				{
					if (high_valid != false)
					{
						high_low = 0;
						clear_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE);
					}
					else
					{
						high_low = 1;
						RSPOSAL::RSP_MemCpy((RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), 
											(RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, (NAND_bank_state[channel][bank].GCbuf_index + 1) * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE),
													RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
						clear_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + 1);
					}
					spare_lpn = spare_area[high_low][0];
					if (spare_lpn == RSP_INVALID_LPN) // check later
						continue;
					read_ppn = get_ppn(spare_lpn, flag);
#ifdef ATL_ASSERTION_TEST
					RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
					RSP_ASSERT(read_ppn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_ppn == VC_MAX || is_in_write_buffer(read_ppn) || is_in_virtual(read_ppn) || is_in_realcopy(read_ppn));
#endif

					temp_ppn = ((channel * BANKS_PER_CHANNEL + bank) * (PAGES_PER_BANK));
					temp_ppn += old_ppn;

					//Read complete
					if (!((temp_ppn * LPAGE_PER_PPAGE + high_low) == read_ppn))	//if L2P equals to P2L //valid bitmap is true, but PPN is invalid
					{
						ppn = get_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, flag);
						if (is_in_virtual(ppn))
						{
							spare_lpn = ppn ^ VIRTUAL_BIT;
							set_P2L((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, VC_MAX, flag);
						}
						else
						{
							if (ppn == VC_MAX)
								RSP_ASSERT(0);
							if (LPN_ADDR[ppn].next == VC_MAX)
							{
								RSP_ASSERT(0);
								//spare_lpn = ((LPN_list*)ppn)->lpn;
								//del_list((old_ppn + (channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) * LPAGE_PER_PPAGE + high_low, spare_lpn);

							}
							else
								spare_lpn = (temp_ppn * LPAGE_PER_PPAGE + high_low) ^ VIRTUAL_BIT;
						}
					}

					write_plane = NAND_bank_state[channel][bank].GCbuf_index / 2;
					buf_offset = NAND_bank_state[channel][bank].GCbuf_index % 2;

					NAND_bank_state[channel][bank].GCbuf_index++;
					NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][0] = spare_lpn;


					if (NAND_bank_state[channel][bank].GCbuf_index == PLANES_PER_BANK * LPAGE_PER_PPAGE)
					{
						new_ppn = free_ppn++;
						GC_write_buffer(channel, bank, new_ppn, flag);
						return_val = 1;
					}
				}
			}
		}
		if(plane != PLANES_PER_BANK)
			src_page--;




		if(free_ppn / PAGES_PER_BLK !=  NAND_bank_state[channel][bank].GC_free_page / PAGES_PER_BLK)
		{
			free_ppn = NAND_bank_state[channel][bank].GC_BLK * PAGES_PER_BLK;
			set_gc_block(channel, bank, VC_MAX);
			NAND_bank_state[channel][bank].GC_free_page = free_ppn;
		}

		if(src_page == PAGES_PER_BLK)
		{

			if(NAND_bank_state[channel][bank].GCbuf_index != 0)
			{
				new_ppn = free_ppn++;
				GC_write_buffer(channel, bank, new_ppn, flag);
				if(free_ppn / PAGES_PER_BLK !=  NAND_bank_state[channel][bank].GC_free_page / PAGES_PER_BLK)
				{
				
					free_ppn = NAND_bank_state[channel][bank].GC_BLK * PAGES_PER_BLK;

				

					set_gc_block(channel, bank, VC_MAX);
				}
			}
			return_val = 2;
			if (channel == 1 && bank == 0, vt_block == 33)
				dbg1++;
			//valid_bitmap test
			{
				RSP_UINT32 tCh, tbank, tpage, tplane, thigh_low, told_page;
				for (tpage = 0; tpage < PAGES_PER_BLK; tpage++)
					for (tplane = 0; tplane < PLANES_PER_BANK; tplane++)
						for (thigh_low = 0; thigh_low < 2; thigh_low++)
						{
							told_page = (vt_block * PLANES_PER_BANK * PAGES_PER_BLK) + (tplane * PAGES_PER_BLK) + (tpage % PAGES_PER_BLK);
							if (channel == 1 && bank == 0 && told_page * LPAGE_PER_PPAGE + thigh_low == 33828)
								dbg1++;
							if (get_valid(channel, bank, told_page * LPAGE_PER_PPAGE + thigh_low))
								RSP_ASSERT(0);
						}
			}
		

			NAND_bank_state[channel][bank].GC_victim_blk = VC_MAX;
			
			if(NAND_bank_state[channel][bank].GC_BLK != VC_MAX)
			{
				set_vcount(channel, bank, vt_block, (RSP_UINT32)VC_MAX);
				add_free_blk(&NAND_bank_state[channel][bank].free_blk_list, &NAND_bank_state[channel][bank].blk_list[vt_block]);
			}
			else
			{
				erase_wrapper(channel, bank, vt_block);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
				if(flag == Prof_FGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_FGC_erase, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_BGC_erase, 1);
				
				set_gc_block(channel, bank, vt_block);
				set_vcount(channel, bank, vt_block, 0);
			}

			//dummy_buffer_flush();
		}
		NAND_bank_state[channel][bank].GC_src_page = src_page;
		NAND_bank_state[channel][bank].GC_plane = plane;
		NAND_bank_state[channel][bank].GC_free_page = free_ppn;
		return return_val;
	}




	RSP_UINT32 ATLWrapper::get_vt_vblock(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 refcount, temp_vcount = PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE, result_block = 0, block, vcount;
		for (block = 0; block < BLKS_PER_PLANE; block++)
		{
			if(block == NAND_bank_state[channel][bank].GC_BLK || block == NAND_bank_state[channel][bank].GC_free_page / PAGES_PER_BLK)
				continue;
			if(block == NAND_bank_state[channel][bank].cur_write_ppn / PAGES_PER_BLK)
				continue;
			vcount = get_vcount(channel, bank, block);
			refcount = get_refcount(channel, bank, block);
			if (refcount != 0)
				continue;
			if (temp_vcount > vcount)
			{
				result_block = block;
				temp_vcount = vcount;
			}
		}
		if(temp_vcount == PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE)
			return VC_MAX;
#ifdef ATL_ASSERTION_TEST
		RSP_ASSERT(temp_vcount < PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE);
#endif
		return result_block;
	}
	RSP_VOID ATLWrapper::GC_write_buffer(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 free_ppn, RSP_UINT8 flag)
	{
		RSP_UINT32 plane, buf_offset, new_ppn = NULL;
		RSP_UINT32 plane_ppn[LPAGE_PER_PPAGE], to_write, super_blk, ppn, head;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];

		to_write = NAND_bank_state[channel][bank].GCbuf_index;
		new_ppn = free_ppn;
		super_blk = new_ppn / PAGES_PER_BLK;
		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
			{
				plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;
				if (plane * LPAGE_PER_PPAGE + buf_offset < to_write)
				{
					if (is_in_virtual(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0]))
					{
						head = get_P2L(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] ^ VIRTUAL_BIT, flag);
						ppn = plane_ppn[buf_offset];
						if (is_in_virtual(head))
						{
							set_ppn(head ^ VIRTUAL_BIT, plane_ppn[buf_offset], flag);
							set_P2L(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] ^ VIRTUAL_BIT, VC_MAX, flag);
						}
						else if (LPN_ADDR[head].next == VC_MAX)
						{
							RSP_ASSERT(0);
							//set_ppn(head->lpn, plane_ppn[buf_offset], flag);
							//del_list(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] ^ VIRTUAL_BIT, head->lpn);
							
						}
						else
						{
							//changing P2V, V2P, L2P
							set_P2L(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] ^ VIRTUAL_BIT, VC_MAX, flag);
							set_P2L(ppn, head, flag);

							ppn ^= VIRTUAL_BIT;
							LPN_list *iter = &LPN_ADDR[head];
							for (int i = 0; iter->next != VC_MAX; i++)
							{
								set_ppn(iter->lpn, ppn, flag);
								iter = &LPN_ADDR[iter->next];
							}

							NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] = LPN_ADDR[head].lpn;
						}
					}
					else
						set_ppn(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0], plane_ppn[buf_offset], flag);
					set_valid(channel, bank, (plane_ppn[buf_offset]) % (PAGES_PER_BANK * LPAGE_PER_PPAGE));
				}
				else
					NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][0] = RSP_INVALID_LPN;
			}



			RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, plane * RSP_BYTES_PER_PAGE);
			RSP_write_ops[plane].pSpareData = &NAND_bank_state[channel][bank].GCbuf_lpn[plane][0][0];
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].bmpTargetSector = 0xffff;
			RSP_write_ops[plane].nBlock = get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = NAND_bank_state[channel][bank].GCbuf_lpn[plane][0][0];
			RSP_write_ops[plane].m_anLPN[1] = NAND_bank_state[channel][bank].GCbuf_lpn[plane][1][0];

		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, to_write);
		if(flag == Prof_FGC)
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_FGC_write, to_write);
		else
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_BGC_write, to_write);
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		NAND_bank_state[channel][bank].GCbuf_index = 0;
		set_vcount(channel, bank, super_blk, get_vcount(channel, bank, super_blk) + to_write);
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif
	}


	RSP_VOID ATLWrapper::RSP_BufferCopy(RSP_UINT32* pstDescBuffer, RSP_UINT32* pstSrcBuffer, RSP_UINT32 bmpTargetSector)
	{
		RSP_UINT32* pstDestBuf;
		RSP_UINT32* pstSrcBuf;

		pstDestBuf = pstDescBuffer;
		pstSrcBuf = pstSrcBuffer;

		for (RSP_UINT32 nSector = 0; nSector < SECTORS_PER_PAGE; nSector++)
		{
			if (RSP_CheckBit(bmpTargetSector, 1 << nSector) == ATL_TRUE)
			{
				RSPOSAL::RSP_BufferMemCpy(pstDestBuf, pstSrcBuf, BYTES_PER_SECTOR);
			}

			pstDestBuf += (BYTES_PER_SECTOR / sizeof(RSP_UINT32));
			pstSrcBuf += (BYTES_PER_SECTOR / sizeof(RSP_UINT32));
		}
	}

	RSP_BOOL ATLWrapper::RSP_CheckBit(RSP_SECTOR_BITMAP nVar, RSP_SECTOR_BITMAP nBit)
	{
		return (nBit == (nVar&nBit));
	}

	RSP_UINT32 ATLWrapper::get_free_blk(free_blk_head *list)
	{
		RSP_UINT32 return_val;
		if(list->count == 0)
			return VC_MAX;
		return_val = (list->head)->block_offset;
		del_free_blk(list);
		return return_val;
	}
	RSP_VOID ATLWrapper::add_free_blk(free_blk_head *list, BLK_STRUCT* temp)
	{

		if(list->count == 0)
		{
			list->head = (BLK_STRUCT*)temp;
			temp->before = temp;
			temp->next = temp;
		}
		else
		{
			temp->before = (BLK_STRUCT*)(list->head)->before;
			temp->next = (BLK_STRUCT*)list->head;
			list->head->before->next = temp;
			list->head->before = temp;
		}

		list->count++;

	}
	RSP_VOID ATLWrapper::del_free_blk(free_blk_head *list)
	{
		BLK_STRUCT*temp = (BLK_STRUCT*)list->head;
#ifdef ATL_ASSERTION_TEST
		if(list->count == 0)
			RSP_ASSERT(0);
#endif
		list->head = (BLK_STRUCT*)temp->next;
		if(list->count != 1)
		{
			(temp->next)->before = (BLK_STRUCT*)temp->before;
			(temp->before)->next = (BLK_STRUCT*)temp->next;
		}
		temp->before = NULL;
		temp->next = NULL;


		list->count--;
	}

	RSP_BOOL ATLWrapper::RSP_Open(RSP_VOID)
	{
		RSP_UINT32 channel, bank, block = 0, loop;
		SM_value = NULL;

		NUM_PBLK = NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * BLKS_PER_PLANE * PLANES_PER_BANK;
		//NUM_LBLK = NUM_PBLK - OP_BLKS;
		NUM_LBLK = NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * BLKS_PER_PLANE * PLANES_PER_BANK - OP_BLKS;
		NUM_CACHED_MAP[L2P] = (NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE) / (BYTES_PER_SUPER_PAGE)* 4;
		NUM_CACHED_MAP[P2L] = CMT_size / (BYTES_PER_SUPER_PAGE);
		VCOUNT = (RSP_UINT32*)rspmalloc(NUM_PBLK * sizeof_u32);
		VALID = (RSP_UINT32*)rspmalloc(NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE / BIT_PER_BYTES);
		VALID_DIRTY = (RSP_BOOL*)rspmalloc(NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE / BYTES_PER_SUPER_PAGE);

		CACHE_ADDR[L2P] = (RSP_UINT32*)rspmalloc(NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE * sizeof_u32);
		CACHE_ADDR[P2L] = (RSP_UINT32*)rspmalloc(CMT_size);
		///////////////

		CACHE_MAPPING_TABLE[L2P] = (cached_map_list*)rspmalloc(NUM_CACHED_MAP[L2P] * sizeof(cached_map_list));
		CACHE_MAPPING_TABLE[P2L] = (cached_map_list*)rspmalloc(NUM_CACHED_MAP[P2L] * sizeof(cached_map_list));
		CACHED_MAP_HEAD[L2P] = NULL;
		CACHED_MAP_HEAD[P2L] = NULL;
		//Map Mapping Table
		MAP_MAPPING_TABLE[L2P] = (RSP_UINT32*)rspmalloc(NUM_MAP_ENTRY(L2P) * sizeof_u32); //GTD
		MAP_VALID_COUNT[L2P] = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK(L2P) * sizeof_u32);
		MAPP2L[L2P] = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK(L2P) * PAGES_PER_BLK * sizeof_u32);
		CACHE_MAP_DIRTY_TABLE[L2P] = (RSP_BOOL*)rspmalloc(NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE / 8192);


		P2L_VALID_COUNT = (RSP_UINT16*)rspmalloc(NUM_MAP_ENTRY(P2L) * sizeof_u16);
		MAP_MAPPING_TABLE[P2L] = (RSP_UINT32*)rspmalloc(NUM_MAP_ENTRY(P2L) * sizeof_u32); //GTD
		MAP_VALID_COUNT[P2L] = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK(P2L) * sizeof_u32);
		MAPP2L[P2L] = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK(P2L) * PAGES_PER_BLK * sizeof_u32);
		CACHE_MAP_DIRTY_TABLE[P2L] = (RSP_BOOL*)rspmalloc(NUM_CACHED_MAP[P2L]);
		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
			NAND_bank_state[channel] = (NAND_bankstat *)rspmalloc(BANKS_PER_CHANNEL * sizeof(NAND_bankstat)); //need to change sizeof API into specific size

		//FTL initialize
		num_cached[L2P] = 0;
		num_cached[P2L] = 0;

		//bank initialize
		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
		{
			for (bank = 0; bank < BANKS_PER_CHANNEL; bank++)
			{



				block = 0;
				NAND_bank_state[channel][bank].blk_list = (BLK_STRUCT*)rspmalloc(BLKS_PER_PLANE * sizeof(BLK_STRUCT));
				NAND_bank_state[channel][bank].free_blk_list.count = 0;
				NAND_bank_state[channel][bank].map_blk_list[L2P].count = 0;
				NAND_bank_state[channel][bank].map_blk_list[P2L].count = 0;

				//GC_value
				NAND_bank_state[channel][bank].GC_victim_blk = VC_MAX;
				NAND_bank_state[channel][bank].GC_src_page = 0;
				NAND_bank_state[channel][bank].GC_plane = 0;
				NAND_bank_state[channel][bank].GC_free_page = VC_MAX;

				//Map GC_value
				NAND_bank_state[channel][bank].MAP_GC_victim_blk[L2P] = VC_MAX;
				NAND_bank_state[channel][bank].MAP_GC_src_page[L2P] = 0;
				NAND_bank_state[channel][bank].MAP_GC_free_page[L2P] = VC_MAX;	
				
				NAND_bank_state[channel][bank].MAP_GC_victim_blk[P2L] = VC_MAX;
				NAND_bank_state[channel][bank].MAP_GC_src_page[P2L] = 0;
				NAND_bank_state[channel][bank].MAP_GC_free_page[P2L] = VC_MAX;
				//
				NAND_bank_state[channel][bank].write_start = false;
				NAND_bank_state[channel][bank].map_start[0] = false;
				NAND_bank_state[channel][bank].map_start[1] = false;
				NAND_bank_state[channel][bank].meta_start = false;

				writebuf_bitmap = 0;
				writebuf_index = 0;

				for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++){
					writebuf_addr[iter] = NULL;
					writebuf_data_bitmap[iter] = 0;
					writebuf_orig_data_bitmap[iter] = 0;
					for (RSP_UINT8 ineriter = 0; ineriter < LPAGE_PER_PPAGE; ineriter++)
					{
						writebuf_lpn[iter][ineriter][0] = RSP_INVALID_LPN;
						writebuf_lpn[iter][ineriter][1] = RSP_INVALID_LPN;
						writebuf_lpn[iter][ineriter][2] = RSP_INVALID_LPN;
						writebuf_orig_lpn[iter][ineriter] = RSP_INVALID_LPN;
					}
				}


				//Meta blk
				erase_wrapper(channel, bank, block);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
				NAND_bank_state[channel][bank].meta_blk = block;
				set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);
				//MAP blk
				NAND_bank_state[channel][bank].cur_map_ppn[L2P] = PAGES_PER_BLK;
				NAND_bank_state[channel][bank].MAP_blk_offset[L2P] = (RSP_UINT32*)rspmalloc(MAP_ENTRY_BLK_PER_BANK(L2P) * sizeof(RSP_UINT32));
				for (loop = 0; loop < MAP_ENTRY_BLK_PER_BANK(L2P); loop++)
				{
					erase_wrapper(channel, bank, block);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
					NAND_bank_state[channel][bank].MAP_blk_offset[L2P][loop] =block;
					add_free_blk(&NAND_bank_state[channel][bank].map_blk_list[L2P], &NAND_bank_state[channel][bank].blk_list[block]);
					set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);
					MAP_VALID_COUNT[L2P][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(L2P) + loop] = VC_MAX;
					
				}
				del_free_blk(&NAND_bank_state[channel][bank].map_blk_list[L2P]);
				del_free_blk(&NAND_bank_state[channel][bank].map_blk_list[L2P]);
				NAND_bank_state[channel][bank].MAP_GC_BLK[L2P] = 0;
				MAP_VALID_COUNT[L2P][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(L2P) + 1] = 0;
				MAP_VALID_COUNT[L2P][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(L2P) + 0] = 0;

				//P2L MAP

				NAND_bank_state[channel][bank].cur_map_ppn[P2L] = PAGES_PER_BLK;
				NAND_bank_state[channel][bank].MAP_blk_offset[P2L] = (RSP_UINT32*)rspmalloc(MAP_ENTRY_BLK_PER_BANK(P2L) * sizeof(RSP_UINT32));
				for (loop = 0; loop < MAP_ENTRY_BLK_PER_BANK(P2L); loop++)
				{
					erase_wrapper(channel, bank, block);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
					NAND_bank_state[channel][bank].MAP_blk_offset[P2L][loop] =block;
					add_free_blk(&NAND_bank_state[channel][bank].map_blk_list[P2L], &NAND_bank_state[channel][bank].blk_list[block]);					
					set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);
					MAP_VALID_COUNT[P2L][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(P2L) + loop] = VC_MAX;
					
				}
				del_free_blk(&NAND_bank_state[channel][bank].map_blk_list[P2L]);
				del_free_blk(&NAND_bank_state[channel][bank].map_blk_list[P2L]);
				NAND_bank_state[channel][bank].MAP_GC_BLK[P2L] = 0;
				MAP_VALID_COUNT[P2L][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(P2L) + 1] = 0;
				MAP_VALID_COUNT[P2L][(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK(P2L) + 0] = 0;
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				NAND_bank_state[channel][bank].cur_meta_ppn = 0;
				for (RSP_UINT8 iter = 0; iter < 2; iter++)
					NAND_bank_state[channel][bank].cpybuf_addr[iter] = (RSP_UINT32*)rspmalloc(BYTES_PER_SUPER_PAGE + RSP_BYTES_PER_PAGE);
				//NAND_bank_state[channel][bank].writebuf_addr = (RSP_UINT32*)rspmalloc(BYTES_PER_SUPER_PAGE);
				for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++)
				{
					writebuf_addr[iter] = NULL;
				}
				NAND_bank_state[channel][bank].GCbuf_addr = (RSP_UINT32*)rspmalloc(BYTES_PER_SUPER_PAGE + RSP_BYTES_PER_PAGE);
				NAND_bank_state[channel][bank].GCbuf_index = 0;

				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				//valid_bitmap blk
				erase_wrapper(channel, bank, block);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
				NAND_bank_state[channel][bank].VALID_BITMAP_PAGE = block * PAGES_PER_BLK;
				set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);

				////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				//GC blk
				set_gc_block(channel, bank, block);
				erase_wrapper(channel, bank, block);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
				set_vcount(channel, bank, block++, 0);

				//active blk
				erase_wrapper(channel, bank, block);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_init_erase, 1);
				set_new_write_ppn(channel, bank, PAGES_PER_BLK * block);
				set_vcount(channel, bank, block++, 0);

				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


				//normal blk
				
				for (; block < BLKS_PER_PLANE; block++)
				{
					set_vcount(channel, bank, block, (RSP_UINT32)VC_MAX);
					add_free_blk(&NAND_bank_state[channel][bank].free_blk_list, &NAND_bank_state[channel][bank].blk_list[block]);
				}
				
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				NAND_bank_state[channel][bank].cpybuf_index = 0;
				
				for(loop = 0; loop < BLKS_PER_PLANE;loop++)
					NAND_bank_state[channel][bank].blk_list[loop].block_offset = loop;
				dbg1 = 0;
			}
		}
		for (loop = 0; loop < NUM_CACHED_MAP[P2L]; loop++){
			CACHE_MAPPING_TABLE[L2P][loop].offset = loop;
			CACHE_MAPPING_TABLE[L2P][loop].next = NULL;
			CACHE_MAP_DIRTY_TABLE[L2P][loop] = false;

			CACHE_MAPPING_TABLE[P2L][loop].offset = loop;
			CACHE_MAPPING_TABLE[P2L][loop].next = NULL;
			CACHE_MAP_DIRTY_TABLE[P2L][loop] = false;
		}
		for (loop = 0; loop < NUM_MAP_ENTRY(L2P); loop++){
			MAP_MAPPING_TABLE[L2P][loop] = (RSP_UINT32)VC_MAX;
		}
		for (loop = 0; loop < NUM_MAP_ENTRY(P2L); loop++){
			MAP_MAPPING_TABLE[P2L][loop] = (RSP_UINT32)VC_MAX;
			P2L_VALID_COUNT[loop] = 0;
		}
		


		for (loop = 0; loop < TOTAL_MAP_BLK(L2P) * PAGES_PER_BLK; loop++)
		{
			MAPP2L[L2P][loop] = VC_MAX;
		}
		for (loop = 0; loop < TOTAL_MAP_BLK(P2L) * PAGES_PER_BLK; loop++)
		{
			MAPP2L[P2L][loop] = VC_MAX;
		}

		RSPOSAL::RSP_MemSet(CACHE_ADDR[L2P], 0xff, NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE * sizeof_u32);
		RSPOSAL::RSP_MemSet(CACHE_ADDR[P2L], 0xff, CMT_size);
		RSPOSAL::RSP_MemSet((RSP_UINT32*)VALID, 0, NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE / BIT_PER_BYTES);
		

		flush_bank_counter = 0;
		ATL_cur_write_bank = 0;
		ATL_meta_cur_write_bank = 0;
		ATL_metadata_cur_write_bank = 0;
		//L2P list alloc
		LPN_ADDR = (LPN_list*)rspmalloc(L2V_MAX_ENTRY * sizeof(LPN_list));
		LPN_LIST_HEAD.count = 0;
		LPN_LIST_HEAD.head = VC_MAX;
		sc = (special_command*)rspmalloc(RSP_BYTES_PER_PAGE);
		sc_other = (special_command*)rspmalloc(RSP_BYTES_PER_PAGE);
		for(loop = 0; loop < L2V_MAX_ENTRY; loop++)
		{
			LPN_ADDR[loop].offset = loop;
			LPN_ADDR[loop].next = LPN_LIST_HEAD.head;
			LPN_LIST_HEAD.head = loop;
			LPN_LIST_HEAD.count++;
		}


		free_list_count = L2V_MAX_ENTRY;


		cur_VC_lpn = 0;
		cur_VC_struct = (special_command*)rspmalloc(RSP_BYTES_PER_PAGE);
		pending_VC_count = 0;
		free_VC_lpn = 0;
		remap_start = 0;
		//meta_write
		meta_write_buffer = (RSP_UINT32*)rspmalloc(NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		one_realcopy_buffer = (RSP_UINT32*)rspmalloc(RSP_BYTES_PER_PAGE);
		meta_write_count = 0;
		for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++)
		{
			for (RSP_UINT8 iner_iter = 0; iner_iter < LPAGE_PER_PPAGE; iner_iter++)
				meta_write_lpn[iter][iner_iter][0] = RSP_INVALID_LPN;
		}

		if (THIS_CORE == 0)
		{
			SM_value = (SM_struct*)rspmalloc(sizeof(RSP_UINT32)* TOTAL_SM_VALUES);
			SM_value->value[51] = 0;//1;
		}
#ifdef VC_debug_JBD
		JBD_state = (RSP_UINT32*)rspmalloc(131072 * sizeof(RSP_UINT32));
		RSPOSAL::RSP_MemSet(JBD_state, 0x00, 131072 * sizeof(RSP_UINT32));
		debug_sc = (special_command*)rspmalloc(sizeof(special_command) * 5000);
#endif
		DB_GC = (RSP_UINT32*)add_addr(CACHE_ADDR[L2P], 60480 * sizeof_u32);
		//Profile init
		for(loop = 0; loop < Prof_total_num;loop++)
			m_pVFLWrapper->RSP_SetProfileData(loop,0);
		return true;
	}
#ifdef Hesper_DEBUG
	RSP_VOID ATLWrapper::L2P_test()
	{
	}

	RSP_VOID ATLWrapper::L2P_spare_test()
	{
		RSP_UINT32 channel, bank, block, src_page, plane, high_low = 0, vcount, old_ppn, real_vcount;
		RSP_UINT32 spare_area[LPAGE_PER_PPAGE][SPARE_LPNS], spare_lpn;
		RSPReadOp RSP_read_op;

		for (channel = 0; channel < RSP_NUM_CHANNEL; channel++)
		{
			for (bank = 0; bank < RSP_NUM_BANK; bank++)
			{
				for (block = 0; block < BLKS_PER_PLANE; block++)
				{
					vcount = get_vcount(channel, bank, block);
					real_vcount = 0;
					if (vcount == 0 || vcount == VC_MAX)
						continue;
					for (src_page = 0; src_page < PAGES_PER_BLK; src_page++)
					{
						for (plane = 0; plane < PLANES_PER_BANK; plane++)
						{
							RSP_BOOL valid_high, valid_low;
							old_ppn = (block * PLANES_PER_BANK * PAGES_PER_BLK) + (plane * PAGES_PER_BLK) + (src_page % PAGES_PER_BLK);
							valid_high = get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE);
							valid_low = get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + 1);

							if (valid_high == false && valid_low == false)
								continue;


							RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
								//RSP_read_op.pSpareData = NULL;
							RSP_read_op.nReqID = RSP_INVALID_RID;
							RSP_read_op.nChannel = channel;
							RSP_read_op.nBank = bank;
							RSP_read_op.nBlock = get_block(old_ppn);
							RSP_read_op.nPage = get_page_offset(old_ppn);
							RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, high_low);
							RSP_read_op.bmpTargetSector = 0xffff;
							RSP_read_op.m_nLPN = RSP_INVALID_LPN;
							
							m_pVFLWrapper->INC_READPENDING();
							m_pVFLWrapper->MetaIssue(RSP_read_op);
							m_pVFLWrapper->WAIT_READPENDING();
							m_pVFLWrapper->_GetSpareData(spare_area[0]);
							spare_lpn = spare_area[0][0];
							if (spare_lpn == RSP_INVALID_LPN) // check later
									continue;
							real_vcount++;

							spare_lpn = spare_area[1][0];
							if (spare_lpn == RSP_INVALID_LPN) // check later
								continue;
							real_vcount++;

						}

					}

					if (vcount != real_vcount && vcount != VC_MAX)
						RSP_ASSERT(0);
				}
			}
		}


	}

	RSP_VOID ATLWrapper::valid_count_test()
	{
		RSP_UINT32 channel, bank, block, src_page, plane, high_low, vcount, old_ppn, real_vcount;
		for (channel = 0; channel < RSP_NUM_CHANNEL; channel++)
		{
			for (bank = 0; bank < RSP_NUM_BANK; bank++)
			{
				for (block = 0; block < BLKS_PER_PLANE; block++)
				{
					if(NAND_bank_state[channel][bank].GC_victim_blk == block)
						continue;
					vcount = get_vcount(channel, bank, block);
					real_vcount = 0;
					for (src_page = 0; src_page < PAGES_PER_BLK; src_page++)
					{
						for (plane = 0; plane < PLANES_PER_BANK; plane++)
						{
							for (high_low = 0; high_low < LPAGE_PER_PPAGE; high_low++)
							{

								old_ppn = (block * PLANES_PER_BANK * PAGES_PER_BLK) + (plane * PAGES_PER_BLK) + (src_page % PAGES_PER_BLK);
								if (!get_valid(channel, bank, old_ppn * LPAGE_PER_PPAGE + high_low))
									continue;
								real_vcount++;
							

							}

						}

					}

					if (vcount != real_vcount && vcount != VC_MAX)
						RSP_ASSERT(0);
				}
			}
		}
	}

		//Cache slot check
	void ATLWrapper::Check_cache_slot(RSP_UINT32 *Buffaddr)
	{
		RSP_UINT32 test_val;
		for(RSP_UINT32 loop = 0; loop < NUM_PAGES_PER_MAP; loop++)
		{
			RSPOSAL::RSP_MemCpy(&test_val, (RSP_UINT32 *)add_addr(Buffaddr,  loop * sizeof_u32), sizeof_u32);
			RSP_ASSERT(test_val < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || test_val == VC_MAX || is_in_write_buffer(test_val) || is_in_realcopy(test_val) || is_in_virtual(test_val));
		}
	}

#endif

	RSP_VOID* ATLWrapper::add_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
	{
		return (RSP_VOID *)((RSP_UINT32)start_addr + offset);
	}
	RSP_VOID* ATLWrapper::sub_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
	{
		return (RSP_VOID *)((RSP_UINT32)start_addr - offset);
	}

	RSP_VOID ATLWrapper::flush()
	{

		dummy_buffer_flush();

		meta_flush();

		m_pVFLWrapper->RSP_INC_ProfileData(Prof_num_flush, 1);
	}
	RSP_VOID ATLWrapper::flush_Remap()
	{
		if(Flush_method == ALL_FLUSH)
		{
			dummy_buffer_flush();
		}

		if(Flush_method != NO_FLUSH)
		{
			meta_flush();
		}
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_flush, 1);
	}

	RSP_VOID ATLWrapper::valid_bitmap_flush()
	{
		RSP_UINT32 bank, channel;

		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
		{
			for (bank = 0; bank < BANKS_PER_CHANNEL; bank++)
			{
				bank_valid_bitmap_flush(channel, bank);
			}
		}
	}
	RSP_VOID ATLWrapper::bank_valid_bitmap_flush(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 start, end, loop, write_channel, write_bank, write_blk, plane;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		start = (channel * BANKS_PER_CHANNEL + bank) * PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE / BIT_PER_BYTES / BYTES_PER_SUPER_PAGE;
		end = ((channel * BANKS_PER_CHANNEL + bank + 1) * PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE - 1) / BIT_PER_BYTES / BYTES_PER_SUPER_PAGE;

		for(loop = start; loop < end + 1; loop++)
		{
			if(VALID_DIRTY[loop] == true)
			{
				write_channel = loop % (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL);
				write_bank = write_channel % BANKS_PER_CHANNEL;
				write_channel /= BANKS_PER_CHANNEL;
				write_blk = NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE / PAGES_PER_BLK;


				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[write_channel][bank].cpybuf_addr[0], plane * RSP_BYTES_PER_PAGE);
					RSP_write_ops[plane].pSpareData = NULL_SPARE;
					RSP_write_ops[plane].nChannel = write_channel;
					RSP_write_ops[plane].nBank = write_bank;
					RSP_write_ops[plane].nBlock = (write_blk) * PLANES_PER_BANK + plane;
					RSP_write_ops[plane].bmpTargetSector = 0xffff;
					RSP_write_ops[plane].nPage = NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE % PAGES_PER_BLK;
					RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
					RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
					RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
					RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
				}
				m_pVFLWrapper->INC_PROGRAMPENDING();
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_VB_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				m_pVFLWrapper->MetaIssue(RSP_write_ops);

#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif

				NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE++;
				if(NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE % PAGES_PER_BLK == 0)
				{
					RSP_UINT32 temp_blk = get_free_blk(&NAND_bank_state[write_channel][write_bank].free_blk_list);
					if(temp_blk == VC_MAX)
					{//erase blk
						NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE -= PAGES_PER_BLK;
						
					}
					else
					{
						add_free_blk(&NAND_bank_state[write_channel][write_bank].free_blk_list, &NAND_bank_state[channel][bank].blk_list[write_blk]);
						write_blk = temp_blk;
						NAND_bank_state[write_channel][write_bank].VALID_BITMAP_PAGE = temp_blk * PAGES_PER_BLK;
					}
					//active blk
					erase_wrapper(write_channel, write_bank, write_blk);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);

				}

			}
			VALID_DIRTY[loop] = false;
		}
	}
	RSP_VOID ATLWrapper::flush_banks()
	{
		if(flush_bank_start)
		{
			map_flush();
			if(remap_start)
				LPN_list_flush();
			valid_bitmap_flush();
			meta_flush();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_num_Bank_flush, 1);
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
		}
	}
	RSP_VOID ATLWrapper::map_flush()
	{
		RSP_UINT32 loop;
		for(loop = 0; loop < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE / 8192; loop++)
		{
			if (CACHE_MAP_DIRTY_TABLE[L2P][loop])
			{
				CACHE_MAP_DIRTY_TABLE[L2P][loop] = false;
				map_write(loop, loop, Prof_Flush, L2P);
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_L2P_flush_log, 1);
			}
		}
		for (loop = 0; loop < NUM_CACHED_MAP[P2L]; loop++)
		{
			if(CACHE_MAP_DIRTY_TABLE[P2L][loop])
			{
				CACHE_MAP_DIRTY_TABLE[P2L][loop] = false;
				map_write(CACHE_MAPPING_TABLE[P2L][loop].map_page, loop, Prof_Flush, P2L);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_P2L_flush_log, 1);
			}
		}
	}

	RSP_VOID ATLWrapper::meta_flush()
	{
		RSP_UINT32 bank, channel;
		channel = get_channel(ATL_metadata_cur_write_bank);
		bank = get_bank(ATL_metadata_cur_write_bank);
		ATL_metadata_cur_write_bank = (ATL_metadata_cur_write_bank + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
		/*for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
		{
			for (bank = 0; bank < BANKS_PER_CHANNEL; bank++)
			{
				bank_meta_flush(channel, bank);
			}
		}*/
		bank_meta_flush(channel, bank);
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_meta_flush, 1);
		m_pVFLWrapper->WAIT_PROGRAMPENDING();
	}


	RSP_VOID ATLWrapper::buffer_flush(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 plane, buf_offset, plane_ppn[LPAGE_PER_PPAGE], super_blk;
		while(!check_realcopy_done());
		for (plane = 0; plane < writebuf_index; plane++)
		{
			for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
			{
				if (writebuf_lpn[plane][buf_offset][0] == RSP_INVALID_LPN)
				{
					if (buf_offset)
						writebuf_data_bitmap[plane] |= 0xff00;
					else
						writebuf_data_bitmap[plane] |= 0xff;
					continue;
				}
				else if (is_in_virtual(writebuf_lpn[plane][buf_offset][0]))
				{

				}
				else
				{
					if(buf_offset)
						meta_write_page(writebuf_lpn[plane][buf_offset][0], (RSP_UINT32*)add_addr(writebuf_addr[plane], RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), true, false);
					else
						meta_write_page(writebuf_lpn[plane][buf_offset][0], writebuf_addr[plane], true, false);
					writebuf_lpn[plane][buf_offset][0] |= VIRTUAL_BIT;
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_buffer_flush_write, 1);

				}
			}
		}
		
		meta_buffer_flush();
		//m_pVFLWrapper->WAIT_PROGRAMPENDING();

	}
	RSP_UINT32 min(RSP_UINT32 a, RSP_UINT32 b)
	{
		if (a >= b)
			return b;
		else
			return a;
	}
	RSP_VOID ATLWrapper::LPN_list_flush()
	{
			dummy_buffer_flush();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_LPN_list_log_write, 1);

	}
	RSP_VOID ATLWrapper::dummy_buffer_flush()
	{
		RSP_UINT32 new_ppn, block = 0;
		RSP_UINT32 plane, buf_offset, plane_ppn[LPAGE_PER_PPAGE], super_blk;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSP_SECTOR_BITMAP bitmap;
		RSP_UINT32 valid_count = 0, channel, bank;

		channel = get_channel(ATL_cur_write_bank);
		bank = get_bank(ATL_cur_write_bank);
	
		new_ppn = assign_new_write_ppn(channel, bank);
		
		super_blk = new_ppn / PAGES_PER_BLK;

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = (RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + RSP_BYTES_PER_PAGE * plane);
			RSP_write_ops[plane].pSpareData = & meta_write_lpn[plane][0][0];
			RSP_write_ops[plane].bmpTargetSector = 0xffff;
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].nBlock = get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);

			RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, valid_count);
		
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		//m_pVFLWrapper->WAIT_PROGRAMPENDING();
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif		

		channel = channel ^ 1;

		if(NAND_bank_state[channel][bank].free_blk_list.count <= 5)
			for (int iter = 0; iter < 3; iter++)
				while(incremental_garbage_collection(channel, bank, Prof_FGC) == 0);
		if (NAND_bank_state[channel][bank].free_blk_list.count == 0)
			while (incremental_garbage_collection(channel, bank, Prof_FGC) != 2);
		if (NAND_bank_state[channel][bank].GCbuf_index != 0)
			dbg1++;
	}
	RSP_VOID ATLWrapper::meta_buffer_flush()
	{
		RSP_UINT32 new_ppn, block = 0;
		RSP_UINT32 plane, buf_offset, plane_ppn[LPAGE_PER_PPAGE], super_blk;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSP_SECTOR_BITMAP bitmap;
		RSP_UINT32 valid_count = 0, channel, bank;

		if(meta_write_count == 0)
			return;

		channel = get_channel(ATL_cur_write_bank);
		bank = get_bank(ATL_cur_write_bank);
	
		new_ppn = assign_new_write_ppn(channel, bank);
		
		super_blk = new_ppn / PAGES_PER_BLK;

		for(plane = 0; plane < min((meta_write_count + LPAGE_PER_PPAGE - 1) / LPAGE_PER_PPAGE, PLANES_PER_BANK); plane++)
		{
			bitmap = 0;
			for(buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
			{
				plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;
				if(meta_write_lpn[plane][buf_offset][0] == RSP_INVALID_LPN)
				{
					continue;	
				}
				valid_count++;

				if(buf_offset)
					bitmap |= 0xff00;
				else
					bitmap = 0xff;
				set_ppn(meta_write_lpn[plane][buf_offset][0], plane_ppn[buf_offset], Prof_Write);
				set_valid(channel, bank, (plane_ppn[buf_offset]) % (PAGES_PER_BANK * LPAGE_PER_PPAGE));
			}
			
			RSP_write_ops[plane].pData = (RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + RSP_BYTES_PER_PAGE * plane);
			RSP_write_ops[plane].pSpareData = & meta_write_lpn[plane][0][0];
			RSP_write_ops[plane].bmpTargetSector = 0xffff;
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].nBlock = get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = meta_write_lpn[plane][0][0];
			RSP_write_ops[plane].m_anLPN[1] = meta_write_lpn[plane][1][0];	
		}
		for (plane = (meta_write_count + LPAGE_PER_PPAGE - 1) / LPAGE_PER_PPAGE; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = (RSP_UINT32 *)add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + RSP_BYTES_PER_PAGE * plane);
			RSP_write_ops[plane].pSpareData = & meta_write_lpn[plane][0][0];
			RSP_write_ops[plane].bmpTargetSector = 0xffff;
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].nBlock = get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);

			RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, valid_count);
		
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		//m_pVFLWrapper->WAIT_PROGRAMPENDING();
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif		
		block = new_ppn / PAGES_PER_BLK;
		meta_write_count = 0;
		for(RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++)
		{
			for(RSP_UINT8 iner_iter = 0; iner_iter < LPAGE_PER_PPAGE; iner_iter++)
				meta_write_lpn[iter][iner_iter][0] = RSP_INVALID_LPN;
		}
		set_vcount(channel, bank, block, get_vcount(channel, bank, block) + valid_count);

		channel = channel ^ 1;

		if(NAND_bank_state[channel][bank].free_blk_list.count <= 5)
			for (int iter = 0; iter < 3; iter++)
				while(incremental_garbage_collection(channel, bank, Prof_FGC) == 0);
		if (NAND_bank_state[channel][bank].free_blk_list.count == 0)
			while (incremental_garbage_collection(channel, bank, Prof_FGC) != 2);
		if (NAND_bank_state[channel][bank].GCbuf_index != 0)
			dbg1++;
		ATL_meta_cur_write_bank = (ATL_meta_cur_write_bank + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
	}
	RSP_VOID ATLWrapper::bank_meta_flush(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32  plane, block, new_ppn;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		new_ppn = NAND_bank_state[channel][bank].cur_meta_ppn;
		block = NAND_bank_state[channel][bank].meta_blk;
		if (new_ppn == 0 && NAND_bank_state[channel][bank].meta_start == true)
		{
			RSP_UINT32 temp_blk = get_free_blk(&NAND_bank_state[channel][bank].free_blk_list);
			if(temp_blk != VC_MAX)
			{
				add_free_blk(&NAND_bank_state[channel][bank].free_blk_list, &NAND_bank_state[channel][bank].blk_list[NAND_bank_state[channel][bank].meta_blk]);
				NAND_bank_state[channel][bank].meta_blk = temp_blk;
				block = temp_blk;
			}
			erase_wrapper(channel, bank, block);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_erase, 1);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_meta_erase, 1);
		}

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = NAND_bank_state[channel][bank].cpybuf_addr[0];
			RSP_write_ops[plane].pSpareData = NULL_SPARE;
			RSP_write_ops[plane].nChannel = channel;
			RSP_write_ops[plane].nBank = bank;
			RSP_write_ops[plane].nBlock = get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_ppn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, new_ppn, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_ppn(channel, bank, RSP_write_ops[plane].nBlock, new_ppn, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_write_4KB_page, 1);
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		NAND_bank_state[channel][bank].cur_meta_ppn = (NAND_bank_state[channel][bank].cur_meta_ppn + 1) % PAGES_PER_BLK;
		NAND_bank_state[channel][bank].meta_start = true;
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Bank_meta_flush, 1);
#ifdef WAIT_TEST
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
#endif
	}

	RSP_UINT32 ATLWrapper::FTL_ReadData2Buffer(RSP_UINT32 nLPN, RSP_UINT8* pnBuf, RSP_UINT32 nNumSectorsPerPage)
	{
		RSP_UINT32 ppn, *BufferAddress;
		RSP_UINT32 bank, channel;
		RSP_SECTOR_BITMAP read_bitmap;
		RSPReadOp RSP_read_op;
		SM_value->value[24]++;
		ppn = SM_value->value[7 + THIS_CORE];

		_realcopy_read(ppn, pnBuf, 0xff); 

		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Read_read, 1);


		SM_value->value[41+THIS_CORE]++;
		return ReadNand;
	}
	RSP_VOID ATLWrapper::check_ppn_is_valid(RSP_UINT32 ppn)
	{
		RSP_UINT32 channel, bank, block, page;
		channel = get_channel_from_ppn(ppn);
		bank = get_bank_from_ppn(ppn);
		block = get_block((ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK);
		page = (ppn / LPAGE_PER_PPAGE) % PAGES_PER_BLK;
		
		/*RSP_ASSERT(pstBlockInfo[BLOCK_INDEX(channel, bank, block)].BlockErase == 0);
		RSP_ASSERT(pstBlockInfo[BLOCK_INDEX(channel, bank, block)].PageCount > page);*/
	}

	RSP_UINT32 ATLWrapper::_FTL_ReadData2Buffer(RSP_UINT32 nLPN, RSP_UINT32 mode)
	{
		RSP_UINT32 lpn = nLPN;
		RSP_UINT32 bank, ppn, channel, block, temp_ppn;
		RSP_BOOL dec_valid = false;
		ppn = get_ppn(lpn, Prof_Remap);

		if(mode == 0)
		{
			if (is_in_virtual(ppn))
			{
				ppn ^= VIRTUAL_BIT;
				channel = get_channel_from_ppn(ppn);
				bank = get_bank_from_ppn(ppn);
				temp_ppn = get_P2L(ppn, Prof_Remap);
			
				block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
				if (is_in_virtual(temp_ppn))
				{
					set_P2L(ppn, VC_MAX, Prof_Remap);
					dec_valid = true;
				}
				else
				{
					if (!del_list(ppn, lpn))
						dec_valid = true;
				}
			}
			else if (is_in_realcopy(ppn))
			{
			}
			else if (ppn != (RSP_UINT32)VC_MAX)
			{

				channel = get_channel_from_ppn(ppn);
				bank = get_bank_from_ppn(ppn);
				block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
				dec_valid = true;
			}
			if(dec_valid)
				{
					set_vcount(channel, bank, block, get_vcount(channel, bank, block) - 1);
					clear_valid(channel, bank, ppn % (PAGES_PER_BANK * LPAGE_PER_PPAGE));
				}
			set_ppn(lpn, VC_MAX, Prof_Remap);
		}
		if(is_in_virtual(ppn))
		{
			ppn ^= VIRTUAL_BIT;
			//check_ppn_is_valid(ppn);
			channel = get_channel_from_ppn(ppn);
			bank = get_bank_from_ppn(ppn);
			block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
			set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);
		}
		else if(is_in_realcopy(ppn))
		{
		}
		else if(ppn != VC_MAX)
		{
			channel = get_channel_from_ppn(ppn);
			bank = get_bank_from_ppn(ppn);
			block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
			//check_ppn_is_valid(ppn);
			set_refcount(channel, bank, block, get_refcount(channel, bank, block) + 1);
		}
		return ppn;
	}
	RSP_UINT32 ATLWrapper::FTL_WriteData2Buffer(RSP_UINT32 nLPN, RSP_UINT8* pnBuf, RSP_UINT32 nNumSectorsPerPage)
	{
		RSP_UINT32 ppn;
		if(cur_VC_struct->command_entry[cur_VC_struct->command_count-1].src_LPN / LPAGE_PER_PPAGE != nLPN)
		{ 
			SM_value->value[43+THIS_CORE]++;
			return false;
		} 
		cur_VC_struct->command_count--;
		ppn = get_ppn(nLPN, Prof_Remap);
		if((ppn) == cur_VC_struct->command_entry[cur_VC_struct->command_count].dst_LPN)
		{
			meta_write_page(nLPN, (RSP_UINT32*)pnBuf, true, false);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);
		}
	
		

		if(cur_VC_struct->command_count == 0)
		{
			pending_VC_count--;
			if (THIS_CORE == 0)
				SM_value->value[18]--;
			else
				SM_value->value[19]--;
			cur_VC_lpn = (cur_VC_lpn + 1) % MAX_PENDDING_VC;
			if (THIS_CORE == 0)
			{
				SM_value->value[22] = 1;
				SM_value->value[30] = 1;
			}
			else
			{
				SM_value->value[23] = 1;
				SM_value->value[31] = 1;
			}
			cur_VC_end();
			read_new_VC();
		}

		SM_value->value[43+THIS_CORE]++;
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_HIL, 1);
		return false;
	}
	RSP_UINT32 ATLWrapper::FTL_Trim(RSP_UINT32 input_lpn, RSP_UINT32 count)
	{
		RSP_UINT32 iter, lpn = input_lpn, ppn;
		

		for(iter = 0; iter < count; iter++, lpn++)
		{
			ppn = get_ppn(lpn, Prof_Trim);
			unmap(lpn, ppn);
			if(ppn != VC_MAX)
				set_ppn(lpn, VC_MAX, Prof_Trim);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Trim_count, 1);
		}
		return 0;
	}
	RSP_VOID ATLWrapper::unmap(RSP_UINT32 lpn, RSP_UINT32 input_ppn)
	{
		RSP_UINT32 channel, bank, block, plane, buf_offset, temp_ppn;
		RSP_UINT32 ppn = input_ppn;


		if(is_in_write_buffer(ppn) && ppn != VC_MAX)
		{
			ppn ^= (WRITE_BUFFER_BIT);
				

			if(ppn >= LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL * RSP_NUM_CHANNEL)
			{//Meta write_buffer
				ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				plane = ppn / LPAGE_PER_PPAGE;
				buf_offset = ppn % LPAGE_PER_PPAGE;
			
				meta_write_lpn[plane][buf_offset][0] = RSP_INVALID_LPN;
			}
			else
			{//Write buffer
				channel = ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
				ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK * BANKS_PER_CHANNEL);
				bank = ppn / (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				ppn = ppn % (LPAGE_PER_PPAGE * PLANES_PER_BANK);
				plane = ppn / LPAGE_PER_PPAGE;
				buf_offset = ppn % LPAGE_PER_PPAGE;
				if (buf_offset)
				{
					writebuf_data_bitmap[plane] ^= 0xff00;
					writebuf_lpn[plane][1][0] = RSP_INVALID_LPN;
					writebuf_bitmap &= (1 << ((plane)* LPAGE_PER_PPAGE + buf_offset)) ^ 0xffff;
				}
				else
				{
					writebuf_data_bitmap[plane] ^= 0xff;
					writebuf_lpn[plane][0][0] = RSP_INVALID_LPN;
					writebuf_bitmap &= (1 << ((plane)* LPAGE_PER_PPAGE + buf_offset)) ^ 0xffff;
				}
			}
		}
		else if(is_in_virtual(ppn) && ppn != VC_MAX)
		{
			ppn ^= VIRTUAL_BIT;
			temp_ppn = get_P2L(ppn, Prof_Trim);
			if (is_in_virtual(temp_ppn))
			{
				set_P2L(ppn, VC_MAX, Prof_Trim);
			}
			else
				del_list(ppn, lpn);
		}
		else if(is_in_realcopy(ppn) && ppn != VC_MAX)
		{
		}
		else if(ppn != VC_MAX)
		{
			channel = get_channel_from_ppn(ppn);
			bank = get_bank_from_ppn(ppn);
			plane = get_plane((ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK);
			block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
			ppn %=  LPAGE_PER_PPAGE * PAGES_PER_BANK;
			if(get_valid(channel, bank, ppn))
			{
				set_vcount(channel, bank, block, get_vcount(channel, bank, block) - 1);
				clear_valid(channel, bank, ppn);
			}
		}

	}
	RSP_VOID ATLWrapper::test_count(RSP_UINT32 count)
	{
		dbg1++;
	}
#ifdef Hesper_DEBUG
RSP_VOID ATLWrapper::test_count(RSP_UINT32 count)
{
	dbg1++;
}

RSP_VOID ATLWrapper::VC_struct_test(special_command vc)
{
	dbg1++;
}
#endif
void ATLWrapper::realcopy_read()
{
	volatile RSP_UINT32 condition;
	if(THIS_CORE == 0)
	{
		condition = SM_value->value[10];
		while (condition == 1)
		{
			condition = SM_value->value[10] & SM_value->value[33];
		}

		if (SM_value->value[34] == 1)
		{
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
			_realcopy_read(SM_value->value[13], (RSP_UINT8*)SM_value->value[15], SM_value->value[17]);
			SM_value->value[11] = 0;
			SM_value->value[34] = 0;
			SM_value->value[40] = 0;
		}
		if (SM_value->value[23] == 1)
		{
			SM_value->value[23] = 0;
		}
	}
	else
	{
		condition = SM_value->value[11];
		while(condition == 1)
		{
			if (SM_value->value[33] == 1)
			{
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
				_realcopy_read(SM_value->value[12], (RSP_UINT8*)SM_value->value[14], SM_value->value[16]);
				SM_value->value[10] = 0;
				SM_value->value[33] = 0;
				SM_value->value[39] = 0;
			}
			if (SM_value->value[30] == 1)
			{
				SM_value->value[22] = 0;
				SM_value->value[30] = 0;
			}
			condition = SM_value->value[11] & SM_value->value[34];
		}
	}
}

void ATLWrapper::cur_VC_end()
{
	volatile RSP_UINT32 condition;
	if (THIS_CORE == 0)
	{
		condition = SM_value->value[22];
		while (condition == 1)
		{
			condition = SM_value->value[22];
		}

		if (SM_value->value[34] == 1)
		{
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
			_realcopy_read(SM_value->value[13], (RSP_UINT8*)SM_value->value[15], SM_value->value[17]);
			SM_value->value[11] = 0;
			SM_value->value[34] = 0;
			SM_value->value[40] = 0;
		}
		if (SM_value->value[31] == 1)
		{

			SM_value->value[23] = 0;
			SM_value->value[31] = 0;
		}
	}
	else
	{
		condition = SM_value->value[23];
		while (condition == 1)
		{
			if (SM_value->value[33] == 1)
			{
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
				_realcopy_read(SM_value->value[12], (RSP_UINT8*)SM_value->value[14], SM_value->value[16]);
				SM_value->value[10] = 0;
				SM_value->value[33] = 0;
				SM_value->value[39] = 0;
			}
			if (SM_value->value[30] == 1)
			{

				SM_value->value[22] = 0;
				SM_value->value[30] = 0;
			}
			condition = SM_value->value[23];
		}
	}
}

void ATLWrapper::_realcopy_read(RSP_UINT32 input_ppn, RSP_UINT8* pnBuf, RSP_SECTOR_BITMAP SectorBitmap)
{
	RSP_UINT32 channel, bank, block, ppn = input_ppn;
	RSP_UINT8 high_low = 0 ;
    RSPReadOp RSP_read_op;
	static RSP_UINT32 old_ppn = 0;

    channel = get_channel_from_ppn(ppn);
    bank = get_bank_from_ppn(ppn);
	if (ppn != (RSP_UINT32)VC_MAX)
    {
   	 	block = get_super_block(ppn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));
		if(old_ppn != input_ppn)
		{
			set_refcount(channel, bank, block, get_refcount(channel, bank, block) - 1);
			old_ppn = input_ppn;
		}
        if (ppn % LPAGE_PER_PPAGE)
        {
			high_low = 1;
        }
        else
        {
			high_low = 0;
        }
        ppn = (ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK;
		if (high_low)
		{
			RSP_read_op.bmpTargetSector = 0xff00;
			RSP_read_op.pData = (RSP_UINT32 *)sub_addr(pnBuf, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		}
		else
		{
			RSP_read_op.bmpTargetSector = 0xff;
			RSP_read_op.pData = (RSP_UINT32 *)pnBuf;
		}

        //RSP_read_op.pSpareData = NULL;
        RSP_read_op.nReqID = RSP_INVALID_RID;
        RSP_read_op.nChannel = channel;
        RSP_read_op.nBank = bank;
        RSP_read_op.nBlock = get_block(ppn);
        RSP_read_op.nPage = get_page_offset(ppn);


		RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, high_low);

		RSP_read_op.m_nLPN = RSP_INVALID_LPN;   //lpn
		

        m_pVFLWrapper->INC_READPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 1);
        m_pVFLWrapper->Issue(RSP_read_op);
        m_pVFLWrapper->WAIT_READPENDING();

   }
	else
		RSP_ASSERT(0);
}
void ATLWrapper::complete_cur_VC()
{
	RSP_BOOL return_value = false;
	//real_copy
	while(return_value == false)
	{
		return_value = one_real_copy();
		Check_other_core_read();
	}
}
void ATLWrapper::read_new_VC()
{
	RSPReadOp RSP_read_op;
	RSP_UINT32 ppn, lpn, high_low = 0;
	RSP_UINT32 channel, bank;
	void* pnBuf = cur_VC_struct;
	if (pending_VC_count == 0)
	{
		remap_start = false;
		return;
	}

	
	lpn = cur_VC_lpn;
	lpn += SPECIAL_COMMAND_PAGE + 2;
	ppn = get_ppn(lpn, Prof_Remap);

	channel = get_channel_from_ppn(ppn);
    bank = get_bank_from_ppn(ppn);

	if (ppn != (RSP_UINT32)VC_MAX)
	{
		if (ppn % LPAGE_PER_PPAGE)
			high_low = 1;

		ppn = (ppn / LPAGE_PER_PPAGE) % PAGES_PER_BANK;
		//RSP_read_op.pSpareData = NULL;
		if (high_low)
			RSP_read_op.pData = (RSP_UINT32 *)sub_addr(pnBuf, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		else
			RSP_read_op.pData = (RSP_UINT32 *)pnBuf;
		RSP_read_op.nReqID = RSP_INVALID_RID;
		RSP_read_op.nChannel = channel;
		RSP_read_op.nBank = bank;
		RSP_read_op.nBlock = get_block(ppn);
		RSP_read_op.nPage = get_page_offset(ppn);
		if (high_low)
			RSP_read_op.bmpTargetSector = 0xff00;
		else
			RSP_read_op.bmpTargetSector = 0xff;


		RSP_read_op.m_nVPN = generate_ppn(channel, bank, RSP_read_op.nBlock, RSP_read_op.nPage, high_low);

		RSP_read_op.m_nLPN = ppn;   //lpn
		m_pVFLWrapper->INC_READPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_NAND_read, 1);
		m_pVFLWrapper->Issue(RSP_read_op);
		m_pVFLWrapper->WAIT_READPENDING();


	}
}
RSP_BOOL ATLWrapper::check_realcopy_done()
{
	if(THIS_CORE == 0)
	{
		if(SM_value->value[37] != 0)
		{
			if(SM_value->value[39] == 1)
			{
				return false;
			}
			else
			{
				meta_write_page(SM_value->value[37], one_realcopy_buffer, true, false);

				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);

				SM_value->value[37] = 0;
			}
		}
	}
	else
	{
		if(SM_value->value[38] != 0)
		{
			if(SM_value->value[40] == 1)
			{
				return false;
			}
			else
			{
				meta_write_page(SM_value->value[38], one_realcopy_buffer, true, false);
				
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);


				SM_value->value[38] = 0;
			}
		}
	}
	return true;
}
RSP_BOOL ATLWrapper::one_real_copy()
{
	RSP_UINT32 ppn = 0, lpn = VC_MAX, plane, channel, bank;
	if(!check_realcopy_done())
	{
		return false;
	}
	for(int iter = cur_VC_struct->command_count - 1; iter >=0; iter--)
	{

		ppn = get_ppn(cur_VC_struct->command_entry[iter].src_LPN / LPAGE_PER_PPAGE, Prof_Remap);
		if((ppn) == cur_VC_struct->command_entry[iter].dst_LPN)
		{
			lpn = cur_VC_struct->command_entry[iter].src_LPN / LPAGE_PER_PPAGE;
			cur_VC_struct->command_count--;
			break;
		}
		else
			cur_VC_struct->command_count--;
	}

	if(cur_VC_struct->command_count == 0 && lpn == VC_MAX)
	{
		pending_VC_count--;
		if (THIS_CORE == 0)
			SM_value->value[18]--;
		else
			SM_value->value[19]--;
		cur_VC_lpn = (cur_VC_lpn + 1) % MAX_PENDDING_VC;
		if (THIS_CORE == 0)
		{
			SM_value->value[22] = 1;
			SM_value->value[30] = 1;
		}
		else
		{
			SM_value->value[23] = 1;
			SM_value->value[31] = 1;
		}
		cur_VC_end();
		read_new_VC();
		return true;
	}
#ifdef ATL_ASSERTION_TEST
	RSP_ASSERT(ppn != 0);
#endif
	if(is_in_realcopy(ppn))
	{
		void **temp;
		if(THIS_CORE == 0)
		{
			SM_value->value[12] = ppn ^ REALCOPY_BIT;
			temp = (void**)&SM_value->value[14];
			*temp = (void*)one_realcopy_buffer;
			SM_value->value[16] = 0xff;
			SM_value->value[10] = 1;
			SM_value->value[33] = 1;
			SM_value->value[37] = lpn;
			SM_value->value[39] = 1;	
		}
		else
		{
			SM_value->value[13] = ppn ^ REALCOPY_BIT;
			temp = (void**)&SM_value->value[15];
			*temp = (void*)one_realcopy_buffer;
			SM_value->value[17] = 0xff;
			SM_value->value[11] = 1;
			SM_value->value[34] = 1;
			SM_value->value[38] = lpn;
			SM_value->value[40] = 1;
		}
	}
	else
	{
		void* pnBuf = add_addr(meta_write_buffer, ATL_meta_cur_write_bank * RSP_BYTES_PER_PAGE * PLANES_PER_BANK + meta_write_count * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
		_realcopy_read(ppn, (RSP_UINT8*)pnBuf, 0xffff);
		meta_write_lpn[meta_write_count / LPAGE_PER_PPAGE][meta_write_count % LPAGE_PER_PPAGE][0] = lpn;
	
		set_ppn(lpn, WRITE_BUFFER_BIT ^ (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL * LPAGE_PER_PPAGE * PLANES_PER_BANK + meta_write_count++), Prof_Remap);
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_Realcopy_write, 1);
		if (meta_write_count == LPAGE_PER_PPAGE * PLANES_PER_BANK)
			meta_buffer_flush();
	}
	return false;
}

void ATLWrapper::Check_other_core_read(void)
{
	if (SM_value == NULL)
		return;
	if (THIS_CORE == 0)
	{
		if (SM_value->value[34] == 1)
		{
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
			_realcopy_read(SM_value->value[13], (RSP_UINT8*)SM_value->value[15], SM_value->value[17]);
			SM_value->value[11] = 0;
			SM_value->value[34] = 0;
			SM_value->value[40] = 0;
		}
		if (SM_value->value[31] == 1)
		{
			SM_value->value[23] = 0;
			SM_value->value[31] = 0;
		}
	}
	else
	{
		if (SM_value->value[33] == 1)
		{
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Remap_by_FTL, 1);
			_realcopy_read(SM_value->value[12], (RSP_UINT8*)SM_value->value[14], SM_value->value[16]);
			SM_value->value[10] = 0;
			SM_value->value[33] = 0;
			SM_value->value[39] = 0;
		}
		if (SM_value->value[30] == 1)
		{
			SM_value->value[22] = 0;
			SM_value->value[30] = 0;
		}
	}
}

void ATLWrapper::FTL_Idle(void)
{
	RSP_UINT32 bank, channel, copy_count = 1;
	static RSP_UINT32 GC_bank = 0, MAP_GC_bank[2] = {0, 0};
	
	Check_other_core_read();
	if(pHILK2L != NULL &&SM_value->value[45 + THIS_CORE] != 0)
	{
		
		if(SM_value->value[45 + THIS_CORE] == 1)
		{
			SM_value->value[45 + THIS_CORE] = 0;
			FTL_ReadData2Buffer(SM_value->value[47 + THIS_CORE] / 2, (RSP_UINT8*)SM_value->value[49 + THIS_CORE], 0xff);
		}
		else
		{
			SM_value->value[45 + THIS_CORE] = 0;
			FTL_WriteData2Buffer(SM_value->value[47 + THIS_CORE] / 2, (RSP_UINT8*)SM_value->value[49 + THIS_CORE], 0xff);
		}

	}
	else if(flush_bank_counter >= FLUSH_BANKS_COUNTER)
	{
				flush_bank_counter = 0;
				flush_banks();
	}
	else if(NAND_bank_state[get_channel(MAP_GC_bank[L2P])][get_bank(MAP_GC_bank[L2P])].map_blk_list[L2P].count <=2)
	{
		channel = get_channel(MAP_GC_bank[L2P]);
		bank = get_bank(MAP_GC_bank[L2P]);
		while(!map_incremental_garbage_collection(channel, bank, L2P));

		MAP_GC_bank[L2P] = (MAP_GC_bank[L2P] + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
	}
	else if(NAND_bank_state[get_channel(MAP_GC_bank[P2L])][get_bank(MAP_GC_bank[P2L])].map_blk_list[P2L].count <=2)
	{
		channel = get_channel(MAP_GC_bank[P2L]);
		bank = get_bank(MAP_GC_bank[P2L]);
		while(!map_incremental_garbage_collection(channel, bank, P2L));

		MAP_GC_bank[P2L] = (MAP_GC_bank[P2L] + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
	}
	else
	{
	
		channel = get_channel(GC_bank);
		bank = get_bank(GC_bank);
		if(NAND_bank_state[channel][bank].free_blk_list.count <= BGC_THRESHOLD) // BGC
		{
			while(!incremental_garbage_collection(channel, bank, Prof_BGC));
			if (NAND_bank_state[channel][bank].GCbuf_index != 0)
				dbg1++;
		}
		GC_bank = (GC_bank + 1) % (RSP_NUM_CHANNEL * BANKS_PER_CHANNEL);
	}
	
	
	return;
}
void ATLWrapper::erase_wrapper(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
{
	RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
	for (int plane = 0; plane < PLANES_PER_BANK; plane++)
	{
		RSP_erase_ops[plane].nChannel = channel;
		RSP_erase_ops[plane].nBank = bank;
		RSP_erase_ops[plane].nBlock = get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
	}
	m_pVFLWrapper->INC_ERASEPENDING();
	m_pVFLWrapper->Issue(RSP_erase_ops);
	m_pVFLWrapper->WAIT_ERASEPENDING();
}

} //end of namespace
