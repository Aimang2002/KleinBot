#ifndef WEB_SEARCH_ACTION_H
#define WEB_SEARCH_ACTION_H

#include "Action.h"
#include "WebToolGuidance.h"
#include "../WebSearch/SearchProvider.h"
#include "../WebSearch/WebSearchOptions.h"
#include "../utils/Live.h"

class WebSearchAction : public Action
{
public:
    // options：组合根的活值（ADR-023），每次执行开头取一份快照（结果条数与正文上限）
    WebSearchAction(SearchProvider &provider, const Live<WebSearchOptions> &options);

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &context) const override
    {
        return webSearchUsageGuidance(scene, context);
    }

private:
    SearchProvider &provider;
    const Live<WebSearchOptions> &options;
};

#endif // WEB_SEARCH_ACTION_H
