---
memory: bc582f90b5ea337f565a96df162d1077
title: "A refused write is a result, not an exception"
origin: written
tags: ["domain=pygim","component=memory","artifact=api","task=design","kind=principle","layer=adapter","concern=api_design"]
---
Every operation returns a plain dict. A refusal comes back with 'refused' and 'facts' naming what stands in the way: the unread memories, the current head, the missing hard question. The agent reads the facts and retries. Exceptions are reserved for a repository that will not load at all.
