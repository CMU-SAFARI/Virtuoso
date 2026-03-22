#include "hooks_py.h"
#include "simulator.h"
#include "core_manager.h"
#include "core.h"
#include "cache.h"
#include "memory_manager_base.h"
#include "mem_component.h"
#include <sstream>
#include <fstream>

// Helper function to get cache by name and core
static Cache* getCacheByName(core_id_t core_id, const char* cache_name)
{
    // Safety check: make sure the simulator is still valid
    if (!Sim() || !Sim()->getCoreManager()) {
        fprintf(stderr, "[py_cache] Simulator or CoreManager not available (simulation may be ending)\n");
        return nullptr;
    }
    
    Core* core = Sim()->getCoreManager()->getCoreFromID(core_id);
    if (!core) {
        fprintf(stderr, "[py_cache] Core %d not found\n", core_id);
        return nullptr;
    }
    
    MemoryManagerBase* mem_mgr = core->getMemoryManager();
    if (!mem_mgr) {
        fprintf(stderr, "[py_cache] Memory manager not found for core %d\n", core_id);
        return nullptr;
    }
    
    std::string name(cache_name);
    MemComponent::component_t component;
    
    if (name == "L1-D" || name == "L1D" || name == "l1d" || name == "L1_DCACHE") {
        component = MemComponent::L1_DCACHE;
    } else if (name == "L1-I" || name == "L1I" || name == "l1i" || name == "L1_ICACHE") {
        component = MemComponent::L1_ICACHE;
    } else if (name == "L2" || name == "l2" || name == "L2_CACHE") {
        component = MemComponent::L2_CACHE;
    } else if (name == "L3" || name == "l3" || name == "LLC" || name == "llc" || name == "L3_CACHE") {
        component = MemComponent::L3_CACHE;
    } else {
        fprintf(stderr, "[py_cache] Unknown cache name: %s\n", cache_name);
        return nullptr;
    }
    
    Cache* cache = mem_mgr->getCache(component);
    if (!cache) {
        fprintf(stderr, "[py_cache] Cache %s (component %d) not found for core %d\n", 
                cache_name, (int)component, core_id);
    }
    return cache;
}

// Save a cache snapshot as a heatmap to a file
static PyObject*
saveCacheHeatmap(PyObject *self, PyObject *args)
{
    int core_id;
    const char* cache_name;
    const char* filename;
    
    if (!PyArg_ParseTuple(args, "iss", &core_id, &cache_name, &filename))
        return NULL;
    
    Cache* cache = getCacheByName(core_id, cache_name);
    if (!cache) {
        PyErr_SetString(PyExc_ValueError, "Cache not found");
        return NULL;
    }
    
    cache->saveSnapshotHeatmap(std::string(filename));
    
    Py_RETURN_NONE;
}

// Get cache snapshot data as a Python dictionary
static PyObject*
getCacheSnapshot(PyObject *self, PyObject *args)
{
    int core_id;
    const char* cache_name;
    
    if (!PyArg_ParseTuple(args, "is", &core_id, &cache_name))
        return NULL;
    
    Cache* cache = getCacheByName(core_id, cache_name);
    if (!cache) {
        PyErr_SetString(PyExc_ValueError, "Cache not found");
        return NULL;
    }
    
    CacheSnapshot snapshot = cache->getCacheSnapshot();
    
    // Build Python dict with snapshot data
    PyObject* result = PyDict_New();
    PyDict_SetItemString(result, "num_sets", PyLong_FromLong(snapshot.num_sets));
    PyDict_SetItemString(result, "num_ways", PyLong_FromLong(snapshot.num_ways));
    
    // Create a list of lists for blocks
    PyObject* blocks_list = PyList_New(snapshot.num_sets);
    for (UInt32 set_idx = 0; set_idx < snapshot.num_sets; ++set_idx) {
        PyObject* way_list = PyList_New(snapshot.num_ways);
        for (UInt32 way = 0; way < snapshot.num_ways; ++way) {
            const CacheBlockSnapshot& block = snapshot.blocks[set_idx][way];
            PyObject* block_dict = PyDict_New();
            PyDict_SetItemString(block_dict, "valid", PyBool_FromLong(block.valid));
            PyDict_SetItemString(block_dict, "type", PyLong_FromLong(block.block_type));
            PyDict_SetItemString(block_dict, "recency", PyLong_FromLong(block.recency));
            PyDict_SetItemString(block_dict, "reuse", PyLong_FromLong(block.reuse_count));
            PyList_SetItem(way_list, way, block_dict);
        }
        PyList_SetItem(blocks_list, set_idx, way_list);
    }
    PyDict_SetItemString(result, "blocks", blocks_list);
    
    return result;
}

