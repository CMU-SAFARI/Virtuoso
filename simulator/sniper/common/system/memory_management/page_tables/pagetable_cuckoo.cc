#include "cache_cntlr.h"
#include "pwc.h"
#include "subsecond_time.h"
#include "memory_manager.h"
#include "pagetable.h"
#include "simulator.h"
#include "config.h"
#include "pagetable_cuckoo.h"
#include "simulator.h"
#include "physical_memory_allocator.h"
#include "mimicos.h"
#include "city.h"

#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <fstream>
#include <array>
#include <numeric>	 // for std::iota
#include <algorithm> // for std::shuffle
#include <random>	 // for std::default_random_engine
#include <chrono>	 // for std::chrono::system_clock

#define DEBUG

namespace ParametricDramDirectoryMSI
{

	/*
	 * PageTableCuckoo implements a Cuckoo Hash–inspired page table.
	 *   - Each page_size uses multiple tables/ways.
	 *   - The "tag" is derived from the VPN bits (tag = VPN >> 3).
	 *   - The block offset is (VPN % 8).
	 *   - Insertions may lead to evictions ("cuckooing"), and if we exceed a loadFactor,
	 *     we try to rehash with a bigger table.
	 *   - Non-resident entries that fail to insert are added to 'nonResidentEntries'.
	 */

	/*
	 * Constructor:
	 *   - Allocates multiple "tables" for each page size (e.g., ways).
	 *   - Each table is an array of "Element" objects, where each Element can store 8 offsets.
	 *   - Allocates stat counters to track hits, page walks, evictions, and rehashes.
	 *   - table_ppns[] keeps track of the "physical address" base for each table array.
	 */
	PageTableCuckoo::PageTableCuckoo(int core_id,
									 String name,
									 String type,
									 int page_sizes,
									 int *page_size_list,
									 int *page_table_sizes,
									 double rehash_threshold,
									 float scale,
									 int ways,
									 bool is_guest)
		: PageTable(core_id, name, type, page_sizes, page_size_list, is_guest),
		  m_page_table_sizes(page_table_sizes)
	{
		log_file_name = "page_table_cuckoo.log";
		log_file_name = std::string(Sim()->getConfig()->getOutputDirectory().c_str()) + "/" + log_file_name;
		log_file.open(log_file_name);

		std::cout << "Page table constructor\n";

		// numItems tracks how many "valid" tags are stored in each page size’s structure
		numItems = (int *)malloc(sizeof(int) * m_page_sizes);
		for (int i = 0; i < m_page_sizes; i++)
		{
			numItems[i] = 0;
		}

		std::cout << "[Cuckoo] Cuckoo has " << page_sizes << " page sizes:\n";
		for (int i = 0; i < page_sizes; i++)
		{
			std::cout << "[Cuckoo] Page size " << i << " has " << page_size_list[i] << " bits\n";
		}

		// loadFactor is the maximum ratio of items / capacity before rehashing
		loadFactor = rehash_threshold;

		// We store arrays of Element* for each page_size, each dimension = ways
		tables = (Element ***)malloc(sizeof(Element **) * page_sizes);

		// table_ppns is a 2D array of the base physical addresses for each table
		table_ppns = (UInt64 **)malloc(sizeof(UInt64 *) * page_sizes);

		m_scale = scale; // factor by which the table may grow if rehashing is needed
		m_ways = ways;	 // how many ways/tables per page size

		// For each page size, allocate "ways" tables, each having "m_page_table_sizes[a]" entries
		for (int a = 0; a < m_page_sizes; a++)
		{
			tables[a] = (Element **)malloc(sizeof(Element *) * ways);
			table_ppns[a] = (UInt64 *)malloc(sizeof(UInt64) * ways);

			std::cout << "[Cuckoo] Initializing the cuckoo tables with " << ways
					  << " ways and " << page_table_sizes[a] << " entries\n";

			for (int i = 0; i < ways; i++)
			{
				// Allocate an array of Elements
				tables[a][i] = (Element *)malloc(sizeof(Element) * m_page_table_sizes[a]);

				// Grab some physical address space for this table array
				table_ppns[a][i] = getPhysicalSpace(m_page_table_sizes[a] * 64);
				std::cout << "[Cuckoo] Table " << i << " has ppn " << table_ppns[a][i] << "\n";

				// Initialize each Element to defaults
				for (int j = 0; j < m_page_table_sizes[a]; j++)
				{
					tables[a][i][j] = Element();
				}
			}
		}

		std::cout << "[Cuckoo] Cuckoo initialized\n";

		cuckoo_stats.cuckoo_hits = 0;
		cuckoo_stats.page_walks_total = 0;
		cuckoo_stats.cuckoo_evictions = 0;
		cuckoo_stats.rehashes = 0;

		std::cout << "[Cuckoo] Registering stats with name " << name << " and core " << core_id << "\n";

		// Track total page walks, total evictions, total rehashes
		registerStatsMetric(name, core_id, "ptws_total", &cuckoo_stats.page_walks_total);
		registerStatsMetric(name, core_id, "evictions", &cuckoo_stats.cuckoo_evictions);
		registerStatsMetric(name, core_id, "rehashes", &cuckoo_stats.rehashes);

		// For each page size, track hits at that level and number of accesses
		cuckoo_stats.cuckoo_hits_per_level = new UInt64[page_sizes];
		cuckoo_stats.cuckoo_accesses = new UInt64[page_sizes];

		for (int i = 0; i < page_sizes; i++)
		{
			cuckoo_stats.cuckoo_hits_per_level[i] = 0;
			cuckoo_stats.cuckoo_accesses[i] = 0;
			registerStatsMetric(name, core_id, "hit_at_level" + itostr(i), &cuckoo_stats.cuckoo_hits_per_level[i]);
			registerStatsMetric(name, core_id, "num_accesses" + itostr(i), &cuckoo_stats.cuckoo_accesses[i]);
		}
	}

