#ifndef GRAPHPOLLINGSTATE_H
#define GRAPHPOLLINGSTATE_H

class GraphPollingState
{
public:
    bool start();
    void stop();
    bool beginCycle();
    void completeCycle();
    bool isRunning() const;
    bool isCycleActive() const;
private:
    bool running=false;
    bool active=false;
};

#endif
