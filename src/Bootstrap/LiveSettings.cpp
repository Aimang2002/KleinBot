#include "LiveSettings.h"

LiveSettings::LiveSettings(const RuntimeSettings &startup)
    : chat(startup.chat),
      drawingModel(startup.models.drawing),
      visionModel(startup.models.vision),
      workerModel(startup.models.worker),
      voice(startup.voice),
      globalVoice(startup.voice.enabled),
      memory(startup.memory),
      webSearch(startup.webSearch),
      webFetch(startup.webFetch),
      dock(startup.dock),
      glance(startup.glance),
      message(startup.message),
      affect(startup.affect)
{
}
