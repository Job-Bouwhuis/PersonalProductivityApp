# Project TODO — Personal Productivity Service

**Stack:** C++ · Qt 6.12 · SVG · Linux desktop first, unless expanded later

This project is a personal productivity daemon: a Qt desktop app that lives in the background, integrates with Microsoft Outlook, manages tasks and notes, and runs a unified notification and timer system.

The most important architectural decision is to make the background services, timer engine, and notification scheduler shared infrastructure rather than implementing separate timing logic for email, TODOs, drinks, and countdowns.

## 1. Project foundation and installation

- [ ] Establish the C++ project structure, build configuration, and module boundaries.
- [ ] Use Qt 6.12 for the application, UI, background operation, and system integration wherever practical.
- [ ] Use SVG icons throughout the UI and system tray wherever supported.
- [ ] Create `setup.sh` that assumes the repository is already cloned and:
  - [ ] Detects the current Linux distribution and available package manager.
  - [ ] Installs or verifies required build dependencies.
  - [ ] Configures and builds the application.
  - [ ] Installs the application into the current system.
  - [ ] Installs the required runtime assets, icons, configuration templates, and service files.
  - [ ] Configures the application to start automatically on login.
  - [ ] Supports clean reinstallation and upgrades.
- [ ] Create a separate installation/bootstrap script that:
  - [ ] Clones the repository into a defined location.
  - [ ] Invokes the repository's `setup.sh`.
  - [ ] Stops safely if cloning or installation fails.
- [ ] Keep user data, settings, credentials, and application binaries in appropriate separate locations.
- [ ] Provide an uninstall path that removes installed application files without deleting user data by default.
- [ ] Add logging, error reporting, and clear installation diagnostics.

## 2. Background application and system tray

- [ ] Run as a background application without requiring an open window.
- [ ] Provide a system tray icon with a context menu.
- [ ] Allow the main window to be opened and hidden without stopping background services.
- [ ] Support launching automatically when the user logs into the desktop.
- [ ] Prevent accidental duplicate application instances.
- [ ] Provide controls to exit, restart services, open settings, and view active timers.
- [ ] Ensure background operations continue while the main window is hidden.
- [ ] Handle application shutdown cleanly, including pending operations and persistent state.

## 3. Microsoft Outlook integration

- [ ] Authenticate through Microsoft's official authentication and mail APIs.
- [ ] Use Microsoft Graph with the appropriate Microsoft identity platform authentication flow and permissions.
- [ ] Store authentication tokens securely using the operating system's available credential storage.
- [ ] Provide connection, disconnection, and authentication recovery flows.
- [ ] Retrieve inbox messages and maintain a local index of previously discovered mail.
- [ ] On first startup, perform a full initial inbox scan.
- [ ] Provide a manual **Scan all mail** action to perform a full reconciliation scan.
- [ ] For subsequent scans, retrieve only new or changed mail without repeatedly scanning the entire inbox.
- [ ] Persist synchronization state so application restarts do not trigger unnecessary full scans.
- [ ] Handle pagination, API throttling, connectivity failures, and expired credentials.
- [ ] Prefer change notifications or delta queries where supported, with polling as a fallback.
- [ ] Keep message identifiers stable so TODOs and notes can link to the original email.

## 4. Email notifications, filtering, and mail views

- [ ] Notify the desktop when new mail arrives.
- [ ] Avoid repeatedly notifying for messages already processed.
- [ ] Allow an individual email to be marked as:
  - [ ] Ignored.
  - [ ] Important.
- [ ] Maintain configurable ignored-sender rules.
- [ ] Maintain configurable ignored-folder rules.
- [ ] Automatically mark messages from ignored senders as read in Microsoft 365/Outlook itself.
- [ ] Automatically mark messages in ignored folders as read in the remote mail service.
- [ ] Apply rules to incoming mail and reconcile rules against existing messages when appropriate.
- [ ] Handle failures to update remote read status and retry safely.
- [ ] Provide an **Unread mail** view:
  - [ ] Include unread messages across all accessible folders, not just the inbox.
  - [ ] Sort newest first.
  - [ ] Show a genuinely empty list when there are no unread messages.
  - [ ] Reflect read-status changes made in Outlook or by the application.
- [ ] Provide an **All mail** view for browsing existing read and unread messages.
- [ ] Support refreshing, searching, opening, and navigating between messages.
- [ ] Provide sender, subject, received date, folder, and read/important status where available.

