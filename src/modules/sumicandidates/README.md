# Sumi candidate bridge

This optional addon transfers only candidate presentation to one explicitly
registered editor. GTK continues to own key handling, inline preedit, surrounding
text and committed text. No global theme, input method, dictionary or capability
flag is changed.

## Protocol

Service `org.fcitx.Fcitx5`, object `/org/fcitx/Fcitx5/SumiCandidates`, interface
`org.fcitx.Fcitx.SumiCandidates1`:

- `SetActive(s token, as programs, b active) -> b`: lease a candidate panel. The
  exact program names identify the application; wildcards and empty names are
  rejected. True means the lease was accepted, possibly waiting for a matching
  focused input context. Once bound, the lease never transfers to another input
  context. Repeat calls with the same token/owner/programs renew idempotently.
- Unicast `Candidates(s token, t serial, a(iss) rows, i cursor, b hasPrev,
  b hasNext)`: each row contains the real candidate-list index, filtered label,
  and filtered text with comment. Placeholders are omitted; indices are not
  renumbered. `cursor` uses the same real index space. An empty list hides the
  panel. Serials increase for every frame and consumed action.
- Unicast `Revoked(s token)`: the lease has ended, including an asynchronously
  failed bind. The client must clear its accepted state and candidate panel.
  This follows the final empty `Candidates` frame. Retry only when the editor
  remains focused and composition has cleared.
- `Select(s token, t serial, i index)` and `Page(s token, t serial, b next)`:
  actions are accepted only from the registered DBus connection for the current
  token, serial, focused input context and current callback. Invalid/stale
  requests are no-ops.

The client must activate only while its **editor widget** is focused and
explicitly deactivate on focus loss, disconnect and disposal. An application-wide
focus flag is insufficient: other note.md input fields share the same program.
Registration should follow the GTK editor's FocusIn processing, so the intended
input context is current. The protocol does not identify a GTK widget itself.

Focus loss, input-context destruction, replacement by another custom input-panel
callback, and registration-owner disconnect revoke the lease. The next focused
context requires a new registration. Callback attachment is checked on both UI
update and UI flush because engines reset their panels between keystrokes.
Existing custom input-panel/virtual-keyboard callbacks are never overwritten.

Late registration while Classic UI is already showing a panel uses the fork's
`ClassicUI::hideInputPanel` addon function. With stock Classic UI lacking this
function it returns false and preserves the existing panel; clients must retain
normal IME behavior. Non-Classic user interfaces are unsupported and rejected.
A headless instance without a UI is supported for integration testing.

GTK may own an existing client-side popup (`ClientSideInputPanel`). Late
registration while that panel is nonempty is always rejected, even with patched
Classic UI. This preserves the ongoing composition without duplicating its popup.
The client may retry after composition is cleared while the editor remains focused.

## Build and validation

From repository root, normal Fcitx CMake builds include this addon when DBus is
enabled. For a standalone addon build against installed Fcitx 5.1.23:

```sh
cmake -S src/modules/sumicandidates -B build-sumicandidates
cmake --build build-sumicandidates
```

The following test compiles with the system C++ compiler and starts the installed
Fcitx/pinyin engine under a new private session bus and temporary XDG directories.
It neither installs files nor restarts the desktop input method. Test artifacts
and the Fcitx log remain in the printed temporary directory.

```sh
src/modules/sumicandidates/tests/run-isolated.sh
```

It checks actual pinyin candidates and commits, ownership and serial checks,
callback reattachment after reset, paging, focus isolation between two contexts
of the same application, context destruction, owner disconnect, and preservation/retry of an existing GTK
client-side popup. It does not
provide visual acceptance evidence for the Classic UI hide function or the Sumi
renderer; those require a separate isolated display/session UI test.