// Get block type names for the legend (simplified to 3 types)
static PyObject*
getBlockTypeNames(PyObject *self, PyObject *args)
{
    PyObject* result = PyDict_New();
    PyDict_SetItemString(result, "PAGE_TABLE", PyLong_FromLong(CacheBlockInfo::PAGE_TABLE));
    PyDict_SetItemString(result, "INSTRUCTION", PyLong_FromLong(CacheBlockInfo::INSTRUCTION));
    PyDict_SetItemString(result, "DATA", PyLong_FromLong(CacheBlockInfo::DATA));
    return result;
}

// Get summary statistics of current cache state
static PyObject*
getCacheTypeCounts(PyObject *self, PyObject *args)
{
    int core_id;
    const char* cache_name;
    
    if (!PyArg_ParseTuple(args, "is", &core_id, &cache_name))
        return NULL;
    
    Cache* cache = getCacheByName(core_id, cache_name);
    if (!cache) {
        PyErr_SetString(PyExc_ValueError, "Cache not found");
        return NULL;
    }
    
    CacheSnapshot snapshot = cache->getCacheSnapshot();
    
    // Count blocks by type
    int counts[CacheBlockInfo::NUM_BLOCK_TYPES] = {0};
    int valid_count = 0;
    int invalid_count = 0;
    
    for (UInt32 set_idx = 0; set_idx < snapshot.num_sets; ++set_idx) {
        for (UInt32 way = 0; way < snapshot.num_ways; ++way) {
            const CacheBlockSnapshot& block = snapshot.blocks[set_idx][way];
            if (block.valid) {
                valid_count++;
                counts[block.block_type]++;
            } else {
                invalid_count++;
            }
        }
    }
    
    PyObject* result = PyDict_New();
    PyDict_SetItemString(result, "valid", PyLong_FromLong(valid_count));
    PyDict_SetItemString(result, "invalid", PyLong_FromLong(invalid_count));
    PyDict_SetItemString(result, "PAGE_TABLE", PyLong_FromLong(counts[CacheBlockInfo::PAGE_TABLE]));
    PyDict_SetItemString(result, "INSTRUCTION", PyLong_FromLong(counts[CacheBlockInfo::INSTRUCTION]));
    PyDict_SetItemString(result, "DATA", PyLong_FromLong(counts[CacheBlockInfo::DATA]));
    
    return result;
}

static PyMethodDef PyCacheMethods[] = {
    { "save_heatmap", saveCacheHeatmap, METH_VARARGS, "Save a cache heatmap to a PPM file (core_id, cache_name, filename)" },
    { "get_snapshot", getCacheSnapshot, METH_VARARGS, "Get cache snapshot data as dict (core_id, cache_name)" },
    { "get_type_names", getBlockTypeNames, METH_VARARGS, "Get block type name to value mapping" },
    { "get_type_counts", getCacheTypeCounts, METH_VARARGS, "Get counts of each block type (core_id, cache_name)" },
    { NULL, NULL, 0, NULL } /* Sentinel */
};

static PyModuleDef PyCacheModule = {
    PyModuleDef_HEAD_INIT,
    "sim_cache",
    "Cache inspection and visualization module",
    -1,
    PyCacheMethods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_sim_cache(void)
{
    PyObject *pModule = PyModule_Create(&PyCacheModule);
    return pModule;
}