## 5. Email message viewer and HTML safety

- [ ] Implement an in-app message viewer.
- [ ] Render HTML email bodies where possible, with a plain-text fallback.
- [ ] Preserve reasonable email formatting, links, lists, tables, and inline images.
- [ ] Treat email content as untrusted input.
- [ ] Prevent email HTML from executing arbitrary JavaScript.
- [ ] Disable access to local files and arbitrary local resources.
- [ ] Prevent remote content from loading automatically unless explicitly allowed.
- [ ] Sanitize unsafe HTML and restrict navigation to permitted URL schemes.
- [ ] Prevent email content from accessing application privileges or local credentials.
- [ ] Provide a way to open legitimate links externally.
- [ ] Clearly handle blocked images, unsupported content, and rendering failures.

## 6. TODO manager

- [ ] Create, edit, complete, reopen, and delete TODO items.
- [ ] Give each item a title, description, status, creation time, and optional deadline.
- [ ] Support linking TODOs to:
  - [ ] Local files.
  - [ ] Websites.
  - [ ] Emails stored in the mail service.
  - [ ] Persistent or quick notes in the note service.
- [ ] Allow links to be opened from the TODO item.
- [ ] Preserve links using stable identifiers where possible, rather than fragile display names.
- [ ] Display upcoming, overdue, completed, and unscheduled tasks.
- [ ] Allow manual creation and editing of deadlines.
- [ ] Support automatic TODO creation for important email senders.
- [ ] Prevent duplicate TODO creation when the same email is encountered during synchronization.
- [ ] Route all deadline reminders through the shared timer and notification service.

### Important-sender deadline rules

When an email from an important sender arrives, create a TODO with a deadline determined by the local time of receipt.

| Email received | TODO deadline |
|---|---|
| Before 16:00 | Today at 17:00 |
| From 16:00 until before 21:00 | Today at 22:00 |
| At or after 21:00 | Tomorrow at 17:00 |

- [ ] Use the system's local timezone.
- [ ] Persist the computed deadline so subsequent scans do not recalculate it.
- [ ] Define a deterministic policy for mail received after midnight and for messages imported long after their original arrival.
- [ ] Ensure all generated TODOs use the same reminder rules as manually created TODOs.

## 7. Unified timer and notification engine

This is the central piece of the application. Email notifications, TODO deadlines, countdown timers, and drink reminders should use shared infrastructure while retaining their own scheduling policies.

- [ ] Implement a centralized timer service.
- [ ] Represent scheduled events with stable IDs, deadlines, status, and notification policy.
- [ ] Use absolute timestamps for deadlines and a monotonic clock for elapsed-time measurements where appropriate.
- [ ] Persist timers and scheduled TODO deadlines across application restarts.
- [ ] Support cancellation, rescheduling, pausing where applicable, and completion.
- [ ] Handle sleep, suspend/resume, clock changes, and missed notifications.
- [ ] Prevent duplicate reminders after restarts or synchronization retries.
- [ ] Provide a unified overview of active timers and upcoming deadlines.
- [ ] Route desktop notifications through one notification coordinator.
- [ ] Update an existing countdown notification when supported, rather than creating a new notification every second.
- [ ] Fall back gracefully when the desktop notification service does not support notification updates.

### Deadline reminder schedule

- [ ] When a deadline is more than 6 hours away, remind every 6 hours.
- [ ] When a deadline is more than 1 hour away, remind every hour.
- [ ] Within the final hour, notify at 30 minutes, 15 minutes, and 5 minutes remaining.
- [ ] Display the final 10 seconds as a countdown.
- [ ] Show the final 10-second countdown as timely notifications rather than waiting until the deadline has passed.
- [ ] Display a reminder before each applicable milestone and keep it visible for 10 seconds when the notification system allows.
- [ ] Update the countdown text to reflect the remaining time.
- [ ] At the deadline, apply a defined completion or overdue policy without silently deleting the item.

**Timing clarification for implementation:** the milestone notifications should be scheduled ahead of the milestone they describe. For example, a notification announcing that one hour remains should appear slightly before that point, with the countdown updating as it approaches. The engine should calculate the exact notification start times rather than simply polling once per minute.

Recurring timers are different:

- [ ] Drink reminders and mail-scan intervals have configurable recurrence settings.
- [ ] Recurring timers do not emit countdown-progress notifications.
- [ ] Recurring timers still appear in the timer overview.
- [ ] Recurring events must not accidentally inherit TODO deadline reminders.

