import { Plugin } from "@opencode/plugin"

// Context-budget guard.
//
// The window is managed primarily by `compaction` and `tool_output` in `opencode.jsonc`. This
// plugin adds an escalating operating hint to the system prompt once a request's assembled history
// grows large, so the model starts economising *before* an automatic compaction wipes detail.
// It only ever appends to `event.system`; it never rewrites messages or tool payloads, so it cannot
// corrupt persisted history or a provider request.
const WARN_CHARS = 240_000 // roughly 60k tokens of serialized history
const HARD_CHARS = 480_000 // roughly 120k tokens

type SystemBlock = { type: "text"; text: string }
type HookEvent = { messages?: unknown; system: SystemBlock[] }

function serializedLength(value: unknown): number {
  try {
    return JSON.stringify(value)?.length ?? 0
  } catch {
    return 0
  }
}

// Keeps the notice idempotent: this plugin can be discovered from more than one
// `.opencode/plugins/` directory (a repository nested in a larger workspace), and the hint must
// appear once per request, not once per discovery.
const MARKER = "Context budget is "

function advise(event: HookEvent): void {
  if (!Array.isArray(event.system)) return
  if (event.system.some((block) => block.type === "text" && block.text.startsWith(MARKER))) return

  const chars = serializedLength(event.messages)
  if (chars < WARN_CHARS) return

  const level = chars >= HARD_CHARS ? "critical" : "elevated"
  const tokens = Math.round(chars / 4)
  event.system.push({
    type: "text",
    text:
      `Context budget is ${level} (~${tokens} tokens of history). ` +
      "Work in small, verified steps. Do not re-read files or re-run commands whose results are " +
      "already summarised above; prefer targeted searches over full-file dumps; record durable " +
      "findings and decisions in the repository's tracking documents instead of restating them. " +
      "Prefer a short handoff/checkpoint over a long narrative when a phase completes.",
  })
}

export default Plugin.define({
  id: "helix.context-budget",
  async setup(ctx) {
    const registrations = await Promise.all([
      ctx.session.hook("context", advise),
      ctx.session.hook("compaction", advise),
    ])

    return () => {
      for (const registration of registrations) {
        void registration.dispose()
      }
    }
  },
})
