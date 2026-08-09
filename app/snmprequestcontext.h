#ifndef SNMPREQUESTCONTEXT_H
#define SNMPREQUESTCONTEXT_H

#include "snmprequestconfig.h"

enum class SnmpRequestOperation
{
    Get,
    GetNext,
    GetBulk,
    Set,
    Walk
};

class SnmpRequestContext
{
public:
    SnmpRequestContext(const SnmpRequestConfig &config,
                       SnmpRequestOperation operation);

    const SnmpRequestConfig &config() const;
    SnmpRequestOperation operation() const;
    int maxRepetitions() const;
    int nonRepeaters() const;

private:
    SnmpRequestConfig requestConfig;
    SnmpRequestOperation requestOperation;
};

#endif