## 8. Drink reminder system

- [ ] Implement a configurable recurring drink reminder.
- [ ] Allow the user to set the interval between reminders.
- [ ] Allow the reminder system to be enabled or disabled.
- [ ] Show desktop notifications when a drink reminder becomes due.
- [ ] Do not play a sound for now.
- [ ] Structure the implementation so a user-supplied sound file can be added later.
- [ ] Keep the next scheduled drink reminder visible in the timer overview.
- [ ] Define how the next reminder is calculated when the app starts, resumes from sleep, or misses a scheduled reminder.

## 9. Notes system

- [ ] Implement two note workflows:
  - [ ] **Quick notes:** capture information rapidly and discard it without ceremony.
  - [ ] **Persistent notes:** store information until explicitly deleted.
- [ ] Make quick-note creation accessible without opening a complicated editor.
- [ ] Allow persistent notes to be created, edited, searched, and deleted.
- [ ] Support Markdown, HTML, or both for persistent note content.
- [ ] Preserve note formatting when saving and reopening.
- [ ] Use stable note IDs for TODO links.
- [ ] Ensure deleting a note does not silently corrupt TODOs that reference it.
- [ ] Persist notes locally and recover them after application restarts.
- [ ] Add autosave or another reliable mechanism to prevent accidental loss of edits.
- [ ] Keep quick-note dismissal separate from permanent-note deletion.

## 10. Manual countdown timers

- [ ] Provide a UI for creating a countdown timer.
- [ ] Allow the duration to be entered in hours, minutes, and seconds.
- [ ] Start, cancel, and inspect active timers.
- [ ] Display the remaining time in the main UI and timer overview.
- [ ] Route approaching-deadline reminders through the shared timer engine.
- [ ] Notify when a countdown finishes.
- [ ] Allow multiple independent timers to run simultaneously.
- [ ] Persist active timers across application restarts.
- [ ] Define whether users can pause and resume countdowns.

## 11. Settings and configuration

- [ ] Provide a central settings interface.
- [ ] Configure Outlook authentication and mail synchronization.
- [ ] Configure ignored senders, ignored folders, and important senders.
- [ ] Configure recurring drink reminders.
- [ ] Configure automatic mail-scan intervals.
- [ ] Configure notification behavior where supported.
- [ ] Configure startup behavior and background-service operation.
- [ ] Store preferences persistently.
- [ ] Validate settings before applying them.
- [ ] Provide sensible defaults and a way to restore them.

## 12. Reliability, security, and diagnostics

- [ ] Use structured application logs with timestamps, severity levels, and subsystem identifiers.
- [ ] Log synchronization failures and timer scheduling errors without exposing message bodies or secrets.
- [ ] Provide useful diagnostics for authentication, connectivity, and API failures.
- [ ] Handle temporary network outages without crashing or losing state.
- [ ] Use transactional or otherwise crash-safe persistence for important state changes.
- [ ] Ensure service failures in one subsystem do not bring down the whole application.
- [ ] Validate external input, including email HTML, links, and imported data.
- [ ] Avoid hardcoding credentials, tokens, or machine-specific paths.
- [ ] Provide automated tests for deadline calculation, timer scheduling, mail deduplication, and persistence.
- [ ] Test application restart, system suspend/resume, and missed-notification recovery.
- [ ] Keep the UI responsive while background operations run.
- [ ] Make background tasks cancellable and ensure shutdown cannot leave corrupted state.

## 13. Suggested architecture

Keep the application modular from day one. Otherwise, the timer rules, Outlook integration, and UI will eventually become one enormous knot.

```text
Qt 6.12 desktop frontend
  Main window · Tray icon · Mail viewer · TODOs · Notes · Settings
                         |
                         v
Application services
  Mail service · TODO service · Notes service
  Countdown timers · Drink reminders
                         |
                         v
Shared infrastructure
  Deadline scheduler · Notification coordinator
  Persistence · Logging
                         |
                         v
External integrations
  Microsoft Graph · Desktop notifications
  Credential storage · Filesystem
```

Recommended implementation choices:

