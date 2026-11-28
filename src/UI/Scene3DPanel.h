#pragma once
#include <QColor>
#include <QTreeWidget>
#include <QSet>
#include <QUuid>
#include <QWidget>

#include "UI/EditorSession.h"
#include "UI/Scene3DActions.h"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QPushButton;
class QToolButton;
class QLabel;
class QListWidget;
class QSpinBox;

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
    QDoubleSpinBox* repeatField(int axis) const { return axis == 0 ? m_repU : m_repV; }
    QDoubleSpinBox* offsetField(int axis) const { return axis == 0 ? m_offU : m_offV; }
    QDoubleSpinBox* intensityField() const { return m_intensity; }
    QToolButton* stopwatch(const QString& what) const { for (const KeyRow& r : m_keyRows) if (r.what == what) return r.watch; return nullptr; }
    QToolButton* keyButton(const QString& what) const { for (const KeyRow& r : m_keyRows) if (r.what == what) return r.key; return nullptr; }
    QGroupBox* cardGroup() const { return m_gCard; }
    QCheckBox* cardLitBox() const { return m_cardLit; }
    QCheckBox* slerpBox() const { return m_slerp; }
    QGroupBox *transformGroup() const { return m_gXform; }
    QGroupBox *materialGroup() const { return m_gMat; }
    QGroupBox *lightGroup() const { return m_gLight; }
    QGroupBox *cameraGroup() const { return m_gCam; }
    // Animation clips (test access)
    QListWidget* clipList() const { return m_clipList; }
    QListWidget* placementList() const { return m_placeList; }
    QGroupBox* clipGroup() const { return m_gClips; }
    bool addSelectedClipToTimeline();                              // what "Add to Timeline" does
    void dropOn(const QUuid& node, const QUuid& target, int where);      // what a drag & drop in the tree does
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
    QSet<QUuid> m_collapsed;                                  // groups the user folded: they stay folded when the list is rebuilt
    bool m_inItemEdit = false, m_treeRebuildQueued = false;   // a tree item's own change signal is running: do not delete it
    bool m_updating = false;
    QGroupBox *m_gXform, *m_gMat, *m_gLight, *m_gCam, *m_gScene;
    QDoubleSpinBox *m_pos[3], *m_rot[3], *m_scl[3];
    QComboBox* m_matCombo;
    QPushButton *m_baseColor, *m_emissive, *m_texture, *m_clearTexture;
    QDoubleSpinBox *m_rough, *m_metal;
    QDoubleSpinBox *m_repU, *m_repV, *m_offU, *m_offV;
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
    QGroupBox* m_gClips = nullptr;
    QGroupBox* m_gCard = nullptr;
    QLabel* m_cardSource = nullptr;
    QCheckBox* m_cardLit = nullptr;
    QCheckBox* m_slerp = nullptr;
    QListWidget *m_clipList = nullptr, *m_placeList = nullptr;
    QSpinBox *m_pStart = nullptr, *m_pOffset = nullptr;
    QDoubleSpinBox* m_pSpeed = nullptr;
    QCheckBox *m_pLoop = nullptr, *m_pEnabled = nullptr;
    QPushButton *m_clipAdd = nullptr, *m_clipRename = nullptr, *m_clipDelete = nullptr, *m_placeRemove = nullptr;
    void refreshClips();
    QUuid selectedPlacement() const;
    void editPlacement(const QString& text, const QString& key, const std::function<void(ClipPlacement&)>& fn);
    QColor m_baseC, m_emisC, m_lightC, m_ambC, m_bgC;
};

}  // namespace mynd
