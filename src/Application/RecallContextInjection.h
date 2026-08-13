#ifndef RECALL_CONTEXT_INJECTION_H
#define RECALL_CONTEXT_INJECTION_H

#include "../Port/ChatRequest.h"
#include <string>

bool attachRecallContext(ChatRequest &request, const std::string &evidence);

#endif
