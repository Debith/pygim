---
memory: 5f2daeb106534990d37b26a9a4e5462d
title: "Re-run the four checks under the commit lock"
origin: written
tags: ["domain=pygim","component=memory","artifact=store","task=implement","kind=procedure","layer=strategy","concern=concurrency"]
---
Closed vocabulary, no unread write, head only, identical content: all four are checked before the lock to give a fast refusal, and then again after taking the commit lock, because another process may have appended in between. Only the second run decides. Append durably (fsync) while holding it.
