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
- [x] Use Microsoft Graph 1.0 with Microsoft identity platform device-code sign-in and delegated User.Read, Mail.ReadWrite, and Mail.Send scopes for the signed-in mailbox and saved ID translation.
- [x] Store authentication tokens securely using the operating system's available credential storage. Outlook access and refresh tokens are stored through Linux Secret Service using `secret-tool`, not in the application database.
- [x] Provide connection, disconnection, and authentication recovery flows. Outlook setup uses account-type choices with a custom-tenant option and client-ID validation; device sign-in opens the browser, copies the code, and closes its dialog after credentials are stored. Saved connection state is restored asynchronously, configuration mismatches are rejected before token refresh, and revoked credentials are cleared with a reconnect prompt.
- [x] Retrieve mailbox messages and maintain a local index of previously discovered mail. The Outlook settings action indexes the latest mailbox page (up to 100) into SQLite, updating existing message metadata without storing bodies and retaining each message's parent folder ID.
- [x] On first startup with a saved Outlook connection, perform a paginated initial mailbox scan and persist its completion state; Graph's opaque next-page URL is preserved, failed initial scans retry automatically on the configured polling interval, and a slow/locked credential store is checked again automatically after giving the unlock prompt time to complete.
- [x] Provide a manual **Scan all mail** action to perform a paginated full mailbox re-index, updating indexed message metadata and folder IDs.
- [x] For subsequent scans, retrieve only new or changed mail without repeatedly scanning the entire inbox. Background checks page newest mail first and stop once a whole page is already indexed, then per-folder Microsoft Graph delta synchronization reconciles changes and deletions; **Sync mail changes** can run delta sync manually. Folder delta links are saved locally.
- [x] Persist initial mailbox scan completion state so application restarts do not trigger unnecessary full scans.
- [x] Handle pagination, API throttling, connectivity failures, and expired credentials. Graph retries transient transport errors and throttling/service-unavailable responses with bounded backoff and `Retry-After`; HTTP 401 forces one token refresh, then asks the user to reconnect if rejected again.
- [x] Prefer change notifications or delta queries where supported, with polling as a fallback. Per-folder delta sync runs after the initial scan and on Outlook reconnect; while connected, a configurable polling timer (1 minute by default) probes newest mail pages and then starts delta sync when idle. The timer stops when Outlook is disconnected.
- [x] Keep message identifiers stable so TODOs and notes can link to the original email. Graph mail requests use `Prefer: IdType="ImmutableId"`; saved legacy IDs are translated in batches before delta sync, merging duplicates safely.

## 4. Email notifications, filtering, and mail views

- [x] Notify the desktop when new mail arrives. Incremental synchronization sends a desktop alert for newly indexed messages, with batch notifications summarized to avoid flooding.
- [x] Avoid repeatedly notifying for messages already processed. Notifications are emitted only for message IDs inserted into the persistent local index; initial and manual full scans stay silent.
- [x] Allow an individual email to be marked as:
  - [x] Ignored. The message viewer's Ignore toggle also adds/removes the sender in ignored-sender settings; changes queue reconciliation for the next scheduled scan.
  - [x] Important. These local flags persist in the mail index and can be changed in the message viewer.
