#ifndef _FTL_H
#define _FTL_H

#include "RSP_Header.h"
#include "RSP_OSAL.h"

#include "VFLWrapper.h"

	//define
#define sizeof_u32 4
#define sizeof_u64 8
	
#define KB (1024)
#define MB (1024 * KB)
	
		//FTL CORE
#define NUM_FTL_CORE 2
#define THIS_CORE (_COREID_ - 1) //should be changed into variable
	
		//RSP_MEM_API
#define rspmalloc(a) RSPOSAL::RSP_MemAlloc(RSPOSAL::DRAM, a)
#define rspsmalloc(a) RSPOSAL::RSP_MemAlloc(RSPOSAL::SRAM, a)
		//#define rspmalloc(a) malloc(a)
	
#define BYTES_PER_SECTOR RSP_BYTE_PER_SECTOR
#define SECTORS_PER_LPN RSP_SECTOR_PER_LPN
#define SECTORS_PER_PAGE RSP_SECTOR_PER_PAGE
#define LPAGE_PER_PPAGE 2
#define PAGES_PER_BLK RSP_PAGE_PER_BLOCK
#define RSP_BYTES_PER_PAGE (BYTES_PER_SECTOR * SECTORS_PER_PAGE)
	
//test
#define BLKS_PER_PLANE (128)

//#define BLKS_PER_PLANE RSP_BLOCK_PER_PLANE
#define BLKS_PER_BANK BLKS_PER_PLANE
#define PLANES_PER_BANK RSP_NUM_PLANE
#define BYTES_PER_SUPER_PAGE (RSP_BYTES_PER_PAGE * PLANES_PER_BANK)
#define BANKS_PER_CHANNEL RSP_NUM_BANK
#define	NAND_NUM_CHANNELS RSP_NUM_CHANNEL
		//static RSP_UINT32 OP_BLKS = 1024;
		//for test
#define NUM_LOGICAL_BLOCK ((110 / NUM_FTL_CORE) * 1024)

#define IS_DFTL (1) //FPM on off, when the FPM, CMT size must be 72MB
#define IS_INCGC (0) //incremental GC on off
#define IS_GCMAPLOG (0) //map logging on GC on/off
#define NUM_READ_PER_INCGC 2

		static RSP_UINT32 OP_BLKS = 7264;
		static RSP_UINT32 NUM_LBLK;
		static RSP_UINT32 NUM_PBLK;
		static RSP_UINT32 CMT_size = 256 * KB; //2MB
	
		//Mapping data
		
#define NUM_MAP_ENTRY ((NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE + (MAP_ENTRY_SIZE / sizeof_u32) - 1) / (MAP_ENTRY_SIZE / sizeof_u32))
#define NUM_MAP_ENTRY_BLK ((NUM_MAP_ENTRY + PAGES_PER_BLK - 1) / PAGES_PER_BLK)
#define MAP_ENTRY_BLK_PER_BANK ((NUM_MAP_ENTRY_BLK + (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) - 1) / (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) + 5)
#define TOTAL_MAP_BLK (MAP_ENTRY_BLK_PER_BANK * (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS))
#define MAP_ENTRY_SIZE (BYTES_PER_SUPER_PAGE)
#define NUM_MAP_ENTRY_PER_BANK ((NUM_MAP_ENTRY + (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) - 1) / (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS))
#define NUM_PAGES_PER_MAP (MAP_ENTRY_SIZE / sizeof_u32)
	
#define OP_BLKS_PER_BANK OP_BLKS / (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL)
#define PAGES_PER_BANK (PAGES_PER_BLK * BLKS_PER_PLANE * PLANES_PER_BANK)
	
		// Logical area layout
		// |------user data-------|-SUPERBLKPAGE-|---JN_LOG---|---RW_LOG---|--SP_CMD--|
		// REMARKS:: SP_CMD is not a portion of LBLK
	
		//VPN layout
		// |--channel--|--bank--|--block--|--plane--|--page--|--high-low--|
	