	/*
	 * hash(...) => Hashes address with CityHash64, modded by the table size.
	 * Used for indexing in each "way".
	 */
	uint64 PageTableCuckoo::hash(uint64 address, int table_size)
	{
		uint64 result = CityHash64((const char *)&address, 8) % table_size;
		return result;
	}

	/*
	 * insertElement(...):
	 *   - Attempts to insert or update an Element in the cuckoo table for a particular page_size_index.
	 *   - We first see if there's an existing entry with the same tag; if so, we update the offset.
	 *   - Otherwise, we do the standard cuckoo algorithm:
	 *       1) Attempt to place the new Element in one of the ways by computing hash(tag + way).
	 *       2) If the position is empty, store it and return.
	 *       3) If not, evict the existing occupant, and repeat with that occupant’s data.
	 *       4) If we exceed a certain number of retries, we fail, moving that occupant to nonResidentEntries.
	 *   - visitedAddresses collects the “physical addresses” we touch while trying to insert (for stats).
	 *   - Returns <success, visitedAddresses>.
	 */
	std::tuple<bool, accessedAddresses> PageTableCuckoo::insertElement(int page_size_index, Element entry, IntPtr address)
	{
		accessedAddresses visitedAddresses;
		int retries;
		uint64_t pos = 0;
		int selected_way = -1;

		// Derive the tag and offset from the VPN
		uint64_t VPN = address >> m_page_size_list[page_size_index];
		uint64_t tag = VPN >> 3;
		uint64_t indexInsideBlock = VPN % 8;

		Element currentElement;
		Element oldElement;

		currentElement.tag = entry.tag;

		// Initialize currentElement to all invalid bits except for the one offset we want to set
		for (int i = 0; i < 8; i++)
		{
			currentElement.validityBits[i] = false;
			currentElement.frames[i] = -1;
		}
		currentElement.validityBits[indexInsideBlock] = entry.validityBits[indexInsideBlock];
		currentElement.frames[indexInsideBlock] = entry.frames[indexInsideBlock];

		oldElement.tag = -1;
		for (int i = 0; i < 8; i++)
		{
			oldElement.validityBits[i] = false;
			oldElement.frames[i] = -1;
		}

#ifdef DEBUG
		log_file << "[Cuckoo] Inserting element with tag " << currentElement.tag
				 << " and index " << indexInsideBlock
				 << " in page size " << m_page_size_list[page_size_index] << "\n";
#endif

		// Try each way to see if we can update an existing entry with the same tag
		for (int i = 0; i < m_ways; ++i)
		{
			pos = hash(tag + i, m_page_table_sizes[page_size_index]);

#ifdef DEBUG
			log_file << "Checking position " << pos << " in way " << i
					 << " for update: " << tables[page_size_index][i][pos].tag
					 << " with validity: ";

			for (int j = 0; j < 8; j++)
			{
				log_file << tables[page_size_index][i][pos].validityBits[j] << " ";
			}
			log_file << "\n";
#endif

			// If we find the tag in that position but offset is not valid => just set it
			if (tables[page_size_index][i][pos].tag == tag && !tables[page_size_index][i][pos].validityBits[indexInsideBlock])
			{
#ifdef DEBUG
				log_file << "Updating translation with tag " << currentElement.tag
						 << " in way " << i << " at position " << pos << "\n";
#endif
				tables[page_size_index][i][pos].frames[indexInsideBlock] = currentElement.frames[indexInsideBlock];
				tables[page_size_index][i][pos].validityBits[indexInsideBlock] = true;
				return std::make_tuple(true, visitedAddresses);
			}
			else if (tables[page_size_index][i][pos].tag == tag)
			{
				// Shouldn’t happen: that means offset is already valid or a conflicting overlap
				log_file << "Error: Cuckoo should not come here P1" << std::endl;
			}
		}

		// If we get here, there is no matching tag => we do the eviction-based insert
		oldElement.tag = currentElement.tag;
		for (int i = 0; i < 8; i++)
		{
			oldElement.validityBits[i] = currentElement.validityBits[i];
			oldElement.frames[i] = currentElement.frames[i];
		}

		// Generate a random permutation of ways for insertion attempts
		std::vector<int> permutation(m_ways);

		unsigned seed = 463576468;
		int counter = 0;
		retries = 48; // max times we’ll attempt to cuckoo

		// Fill with 0..(m_ways-1) then shuffle
		std::iota(permutation.begin(), permutation.end(), 0);
		std::shuffle(permutation.begin(), permutation.end(), std::default_random_engine(seed));

		// We’ll cycle through this permutation multiple times if needed
		for (int i = m_ways; i < (retries + 1); ++i)
		{
			permutation.push_back(permutation[(i - m_ways) % m_ways]);
		}

		// Start with the first element in the permutation
		int new_way = -1;
		if (!permutation.empty())
		{
			new_way = permutation.front();
			permutation.erase(permutation.begin());
		}
		else
		{
			assert(false);
		}

		selected_way = new_way;

		// The main cuckoo loop: we keep evicting until we find an empty spot or run out of retries
		while (retries)
		{
			currentElement.tag = oldElement.tag;
			for (int i = 0; i < 8; i++)
			{
				currentElement.validityBits[i] = oldElement.validityBits[i];
				currentElement.frames[i] = oldElement.frames[i];
			}

			pos = hash(currentElement.tag + selected_way, m_page_table_sizes[page_size_index]);

#ifdef DEBUG
			log_file << "Trying to insert tag: " << currentElement.tag
					 << " in position " << pos
					 << " in way " << selected_way
					 << " with tag " << tables[page_size_index][selected_way][pos].tag
					 << " and validity: ";

			for (int j = 0; j < 8; j++)
			{
				log_file << tables[page_size_index][selected_way][pos].validityBits[j] << " ";
			}
			log_file << "\n";
#endif

			// If the slot is empty, store our item and return success
			if (tables[page_size_index][selected_way][pos].tag == static_cast<uint64_t>(-1))
			{
#ifdef DEBUG
				log_file << "Inserting translation with tag " << currentElement.tag
						 << " in way " << selected_way
						 << " at position " << pos << "\n";
#endif
				tables[page_size_index][selected_way][pos].tag = currentElement.tag;
				for (int i = 0; i < 8; i++)
				{
					tables[page_size_index][selected_way][pos].validityBits[i] = currentElement.validityBits[i];
					tables[page_size_index][selected_way][pos].frames[i] = currentElement.frames[i];
				}

				numItems[page_size_index]++;
				visitedAddresses.push_back(make_tuple(page_size_index, counter,
													  (uint64_t)(table_ppns[page_size_index][selected_way] + pos * 64), false));
				return std::make_tuple(true, visitedAddresses);
			}
			// Otherwise, we evict the occupant => occupant becomes oldElement; occupant’s spot gets our item
			else if (tables[page_size_index][selected_way][pos].tag != currentElement.tag)
			{
#ifdef DEBUG
				log_file << "Evicting translation with tag "
						 << tables[page_size_index][selected_way][pos].tag
						 << " in way " << selected_way
						 << " at position " << pos << "\n";
#endif
				// Save occupant
				oldElement.tag = tables[page_size_index][selected_way][pos].tag;
				for (int i = 0; i < 8; i++)
				{
					oldElement.validityBits[i] = tables[page_size_index][selected_way][pos].validityBits[i];
					oldElement.frames[i] = tables[page_size_index][selected_way][pos].frames[i];
				}

				// Next way in the permutation
				int new_way = -1;
				if (!permutation.empty())
				{
					new_way = permutation.front();
					permutation.erase(permutation.begin());
				}
				else
				{
					assert(false);
				}

#ifdef DEBUG
				log_file << "Inserting translation with tag " << currentElement.tag
						 << " in way " << selected_way
						 << " at position " << pos << "\n";
				log_file << "Before eviction: " << tables[page_size_index][selected_way][pos].tag;
#endif
				// Overwrite occupant with our item
				tables[page_size_index][selected_way][pos].tag = currentElement.tag;
#ifdef DEBUG
				log_file << "After eviction: " << tables[page_size_index][selected_way][pos].tag << "\n";
#endif
				for (int i = 0; i < 8; i++)
				{
					tables[page_size_index][selected_way][pos].validityBits[i] = currentElement.validityBits[i];
					tables[page_size_index][selected_way][pos].frames[i] = currentElement.frames[i];
				}

				visitedAddresses.push_back(make_tuple(page_size_index, counter,
													  (uint64_t)(table_ppns[page_size_index][selected_way] + pos * 64), false));
				selected_way = new_way;
			}
			else
			{
#ifdef DEBUG
				log_file << "Error: Cuckoo should not come here" << std::endl;
				log_file << "Current tag: " << currentElement.tag
						 << " and occupant tag: "
						 << tables[page_size_index][selected_way][pos].tag << "\n";
#endif
			}

			retries--;
			counter++;
		}

		// If we exit the while loop, we failed to place the item => push occupant to nonResidentEntries
		if (oldElement.tag != static_cast<uint64_t>(-1))
		{
			nonResidentEntries.push_back(oldElement);
#ifdef DEBUG
			log_file << "Non-resident entry with tag " << oldElement.tag << " added to the list\n";
#endif
		}

		return std::make_tuple(false, visitedAddresses);
	}

