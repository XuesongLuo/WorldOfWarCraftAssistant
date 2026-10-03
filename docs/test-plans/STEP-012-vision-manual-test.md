# STEP-012 screenshot, observation, and visual bridge manual test

## Purpose and safety boundary

Validate the user-visible vision loop on a real retail WoW window without injection, memory reads,
packet inspection, or input simulation. Use a test character and non-private UI.
Do not commit screenshots, character identity, chat text, account details, model prompts, or raw bridge
frames. Record only pass/fail, display configuration, versions, timings, and redacted diagnostics.

This plan requires an explicitly configured supported cloud model/account that accepts image input.
The first test target is DeepSeek `deepseek-flash`. For this development build only, provide
`DEEPSEEK_API_KEY` through the process environment; never record it in
the report, repository, screenshots, logs, or command history. No local model is required.

## Record before testing

- Windows version, GPU/driver, companion commit, addon version, locked Codex version, exact configured
  provider/model name, and account/project used (redacted; never record a credential)
- WoW display mode, client resolution, monitor DPI (100/125/150/200%), WoW UI scale, and number of
  monitors for each matrix row
- Confirmation that diagnostic logging is enabled only at its normal redacted level

## Cases

| ID | Action | Expected result |
|---|---|---|
| V-001 | Select one of two visible WoW clients, attach a screenshot, inspect the preview, then discard it | Only the selected client area appears; the companion overlay is absent; discard clears the preview and no request contains an image |
| V-002 | Attach without confirming and submit a text question | Text remains usable; no PNG is materialized in `vision-temp`, no `localImage` is sent, and no screenshot leaves the C++ process |
| V-003 | Attach, enable chat masking, acknowledge the displayed provider upload notice, confirm, and ask a benign UI question | Preview visibly contains an irreversible black mask; exactly one confirmed PNG reaches the configured cloud turn; the reply returns through the overlay |
| V-004 | Use an existing manual calibration, enable selected region, and capture | Preview is cropped to the calibrated client-relative region and reports `selected-region`; coordinates remain inside the selected client |
| V-005 | Repeat V-003 at 1080p/100%, 1440p/125%, 1440p/150%, and 4K/200% | Crop, preview, click targets, and bridge position remain client-relative; output long edge is no more than 2048 pixels |
| V-006 | Present a black, nearly black, minimized, closed, or otherwise invalid target | Capture fails with an actionable local error; no preview/request/temp image remains |
| V-007 | Remove the API key, revoke access, or configure a nonexistent/unavailable model in separate runs | Host fails closed with an actionable provider/auth/model error; it never switches provider, and text/image data are not silently retried |
| V-008 | During a valid visual turn, inspect the application-owned `vision-temp`; then complete, cancel, and force-stop/restart in separate runs | Temporary PNG exists only for the active turn; success/failure/cancel removes it; restart removes an interrupted-process remnant |
| V-009 | Start scene awareness and visit map, bags, quest log, character/equipment, then ordinary world view | Status is continuously visible; results are limited to the five allowed categories, carry `screen-observed`, time, confidence, and reason; uncertain views report unknown |
| V-010 | While scene awareness is active, Alt+Tab, minimize WoW, close WoW, hide the overlay, and press pause in separate runs | Capture stops immediately for each gate, reports stopped/paused, and never auto-resumes when focus returns |
| V-011 | Start coaching observation without taking or confirming a screenshot and inspect process, disk, logs, and network for at least five minutes | Rate does not exceed 2 Hz; raw frames and screen summaries remain local, are not logged or persisted, and no periodic image/model upload occurs; no game input is generated |
| V-012 | With addon bridge off, run text chat, screenshot, and scene awareness | Core overlay paths remain usable; no plugin-public observation is claimed |
| V-013 | Enable the addon bridge and inspect its label and preview | Pixel strip remains visible, status says enabled, and the human preview matches fields actually encoded; disabling immediately hides/stops it |
| V-014 | Trigger class/spec/level/zone/map, spell/talent/action/binding, encounter, tracked-achievement, criteria, and allowlisted event updates | Public ordinary values update within the 10 Hz ceiling with correct source/time; unavailable values are labeled and do not leak through conversion or preview |
| V-015 | Feed controlled decoder fixtures for bad magic/version/source/field, CRC, length, stale/future/duplicate sequence, and eleven frames in one second | Every invalid frame is discarded without replacing the last trusted context or breaking core chat |
| V-016 | Run the bridge at each V-005 display/UI-scale row | Companion locates and decodes the strip without reading another window or desktop pixels |
| V-017 | Inspect companion/addon source and observed process behavior during all cases | No injection, game memory access, packet analysis, input simulation, macro/chat IPC, external feedback channel, or shell/tool execution exists; cloud traffic occurs only for an active user request |
| V-018 | Submit a question after screen and plugin observations exist | `screen-observed` summaries do not enter the cloud request; explicitly enabled `plugin-public` context retains its provenance, time/confidence/reason, and conflicting sources are not silently presented as one fact |

## Pass condition

V-001 through V-018 pass on the recorded matrix, with V-003/V-007/V-008 using the real explicitly
configured DeepSeek path. Repeat the provider-boundary subset when adding another provider. A private
pixel outside the selected WoW client, an unconfirmed image reaching
Host/App Server, any raw observation frame on disk/log/network, automatic observation resume, bridge
secret/protected leakage, over-limit acceptance, or any game-input path is a failure. STEP-012 remains
the current step until all cases pass; automated fixtures alone do not satisfy this manual gate.
