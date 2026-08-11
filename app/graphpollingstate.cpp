#include "graphpollingstate.h"

bool GraphPollingState::start(){if(running)return false;running=true;return true;}
void GraphPollingState::stop(){running=false;}
bool GraphPollingState::beginCycle(){if(!running||active)return false;active=true;return true;}
void GraphPollingState::completeCycle(){active=false;}
bool GraphPollingState::isRunning()const{return running;}
bool GraphPollingState::isCycleActive()const{return active;}
