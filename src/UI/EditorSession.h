#pragma once
#include <QColor>
#include <QObject>
#include <QSet>

#include "Drawing/BrushEngine.h"
#include "Scene/Document.h"
#include "Scene/TextContent.h"

namespace mynd {

enum class ToolId {
    Select, DirectSelect, Brush, Pencil, Eraser, Pen, Line, Rectangle, Ellipse, Polygon,
    Bucket, Gradient, Eyedropper, Hand, Zoom, Smudge, FreeTransform, Text
};
QString toolName(ToolId t);

struct VectorStyle {
    bool fillEnabled = true;
    QColor fill = QColor("#4a90d9");
    bool strokeEnabled = true;
    QColor stroke = QColor("#1b1b1b");
    double strokeWidth = 3.0;
};

// Transient editing state (not saved in the project): current tool, colors, brushes, selection,
// active layer, view toggles. Tools and panels talk to each other through this object.
class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    Document* doc() const { return m_doc; }
    void setDocument(Document* d);

    // Active layer / selection
    QUuid activeLayerId() const { return m_activeLayer; }
    Layer* activeLayer() const { return m_doc ? m_doc->findLayer(m_activeLayer) : nullptr; }
    void setActiveLayer(const QUuid& id);
    // Returns the active layer if it has the requested type and is editable; otherwise (optionally)
    // creates one above the active layer. Emits message() when it cannot edit.
    Layer* editableLayer(LayerType t, bool autoCreate);
    QUuid addLayerAboveActive(LayerType t, const QString& name);
    QUuid addTextLayerAboveActive(const TextContent& c, const QString& name);
    QUuid addImageLayerAboveActive(const QImage& img, const QString& name);

    const QSet<QUuid>& selectedShapes() const { return m_selected; }
    void setSelectedShapes(const QSet<QUuid>& s, bool pullStyle = true);
    void clearSelection() { setSelectedShapes({}); }
    // Expand ids to include every shape sharing a group id in the layer.
    QSet<QUuid> expandGroups(const Layer& l, const QSet<QUuid>& ids) const;

    // Tool
    ToolId tool() const { return m_tool; }
    void setTool(ToolId t);

    // Paint settings
    QColor foreground() const { return m_fg; }
    QColor background() const { return m_bg; }
    void setForeground(const QColor& c);
    void setBackground(const QColor& c);
    void swapColors();
    BrushSettings& brushFor(ToolId t);
    BrushSettings brush(ToolId t) const;
    void notifyBrushChanged() { emit brushChanged(); }

    // Vector style
    const VectorStyle& style() const { return m_style; }
    // Changes the default style and, if `applyToSelection`, restyles the selected shapes (undoable).
    void setStyle(const VectorStyle& s, bool applyToSelection = true);
    VectorShape styled(VectorShape s) const;  // apply current style to a new shape

    // Text defaults (used for new text). If the active layer is a text layer, changes are applied to it too.
    const TextContent& textStyle() const { return m_textStyle; }
    void applyTextStyle(const TextContent& style);

    int polygonSides = 5;
    int bucketTolerance = 24;

    // View toggles (persisted by MainWindow)
    bool gridVisible = false, rulersVisible = true, guidesVisible = true, snapEnabled = false;

signals:
    void documentChanged();
    void activeLayerChanged(const QUuid& id);
    void selectionChanged();
    void toolChanged(ToolId t);
    void colorsChanged();
    void styleChanged();
    void textStyleChanged();
    void brushChanged();
    void viewTogglesChanged();
    void message(const QString& text);

private:
    Document* m_doc = nullptr;
    QUuid m_activeLayer;
    QSet<QUuid> m_selected;
    ToolId m_tool = ToolId::Brush;
    QColor m_fg = Qt::black, m_bg = Qt::white;
    BrushSettings m_brush, m_pencil, m_eraser, m_smudge;
    TextContent m_textStyle;
    VectorStyle m_style;
};

}  // namespace mynd
