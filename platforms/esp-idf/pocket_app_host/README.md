# pocket_app_host

Multi-app host for PocketJS on ESP-IDF. One FreeRTOS task owns the runtime and
runs **one guest at a time**: the embedded home package or an installed
`.pocket` package compiled for the same profile.

- **Install:** at task start every `*.pocket` in `inbox_dir` is checked
  (format version 2, FNV-1a64 footer, a variant with the program's profile
  hash and binding generation, an identity section) and moved to
  `apps_dir/<output>.pocket`. A package with the same output is replaced. A
  rejected file is renamed `<file>.rejected`.
- **Catalog:** `*.pocket` files in `apps_dir` with a matching variant, sorted by
  title. The home output is reserved; a second package with the same output is
  rejected.
- **Switch:** a request raised during a tick takes effect after the tick
  returns. The host destroys the runtime, then the QuickJS guest, then frees
  the package, and boots the next package on a new runtime. Display, renderer
  and input drivers persist across sessions.
- **Failure:** an installed app that fails to boot or returns a tick error is
  recorded as `lastError` and the host returns to home. Home failing three
  times in a row stops the task.

## Guest operations

The host registers three operations from the PocketJS OP table on every
runtime; `@pocketjs/framework/launcher` wraps them.

| OP | Name | Result |
| --- | --- | --- |
| 39 | `appTable` | JSON: `apps[{output,id,title,bytes}]`, `current`, `resume`, `launcher`, `storage` (`ready`/`missing`), `lastError`, `installErrors[{file,message}]` |
| 40 | `appLaunch(output)` | 1 when `output` is installed or is home; the switch follows the current tick |
| 41 | `appShot` | -1: no frozen frame is captured |

## Input policy

The host registers a proxy input source around the program's source:

- After every switch all contacts are withheld until a poll reports none, so
  the tap that launched an app does not reach it.
- While an installed app runs, a contact that starts within `home_edge`
  logical pixels of the bottom edge is withheld; when it travels `home_travel`
  pixels upward the host returns to home with `resume` set to that app.

## Memory

Installed packages are read into PSRAM and stay there until the guest is
destroyed. Per session the runtime heap is bounded by `runtime_heap_limit` and
the QuickJS heap by `guest_heap_limit`. The task stack must exceed
`guest_stack_limit`, since runtime operations called from JavaScript run on
the same stack.