#define RW_LOG_SIZE_IN_PAGE (64 * MB >> 12) //for test
#define RW_LOG_START_IN_PAGE (NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE - RW_LOG_SIZE_IN_PAGE)
#define JN_LOG_SIZE_IN_PAGE (64 * MB >> 12) //for test
#define JN_LOG_START_IN_PAGE RW_LOG_START_IN_PAGE - JN_LOG_SIZE_IN_PAGE
	
#define JN_SUPERBLK_PAGE_IDX JN_LOG_START_IN_PAGE - 1	//first page in the JN log is super block
		//JN block group should be switched when the super block is written.
	
#define NUM_MAX_TWRITE (32)
#define NUM_MAX_REMAP (32)
#define NUM_MAX_TWRITE_ENTRY (128)
#define NUM_MAX_REMAP_ENTRY  (511)
	
#define TWRITE_CMD_IN_PAGE  (NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE)
#define REMAP_CMD_IN_PAGE (TWRITE_CMD_IN_PAGE + NUM_MAX_TWRITE) //# of NCQ
#define CONFIRM_READ_CMD_IN_PAGE (REMAP_CMD_IN_PAGE + NUM_MAX_REMAP)
	
	
#define NUM_META_BLKS 1
#define NUM_SPARE_LPN 2
	
		/*
			it is different from the original meaning of o_addr and t_addr,
			because of the functional simplicity, REQ_LPN means requested logical address,
			and REMAP_LPN means need-to-remapped logical address.
			Thus, when the twrite is coming, T_addr will be written in the REQ_LPN,
			and also when the sequential write is coming, o_addr will be written in the REQ_LPN.
			*/
#define REQ_LPN 0     
#define REMAP_LPN 1

	//write buffer layout
	//  |-1-|---------|-channel--|--bank---|--buf type---|--plane--|--buf offset--|
	//TODO:: At the performance optimizing session, we need to make all of multiply and divide function into shift operation.
	//At this point, we need to change WRITE_TYPE_NUM into WRITE_TYPE_BITS 2, and pluse WRITE_BUFFER_BIT.
#define WRITE_TYPE_NUM 3

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BITS_PER_LONG (32)
#define BIT(nr)			(1UL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE (8)
#define BITS_TO_LONGS(nr) DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(RSP_UINT32))

	struct rb_node
	{
		RSP_UINT32	rb_parent_color;
#define	RB_RED		0
#define	RB_BLACK	1
		rb_node *rb_right;
		rb_node *rb_left;
	};
	
	struct rb_root
	{
		rb_node *rb_node;
	};

	//log map for GC (map logging)
	struct log_map{
		rb_node node;
		RSP_UINT32 lpn;
		RSP_UINT32 vpn;
	};

	struct block_struct{

		block_struct* next;
		block_struct* before;
		RSP_UINT32 block_no;
		RSP_UINT32 vcount;
		RSP_UINT32 remained_remap_cnt;
		RSP_UINT32 vbitmap[BITS_TO_LONGS(PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE)]; //valid bitmap for GC
	};

	struct block_struct_head
	{
		RSP_UINT32 size;
		block_struct* head;
	};

	enum WRITE_TYPE{
		SHRD_SW,
		SHRD_RW,
		SHRD_JN
	};

	struct NAND_bankstat
	{
		RSP_UINT32 GC_BLK;

		RSP_UINT32 cur_write_vpn;
		RSP_UINT32 cur_write_vpn_JN;
		RSP_UINT32 cur_write_vpn_RW;

		//for incremental GC
		RSP_UINT32 cur_gc_vpn;  //if there are no GC block, then VC_MAX (super block level 32KB page)
		RSP_UINT32 cur_vt_vpn;  //if there are no victim, then VC_MAX (nrumal block levl, 8KB page)
		RSP_UINT32 num_gcbuffed_valid_page;  //need to count the number of buffered valid page to keep up there remainings, if the write happened, then reduce it
		RSP_UINT32 cur_gcbuff_idx;

		RSP_UINT32 cur_map_vpn;
		RSP_UINT32 MAP_GC_BLK;
		RSP_UINT32 map_free_blk;
		RSP_UINT32 cur_meta_vpn;
		RSP_UINT32 meta_blk;
		RSP_UINT32* cpybuf_addr;
		RSP_UINT32* GCbuf_addr;
		RSP_UINT32 GCbuf_index;
		RSP_UINT32 GCbuf_lpn[PLANES_PER_BANK][LPAGE_PER_PPAGE][NUM_SPARE_LPN];
		RSP_UINT32* writebuf_addr[WRITE_TYPE_NUM][PLANES_PER_BANK];
		RSP_UINT16 writebuf_addr_bitmap[WRITE_TYPE_NUM][PLANES_PER_BANK];
		RSP_UINT32 writebuf_index[WRITE_TYPE_NUM];
		RSP_UINT8 writebuf_bitmap[WRITE_TYPE_NUM];
		RSP_UINT32 writebuf_lpn[WRITE_TYPE_NUM][PLANES_PER_BANK][LPAGE_PER_PPAGE][NUM_SPARE_LPN];
		RSP_BOOL write_start[WRITE_TYPE_NUM];
		RSP_BOOL map_start;
		RSP_BOOL meta_start;

		RSP_UINT32 *map_blk_offset;

		RSP_UINT32 remap_inbuff_cnt;

		log_map *gc_map_log;
		rb_root gc_map_tree;

		block_struct* block_list;
		block_struct_head free_list;
		block_struct_head JN_log_list;
		block_struct_head JN_todo_list;
		block_struct_head data_list;
		block_struct_head RW_log_list;
		block_struct_head victim_list;
	};

	struct TWRITE_HDR_ENTRY
	{
		RSP_UINT32 addr_start;
		RSP_UINT32 io_count;
		RSP_UINT32 o_addr[NUM_MAX_TWRITE_ENTRY];
		RSP_BOOL write_complete[NUM_MAX_TWRITE_ENTRY]; //it is not necessary for the scheme but useful for debug
		RSP_UINT32 epoch;
		RSP_UINT32 remained;
		TWRITE_HDR_ENTRY* next;
		TWRITE_HDR_ENTRY* before;

	}; //it is not actually 4KB

