# Rose development plan

Updated 2026-09-29. Baseline: `Marcellas/Rose` `main` at
`088df85b36a44c9d53de49cd614849cb5b385431` plus the proposed offline
search change. This is a working order, not a claim that every item is already
implemented. Update the baseline after new user pushes.

## Product rules for these slices

- Local work remains usable without a network connection. Online operations say
  when the network or their configured provider is unavailable.
- A user request starts a bounded task. A read approval covers only its named
  file or directory scope for that task. Writes, sends, deletions, and external
  actions identify their targets before execution. A repeated substep of one
  approved action should not ask again.
- Rose names the actual file, source, screen capture time, or tool result behind
  a claim. If a tool fails or reaches a bound, she reports that instead of
  guessing where the answer might be.
- Keep the bored witch voice in presentation; permissions, errors, paths, and
  legal/source qualifications stay plain and exact.

## Current defect and verification queue

| Priority | Observation in current source or user report | Next proof or fix |
| --- | --- | --- |
| P0 | Explicit offline search previously skipped text inside unknown extensions and extensionless files; the user also saw Rose speculate instead of searching the supplied directory. | Proposed bounded content probe in this change; run the exact-path regression test, then try `Agent loop` under the real Windows folder and check that the answer cites returned paths or states no matches. |
| P0 | `Search online` is disabled in `SdlRoseContextMenu.cpp` and the corresponding action has no implementation. | Build a configured online search tool and grounded answer flow; test enabled, offline, unavailable-provider, and no-results states. |
| P1 | `stageScreenCapture()` captures one image and submits it. The user needs analysis of an ongoing task. Source contains a staged clipboard-image preview, but the user reports that the current app presents it like a dragged file. | Verify the preview in the built Windows app and image delivery to the model; then add an opt-in, bounded screen-watch task with timestamps and stop control. |
| P1 | Generated artifacts are appended to `ArtifactCardStack` below the conversation. Source draws and handles an `X`, but the user reports it is absent in the current app; `RichTranscript` exists but is not the live chat view. | Verify the built panel and hit target, then put artifacts inline with their originating turn, retaining close, open, and reveal actions after scrolling and reopening a discussion. |
| P1 | A previous Windows build/regression failure and confirmation-loop behavior were reported. Earlier source changes added a scoped read approval and a regression check, but the current Windows executable has not been verified here. | Build and run targeted Windows tests, then manually complete an approved multistep directory task with one prompt and an explicit out-of-scope denial. |
| Deferred | Avatar frame overdraw, cutoffs, red edges, and double-click spell transitions were reported. | Revisit after cleaned frame plates arrive; inspect the decoded alpha bounds and event transition rules independently. No asset edits in this batch. |

P0/P1 here describe what to tackle first, not a claim of user data loss. The
first row's code change has a portable regression test; user-visible Windows
behavior remains to be checked.

## Build and release gate

1. Record the source commit and clean build configuration. On Windows x64:
   `cmake -S . -B build`, then `cmake --build build --config Debug --target Rose`.
2. Run `ctest --test-dir build -C Debug --output-on-failure`, with particular
   attention to `RoseSearchAndApprovalTest`, `RoseAgentExecutionTest`, and
   `RoseToolSelectionAgentTest`. Fix build or test failures before adding a new
   feature layer.
3. Manual smoke pass on the built app: local model offline; exact-folder search;
   one scoped read confirmation; paste/capture an image; artifact open/reveal;
   quit/relaunch and reopen a discussion. Keep a short result and screenshot or
   log for each failure. Repeat focused checks after each slice.

## Ordered implementation slices

### 1. Exact offline search and grounded file work

The proposed change inspects up to 4 KiB to recognize likely plain text in
small files regardless of extension. It keeps the existing 5,000-entry,
eight-level, 1 MiB-per-file, 32 MiB read-budget, 60-match, and 24 KiB output
bounds. Binary file names remain searchable; UTF-16 and container formats need
format-aware readers. Do not silently search a similar path when the exact
directory is unavailable. In a follow-up, add explicit UTF-16 decoding,
visible truncation reason, and a user-invoked broader search only when wanted.

