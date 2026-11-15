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
    // options：组合根的活值（ADR-023），每次执行开头取一份快照（结果条数与正文上限）；
    // 是否对模型开放（available）每次问的时候读 enabled
    WebSearchAction(SearchProvider &provider, const Live<WebSearchOptions> &options);

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;
    // web_search.enabled 是活值：关掉后工具仍注册，只是不对模型开放（ADR-023 稳定外壳）
    bool available() const override { return options.get()->enabled; }
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &context) const override
    {
        return webSearchUsageGuidance(scene, context);
    }

private:
    SearchProvider &provider;
    const Live<WebSearchOptions> &options;
};

#endif // WEB_SEARCH_ACTION_H
