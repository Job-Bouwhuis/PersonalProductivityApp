# Project TODO — Personal Productivity Service

**Stack:** C++ · Qt 6.12 · SVG · Linux desktop first, unless expanded later

This project is a personal productivity daemon: a Qt desktop app that lives in the background, integrates with Microsoft Outlook, manages tasks and notes, and runs a unified notification and timer system.

The most important architectural decision is to make the background services, timer engine, and notification scheduler shared infrastructure rather than implementing separate timing logic for email, TODOs, drinks, and countdowns.

## 1. Project foundation and installation

- [x] Establish the C++ project structure, build configuration, and module boundaries.
- [x] Use Qt 6.12 for the application, UI, background operation, and system integration wherever practical.
- [x] Use SVG icons throughout the UI and system tray wherever supported.
- [x] Create `setup.sh` that assumes the repository is already cloned and:
  - [x] Detects the current Linux distribution and available package manager.
  - [x] Installs or verifies required build dependencies.
  - [x] Configures and builds the application.
  - [x] Installs the application into the current system.
  - [x] Installs the required runtime assets, icons, configuration templates, and service files.
  - [x] Configures the application to start automatically on login.
  - [x] Supports clean reinstallation and upgrades.
- [x] Create a separate installation/bootstrap script that:
  - [x] Clones the repository into a defined location.
  - [x] Invokes the repository's `setup.sh`.
  - [x] Stops safely if cloning or installation fails.
- [x] Keep user data, settings, credentials, and application binaries in appropriate separate locations.
  - [x] Store application data and logs under Qt's per-user local-data location.
  - [x] Store preferences under Qt's per-user configuration location.
  - [x] Store authentication credentials in the operating system credential store.
  - [x] Install application binaries and bundled Qt runtime files under the configured installation prefix.
- [x] Provide an uninstall path that removes installed application files without deleting user data by default.
- [x] Add logging, error reporting, and clear installation diagnostics.

## 2. Background application and system tray

- [x] Run as a background application without requiring an open window.
- [x] Provide a system tray icon with a context menu.
- [x] Allow the main window to be opened and hidden without stopping background services.
- [x] Support launching automatically when the user logs into the desktop.
- [x] Prevent accidental duplicate application instances.
- [x] Provide controls to exit, restart services, open settings, and view active timers.
- [x] Ensure background operations continue while the main window is hidden.
- [x] Handle application shutdown cleanly, including pending operations and persistent state.

## 3. Microsoft Outlook integration

- [x] Authenticate through Microsoft's official authentication and mail APIs; Microsoft identity platform device-code flow obtains delegated tokens and Microsoft Graph v1.0 handles mail requests.
- [x] Use Microsoft Graph 1.0 with Microsoft identity platform device-code sign-in and delegated Mail.ReadWrite and Mail.Send scopes for the signed-in mailbox.
- [x] Store authentication tokens securely using the operating system's available credential storage. Outlook access and refresh tokens are stored through Linux Secret Service using `secret-tool`, not in the application database.
- [x] Provide connection, disconnection, and authentication recovery flows. Outlook setup uses account-type choices with a custom-tenant option and client-ID validation; device sign-in opens the browser, copies the code, and closes its dialog after credentials are stored. Saved connection state is restored asynchronously, configuration mismatches are rejected before token refresh, and revoked credentials are cleared with a reconnect prompt.
- [x] Retrieve mailbox messages and maintain a local index of previously discovered mail. The Outlook settings action indexes the latest mailbox page (up to 100) into SQLite, updating existing message metadata without storing bodies and retaining each message's parent folder ID.
- [x] On first startup with a saved Outlook connection, perform a paginated initial mailbox scan and persist its completion state; Graph's opaque next-page URL is preserved, and failed scans remain eligible for retry on the next connection.
- [x] Provide a manual **Scan all mail** action to perform a paginated full mailbox re-index, updating indexed message metadata and folder IDs.
- [x] For subsequent scans, retrieve only new or changed mail without repeatedly scanning the entire inbox. Outlook connections after the initial mailbox scan perform per-folder Microsoft Graph delta synchronization; **Sync mail changes** can run it manually. Folder delta links are saved locally and deleted-message changes remove rows from the local index.
- [x] Persist initial mailbox scan completion state so application restarts do not trigger unnecessary full scans.
- [x] Handle pagination, API throttling, connectivity failures, and expired credentials. Graph retries transient transport errors and throttling/service-unavailable responses with bounded backoff and `Retry-After`; HTTP 401 forces one token refresh, then asks the user to reconnect if rejected again.
- [x] Prefer change notifications or delta queries where supported, with polling as a fallback. Per-folder delta sync runs after the initial scan and on Outlook reconnect; while connected, a five-minute polling timer starts another delta round when idle. The timer stops when Outlook is disconnected.
- [x] Keep message identifiers stable so TODOs and notes can link to the original email. Graph mail requests use `Prefer: IdType="ImmutableId"`; saved legacy IDs are translated in batches before delta sync, merging duplicates safely.

