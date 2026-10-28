# OpenCode Operating Protocols

> Loaded for this repository on top of its `AGENTS.md` and `CONTRIBUTING.md`. Those documents stay
> authoritative; this file only covers OpenCode-specific operation.

## Model

- The only permitted model is **Big Pickle** (`opencode/big-pickle`).
- `opencode.jsonc` pins `model` and blocks every other provider via `policies`;
  `.opencode/plugins/big-pickle-only.ts` removes every other model from the catalog.
- Do not add providers, models, or model overrides to this repository.

## Research-first mandate (all work)

- Guesswork and trial-and-error edits are strictly banned across **all work** (every domain,
  language, tool, script, configuration, asset, or documentation task — never limited to specific
  fields).
- Always research official primary documentation online (`search_web`) and empirically inspect the
  live state before editing any file.

## Context discipline

- Prefer targeted searches (`grep`/`glob`) over full-file dumps once a file has been summarised.
- Never repeat a command whose result is already in context.
- Batch independent tool calls instead of serialising them.
- Record durable findings and next steps in this repository's `ROADMAP.md`, `PLAN.md`, `TODO.md`,
  `BUGS.md` and `HANDOFF.md` rather than restating them in conversation.
- Close a phase with a short checkpoint (state, decisions, next move), not a long narrative.
- Filter commands that can emit unbounded output.

## Tool hygiene

- Edit files with dedicated edit/write tools, never through the shell; preserve LF line endings.
- Pass `workdir` instead of changing directories; quote Windows paths containing spaces.
- Subagents start with fresh context: pass every needed fact explicitly and request a compact
  result.

## Git

- Read-only git commands (`status`, `log`, `diff`, `show`, `ls-files`, `remote -v`) are fine.
- Never run a repository-mutating git command (`add`, `commit`, `push`, `pull`, `merge`, `rebase`,
  `reset`, `stash`, `checkout`, `switch`, `clean`, `tag`, `branch`, `mv`, `rm`, `restore`,
  `cherry-pick`, `revert`) unless the user explicitly authorises that exact operation in the
  current conversation.

## Verification

- Zero errors and zero warnings are mandatory: MSVC `/W4 /WX` and a clean `svelte-check` where
  applicable.
- Build: `pwsh -NoProfile -File scripts/build.ps1 -Configuration Release`.
- Test: `pwsh -NoProfile -File scripts/test.ps1 -Configuration Release`.
- Plugin sources under `.opencode/plugins/` type-check with `tsc -p .opencode/tsconfig.json`
  (zero diagnostics); `types/opencode-plugin.d.ts` is the local shim for the runtime-provided
  `@opencode/plugin` module — keep it in sync with the documented plugin API.
- Never claim a check passed without running it; never silence a warning to make a gate pass.

## Scope

- Touch only what the task names. Do not reformat, refactor, or "clean up" unrelated code.
- Follow the repository's own conventions over personal preference.