#define TWRITE_HDR_BYTE_SIZE ((NUM_MAX_TWRITE_ENTRY + 2) * sizeof(RSP_UINT32))

	struct REMAP_HDR_ENTRY
	{
		RSP_UINT32 remap_count;
		RSP_UINT32 t_addr[NUM_MAX_REMAP_ENTRY]; //need to recalculate addr 
		RSP_UINT32 o_addr[NUM_MAX_REMAP_ENTRY];
		RSP_UINT32 epoch;
		RSP_UINT8 remap_offset; //store remap entry number with given LPN on write and be used for confirm-read cmd
		REMAP_HDR_ENTRY* next;
		REMAP_HDR_ENTRY* before;
	}; //4KB entry

#define REMAP_HDR_BYTE_SIZE ((NUM_MAX_REMAP_ENTRY * 2 + 3) * sizeof(RSP_UINT32))

	struct CONFIRM_READ_ENTRY{
		RSP_UINT32 complete[NUM_MAX_REMAP_ENTRY];
	};

	struct TWRITE_HDR_LIST
	{
		RSP_UINT32 size;
		TWRITE_HDR_ENTRY *head;
	};
	struct REMAP_HDR_LIST
	{
		RSP_UINT32 size;
		REMAP_HDR_ENTRY *head;
	};

	static RSP_UINT32 NULL_SPARE[4] = {
		RSP_INVALID_LPN,
		RSP_INVALID_LPN,
		RSP_INVALID_LPN,
		RSP_INVALID_LPN
	};
	
namespace Hesper{

#define RSP_ASSERT(bCondition) if (!(bCondition)) {while(1);}
//#define RSP_ASSERT(bCondition) if (!(bCondition)) {printf("ASSERT!!");while(1);}
	
	enum ReadState{
		ReadWriteBuffer,
		ReadError,
		ReadNand,
	};