- [x] Maintain configurable ignored-sender rules. Outlook settings and the message viewer can add/remove sender rules; `*` matches any sequence anywhere in the address, including both sides of `@` (for example `*@domain.com` and `*.marketing@domain.com`). An optional sender display-name or email username pattern can narrow a rule; an empty name matches all names at the address/domain. Values are stored in persistent settings and deduplicated case-insensitively. Rule changes queue indexed folders for reconciliation during the next scheduled scan.
- [x] Maintain configurable ignored-folder rules. Outlook settings can load the connected mailbox's folder hierarchy, add/remove selected folders, and persist each exact folder ID with its display path. Rules apply only to the selected folder; changes queue reconciliation for the next scheduled scan, using the active delta folder ID when a message omits its parentFolderId.
- [x] Automatically mark messages from ignored senders as read in Microsoft 365/Outlook itself. Delta sync patches matching unread messages through Graph, updates the local index only after Microsoft confirms success, and suppresses new-mail alerts for those ignored senders.
- [x] Automatically mark messages in ignored folders as read in the remote mail service. Delta sync marks unread messages in explicitly selected folders as read through Graph, falls back to the active delta folder ID if Graph omits parentFolderId, and updates the local index after Microsoft confirms success. Existing messages are reconciled on the next scheduled scan.
- [x] Apply rules to incoming mail and reconcile rules against existing messages when appropriate. Delta synchronization applies current rules to incoming/changed messages; adding a sender or folder rule persists a rescan request for the next scheduled system scan, and rescan requests remain until a successful folder scan completes.
- [x] Handle failures to update remote read status and retry safely. Graph retries transient failures; if a read-state update still fails, sync stops before saving the folder delta cursor so the batch is replayed next time.
- [x] Provide an **Unread mail** view:
  - [x] Include unread messages across all accessible folders, not just the inbox. The view reads all unread rows from the local index, which is populated by mailbox scans and per-folder delta synchronization.
  - [x] Sort newest first.
  - [x] Show a genuinely empty list when there are no unread messages.
  - [x] Reflect read-status changes made in Outlook or by the application. Delta sync updates the shared index; **Refresh from Outlook** runs a Graph delta sync and both mail lists rebuild after sync, while the unread view also refreshes every five seconds.
  - [x] Mark all unread messages as read from the Unread view. Messages are updated sequentially through Microsoft Graph, local read state changes only after remote success, and a WorkDescriptor reports progress and failures.
  - [x] Mark an individual unread message as read from its message viewer; update the local index only after Microsoft Graph confirms success.
- [x] Show a startup overview notification with open TODOs due today, unread mail, and unread mail flagged important. It is sent after the first successful startup delta refresh, uses simple wording, includes up to four TODO titles, and requests an 8-second display timeout.
- [x] Provide an **All mail** view for browsing existing read and unread messages. The All mail tab displays all locally indexed messages, newest first, and shares manual refresh and Graph synchronization controls.
- [x] Support refreshing, searching, opening, and navigating between messages. Both mail lists' **Refresh from Outlook** action runs a Graph delta sync and rebuilds the views; sender/subject search is local, and double-click opens a message dialog with Newer/Older navigation through the current filtered results.
- [x] Show the remaining time until the next automatic mail scan in Outlook settings; display an active-scan status while a scan is running and a waiting status while Outlook is disconnected.
- [x] Provide sender, subject, received date, folder, and read/important status where available. Mail rows show sender, subject, and received time; message details show read state, importance, and the Graph parent-folder ID.

## 5. Email message viewer and HTML safety

- [x] Implement an in-app message viewer. Opening a message from either mail list fetches it from Microsoft Graph and shows its headers and plain-text body/preview in a non-blocking dialog; HTML rendering remains a separate item below.
- [x] Render HTML email bodies where possible, with a plain-text fallback. The in-app viewer uses Qt rich text for HTML bodies, falls back to Graph's plain-text preview when the body is unavailable, and blocks links and all resource loads.
- [x] Preserve reasonable email formatting, links, lists, tables, and inline images. Qt rich text preserves supported HTML structure; inline CID images are resolved only from bounded, allowlisted image attachments, while links remain inert in this rendering step.
- [x] Treat email content as untrusted input. The in-app renderer uses Qt rich text, keeps links inert, blocks resource loading by default, and only permits validated, bounded inline CID image attachments.
- [x] Prevent email HTML from executing arbitrary JavaScript. Messages render through Qt's QTextBrowser rich-text engine, which does not provide a JavaScript runtime; the application does not load email into a web engine.
- [x] Disable access to local files and arbitrary local resources. The viewer overrides resource loading and only serves validated inline CID images already fetched from the current message; local paths and all other schemes are rejected.
- [x] Prevent remote content from loading automatically unless explicitly allowed. Remote URLs are rejected by the viewer's resource loader; no automatic remote-content setting is enabled.
- [x] Sanitize unsafe HTML and restrict navigation to permitted URL schemes. libxml2 parses fragments with network access disabled; active-content elements and unapproved attributes are removed, inline CSS is allowlisted, and links retain only HTTP, HTTPS, or mailto schemes. Link activation remains disabled in the viewer.
- [x] Prevent email content from accessing application privileges or local credentials. Email is rendered in an isolated Qt rich-text widget without a web engine, JavaScript bridge, application callbacks, or credential APIs; local resource access is blocked.
- [x] Provide a way to open legitimate links externally. Clicking a sanitized HTTP(S) or mailto link opens it through the desktop's default handler after a second scheme/host validation; automatic navigation remains disabled.
- [x] Ask before opening links from email, allow exact domains to be trusted, and manage saved trust in Settings. Each untrusted HTTP(S) domain prompts with Open once, Always trust this domain, or Cancel; exact host rules persist and can be removed from Application settings. mailto links prompt for each open.
- [x] Clearly handle blocked images, unsupported content, and rendering failures. The viewer explains blocked remote/local images and unsupported formats, identifies unsupported or unavailable body formats, falls back to plain text when sanitization/rendering cannot proceed, and reports malformed Graph message responses.

