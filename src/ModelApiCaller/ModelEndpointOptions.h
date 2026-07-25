#ifndef MODEL_ENDPOINT_OPTIONS_H
#define MODEL_ENDPOINT_OPTIONS_H

#include <string>

struct ModelEndpointOptions
{
    std::string model;
    std::string endpoint;
    std::string apiKey;
    std::string apiStandard;

    bool configured() const
    {
        return !model.empty() && !endpoint.empty() && !apiStandard.empty();
    }
};

#endif