	//Profile data
	enum{
	//SW
		Prof_SW_write = 0,
		Prof_SW_read,
		Prof_SW_Null_read,
		Prof_SW_buf_read,
		Prof_SW_modify_read,
		Prof_SW_modify_buf_write,
		Prof_SW_map_load,
		Prof_SW_map_write,
		Prof_SW_block_alloc,
		
	//RW
		Prof_RW_write,
		Prof_RW_read,
		Prof_RW_Null_read,
		Prof_RW_buf_read,
		Prof_RW_modify_read,
		Prof_RW_modify_buf_write,
		Prof_RW_map_load,
		Prof_RW_map_write,
		Prof_RW_block_alloc,
		Prof_RW_Remap_cnt,
		Prof_RW_Remap_entry,
		Prof_RW_Remap_map_load,
		Prof_RW_Remap_map_write,
		Prof_RW_twrite_cnt,
		
	//JN
		Prof_JN_write,
		Prof_JN_read,
		Prof_JN_Null_read,
		Prof_JN_buf_read,
		Prof_JN_modify_read,
		Prof_JN_modify_buf_write,
		Prof_JN_map_load,
		Prof_JN_map_write,
		Prof_JN_block_alloc,
		Prof_JN_Remap_cnt,
		Prof_JN_Remap_entry,
		Prof_JN_Remap_map_load,
		Prof_JN_Remap_map_write,
		Prof_JN_twrite_cnt,

	//Intra_GC
		Prof_IntraGC_num,
		Prof_IntraGC_write,
		Prof_IntraGC_read,
		Prof_IntraGC_map_load,
		Prof_IntraGC_map_write,
		Prof_IntraGC_erase,

	//Inter_GC
		Prof_InterGC_num,
		Prof_InterGC_write,
		Prof_InterGC_read,
		Prof_InterGC_map_load,
		Prof_InterGC_map_write,
		Prof_InterGC_erase,
		
	//Map_manage	
		Prof_map_hit,
		Prof_map_log_hit,
		Prof_map_log_flush,
		Prof_map_miss,
		Prof_map_erase,

	//Flush
		Prof_num_flush,
		Prof_meta_flush,
		Prof_meta_erase,
		Prof_map_flush,
		Prof_SW_flush,
		Prof_RW_flush,
		Prof_JN_flush,

	//ETC	
		Prof_Init_erase,
		Prof_total_num,
	};
	
	enum{
		Prof_SW = 0,
		Prof_RW,
		Prof_JN,
		Prof_JN_remap,
		Prof_RW_remap,
		Prof_IntraGC,
		Prof_InterGC,
	};

	class ATLWrapper
	{
	public:

		VFLWrapper* m_pVFLWrapper;
		NAND_bankstat *NAND_bank_state[NAND_NUM_CHANNELS];

//DRAM MAP cache
		RSP_UINT32 NUM_CACHED_MAP;
		RSP_UINT32* CACHE_MAPPING_TABLE;
		RSP_UINT32* CACHE_ADDR;
		RSP_UINT32* cache_count;

		RSP_UINT32* MAP_MAPPING_TABLE;
		RSP_UINT32* MAP_VALID_COUNT;
		RSP_UINT32* MAPP2L;
		RSP_BOOL* CACHE_MAP_DIRTY_TABLE;

		//Map Management
		RSP_UINT32 num_cached;

		//dirty block bitmap page must be flushed together with dirty VTP at the change of active block 
#define NUM_TOTAL_BLOCKS (BLKS_PER_PLANE * BANKS_PER_CHANNEL * NAND_NUM_CHANNELS)
#define BB_PER_BBP (256) //bitmap size = 128byte, superpage size = 32KB, superpage / bitmap
#define NUM_BBP DIV_ROUND_UP(NUM_TOTAL_BLOCKS, BB_PER_BBP) //# of block bitmap page for this ATL

		RSP_UINT32 dirty_BBP_bitmap[BITS_TO_LONGS(NUM_BBP)];

		//currently write bank number (include channel in it)
		//e.g. 0 means channel 0 bank 0, 1 means channel 1 bank 0, etc.
		RSP_UINT32	cur_write_bank;

#define inc_cur_write_bank() {cur_write_bank++; if(cur_write_bank == (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL)){cur_write_bank = 0;}}
#define cur_gc_bank() {(cur_write_bank + (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL) / 2) % (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL)}
#define GC_THRESHOLD 8