## 6. TODO manager

- [x] Create, edit, complete, reopen, and delete TODO items.
- [x] Give each item a title, description, status, creation time, and optional deadline.
- [x] Support linking TODOs to:
  - [x] Local files.
  - [x] Websites.
  - [x] Emails stored in the mail service. TODOs can select a locally indexed message, persist its stable message ID, and open it through the in-app All mail viewer.
  - [x] Persistent notes in the note service.
  - [x] Quick notes in the note service.
- [x] Allow links to be opened from the TODO item.
- [x] Preserve links using stable identifiers where possible, rather than fragile display names.
- [x] Display upcoming, overdue, completed, and unscheduled tasks.
- [x] Allow manual creation and editing of deadlines.
- [x] Support automatic TODO creation for important email senders. Newly indexed delta-sync messages matching configured sender rules create linked follow-up TODOs; initial/full mailbox scans stay silent.
  - [x] Configure important sender rules in Outlook settings with add/remove controls and persistent storage; `*` can appear anywhere in the address, with an optional sender display-name or username pattern.
- [x] Use the system's local timezone. Receipt times are converted to local time before applying the deadline schedule.
- [x] Persist the computed deadline so subsequent scans do not recalculate it. The deadline is saved in the TODO record as UTC.
- [x] Define a deterministic policy for mail received after midnight and for messages imported long after their original arrival. Deadlines use the message's original local receipt date; a late-discovered delta message can therefore create an already-overdue TODO. Initial and full scans do not create these TODOs.
- [x] Prevent duplicate TODO creation when the same email is encountered during synchronization. Before generating a follow-up, the service checks the persistent TODO store for the stable email link; repeated messages in the same sync batch are skipped too.
- [x] Route all deadline reminders through the shared timer and notification service. Manual and generated TODOs create a stable timer event for their deadline; edits reschedule it, completion or deletion cancels it, and startup reconciles open TODO deadlines before the shared deadline monitor runs.

### Important-sender deadline rules

When an email from an important sender arrives, create a TODO with a deadline determined by the local time of receipt.

| Email received | TODO deadline |
|---|---|
| Before 16:00 | Today at 17:00 |
| From 16:00 until before 21:00 | Today at 22:00 |
| At or after 21:00 | Tomorrow at 17:00 |

- [x] Use the system's local timezone.
- [x] Persist the computed deadline so subsequent scans do not recalculate it.
- [x] Define a deterministic policy for mail received after midnight and for messages imported long after their original arrival.
- [x] Ensure all generated TODOs use the same reminder rules as manually created TODOs. Both paths save through TodoService, which schedules the same deadline notification policy.

## 7. Unified timer and notification engine

This is the central piece of the application. Email notifications, TODO deadlines, countdown timers, and drink reminders should use shared infrastructure while retaining their own scheduling policies.

- [x] Implement a centralized timer service.
- [x] Represent scheduled events with stable IDs, deadlines, status, and notification policy.
- [x] Use absolute timestamps for deadlines and a monotonic clock for elapsed-time measurements where appropriate. UTC deadlines remain the persisted recovery value; manual countdowns also store a monotonic deadline and boot ID so elapsed time is unaffected by wall-clock changes during the same system boot. Calendar-based TODO deadlines continue to use UTC wall time.
- [x] Persist timers and scheduled TODO deadlines across application restarts. Countdown timers and TODO deadline fields persist; startup reconciles TODO records into stable timer events.
- [x] Support cancellation, rescheduling, pausing where applicable, and completion.
- [x] Handle sleep, suspend/resume, clock changes, and missed notifications. Manual countdown elapsed time uses Linux CLOCK_BOOTTIME where available, including suspend duration; the deadline monitor catches overdue events on its next wake, compares calendar deadlines against UTC after clock changes, and retries failed delivery.
- [x] Prevent duplicate reminders after restarts or synchronization retries.
- [x] Provide a unified overview of active timers and upcoming deadlines.
- [x] Route desktop notifications through one notification coordinator.
- [x] Update an existing countdown notification when supported, rather than creating a new notification every second. The notification coordinator accepts a stable replacement key and passes the previous freedesktop notification ID to subsequent Notify calls; deadline notifications use stable event keys.
- [x] Fall back gracefully when the desktop notification service does not support notification updates. If a replacement call fails, retry as a new notification; if that also fails, use the system tray notification when available.

