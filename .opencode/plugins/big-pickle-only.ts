import { Plugin } from "@opencode/plugin"

// Enforces a single-model policy for this repository.
//
// The declarative half of this rule lives in `opencode.jsonc` (`model` + `policies`). This plugin
// closes the remaining gap: even inside the allowed `opencode` provider, every model except
// Big Pickle is removed from the catalog, and the default selection is pinned to it. Provider and
// model transforms are replayed whenever the registry is rebuilt, so later provider refreshes
// cannot reintroduce another model.
const ALLOWED_PROVIDER = "opencode"
const ALLOWED_MODEL = "big-pickle"

export default Plugin.define({
  id: "helix.big-pickle-only",
  async setup(ctx) {
    await ctx.provider.transform((editor) => {
      for (const record of editor.list()) {
        if (record.provider.id !== ALLOWED_PROVIDER) {
          editor.remove(record.provider.id)
        }
      }
    })

    await ctx.model.transform((editor) => {
      for (const model of editor.list()) {
        if (model.providerID !== ALLOWED_PROVIDER || model.id !== ALLOWED_MODEL) {
          editor.remove(model.providerID, model.id)
        }
      }

      const target = editor.get(ALLOWED_PROVIDER, ALLOWED_MODEL)
      if (target !== undefined) {
        target.enabled = true
        editor.default.set(ALLOWED_PROVIDER, ALLOWED_MODEL)
      }
    })
  },
})
