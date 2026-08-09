#include "snmprequestcontext.h"

SnmpRequestContext::SnmpRequestContext(const SnmpRequestConfig &config,
                                       SnmpRequestOperation operation)
    : requestConfig(config), requestOperation(operation)
{
}

const SnmpRequestConfig &SnmpRequestContext::config() const
{
    return requestConfig;
}

SnmpRequestOperation SnmpRequestContext::operation() const
{
    return requestOperation;
}

int SnmpRequestContext::maxRepetitions() const
{
    return requestConfig.maxRepetitions;
}

int SnmpRequestContext::nonRepeaters() const
{
    return requestConfig.nonRepeaters;
}