	/*
	 * rehash(...):
	 *   - Attempts to increase the table size for the specified page_size_index and re-insert everything.
	 *   - We allocate new arrays, then call insertElement(...) for all existing items.
	 *   - If any insertion fails, we restore the old arrays. This can happen if we get repeated collisions.
	 *   - If successful, we track a rehash count and clean up old memory.
	 *   - Returns true if rehash is successful, false otherwise.
	 */
	bool PageTableCuckoo::rehash(int page_size_index, int newSize)
	{
#ifdef DEBUG
		log_file << "Rehashing the cuckoo table for page size " << m_page_size_list[page_size_index] << "\n";
		log_file << "Old size: " << m_page_table_sizes[page_size_index] << "\n";
		log_file << "Scale factor: " << m_scale << "\n";
		log_file << "New size: " << newSize << "\n";
#endif
		int oldSize = m_page_table_sizes[page_size_index];

		Element **temp = tables[page_size_index];

		// Allocate new tables
		Element **newTables = (Element **)malloc(sizeof(Element *) * m_ways);
		for (int i = 0; i < m_ways; i++)
		{
			newTables[i] = (Element *)malloc(sizeof(Element) * newSize);
			for (int j = 0; j < newSize; j++)
			{
				newTables[i][j] = Element();
			}
		}

		// Switch to new tables
		tables[page_size_index] = newTables;

#ifdef DEBUG
		log_file << "We need to migrate " << numItems[page_size_index] << " elements\n";
#endif
		m_page_table_sizes[page_size_index] = newSize; // record the updated size

		// Migrate old data
		for (int way = 0; way < m_ways; way++)
		{
			for (int pos = 0; pos < oldSize; pos++)
			{
				if (temp[way][pos].tag != static_cast<uint64_t>(-1))
				{
					// For each valid offset, re-insert the item
					for (int i = 0; i < 8; i++)
					{
						if (temp[way][pos].validityBits[i])
						{
							uint64_t VPN = (temp[way][pos].tag << 3) + i;
							uint64_t address = VPN << m_page_size_list[page_size_index];
#ifdef DEBUG
							log_file << "Migrating element with tag "
									 << temp[way][pos].tag
									 << " and index " << VPN % 8
									 << " at validity " << i
									 << " in way " << way
									 << " at position " << pos << "\n";
#endif
							std::tuple<bool, accessedAddresses> result = insertElement(page_size_index, temp[way][pos], address);
							if (get<0>(result) == false)
							{
								// Failure => revert changes
								tables[page_size_index] = temp;
								for (int i = 0; i < m_ways; i++)
								{
									free(newTables[i]);
								}
								free(newTables);
								m_page_table_sizes[page_size_index] = oldSize;
#ifdef DEBUG
								log_file << "Rehash failed" << std::endl;
#endif
								nonResidentEntries.clear();
								return false;
							}
						}
					}
				}
			}
		}

		// Next, handle the nonResidentEntries
		std::vector<Element> tempNonResidentEntries;
		for (UInt32 j = 0; j < nonResidentEntries.size(); j++)
		{
			Element entry = nonResidentEntries[j];
			tempNonResidentEntries.push_back(entry);
		}

		// Insert them in the new tables
		for (UInt32 j = 0; j < nonResidentEntries.size(); j++)
		{
			Element entry = nonResidentEntries[j];
			for (int i = 0; i < 8; i++)
			{
				if (entry.validityBits[i])
				{
					uint64_t VPN = (entry.tag << 3) + i;
					uint64_t address = VPN << m_page_size_list[page_size_index];
#ifdef DEBUG
					log_file << "Migrating non-resident element with tag " << entry.tag
							 << " in page size " << m_page_size_list[page_size_index] << "\n";
#endif
					std::tuple<bool, accessedAddresses> result = insertElement(page_size_index, entry, address);
					if (get<0>(result) == false)
					{
						// Failure => revert
						tables[page_size_index] = temp;
						for (int i = 0; i < m_ways; i++)
						{
							free(newTables[i]);
						}
						free(newTables);

						m_page_table_sizes[page_size_index] = oldSize;
#ifdef DEBUG
						log_file << "Rehash failed" << std::endl;
#endif
						nonResidentEntries.clear();

						// restore the prior nonResidentEntries
						for (UInt32 j = 0; j < tempNonResidentEntries.size(); j++)
						{
							Element entry = tempNonResidentEntries[j];
							nonResidentEntries.push_back(entry);
						}
						return false;
					}
				}
			}
			// remove the entry from the non-resident list
			nonResidentEntries.erase(nonResidentEntries.begin());
		}

		log_file << "Rehash successful" << std::endl;
		// Clean up old memory
		for (int i = 0; i < m_ways; i++)
		{
			free(temp[i]);
		}
		free(temp);

		m_page_table_sizes[page_size_index] = newSize;
		cuckoo_stats.rehashes++;
		return true;
	}

