#ifndef MEMORY_QUERY_PLANNER_H
#define MEMORY_QUERY_PLANNER_H

#include <string>

bool hasExplicitRecallIntent(const std::string &text);
bool looksLikeFactQuestion(const std::string &text);
std::string detectFactTemporal(const std::string &text);

#endif // MEMORY_QUERY_PLANNER_H