## 4. Email notifications, filtering, and mail views

- [x] Notify the desktop when new mail arrives. Incremental synchronization sends a desktop alert for newly indexed messages, with batch notifications summarized to avoid flooding.
- [x] Avoid repeatedly notifying for messages already processed. Notifications are emitted only for message IDs inserted into the persistent local index; initial and manual full scans stay silent.
- [ ] Allow an individual email to be marked as:
  - [ ] Ignored.
  - [ ] Important.
- [x] Maintain configurable ignored-sender rules. Outlook settings can add and remove exact sender email addresses; values are stored in persistent settings and deduplicated case-insensitively. Delta synchronization applies these rules to incoming and changed messages; adding a sender rule queues all indexed folders for reconciliation against existing mail.
- [x] Maintain configurable ignored-folder rules. Outlook settings can load the connected mailbox's folder hierarchy, add/remove selected folders, and persist each exact folder ID with its display path. Rules apply only to the selected folder.
- [x] Automatically mark messages from ignored senders as read in Microsoft 365/Outlook itself. Delta sync patches matching unread messages through Graph, updates the local index, and suppresses new-mail alerts for those ignored senders.
- [x] Automatically mark messages in ignored folders as read in the remote mail service. Delta sync marks unread messages in explicitly selected folders as read through Graph and updates the local index after Microsoft confirms success. Adding a folder rule queues a full sync of that folder to include existing unread messages.
- [x] Apply rules to incoming mail and reconcile rules against existing messages when appropriate. Delta synchronization applies current rules to incoming/changed messages; adding a sender rule queues all indexed folders, while adding a folder rule queues only that folder. Rescan requests persist until a successful folder scan completes.
- [x] Handle failures to update remote read status and retry safely. Graph retries transient failures; if a read-state update still fails, sync stops before saving the folder delta cursor so the batch is replayed next time.
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

- [x] Create, edit, complete, reopen, and delete TODO items.
- [x] Give each item a title, description, status, creation time, and optional deadline.
- [ ] Support linking TODOs to:
  - [x] Local files.
  - [x] Websites.
  - [ ] Emails stored in the mail service.
  - [x] Persistent notes in the note service.
  - [x] Quick notes in the note service.
- [x] Allow links to be opened from the TODO item.
- [x] Preserve links using stable identifiers where possible, rather than fragile display names.
- [x] Display upcoming, overdue, completed, and unscheduled tasks.
- [x] Allow manual creation and editing of deadlines.
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

- [x] Implement a centralized timer service.
- [x] Represent scheduled events with stable IDs, deadlines, status, and notification policy.
- [ ] Use absolute timestamps for deadlines and a monotonic clock for elapsed-time measurements where appropriate. Persistent deadlines are stored as UTC timestamps; monotonic elapsed-time tracking remains unimplemented.
- [ ] Persist timers and scheduled TODO deadlines across application restarts. Countdown timers and TODO deadline fields persist; scheduling TODO reminders through the timer engine remains pending.
- [x] Support cancellation, rescheduling, pausing where applicable, and completion.
- [ ] Handle sleep, suspend/resume, clock changes, and missed notifications.
- [x] Prevent duplicate reminders after restarts or synchronization retries.
- [x] Provide a unified overview of active timers and upcoming deadlines.
- [x] Route desktop notifications through one notification coordinator.
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