	/*
	 * currentLoadFactor(...) => calculates (numItems / (table_size * ways)).
	 * If this exceeds 'loadFactor', we trigger rehash in updatePageTableFrames().
	 */
	double PageTableCuckoo::currentLoadFactor(int page_size_index) const
	{
		return (double)numItems[page_size_index] / (m_page_table_sizes[page_size_index] * m_ways);
	}

	/*
	 * initializeWalk(...):
	 *   - For each page size, compute a tag and offset from the address.
	 *   - For each way, compute pos = hash(tag + way), and check if that slot has the correct tag & offset.
	 *   - If found, return a PTWResult with page_size_result and the PPN.
	 *   - If none match => page fault.
	 *   - If restart_walk_after_fault is set, we handle_page_fault(...) and try again.
	 */
	PTWResult PageTableCuckoo::initializeWalk(IntPtr address, bool count, bool is_prefetch, bool restart_walk_after_fault)
	{
#ifdef DEBUG
		log_file << std::endl;
		log_file << "[Cuckoo] Starting page walk for address " << address << "\n";
#endif
		if (count)
			cuckoo_stats.page_walks_total++;

		accessedAddresses visitedAddresses;
		bool is_page_fault_in_every_page_size = false;
		int page_size_result = -1;
		IntPtr ppn_result = 0;

	restart_walk:;

		// Attempt a lookup in each page size
		for (int i = 0; i < m_page_sizes; i++)
		{
			IntPtr VPN = address >> m_page_size_list[i];
			IntPtr tag = VPN >> 3;
			IntPtr offset = VPN % 8;

#ifdef DEBUG
			log_file << "[Cuckoo] Doing page walk with tag " << tag
					 << " and offset " << offset
					 << " in page size " << m_page_size_list[i] << "\n";
#endif

			// For each way, compute the hash and check if we have a match
			for (int a = 0; a < m_ways; ++a)
			{
				uint64_t pos = hash(tag + a, m_page_table_sizes[i]);

#ifdef DEBUG
				log_file << "[Cuckoo] Size of the table: " << m_page_table_sizes[i] << "\n";
				log_file << "[Cuckoo] Position of the block:" << pos << "\n"
						 << "[Cuckoo] Tag of the block: " << tables[i][a][pos].tag
						 << " and validity: ";
				for (int j = 0; j < 8; j++)
				{
					log_file << tables[i][a][pos].validityBits[j] << " ";
				}
				log_file << "\n";
#endif
				// If tags and validity match => we found the PPN
				if (tables[i][a][pos].tag == tag && tables[i][a][pos].validityBits[offset])
				{
					ppn_result = tables[i][a][pos].frames[offset];
					visitedAddresses.push_back(make_tuple(i, 0, (IntPtr)(table_ppns[i][a] * 4096 + pos * 64), true));
					page_size_result = m_page_size_list[i];

					cuckoo_stats.cuckoo_accesses[i]++;
					if (count)
					{
#ifdef DEBUG
						log_file << "[Cuckoo] Cuckoo hit at level " << i
								 << " with tag " << tag
								 << " and index " << offset
								 << " in page size " << m_page_size_list[i] << "\n";
#endif
						cuckoo_stats.cuckoo_hits_per_level[i]++;
					}
				}
				else
				{
					// If it’s not a match, we record an access for stats
					visitedAddresses.push_back(make_tuple(i, 0, (IntPtr)(table_ppns[i][a] * 4096 + pos * 64), false));
					cuckoo_stats.cuckoo_accesses[i]++;
				}
			}
		}

		// If no match found, page_size_result remains -1 => page fault
		if (page_size_result == -1)
		{
			if (restart_walk_after_fault)
				Sim()->getMimicOS()->handle_page_fault(address, core_id, false);

			is_page_fault_in_every_page_size = true;

			if (restart_walk_after_fault)
				goto restart_walk;
			else
				return PTWResult(page_size_result, visitedAddresses, ppn_result, SubsecondTime::Zero(), is_page_fault_in_every_page_size);
		}

		// If we found a match in some page size
		if (page_size_result != -1)
		{
			// We already incremented stats above
#ifdef DEBUG
			log_file << "[Cuckoo] Page walk finished\n";
			log_file << "[Cuckoo] Page size result: " << page_size_result << "\n";
			log_file << "[Cuckoo] PPN result: " << ppn_result << "\n";
			log_file << "[Cuckoo] Was it a pagefault? " << is_page_fault_in_every_page_size << "\n";
#endif
			return PTWResult(page_size_result, visitedAddresses, ppn_result, SubsecondTime::Zero(), is_page_fault_in_every_page_size);
		}

		// Should not reach here
		assert(false);
		return PTWResult(-1, visitedAddresses, -1, SubsecondTime::Zero(), is_page_fault_in_every_page_size);
	}

