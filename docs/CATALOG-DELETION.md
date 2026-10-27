# Deleting saved sessions and folders

Connections → **… → Select sessions / folders…** enables checkboxes. Tick entries
and use **Delete…**. **Visible** (Ctrl+A while the selection area has focus) adds
the currently listed sessions, including results below the scroll viewport, but
not collapsed or filtered-out sessions. Search-result ancestor folders are not
automatically selected. Selection stays explicit when the filter changes.

- A session's context menu → **Delete…** removes only that saved entry.
- A folder's context menu → **Delete folder and contents…** removes its entire
  subtree, including hidden or filtered-out descendants. Prefix siblings are safe.
- Connections → **… → Delete all saved sessions / folders…** clears the entire
  saved catalog, independently of the current filter.
- Delete on a focused row opens confirmation; in selection mode it reviews the
  selection. Escape leaves selection mode. F2 editing and normal drag/drop remain
  available outside selection mode.

The confirmation gives exact unique session/folder counts and focuses Cancel.
Selections use stable IDs, not row offsets. A catalog revision change or a file
edited by another window rejects the operation and requires a fresh review.
The catalog is saved atomically once before any removal is published to the UI.
Folder macro assignments are removed in one separate save; macro groups survive.

Deleting saved entries does **not** close live terminals, send SSH commands, delete
remote/local explorer files, delete SSH keys/known-host trust, remove connection
routes or erase logs. No automatic backup or in-app undo is created. Export a
catalog beforehand only if keeping a copy is appropriate for your data policy.
This is ordinary file/configuration deletion, not forensic secure erasure.

Imported inventories and connection logs are private runtime data outside the
source tree. Avoid saving real organization exports inside a public repository.
Clearing the application catalog does not remove the source import, manually
exported copies, chat attachments/history, historical logs or backups elsewhere.
