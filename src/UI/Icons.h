#pragma once
#include <QIcon>

#include "Scene/Layer.h"
#include "UI/EditorSession.h"

namespace mynd::icons {
QIcon tool(ToolId id);
QIcon layerType(LayerType t);
QIcon colorSwatch(const QColor& c, int size = 20);
}  // namespace mynd::icons