	/*
	 * updatePageTableFrames(...):
	 *   - Called whenever we need to map "address" -> "ppn" for a given page_size.
	 *   - If the load factor is too high, we rehash with an increased table size.
	 *   - Then we build an Element object from the (tag, offset, and ppn) and attempt to insert it.
	 *   - If insertion fails, we attempt to rehash repeatedly until successful.
	 */
	int PageTableCuckoo::updatePageTableFrames(IntPtr address, IntPtr core_id, IntPtr ppn, int page_size, std::vector<UInt64> frames)
	{
#ifdef DEBUG
		log_file << "[Cuckoo] Updating page table frames for address "
				 << address << " with ppn " << ppn
				 << " and page size " << page_size << "\n";
#endif

		// Find which index corresponds to this page_size
		int page_size_index = 0;
		for (int i = 0; i < m_page_sizes; i++)
		{
			if (m_page_size_list[i] == page_size)
			{
				page_size_index = i;
				break;
			}
		}

		// If we exceed loadFactor => rehash
		if (currentLoadFactor(page_size_index) > loadFactor)
		{
			int new_size = m_page_table_sizes[page_size_index] * m_scale;
			while (!rehash(page_size_index, new_size))
			{
				new_size *= m_scale;
				log_file << "Rehash failed, trying again with new size " << new_size << "\n";
			}
		}

		// Build an Element with the correct tag/offset
		Element entry;
		uint64_t VPN = address >> m_page_size_list[page_size_index];
		uint64_t tag = VPN >> 3;
		uint64_t offset = VPN % 8;

		entry.tag = tag;
		entry.validityBits[offset] = true;
		entry.frames[offset] = ppn;

		int new_size = m_page_table_sizes[page_size_index];

		// Attempt to insert the new Element
		std::tuple<bool, accessedAddresses> result = insertElement(page_size_index, entry, address);

		// If it fails => keep rehashing with bigger sizes until success
		if (get<0>(result) == false)
		{
			new_size *= m_scale;
			while (!rehash(page_size_index, new_size))
			{
				new_size *= m_scale;
				log_file << "[Cuckoo] Rehash failed, trying again with new size " << new_size << "\n";
			}
		}

		return 0;
	}

