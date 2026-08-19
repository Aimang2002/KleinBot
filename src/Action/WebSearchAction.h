#ifndef WEB_SEARCH_ACTION_H
#define WEB_SEARCH_ACTION_H

#include "Action.h"
#include "../WebSearch/SearchProvider.h"
#include "../WebSearch/WebSearchOptions.h"

class WebSearchAction : public Action
{
public:
    WebSearchAction(SearchProvider &provider, WebSearchOptions options);

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;

private:
    SearchProvider &provider;
    WebSearchOptions options;
};

#endif // WEB_SEARCH_ACTION_H
