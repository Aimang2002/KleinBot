#include "LiveSettings.h"

LiveSettings::LiveSettings(const RuntimeSettings &startup)
    : chat(startup.chat),
      models(startup.models),
      voice(startup.voice),
      memory(startup.memory),
      webSearch(startup.webSearch),
      webFetch(startup.webFetch),
      dock(startup.dock),
      glance(startup.glance),
      message(startup.message)
{
}