	/*
	 * deletePage(...):
	 *   - Removes the offset for 'address' from the "way 0" page table (?).
	 *   - If the occupant becomes entirely empty (no valid offsets), we set tag = -1.
	 *   - The logic is fairly simple, scanning each way in turn for a matching tag.
	 */
	void PageTableCuckoo::deletePage(IntPtr address)
	{
		bool found = false;

		IntPtr VPN = address >> m_page_size_list[0];
		IntPtr tag = VPN >> 3;
		IntPtr indexInsideBlock = VPN % 8;

		for (int a = 0; a < m_ways; ++a)
		{
			uint64_t pos = hash(tag + a, m_page_table_sizes[0]);
			if (tables[0][a][pos].tag == tag)
			{
				tables[0][a][pos].validityBits[indexInsideBlock] = false;
				tables[0][a][pos].frames[indexInsideBlock] = -1;

				// If any offset remains valid, we keep the tag
				for (int j = 0; j < 8; j++)
				{
					if (tables[0][a][pos].validityBits[j])
					{
						found = true;
						break;
					}
				}
				// If not found => occupant is fully empty
				if (!found)
					tables[0][a][pos].tag = -1;

				break;
			}
		}
	}

	/*
	 * getPhysicalSpace(...) => convenience wrapper to ask the memory allocator for
	 * a chunk of space of size “size,” used for storing a cuckoo table array in memory.
	 */
	IntPtr PageTableCuckoo::getPhysicalSpace(int size)
	{
		return Sim()->getMimicOS()->getMemoryAllocator()->handle_page_table_allocations(size);
	}

} // namespace ParametricDramDirectoryMSI
