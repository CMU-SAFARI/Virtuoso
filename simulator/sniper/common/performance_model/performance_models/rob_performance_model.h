#ifndef ROB_PERFORMANCE_MODEL_H
#define ROB_PERFORMANCE_MODEL_H

#include "micro_op_performance_model.h"
#include "rob_timer.h"
#include "debug_config.h"

class RobPerformanceModel : public MicroOpPerformanceModel
{
public:
   RobPerformanceModel(Core *core);
   ~RobPerformanceModel();
   void drain(){
#if DEBUG_ROB_PERF_MODEL >= DEBUG_DETAILED
      std::cout << "RobPerformanceModel::drain() called" << std::endl;
#endif
      rob_timer.drainRob();
   }
protected:
   virtual boost::tuple<uint64_t,uint64_t> simulate(const std::vector<DynamicMicroOp*>& insts);
   virtual void notifyElapsedTimeUpdate();
private:
   RobTimer rob_timer;
};

#endif
