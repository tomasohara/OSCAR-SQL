## Using code-review-graph

The graph is already built for your repo (2840 nodes, 421 files, last updated today). The highest-value ways to use it:

##### Before editing — understand scope

  - Ask: "Use code-review-graph to show the impact radius of ProfileBackup::exportPrivacyTable" → I'll call
    get_impact_radius_tool and you see callers/callees/related symbols without me grepping.
  - Ask: "Give me minimal context for editing profile_restore.cpp" → get_minimal_context_tool returns just the symbols
    and files I actually need to read, saving tokens vs. me exploring blind.

##### During a review

  - "Review the staged changes with code-review-graph" → detect_changes_tool + get_review_context_tool surfaces what changed, what it touches, and which flows break. Good complement to /review.
  - "What flows does Machine::Load participate in?" → get_affected_flows_tool traces execution paths.

##### Understanding the codebase

  - "Show the architecture overview" → get_architecture_overview_tool (high-level modules/communities).
  - "What are the hub nodes in SleepLib?" → get_hub_nodes_tool finds the most-connected symbols — useful for orienting before a refactor.
  - "Find large functions" → find_large_functions_tool (refactor candidates).

##### Search

  - Semantic search: "find code related to session date handling" → semantic_search_nodes_tool beats grep when you don't know the exact names.
  - Graph traversal: "trace calls from Daily::on_LoadSessionsFromDatabase" → traverse_graph_tool.

##### Keeping it fresh

  - Your PostToolUse hook already runs update --skip-flows after Edit/Write/Bash, so the graph stays current as you work. No manual upkeep needed.
  - Run full postprocess (run_postprocess_tool) occasionally if you want flows/communities re-computed.

**Practical pattern: when you give me a bug or task, add "use code-review-graph first" — I'll ask it for impact radius +  minimal context before opening files. That's the token-savings payoff this tool is built for.**



------



  **/review is GitHub-PR-focused and not useful for your GitLab setup:**

  1. It takes an argument like /review [PR] — a GitHub PR number or URL. Without a PR, it has nothing to review.
    2. Under the hood it uses gh (GitHub CLI) to fetch the PR and its diff. GitLab isn't supported — the whole managed Code Review feature is a GitHub-only cloud service requiring the Claude GitHub App.
    3. For your workflow, stick with ad-hoc "review my last N commits" or "review this branch vs master" — I'll run git log / git diff and give you a review in chat. That's effectively what /review would do anyway, minus the PR plumbing.
    4. If you want a heavier review, there's also /ultrareview (also mentioned in your system prompt) — a cloud multi-agent review that, unlike /review, does NOT require a GitHub remote for the no-arg form (it bundles your local branch). That one would work for your GitLab project. It's user-billed, though.

  Source: https://code.claude.com/docs/en/commands.md, https://code.claude.com/docs/en/code-review.md.