- [x] Implement two note workflows:
  - [x] **Quick notes:** capture information rapidly and discard it without ceremony.
  - [x] **Persistent notes:** store information until explicitly deleted.
- [x] Make quick-note creation accessible without opening a complicated editor.
- [x] Allow persistent notes to be created, edited, searched, and deleted.
- [x] Support Markdown, HTML, or both for persistent note content.
- [x] Preserve note formatting when saving and reopening.
- [x] Use stable note IDs for TODO links.
- [x] Ensure deleting a note does not silently corrupt TODOs that reference it.
- [x] Persist notes locally and recover them after application restarts.
- [x] Add autosave or another reliable mechanism to prevent accidental loss of edits.
- [x] Keep quick-note dismissal separate from permanent-note deletion.

## 10. Manual countdown timers

- [x] Provide a UI for creating a countdown timer.
- [x] Allow the duration to be entered in hours, minutes, and seconds.
- [x] Start, cancel, and inspect active timers.
- [x] Display the remaining time in the main UI and timer overview.
- [ ] Route approaching-deadline reminders through the shared timer engine.
- [x] Notify when a countdown finishes.
- [x] Allow multiple independent timers to run simultaneously.
- [x] Persist active timers across application restarts.
- [x] Allow users to pause and resume countdowns while preserving the remaining duration.
- [x] Allow a default countdown duration to be configured and prefilled for new timers.

## 11. Settings and configuration

- [x] Provide a central settings interface.
- [x] Configure Outlook authentication and mail synchronization.
  - [x] Add per-service settings for the Microsoft Entra tenant and public-client application ID.
  - [x] Implement Microsoft device-code sign-in and store tokens using Secret Service; personal accounts use the `common` or `consumers` tenant with a matching app registration.
  - [x] Add a Microsoft Graph client with bearer-authenticated message listing, mark-read, and send-mail operations, plus access-token refresh.
  - [x] Implement complete mail synchronization, local indexing, and connection recovery. Synchronization and local indexing use paginated scans and per-folder delta links; authentication recovery rejects mismatched settings and clears revoked credentials with a reconnect prompt.
- [ ] Configure ignored senders, ignored folders, and important senders.
- [ ] Configure recurring drink reminders.
- [ ] Configure automatic mail-scan intervals.
- [ ] Configure notification behavior where supported.
- [x] Configure startup behavior and background-service operation.
- [x] Store preferences persistently.
- [x] Validate settings before applying them.
- [x] Provide sensible defaults and a way to restore them.

## 12. Reliability, security, and diagnostics

- [x] Use structured application logs with timestamps, severity levels, and subsystem identifiers.
- [ ] Log synchronization failures and timer scheduling errors without exposing message bodies or secrets. Timer scheduling failures log only database error metadata; mail synchronization logging remains pending.
- [ ] Provide useful diagnostics for authentication, connectivity, and API failures. Device-code sign-in now shows Microsoft's sanitized error description and AADSTS code; broader diagnostics remain pending.
- [ ] Handle temporary network outages without crashing or losing state.
- [x] Use transactional or otherwise crash-safe persistence for important state changes. Multi-statement changes use transactions; individual inserts, updates, and deletes are atomic database statements.
- [x] Ensure service failures in one subsystem do not bring down the whole application. Timer, notes, and TODO initialization fail independently and show an unavailable tab; the shared database remains a required foundation.
- [ ] Validate external input, including email HTML, links, and imported data. Website TODO links require a valid HTTP(S) URL with a host and no embedded credentials; email HTML and imported data remain pending.
- [x] Avoid hardcoding credentials, tokens, or machine-specific paths. No credentials or tokens are embedded; user data uses Qt standard locations and the install prefix is configurable.
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
- [x] Clean build.
- [x] Installation works.
- [x] App starts in the background.

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
- [x] **Ignored folders:** rules apply only to the selected folder, not subfolders. Adding a rule queues the next sync to mark existing unread messages in that folder as read.
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
