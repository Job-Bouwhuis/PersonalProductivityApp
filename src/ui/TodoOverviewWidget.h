#pragma once

#include "TodoService.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLineEdit;
class QTableWidget;

namespace productivity::services {
class NotesService;
}

namespace productivity::ui {

class TodoOverviewWidget final : public QWidget
{
public:
    TodoOverviewWidget(services::TodoService& todoService,
                       services::NotesService& notesService,
                       std::function<void(const QString&, bool)> openNote,
                       QWidget* parent = nullptr);

private:
    void refresh(const QString& preferredId = {});
    void createTodo();
    void editTodo();
    void toggleCompletion();
    void deleteTodo();
    void openLink();
    services::TodoRecord selectedTodo() const;

    services::TodoService& todoService_;
    services::NotesService& notesService_;
    std::function<void(const QString&, bool)> openNote_;
    QLineEdit* searchInput_ = nullptr;
    QComboBox* filterInput_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace productivity::ui
