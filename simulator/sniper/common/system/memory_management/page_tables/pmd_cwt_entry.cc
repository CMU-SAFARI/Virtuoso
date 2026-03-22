#include "pmd_cwt_entry.h"
#include "cwc.h"  // Required for CWCRow definition

namespace ParametricDramDirectoryMSI {

    CWCRow PmdCwtEntry::toCWCRow() {
        return CWCRow{this};
    }

}