- **C++ and Qt 6.12:** Qt Widgets for a conventional desktop UI, with Qt Network, Qt SQL, Qt SVG, and Qt's model/view framework as appropriate.
- **SQLite:** local storage for messages' metadata, TODOs, notes, settings, synchronization cursors, and scheduled events. Avoid duplicating entire email bodies unnecessarily.
- **Microsoft Graph:** official Microsoft API access, using OAuth authentication through MSAL or another appropriate supported authentication implementation. Request only the permissions needed.
- **Desktop notifications:** use the Linux desktop notification service, with a fallback for environments that cannot update existing notifications in place.
- **Background operation:** a single-instance tray application launched at login is a good starting point. Separate it into a system service and UI process only if a real requirement emerges.
- **Testing:** isolate deadline calculations and scheduling logic from Qt UI code so they can be tested without launching the application.

## 14. Recommended implementation milestones

These milestones give Codex a sensible order of work, with a testable result after each stage.

### Milestone 1 — Foundation

**Scope:** Project skeleton, CMake, `setup.sh`, clone-and-install script, tray icon, logging, and SQLite.

Acceptance checks:
- [ ] Clean build.
- [ ] Installation works.
- [ ] App starts in the background.

### Milestone 2 — Shared scheduler

**Scope:** Persistent timers, notification coordinator, countdown display, and deadline reminder rules.

Acceptance checks:
- [ ] Correct reminder times.
- [ ] Restart recovery.
- [ ] No duplicate notifications.

### Milestone 3 — Outlook integration

**Scope:** Authentication, initial inbox scan, incremental synchronization, manual full scan, and unread/all-mail views.

Acceptance checks:
- [ ] OAuth works.
- [ ] Incremental scan works.
- [ ] Unread view spans folders.

### Milestone 4 — Mail processing

**Scope:** Ignored and important rules, remote read-state updates, HTML viewer, and email-linked TODOs.

Acceptance checks:
- [ ] Remote read state changes.
- [ ] No duplicate TODOs.
- [ ] Unsafe HTML is restricted.

### Milestone 5 — TODOs and notes

**Scope:** CRUD workflows, deadlines, external links, quick notes, and persistent formatted notes.

Acceptance checks:
- [ ] Links survive restart.
- [ ] Notes persist.
- [ ] Deadlines schedule correctly.

### Milestone 6 — Recurring utilities

**Scope:** Configurable drink reminders, mail polling intervals, and manual countdown timers.

Acceptance checks:
- [ ] Multiple timers work.
- [ ] Recurring events behave correctly.
- [ ] Timers survive restart.

### Milestone 7 — Hardening and release

**Scope:** Failure recovery, security review, installation upgrades, uninstall, tests, and documentation.

Acceptance checks:
- [ ] Network recovery.
- [ ] Clean upgrade.
- [ ] No user-data loss.

## 15. Decisions to settle before handing it to Codex

A few details have a significant impact on the implementation. Establish these up front rather than letting Codex invent behavior.

- [ ] **Mail synchronization:** use Microsoft Graph delta queries to track changes incrementally, with a configurable polling interval. Consider Graph change notifications later if their setup is worthwhile.
- [ ] **Folder coverage:** explicitly decide whether unread mail includes every mail folder, including archive folders and nested folders, or only folders accessible through the connected mailbox.
- [ ] **Ignored folders:** define whether ignoring a folder applies recursively to subfolders and whether the rule should mark existing messages as read.
- [ ] **Important emails:** decide what happens when one message matches both ignored and important rules. Suggested default: explicit ignore rules take precedence.
- [ ] **Late imports:** decide whether an important email discovered hours or days after arrival gets its original deadline, an adjusted deadline, or an immediate TODO.
- [ ] **Notification persistence:** desktop notifications can disappear or behave differently between desktop environments. The app's own timer overview should remain authoritative.
- [ ] **Email HTML:** rendering must be isolated and sanitized; merely hiding JavaScript in a WebEngine view is not sufficient protection.
- [ ] **Timer semantics:** define whether a missed deadline generates a catch-up notification after the system resumes from sleep, and whether completed TODOs cancel all pending reminders.

### Important-sender deadline boundaries

Interpret the deadline rules as:

- Before 16:00 → today at 17:00.
- From 16:00 until before 21:00 → today at 22:00.
- At or after 21:00 → tomorrow at 17:00.

This resolves the exact 16:00 and 21:00 cases deterministically.

## Final recommendation

Make the scheduler and notification coordinator milestone two, not something added at the end. They are the backbone of this whole project, and building them early will make every subsequent feature easier to integrate.
