# Chat-turn workflow

Use `personal_productivity_service_todo.md` as the project roadmap.

- At the start of each chat turn, identify the next incomplete, dependency-ready TODO item and state the single reviewable step you will work on.
- Work on that step only. Keep its changes focused and do not begin the next TODO item or milestone in the same turn unless the user explicitly asks for multiple steps.
- When the step is complete, summarize what changed, point to the relevant files, and report any verification performed. Then stop and wait for the user's review or direction before continuing.
- If a TODO item needs a decision or prerequisite, explain the specific issue and ask the user; continue only with independent work that stays within the current step.
- Treat user corrections and requests as the latest direction. A request to revise the current step does not authorize moving on to another step.

# Visible work descriptors

- Register user-visible asynchronous or long-running work with `WorkDescriptorService` so it appears in the Activity tab and the main window status bar.
- Create a root task for an operation and pass its ID to `startTask` for recursive subtasks. Update the detail as phases change.
- Report a percentage only when there is a defensible estimate. Use `std::nullopt` for indeterminate work; the UI will show an indeterminate progress bar.
- Complete or fail every task on all exit paths. Do not include credentials, tokens, or private message content in task titles or details.
- When adding background work, include its descriptor wiring and user-visible progress/status in the same roadmap step.
