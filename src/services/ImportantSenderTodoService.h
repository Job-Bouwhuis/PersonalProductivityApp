#pragma once

#include <QJsonArray>

namespace productivity::services {

class ApplicationSettings;
class TodoService;
class WorkDescriptorService;

class ImportantSenderTodoService final
{
public:
    ImportantSenderTodoService(ApplicationSettings& settings,
                               TodoService& todoService,
                               WorkDescriptorService& workService);

    void createForNewMessages(const QJsonArray& messages);

private:
    ApplicationSettings& settings_;
    TodoService& todoService_;
    WorkDescriptorService& workService_;
};

} // namespace productivity::services