		TWRITE_HDR_ENTRY *twrite_hdr_entry;
		REMAP_HDR_ENTRY *remap_hdr_entry;

#define init_cmd_list(list) do{\
	list.size = 0; \
	list.head = NULL; \
		}while (0)

		TWRITE_HDR_LIST free_twrite_list;
		TWRITE_HDR_LIST RW_twrite_list;
		TWRITE_HDR_LIST JN_twrite_list;

		REMAP_HDR_LIST free_remap_list;
		REMAP_HDR_LIST RW_remap_list;
		REMAP_HDR_LIST JN_remap_list;
		REMAP_HDR_LIST completed_remap_list; //when the CONFIRM read is done, then the list will go to free list.

#define MAX_EPOCH_NUMBER 65536
		RSP_UINT32 epoch_number[WRITE_TYPE_NUM];

		//for simulator
		RSP_UINT32 _COREID_;
		ATLWrapper(VFLWrapper *pVFL, RSP_UINT32 COREID); //overloading function for simulator
		//end

		ATLWrapper(VFLWrapper* pVFL);
		virtual ~ATLWrapper(RSP_VOID);

		RSP_VOID map_read(RSP_UINT32 map_offset, RSP_UINT32 cache_offset);
		RSP_VOID map_write(RSP_UINT32 map_offset, RSP_UINT32 cache_offset);
		RSP_UINT32 get_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);
		RSP_VOID set_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount);
		RSP_UINT32 RSP_ReadPage(RSP_UINT32 request_ID, RSP_LPN LPN, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress);
		RSP_BOOL RSP_WritePage(RSP_LPN LPN[2], RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress);

		RSP_BOOL write_page(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress, RSP_UINT32 oLPN, RSP_UINT8 WRITE_TYPE);

		RSP_UINT32 assign_new_write_vpn(RSP_UINT32 channel, RSP_UINT32 bank);

		RSP_UINT32 assign_new_write_vpn_JN(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_UINT32 assign_new_write_vpn_RW(RSP_UINT32 channel, RSP_UINT32 bank);

		RSP_UINT32 get_vpn(RSP_UINT32 lpn, RSP_UINT32 flag);
		RSP_VOID set_vpn(RSP_UINT32 lpn, RSP_UINT32 vpn, RSP_UINT32 flag);
		RSP_UINT32 get_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);
		RSP_VOID set_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount);

		RSP_BOOL is_valid_vpn(RSP_UINT32 vpn);
		RSP_VOID validate_vpn(RSP_UINT32 vpn);
		RSP_VOID invalidate_vpn(RSP_UINT32 vpn);
		
		RSP_VOID garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_UINT32 inter_GC(RSP_UINT32 channel, RSP_UINT32 bank);

		//if non-zero, it did incremental GC, else not
		RSP_UINT32 incremental_GC(RSP_UINT32 channel, RSP_UINT32 bank);
		//decrease vcount of victim block, if the count is larger than remained vcount of top block of victim list, then additionally dcrease vcount of next victim block
		RSP_VOID decrease_vcount_of_vt_block(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 count);
		//read victim page into gc buffer, check whether the LPN is valid or not, pull the valid page of latter buffer if needed.
		RSP_VOID read_victim_page(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 vt_page, RSP_UINT32 *spare_lpn, RSP_UINT32 *is_valid);
		//flush the data in the gc buffer into available gc active block
		RSP_VOID write_gc_buffer(RSP_UINT32 channel, RSP_UINT32 bank);
		//if there are full invalid block in the victim list, than erase, and return the block into gc block or free block 
		RSP_UINT32 erase_victim_block(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_UINT32 do_incremental_GC(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_UINT32 check_n_invalid_GCbuff(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 lpn, RSP_UINT32 block);

		RSP_UINT32 get_free_block(RSP_UINT32 channel, RSP_UINT32 bank);

		RSP_UINT32 get_vt_vblock(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_BOOL RSP_Open(RSP_VOID);

		RSP_VOID RSP_BufferCopy(RSP_UINT32* pstDescBuffer, RSP_UINT32* pstSrcBuffer, RSP_SECTOR_BITMAP bmp);
		RSP_BOOL RSP_CheckBit(RSP_SECTOR_BITMAP nVar, RSP_SECTOR_BITMAP nBit);
		RSP_VOID insert_bl_front(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, block_struct_head* list_head);
		RSP_VOID insert_bl_tail(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, block_struct_head* list_head);
		RSP_VOID del_blk_from_list(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, block_struct_head* list_head);

		//to do
		RSP_VOID twrite_header_handler(RSP_UINT32* Buff);
		//search twrite header list to find oLPN
		TWRITE_HDR_ENTRY *find_twrite_oLPN(RSP_UINT32 tLPN, RSP_UINT32 *oLPN);

		TWRITE_HDR_ENTRY *find_twrite_entry_of_tLPN(RSP_UINT32 tLPN, TWRITE_HDR_LIST *list);
		//to do
		RSP_VOID remap_handler(RSP_UINT32 LPN, RSP_UINT32* Buff);
		RSP_VOID __do_remap(RSP_UINT8 REMAP_TYPE);
		RSP_VOID do_remap(RSP_UINT8 REMAP_TYPE);	//gen num is for the twrite and remap version handling
		//when the twrite entry is deleted from the list, do_remap should be called
		//also do_remap can be called from the remap_handler when the all of twrite cmd is not ongoing.
		RSP_VOID do_confirm_read(RSP_UINT32 *buff);

		RSP_VOID insert_twrite_entry(TWRITE_HDR_ENTRY *entry, TWRITE_HDR_LIST *list);
		RSP_VOID del_twrite_entry(TWRITE_HDR_ENTRY *entry, TWRITE_HDR_LIST * list);

		RSP_VOID insert_remap_entry(REMAP_HDR_ENTRY *entry, REMAP_HDR_LIST *list);
		RSP_VOID del_remap_entry(REMAP_HDR_ENTRY *entry, REMAP_HDR_LIST *list);

		TWRITE_HDR_ENTRY* get_free_twrite_entry(RSP_VOID);
		REMAP_HDR_ENTRY* get_free_remap_entry(RSP_VOID);

		RSP_VOID* add_addr(RSP_VOID* start_addr, RSP_UINT32 offset);
		RSP_VOID* sub_addr(RSP_VOID* start_addr, RSP_UINT32 offset);

		RSP_UINT32 return_map_ppn(RSP_UINT32 map_offset);

		RSP_VOID map_flush(RSP_VOID);
		RSP_VOID BBP_flush(RSP_VOID);
		RSP_VOID bank_meta_flush(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_VOID flush_bank(RSP_UINT32 channel, RSP_UINT32 bank);
		RSP_VOID test_NAND_list();

		RSP_BOOL Issue(RSPProgramOp* RSPOp);
		RSP_BOOL MetaIssue(RSPProgramOp* RSPOp);
		RSP_BOOL Issue(RSPReadOp RSPOp);
		RSP_BOOL MetaIssue(RSPReadOp RSPOp);
		RSP_BOOL Issue(RSPEraseOp* RSPOp);

		RSP_BOOL WAIT_PROGRAMPENDING();

		RSP_BOOL WAIT_ERASEPENDING();

		RSP_BOOL WAIT_READPENDING();

		RSP_UINT32 map_validation_check();
		RSP_UINT32 map_vcount_test(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);
		RSP_UINT32 vcount_test(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);

		inline RSP_BOOL get_bit(RSP_UINT32 * bitmap,RSP_UINT32 offset){
			RSP_UINT32 *p = bitmap + BIT_WORD(offset);
			return (*p & BIT_MASK(offset));
		}

		inline RSP_VOID set_bit(RSP_UINT32 * bitmap,RSP_UINT32 offset){
			RSP_UINT32 *p = bitmap + BIT_WORD(offset);
			*p |= BIT_MASK(offset);
		}

		inline RSP_VOID clear_bit(RSP_UINT32 * bitmap,RSP_UINT32 offset){
			RSP_UINT32 *p = bitmap + BIT_WORD(offset);
			*p &= ~(BIT_MASK(offset));
		}

		inline RSP_VOID clear_blockbitmap(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block){
			RSPOSAL::RSP_MemSet(NAND_bank_state[channel][bank].block_list[block].vbitmap, 0x00, sizeof(RSP_UINT32) * BITS_TO_LONGS(PAGES_PER_BLK * PLANES_PER_BANK));
			
		}

		//return block bitmap page number for the block 
		inline RSP_UINT32 block_to_BBPN(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block){
			RSP_UINT32 global_block = ((channel * BANKS_PER_CHANNEL + bank) * BLKS_PER_BANK) + block;
			return global_block / BB_PER_BBP;
		}

		//RB tree 
#define container_of(ptr, type,member) \
			((type *)(((RSP_UINT8 *)(ptr)) - ((RSP_UINT8*)(&((type*)0)->member))))
//#define offsetof(TYPE, MEMBER) ((RSP_UINT32) &((TYPE *)0)->MEMBER)
		
#define rb_parent(r)   ((rb_node *)((r)->rb_parent_color & ~3))
#define rb_color(r)   ((r)->rb_parent_color & 1)
#define rb_is_red(r)   (!rb_color(r))
#define rb_is_black(r) rb_color(r)
#define rb_set_red(r)  do { (r)->rb_parent_color &= ~1; } while (0)
#define rb_set_black(r)  do { (r)->rb_parent_color |= 1; } while (0)
		
		static inline RSP_VOID rb_set_parent(rb_node *rb, rb_node *p)
		{
			rb->rb_parent_color = (rb->rb_parent_color & 3) | (RSP_UINT32)p;
		}
		static inline RSP_VOID rb_set_color(rb_node *rb, RSP_INT32 color)
		{
			rb->rb_parent_color = (rb->rb_parent_color & ~1) | color;
		}
		
#define RB_ROOT	(rb_root) { NULL, }
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)
		
#define RB_EMPTY_ROOT(root)	((root)->rb_node == NULL)
#define RB_EMPTY_NODE(node)	(rb_parent(node) == node)
#define RB_CLEAR_NODE(node)	(rb_set_parent(node, node))
		
		RSP_VOID __rb_rotate_left(rb_node *node, rb_root *root);
		RSP_VOID __rb_rotate_right(rb_node *node, rb_root *root);
		RSP_VOID __rb_erase_color(rb_node *node, rb_node *parent, rb_root *root);
		
		static inline RSP_VOID rb_init_node(rb_node *rb)
		{
			rb->rb_parent_color = 0;
			rb->rb_right = NULL;
			rb->rb_left = NULL;
			RB_CLEAR_NODE(rb);
		}
		
		RSP_VOID rb_insert_color(rb_node *, rb_root *);
		RSP_VOID rb_erase(rb_node *, rb_root *);
		
		/* Find logical next and previous nodes in a tree */
		rb_node *rb_next(const rb_node *);
		rb_node *rb_prev(const rb_node *);
		rb_node *rb_first(const rb_root *);
		rb_node *rb_last(const rb_root *);
		
		static inline RSP_VOID rb_link_node(rb_node * node, rb_node * parent, rb_node ** rb_link)
		{
			node->rb_parent_color = (RSP_UINT32 )parent;
			node->rb_left = node->rb_right = NULL;
		
			*rb_link = node;
		}

		log_map* log_map_search(rb_root *root, RSP_UINT32 lpn);
		RSP_UINT32 log_map_insert(rb_root *root, log_map *map_entry);
		RSP_VOID log_map_remove(rb_root *root, RSP_UINT32 lpn);

		RSP_VOID set_map_log(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 lpn, RSP_UINT32 vpn);
		log_map *get_map_log(RSP_UINT32 lpn, RSP_UINT32 *channel, RSP_UINT32 *bank);
		RSP_VOID flush_map_log(RSP_UINT32 channel, RSP_UINT32 bank);
	};
}

#endif
