---
name: debug
description: "Pick the smallest loop that can fail a change. Use when deciding how to check a change, for a live game picture, for a scenario compare, or when debugging a port bug."
---

# Debug

Pick the smallest loop that can fail the change.
The four loops are the inventory in `docs/developer/agent-debug.md`.
Walk that list from the top and stop at the first loop that can fail this change.

1. Name the failure this change can show.
   Done when you can point at the bad result.

2. Read `docs/developer/agent-debug.md` and choose the first loop that can show that failure.
   Done when one loop is named.

3. Run that loop as the inventory describes.
   Done when the result can fail this change.
   If it cannot, go back to step 2.
   Take the next loop.

4. If a live game is open, call `session_close`.
   Done when that game is closed.