**Done when:** a query with a quoted directory uses that exact path, lists
matching file and line evidence from ordinary, unusual, and extensionless
text, and reports no matches, missing path, or bounds without speculation.

### 2. Chat attachments and artifact lifecycle

Verify the source's staged thumbnail, filename, and removal action for a
pending pasted or dropped image in the Windows build; fix the failing path.
Preserve the clipboard image through model analysis and surface OCR/vision
limitations in the reply. Integrate the existing rich
transcript renderer into the live chat, retaining streaming text, selection,
math glyphs, discussion restore, and scroll behavior. Put generated images and
other artifacts beside the producing turn with `X`, open, and reveal actions.

**Done when:** paste previews before sending; the model describes image
contents when a capable provider is configured; `X` dismisses only that
preview; left-click opens and right-click reveals the file; discussion reload
does not misplace an artifact.

### 3. Online search with traceable sources

Add a provider interface and explicit configuration for an available search
service. The menu enables only when configured; the same query can be issued
from chat. Keep query/result limits, URLs, titles, dates when supplied, and
short snippets. Use fetched or returned evidence for the answer and cite URLs.
On network, auth, quota, or provider failure, show the actual condition.

**Done when:** online search returns clickable attributable sources and a
grounded summary, and an offline run gives a clear failure without fabricated
results or changing the user's requested offline scope.

### 4. Coding and document work

Complete a task loop that inventories relevant files, proposes a small edit,
applies it after the appropriate approval, runs a selected build/test command,
and summarizes diffs and failures. Keep compiler output distinct from Rose's
inference. Extend document readers and planners by detected format/capability,
with honest unsupported-format responses; do not infer handling from a suffix.

**Done when:** Rose can inspect her own source, identify a concrete defect,
make an approved change, run the selected target, and report the changed paths,
test result, and remaining uncertainty without repeated prompts for one
approved read scope.

### 5. Legal research and drafting workspace

Let the user select jurisdiction, date, matter, and documents. Separate source
text, citations, assumptions, and possible courses of action. Provide a draft
with linked supporting passages and a checklist for missing facts or review.
Keep actual filing, sending, or other external action user-directed.

**Done when:** a legal analysis identifies its jurisdiction and date, ties
material claims to supplied or retrieved sources, flags gaps, and exports a
reviewable draft without presenting an unsupported claim as settled law.

### 6. Screen-watch tasks

After the one-shot image flow works, add an opt-in task that samples the selected
display/window at a bounded interval and count, timestamps each capture, notes
changes, and feeds a small relevant sequence to the vision-capable provider.
Expose a visible stop control and clear temporary captures at task end.

**Done when:** Rose can explain a changing UI across at least two captures,
refer to their times, stop on request, and state when capture or vision is
unavailable. A single screenshot is described as a snapshot.

### 7. Image generation and creative files

Make the local generation backend, prompt/options, progress, output paths, and
preview lifecycle explicit. Provider choice and capability should determine
which requests can run offline and which require an online service. Later add
format detection and adapters for Blender/reference-image workflows and
alpha/metadata driven 3D grid generation; prototype one grid before expanding
file-type claims.

**Done when:** a generated image appears in its chat turn, can be dismissed,
opened, and located, and generation errors name the actual backend constraint.

### 8. Outlook and phone screening

Build on the existing Outlook connection and message listing/search. Start
with a review queue and visible evidence for suspicious messages; make any
move, delete, or block action target-specific and confirmed. Separately test
which Windows phone integration actually exposes call events and controls for
the paired device before designing reject/block behavior. Add the zap animation
after the cleaned plates are available.

**Done when:** a suspicious item is presented with reasons and a reversible
review action; phone support is backed by a tested integration rather than a
Bluetooth pairing assumption.

## Decision points and limits

- Choose an online search provider and its credential/configuration route
  before slice 3 implementation.
- Choose a local vision model or configured vision provider for pasted images
  and screen-watch. A captured bitmap alone cannot establish ongoing context.
- Track performance on large OneDrive trees; bounds are intentional, and a
  `bounded/partial` result is not an exhaustive search.
- Resume avatar frame and animation-transition work after the cleaned assets
  arrive. It is explicitly outside the current nonvisual batch.
