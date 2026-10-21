# AGENTS.md

Upstream's agent notes are in [CLAUDE.md](/CLAUDE.md); follow them too. The
sections below are this fork's own.

# This fork

`master` is upstream's [racers-portable](https://github.com/isledecomp/racers-portable)
`master` plus our changes, one commit per change. Its main addition is an Xbox
(UWP) build, modelled on [isle-portable](https://github.com/isledecomp/isle-portable)'s,
which builds SDL3 from the [SDL3-uwp](https://github.com/Helloyunho/SDL3-uwp) fork.

# Commit scope

Keep each commit focused: implement one change or fix per commit, with the
documentation for that change (its commit message, and the README or other docs
that describe it). Split unrelated changes into separate commits, even when
they're made in the same session.

# Staying rebaseable

Our commits are rebased onto newer upstream releases, so keep them easy to
replay:

- Prefer new files and small, contained hooks over edits spread through upstream
  code. Don't reformat, rename or reorder upstream code you aren't changing.
- Gate platform-specific code so every platform keeps building: `WINDOWS_STORE`
  (the CMake variable, also defined for the compiler) for the UWP build. Windows
  desktop, Linux, macOS and web builds must build without it.
- A fix to a change that has its own commit can be made with
  `git commit --fixup=<commit>`, to be squashed into it at the next rebase.

# Commit attribution

Every commit created or amended with agent assistance must name the agents and
models that contributed in the commit message. Use the body or trailers; keep the
subject focused on the change. List each participating agent and its actual model,
and preserve existing attribution when amending or squashing a commit.

For example:

```text
Agent: Codex (Sol)
Model: gpt-6.1-sol
```

Preserve the human Git author identity. Do not invent model names or attribute
earlier work to the current agent.

# Delegating work

To reduce token usage, a more capable agent should hand routine work to
subagents rather than doing it itself, and keep the design decisions, reviews
and replies to the user. Give each subagent a self-contained brief: the
settled design, constraints, and the commit attribution to use.

- Menial tasks (mechanical edits, searches, log extraction, documentation
  updates): prefer Haiku 5.5, falling back to the latest GPT Luna model.
- Medium tasks (implementing a settled design, focused fixes, writing tests):
  prefer GPT-6.1-Sol, falling back to Sonnet 5.5.

Review a subagent's changes before committing or pushing them.
