This is a classic challenge with large codebases, and your instinct to partition conversations by area is already one of the best approaches. Here's a practical framework:

## 1. Create a "Code Map" Document (Most Important)

Write a single reference document (1–2 pages) that describes:

- The top-level module/folder structure and what each does
- Key architectural patterns (e.g., MVC, event-driven, layered)
- The main data models and their relationships
- Entry points and critical execution paths
- Naming conventions and abbreviations (especially important for a domain-specific app like OSCAR)

Paste this at the start of every conversation. It costs ~1–2K tokens but orients Claude immediately.

------

## 2. Partition by Layer, Not Just Feature

You're already doing topic-based partitioning. Make sure the partitions align with *architectural layers* too, since cross-cutting concerns bleed context fast:

- **Data layer** (schema, models, DAOs) — one conversation family
- **Business logic / services** — another
- **UI / controllers** — another
- **Integration points** (APIs, external systems) — another

------

## 3. Use "Focused File Dumps"

Instead of pasting entire files, paste only the *relevant slices*:

- Class/function signatures with docstrings (strip bodies)
- The specific method you're working on, plus its direct callers/callees
- Interface definitions rather than implementations

A tool like `grep`, `ctags`, or a script that extracts signatures can produce a 200-line "skeleton" of a 2,000-line file.

------

## 4. Build Persistent Context Files Per Module

For each major module, maintain a short `.md` file (think of it as Claude's cheat sheet) containing:

- Purpose of the module
- Public API / key functions
- Known quirks, tech debt, or gotchas
- Recent changes

Keep these in your repo. When starting a conversation about that module, paste the relevant cheat sheet(s) plus only the code you're actively changing.

------

## 5. Use Claude for Summarization (Meta-technique)

Have Claude *help you build* the knowledge base:

- Paste a file → ask Claude to write the cheat sheet for it
- Ask Claude to generate a dependency summary
- Ask Claude to identify what a reviewer would need to know about this module

Then save those outputs and reuse them in future conversations.

------

## 6. Conversation Templates

Start each conversation with a fixed preamble like:

> *"We are working on OSCAR, a [brief description]. Architecture overview: [paste map]. Today's scope: [module X]. Relevant context: [paste cheat sheet]. Here is the specific code: [paste targeted snippet]."*

This discipline alone dramatically reduces wasted context.

------

## The Core Principle

Think of Claude's context window as **working memory** — you want to load it like a surgeon's tray, not a storage room. You're the one who holds the full map; Claude only needs the local terrain for the current task.

Your current approach of separate conversations per area is sound. The main upgrade is to invest in those **persistent cheat-sheet `.md` files** — they pay compound dividends across every future conversation.