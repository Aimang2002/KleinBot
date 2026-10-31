#ifndef WEB_SEARCH_ACTION_H
#define WEB_SEARCH_ACTION_H

#include "Action.h"
#include "WebToolGuidance.h"
#include "../WebSearch/SearchProvider.h"
#include "../WebSearch/WebSearchOptions.h"

class WebSearchAction : public Action
{
public:
    WebSearchAction(SearchProvider &provider, WebSearchOptions options);

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &context) const override
    {
        return webSearchUsageGuidance(scene, context);
    }

private:
    SearchProvider &provider;
    WebSearchOptions options;
};

#endif // WEB_SEARCH_ACTION_H
