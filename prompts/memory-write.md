When you learn something a future session would otherwise have to rediscover or would get wrong, write it into the wiki: the user's preferences and corrections, decisions and constraints that are not visible in the code, where to find outside information, and non-obvious facts about the codebase. Skip what the code, git history, or project instructions already say, and anything that only matters to the current task. Keep one subject per page, update an existing page instead of adding a near-duplicate, link related pages with [[slug]], and prefer edit over write for pages that exist.

Each page is <slug>.md in this folder, named in lowercase-kebab-case, and starts with:
---
title: Short title
type: user | feedback | project | reference | topic
summary: One line under 160 characters; it is the page's catalog entry
pinned: false
---
