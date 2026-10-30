#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace productivity::services {
class TimerService;
class WorkDescriptorService;
}

namespace productivity::ui {

class TimerOverviewWidget final : public QWidget
{
public:
    TimerOverviewWidget(services::TimerService& timerService,
                        services::WorkDescriptorService& workService,
                        QWidget* parent = nullptr);
    void setDefaultDuration(int seconds);

private:
    void addCountdown();
    void cancelSelected();
    void completeSelected();
    void togglePauseSelected();
    void refreshTimers();
    QString selectedEventId() const;
    void showStatus(const QString& message);

    services::TimerService& timerService_;
    services::WorkDescriptorService& workService_;
    QLineEdit* titleInput_ = nullptr;
    QSpinBox* hoursInput_ = nullptr;
    QSpinBox* minutesInput_ = nullptr;
    QSpinBox* secondsInput_ = nullptr;
    QTableWidget* timerTable_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    int defaultDurationSeconds_ = 300;
};

} // namespace productivity::ui