### Deadline reminder schedule

- [x] For deadlines on the current local calendar day, remind 10 seconds before the 18, 12, 6, and 3 hour remaining milestones. If the app wakes after several milestones, deliver only the nearest missed one and record earlier missed milestones as skipped.
- [x] For same-day deadlines under 6 hours away, add a 3-hour remaining milestone; hourly reminders are not used.
- [x] Within the final hour, notify at 30 minutes, 15 minutes, and 5 minutes remaining. Each milestone notification starts 10 seconds before the milestone and missed milestones collapse to the nearest one.
- [x] Display the final 10 seconds as a countdown. The notification text updates once per second and replaces the previous desktop notification when supported.
- [x] Show the final 10-second countdown as timely notifications rather than waiting until the deadline has passed. The countdown begins with 10 seconds remaining and the deadline notification updates it when time expires.
- [x] Display a reminder before each applicable milestone and keep it visible for about 20 seconds when the notification system allows. Deadline notifications request a 20-second timeout from both desktop and tray notification paths.
- [x] Update the countdown text to reflect the remaining time during the final 10 seconds.
- [x] At the deadline, apply a defined completion or overdue policy without silently deleting the item. An open TODO remains open and appears overdue until the user completes it; after its deadline notification is delivered, its associated timer event is completed and disappears from the active timer overview.
- [ ] For deadlines after today in the local calendar, notify 1 month, 1 week, and 1 day before; schedule each notice 10 seconds before its milestone, keep it visible for about 20 seconds where supported, and do not schedule year-level reminders.
- [ ] Show a TODO deadline progress bar based on its creation time.

**Timing clarification for implementation:** schedule each milestone notification 10 seconds before the milestone it describes, then keep it visible for about 20 seconds when supported so the milestone passes while the notification is shown. For deadlines after today in the local calendar, use 1 month, 1 week, and 1 day before the deadline; do not schedule year-level reminders, and do not remind earlier than 1 month before. A progress bar based on TODO creation time is desirable to show how the deadline is approaching. The engine should calculate exact notification start times rather than simply polling once per minute.

Recurring timers are different:

- [x] Drink reminders and mail-scan intervals have configurable recurrence settings.
- [x] Recurring timers do not emit countdown-progress notifications; they notify only when their scheduled occurrence is due.
- [x] Recurring timers still appear in the timer overview with their next scheduled time.
- [x] Recurring events must not accidentally inherit TODO deadline reminders; the deadline monitor handles recurring events through a separate due-only path.

## 8. Drink reminder system

- [x] Implement a configurable recurring drink reminder, disabled by default with a 60-minute interval.
- [x] Allow the user to set the interval between reminders from 1 minute to 24 hours.
- [x] Allow the reminder system to be enabled or disabled in Settings.
- [x] Show desktop notifications when a drink reminder becomes due through the shared notification coordinator.
- [x] Do not play a sound for now.
- [x] Structure the implementation so a user-supplied sound file can be added later. Drink reminders use the shared NotificationCoordinator, providing one future extension point for optional sound playback; the current implementation remains silent.
- [x] Keep the next scheduled drink reminder visible in the timer overview.
- [x] Define how the next reminder is calculated when the app starts, resumes from sleep, or misses a scheduled reminder. On app startup, show one reminder immediately and restart the interval from that point; after missed occurrences while running, deliver once and schedule the next occurrence one full interval after delivery to avoid bursts.
- [x] Offer an optional automatic annual Christmas countdown in the timer overview, with a festive notification on December 25 and calendar-based scheduling that stays on Christmas across leap years.

## 9. Notes system

- [x] Implement two note workflows:
  - [x] **Quick notes:** capture information rapidly and discard it without ceremony.
  - [x] **Persistent notes:** store information until explicitly deleted.
