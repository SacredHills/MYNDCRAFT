#pragma once
#include <QListWidget>
#include <QWidget>

#include "UI/EditorSession.h"
#include "UI/SymbolActions.h"

namespace mynd {

// The symbol library: every symbol of the project with its type and how many instances use it. Double-click
// edits a symbol; the buttons insert an instance or convert the active layer.
class SymbolPanel : public QWidget {
    Q_OBJECT
public:
    SymbolPanel(EditorSession* s, SymbolActions* a, QWidget* parent = nullptr);
    QListWidget* list() const { return m_list; }

private:
    void rebuild();
    EditorSession* m_s;
    SymbolActions* m_a;
    QListWidget* m_list;
    QMetaObject::Connection m_c1, m_c2, m_c3;
    bool m_busy = false;
};

}  // namespace mynd
