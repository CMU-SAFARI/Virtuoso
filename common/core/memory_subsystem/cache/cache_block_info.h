#ifndef __CACHE_BLOCK_INFO_H__
#define __CACHE_BLOCK_INFO_H__

#include "fixed_types.h"
#include "cache_state.h"
#include "cache_base.h"

class CacheBlockInfo
{
public:
	enum option_t
	{
		PREFETCH,
		WARMUP,
		NUM_OPTIONS
	};

	enum block_type_t
	{
		PAGE_TABLE,
		PAGE_TABLE_PASSTHROUGH,
		RANGE_TABLE,
		UTOPIA,
		SECURITY,
		EXPRESSIVE,
		TLB_ENTRY,			   // used to store TLB entries inside the caches
		TLB_ENTRY_PASSTHROUGH, // used to store TLB entries inside the caches but passthrough L1 cache
		NON_PAGE_TABLE,
		NUM_BLOCK_TYPES
	};

	static const UInt8 BitsUsedOffset = 3; // Track usage on 1<<BitsUsedOffset granularity (per 64-bit / 8-byte)
	typedef UInt8 BitsUsedType;			   // Enough to store one bit per 1<<BitsUsedOffset byte element per cache line (8 8-byte elements for 64-byte cache lines)
										   // This can be extended later to include other information
										   // for different cache coherence protocols
private:
	// specifically for Victima
	//  IntPtr* m_sub_tag_array_tlb; // for TLB: hold tag array for each cache block
	//  bool* m_valid_tlb; // for TLB: hold valid bit for each cache block
	IntPtr ppn;

	bool m_tlb_entry;
	int m_page_size;	  //@kanellok for TLB: hold page size for each cache block
	int m_num_sub_blocks; //@kanellok for TLB: hold number of sub blocks for each cache block

	IntPtr m_tag;
	CacheState::cstate_t m_cstate;
	UInt64 m_owner;
	BitsUsedType m_used;
	UInt8 m_options; // large enough to hold a bitfield for all available option_t's
	block_type_t m_block_type;
	int m_reuse; //@kanellok tracking reuse
	int utilization;

	static const char *option_names[];

public:
	CacheBlockInfo(IntPtr tag = ~0,
				   CacheState::cstate_t cstate = CacheState::INVALID,
				   UInt64 options = 0);

	virtual ~CacheBlockInfo();

	static CacheBlockInfo *create(CacheBase::cache_t cache_type);
	virtual void invalidate(void);
	virtual void clone(CacheBlockInfo *cache_block_info);

	bool isValid() const { return (m_tag != ((IntPtr)~0)); }

	// IntPtr* getTagArrayTLB() const { return m_sub_tag_array_tlb; }
	// IntPtr getTagSublockTLB(int index) const { return m_sub_tag_array_tlb[index]; }
	// bool* getValidArrayTLB() const { return m_valid_tlb; }
	// bool isValidSubBlockTLB(int index) const { return m_valid_tlb[index]; }

	IntPtr getTag() const { return m_tag; }

	CacheState::cstate_t getCState() const { return m_cstate; }

	void setTag(IntPtr tag) { m_tag = tag; }
	// void setTagTLB(IntPtr tag, int index) { m_sub_tag_array_tlb[index] = tag; }
	// bool tagMatchTLB(IntPtr tag, int index) const { return (m_sub_tag_array_tlb[index] == tag); }

	void setCState(CacheState::cstate_t cstate) { m_cstate = cstate; }
	void setPPN(IntPtr _ppn) { ppn = _ppn; };
	IntPtr getPPN() { return ppn; };
	UInt64 getOwner() const { return m_owner; }
	void setOwner(UInt64 owner) { m_owner = owner; }

	bool hasOption(option_t option) { return m_options & (1 << option); }
	void setOption(option_t option) { m_options |= (1 << option); }
	void clearOption(option_t option) { m_options &= ~(UInt64(1) << option); }

	inline void setBlockType(block_type_t bt) { m_block_type = bt; }
	inline block_type_t getBlockType() { return m_block_type; }
	inline bool isPageTableBlock() { return (m_block_type == block_type_t::PAGE_TABLE); }
	inline bool isSecurityBlock() { return (m_block_type == block_type_t::SECURITY); }
	inline bool isExpressiveBlock() { return (m_block_type == block_type_t::EXPRESSIVE); }
	inline bool isUtopiaBlock() { return (m_block_type == block_type_t::UTOPIA); }
	inline bool isTLBBlock() { return (m_block_type == block_type_t::TLB_ENTRY); }

	void setPageSize(int pagesize) { m_page_size = pagesize; }
	int getNumSubBlocks()
	{
		return m_num_sub_blocks;
		;
	}
	int getPageSize() { return m_page_size; }

	BitsUsedType getUsage() const { return m_used; };
	bool updateUsage(UInt32 offset, UInt32 size);
	bool updateUsage(BitsUsedType used);

	static const char *getOptionName(option_t option);

	void increaseReuse() { m_reuse++; }
	int getReuse() { return m_reuse; }
};

class CacheCntlr
{
public:
	virtual bool isInLowerLevelCache(CacheBlockInfo *block_info) { return false; }
	virtual void incrementQBSLookupCost() {}
};

#endif /* __CACHE_BLOCK_INFO_H__ */