- [x] Make quick-note creation accessible without opening a complicated editor.
- [x] Open a dedicated quick-note capture window with a configurable desktop-wide keyboard shortcut; use KDE's KGlobalAccel service on Plasma and the XDG Global Shortcuts portal on desktops that provide it. Shortcut configuration is available from Settings.
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
- [x] Route approaching-deadline reminders through the shared timer engine. Countdown and TODO deadline milestones are monitored from persisted TimerService events and delivered by the NotificationCoordinator.
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
- [x] Configure ignored senders, ignored folders, and important senders.
- [x] Configure recurring drink reminders.
- [x] Configure automatic mail-scan intervals from 1 minute to 24 hours; the default is 1 minute and applies while Outlook is connected after the initial scan.
- [x] Configure notification behavior where supported. Settings provides a persistent desktop-notification enable/disable switch; notifications are enabled by default and the shared NotificationCoordinator honors the preference for mail, deadlines, and recurring reminders.
- [x] Configure startup behavior and background-service operation.
- [x] Store preferences persistently.
- [x] Validate settings before applying them.
- [x] Provide sensible defaults and a way to restore them.

## 12. Reliability, security, and diagnostics

- [x] Provide a shared, visible work-descriptor system with recursive subtasks, calculated or indeterminate progress, and task outcomes. The Activity tab and main status bar show Microsoft Graph work, mailbox scans and sync trees, Outlook authentication, notification delivery, timer/TODO/note changes, and user-triggered settings changes. Future operations follow `WORK_DESCRIPTORS.md` and `AGENTS.md`.
- [x] Use structured application logs with timestamps, severity levels, and subsystem identifiers.
- [x] Log synchronization failures and timer scheduling errors without exposing message bodies or secrets. Mail sync logs phase and aggregate counts without message IDs, error payloads, or message contents; timer scheduling logs database error metadata.
- [x] Provide useful diagnostics for authentication, connectivity, and API failures. Device-code sign-in shows Microsoft's sanitized error description and AADSTS code; Graph transport failures offer connection guidance, and HTTP failures in request activity, mailbox scans, folder loading, and delta sync include status-specific guidance without exposing response bodies.
- [x] Handle temporary network outages without crashing or losing state. Graph transport failures retry with backoff; throttling and transient server errors honor retry timing. Mail delta cursors advance only after a page is indexed and its queued ignore-rule updates succeed, so failed work can be retried without losing the saved sync position. Background checks continue with incremental sync after recent-page failures, and future scheduled checks retry sync.
- [x] Use transactional or otherwise crash-safe persistence for important state changes. Multi-statement changes use transactions; individual inserts, updates, and deletes are atomic database statements.
- [x] Ensure service failures in one subsystem do not bring down the whole application. Timer, notes, and TODO initialization fail independently and show an unavailable tab; the shared database remains a required foundation.
- [ ] Validate external input, including email HTML, links, and imported data. Website TODO links require a valid HTTP(S) URL with a host and no embedded credentials, and email HTML is sanitized before display; validation for imported data remains pending.
- [x] Avoid hardcoding credentials, tokens, or machine-specific paths. No credentials or tokens are embedded; user data uses Qt standard locations and the install prefix is configurable.
- [ ] Provide automated tests for deadline calculation, timer scheduling, mail deduplication, and persistence. Qt Tests cover TODO deadline scheduling with stable-ID updates and mail deduplication on repeated message IDs; deadline arithmetic, restart recovery, and broader persistence coverage remain pending.
- [ ] Test application restart, system suspend/resume, and missed-notification recovery.
- [x] Keep the UI responsive while background operations run. Microsoft Graph requests use asynchronous QNetworkAccessManager callbacks; mailbox and delta scans handle one page of at most 100 messages per callback, returning to the event loop between pages, and long-running work reports through WorkDescriptorService.
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
- [ ] Correct reminder times. Same-day and final-hour milestones are implemented; later-calendar 1 month, 1 week, and 1 day reminders remain pending.
- [x] Restart recovery. Active timer events persist and are recovered; open TODO deadlines are reconciled into stable timer events at startup.
- [x] No duplicate notifications. TimerService persists per-event notification keys and the deadline monitor records milestones before delivery, with retry handling when delivery fails.

### Milestone 3 — Outlook integration

**Scope:** Authentication, initial inbox scan, incremental synchronization, manual full scan, and unread/all-mail views.

