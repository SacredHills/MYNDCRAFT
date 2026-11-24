#pragma once
#include <QAction>
#include <QMenu>
#include <QObject>

#include "UI/EditorSession.h"

namespace mynd {

// Symbol commands as QActions (menu + library panel). Dialog-free variants (convertActive, newSymbol, ...) do the
// actual work so they can be driven from tests and from the panel.
class SymbolActions : public QObject {
    Q_OBJECT
public:
    SymbolActions(EditorSession* s, QWidget* dialogParent);
    QAction *exportSymbolAction, *importSymbolsAction;
    QAction *convert, *newSymbol, *insertInstance, *editSymbol, *exitSymbol, *deleteSymbol, *renameSymbol;
    void fillMenu(QMenu* menu);

    // The symbol picked in the library panel (null = none); "Insert" and "Edit" act on it, falling back to the
    // symbol of the active instance.
    void setLibrarySelection(const QUuid& id);
    QUuid targetSymbol() const;

    QUuid convertActive(const QString& name, SymbolKind kind);
    QUuid makeNewSymbol(const QString& name, SymbolKind kind);
    QUuid insertTarget();
    bool editTarget();
    bool leave();
    bool removeTarget(QString* why = nullptr);
    bool exportTarget(const QString& path, QString* error);     // the symbol (and the ones it uses) as a .studio asset
    QVector<QUuid> importFrom(const QString& path, QString* error);
    void refresh();

private:
    EditorSession* m_s;
    QWidget* m_parent;
    QUuid m_sel;
};

}  // namespace mynd
