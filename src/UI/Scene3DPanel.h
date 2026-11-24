#pragma once
#include <QColor>
#include <QTreeWidget>
#include <QWidget>

#include "UI/EditorSession.h"
#include "UI/Scene3DActions.h"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QPushButton;
class QToolButton;

namespace mynd {

// Scene hierarchy tree plus an inspector for the selected node: transform, material, light, camera, and scene settings.
class Scene3DPanel : public QWidget {
    Q_OBJECT
public:
    struct KeyRow { QToolButton *watch = nullptr, *key = nullptr; QString what; bool material = false; };
    Scene3DPanel(EditorSession* s, Scene3DActions* a, QWidget* parent = nullptr);
    // Test access.
    QTreeWidget* tree() const { return m_tree; }
    QDoubleSpinBox* posField(int axis) const { return m_pos[axis]; }
    QDoubleSpinBox* roughnessField() const { return m_rough; }
    QDoubleSpinBox* intensityField() const { return m_intensity; }
    QToolButton* stopwatch(const QString& what) const { for (const KeyRow& r : m_keyRows) if (r.what == what) return r.watch; return nullptr; }
    QToolButton* keyButton(const QString& what) const { for (const KeyRow& r : m_keyRows) if (r.what == what) return r.key; return nullptr; }
    QGroupBox *transformGroup() const { return m_gXform; }
    QGroupBox *materialGroup() const { return m_gMat; }
    QGroupBox *lightGroup() const { return m_gLight; }
    QGroupBox *cameraGroup() const { return m_gCam; }
    void dropOn(const QUuid& node, const QUuid& newParent);      // what a drag & drop in the tree does
    bool setBaseTexture(const QImage& img);                       // what "Texture…" does after the file is read

private:
    QWidget* labelWithKeys(const QString& text, const QString& what, bool material);
    QString keyPropId(const KeyRow& r) const;      // empty when nothing is selected
    void rebuildTree();
    void refreshInspector();
    void edit(const QString& text, const QString& mergeKey, const std::function<bool(Scene3D&, Node3D&)>& fn);
    void editScene(const QString& text, const QString& mergeKey, const std::function<bool(Scene3D&)>& fn);
    void editMaterial(const QString& text, const QString& mergeKey, const std::function<bool(Material&)>& fn);
    QUuid currentMaterialId() const;
    void pickColor(QPushButton* b, const QString& title, bool alpha, const std::function<void(const QColor&)>& apply);
    static void paintColor(QPushButton* b, const QColor& c);

    EditorSession* m_s;
    Scene3DActions* m_a;
    QTreeWidget* m_tree;
    bool m_updating = false;
    QGroupBox *m_gXform, *m_gMat, *m_gLight, *m_gCam, *m_gScene;
    QDoubleSpinBox *m_pos[3], *m_rot[3], *m_scl[3];
    QComboBox* m_matCombo;
    QPushButton *m_baseColor, *m_emissive, *m_texture, *m_clearTexture;
    QDoubleSpinBox *m_rough, *m_metal;
    QCheckBox* m_double;
    QComboBox* m_lightType;
    QPushButton* m_lightColor;
    QDoubleSpinBox *m_intensity, *m_range, *m_spot;
    QDoubleSpinBox *m_fov, *m_near, *m_far;
    QCheckBox* m_isSceneCam;
    QPushButton *m_ambientColor, *m_bgColor;
    QDoubleSpinBox* m_ambientInt;
    QComboBox* m_cameraCombo;
    QVector<KeyRow> m_keyRows;
    QColor m_baseC, m_emisC, m_lightC, m_ambC, m_bgC;
};

}  // namespace mynd