Acceptance checks:
- [x] OAuth works. Device-code authentication, Secret Service token storage, refresh, reconnect handling, and Graph requests are implemented.
- [x] Incremental scan works. Newest-page probing and per-folder Graph delta synchronization are implemented.
- [x] Unread view spans folders. The view reads unread messages from the complete local index populated by root and nested folder scans.

### Milestone 4 — Mail processing

**Scope:** Ignored and important rules, remote read-state updates, HTML viewer, and email-linked TODOs.

Acceptance checks:
- [x] Remote read state changes. Ignore rules update unread messages through Microsoft Graph and commit local state only after success.
- [x] No duplicate TODOs. Email-linked TODOs are checked by stable message ID before creation, including within a sync batch.
- [x] Unsafe HTML is restricted. Email markup is sanitized, active elements are removed, remote resources are blocked, and links are revalidated before external opening.

### Milestone 5 — TODOs and notes

**Scope:** CRUD workflows, deadlines, external links, quick notes, and persistent formatted notes.

Acceptance checks:
- [x] Links survive restart. TODO link type, stable target, and label are stored in SQLite.
- [x] Notes persist. Persistent and quick notes are stored in SQLite and loaded from the service after startup.
- [ ] Deadlines schedule correctly. TODO deadlines persist and route through stable timer events; later-calendar milestone reminders remain pending.

### Milestone 6 — Recurring utilities

**Scope:** Configurable drink reminders, mail polling intervals, and manual countdown timers.

Acceptance checks:
- [x] Multiple timers work. TimerService stores independent events with stable IDs and the timer overview can display and control them.
- [x] Recurring events behave correctly. Drink reminders use a separate due-only recurrence path and reschedule after delivery or recovery.
- [x] Timers survive restart. Active events are persisted and recovered from SQLite, with countdown monotonic state tied to the boot ID.

### Milestone 7 — Hardening and release

**Scope:** Failure recovery, security review, installation upgrades, uninstall, tests, and documentation.

Acceptance checks:
- [x] Network recovery. Graph requests retry transient transport failures, throttling, and service errors with bounded backoff; sync cursors remain recoverable.
- [x] Clean upgrade. The setup script supports reinstall/upgrade while keeping user data in Qt's per-user data locations.
- [ ] No user-data loss. Persistence changes use transactions or atomic statements, but the restart/upgrade recovery scenario has not been exercised end to end.

## 15. Decisions to settle before handing it to Codex

A few details have a significant impact on the implementation. Establish these up front rather than letting Codex invent behavior.

- [x] **Mail synchronization:** use Microsoft Graph delta queries to track changes incrementally, with a configurable polling interval. Consider Graph change notifications later if their setup is worthwhile. Implemented with per-folder delta links and configurable polling; Graph change notifications remain a possible future enhancement.
- [x] **Folder coverage:** include all folders Microsoft Graph exposes through the connected mailbox, including recursively listed child folders; unread mail is built from the local index across those folders.
- [x] **Ignored folders:** rules apply only to the selected folder, not subfolders. Adding a rule queues the next scheduled scan to mark existing unread messages in that folder as read.
- [x] **Important emails:** explicit ignore rules take precedence when a message also matches an important-sender rule; ignored messages are excluded from new-mail notifications and TODO generation.
- [x] **Late imports:** important messages discovered later use their original local receipt date and can create an already-overdue TODO; initial/full scans remain silent.
- [x] **Notification persistence:** desktop notifications are transient; the app's timer overview remains authoritative for active deadlines and recurring events.
- [x] **Email HTML:** rendering uses Qt's non-WebEngine rich-text view and sanitizes markup before display. Active elements and unsafe attributes are removed, remote resources are blocked, and only validated inline images and HTTP(S)/mailto links are retained; external links are revalidated before opening.
- [x] **Timer semantics:** after resume, deliver the nearest applicable missed milestone or the due notification; completed TODOs cancel their timer event and pending reminders.

### Important-sender deadline boundaries

Interpret the deadline rules as:

- Before 16:00 → today at 17:00.
- From 16:00 until before 21:00 → today at 22:00.
- At or after 21:00 → tomorrow at 17:00.

This resolves the exact 16:00 and 21:00 cases deterministically.

## Final recommendation

Make the scheduler and notification coordinator milestone two, not something added at the end. They are the backbone of this whole project, and building them early will make every subsequent feature easier to integrate.
