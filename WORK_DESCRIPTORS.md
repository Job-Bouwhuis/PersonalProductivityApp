# Visible work descriptors

The app's shared `WorkDescriptorService` tracks user-visible asynchronous and long-running operations. The Activity tab shows each task as a tree; the main window status bar summarizes active root tasks and shows their progress.

Use a root task for each operation and child tasks for meaningful phases or requests. Progress is calculated recursively from child progress when a parent has no explicit estimate. Child progress is averaged evenly. If any active child has unknown progress, its parent's progress is indeterminate. A parent may instead supply an explicit percentage when it has a reliable estimate, as mail synchronization does from the number of folders completed.

```cpp
const QString operation = workService.startTask(QStringLiteral("Synchronize Outlook mail"));
const QString request = workService.startTask(QStringLiteral("Load mail folders"), operation);
workService.updateTask(request, QStringLiteral("Waiting for Microsoft Graph…"));

// On a successful response:
workService.completeTask(request, QStringLiteral("Folder list loaded"));
workService.updateTask(operation, QStringLiteral("Synchronizing folder 1 of 8…"), 0);

// On a terminal error:
workService.failTask(request, QStringLiteral("Microsoft Graph returned HTTP 403"));
workService.failTask(operation, QStringLiteral("Mail synchronization failed"));
```

For work without a reliable percentage, call `updateTask(id, detail, std::nullopt)` or `updateTaskDetail(id, detail)`. Complete and fail tasks on every terminal path. Finished tasks remain visible briefly so users can read the outcome, then the service removes them automatically.

Current integrations include Microsoft Graph requests, initial and full mailbox scans, delta synchronization and its request subtasks, Outlook sign-in/disconnection, desktop notification delivery, countdown timer mutations, TODO and note mutations, user-triggered settings changes, and email link opening/trusted-domain changes. Long-running operations use nested descriptors; quick operations remain visible briefly with a completed or failed outcome.
