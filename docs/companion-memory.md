# Companion memory

Companions keep a bounded local archive, with a smaller selection supplied to the
language model for each reply. Increasing archive capacity does not increase the
per-reply memory budget or train the language model's weights.

## Capacity

| Archive | Capacity |
| --- | --- |
| Player notes | 5,000 total, with companion ownership/knowledge |
| Conversation summaries | 5,000 total, at most 1,000 per companion |
| Saved conversations | 1,000 per companion |
| Suggested games | 1,000 total |
| Reflections | 1,200 total, with companion ownership |
| Behavioral schemas | 400 total, with companion ownership |
| Concept associations | 4,000 total |
| Learned concepts | 256 per companion |
| Key relationship memories | 256 per companion |
| Shared phrases and jokes | 128 per companion |
| Named people, places and projects | 128 per companion |

Short-lived state keeps its smaller limits: 30 recent remarks and 120 hidden thoughts
in total, and 80 named emotional moments per companion. Notes are shortened to about 200
characters, summaries to 300. Complete chat archives occupy additional disk space.

The primary note/summary file is encrypted with Windows DPAPI. Its read/parse
ceiling is 32 MiB, allowing the expanded archive to survive a restart. This is an
input ceiling, not preallocated space. Older saves remain readable; missing
confirmation counts default to one. Portable backups preserve the new metadata.

## Weighted retention and recall

Notes and summaries use a deterministic retention score:

- **Importance:** a foundational memory gets a strong bonus.
- **Stable context:** a known location gets a smaller bonus.
- **Evidence:** direct statements/observations and higher confidence outweigh
  guesses.
- **Confirmation:** repeated direct evidence on a later turn strengthens a note,
  with diminishing returns and a count capped at 64. Repeating a guess or simply
  retrieving a memory does not add confirmations. A corrected fact starts fresh.
- **Recency:** a bounded bonus fades with age; age alone cannot overpower a key memory.

At capacity, the weakest entries are removed. Ties favor newer evidence and then
later entries. Surviving entries retain their original order. This also applies
when an oversized primary archive is loaded. Even important memories have a finite
capacity; the former arbitrary 16-entry protection limit is gone.

Other stores retain their own policies: conversation files and the expanded
concept/joke/relationship lists retain their newest entries; concept associations
retain the strongest age-adjusted links.

Each reply's primary memory section includes:

- At most 30 notes: up to 8 high-priority facts, with the remaining slots filled by
  the most recently confirmed notes known to that companion.
- The latest 3 relevant-owner summaries.
- Up to 8 additional matching notes and 4 matching summaries for the current query.

Keyword relevance remains primary; the retention score adds only a bounded ranking
bonus. An unrelated memory cannot become a keyword match merely because it is
important. The explicit recall tool uses the same scoring, and associative recall
also factors in memory importance. Owner filtering happens before selection.

## Semantic retrieval: a possible next step

Current retrieval uses words/stems plus a two-hop association graph, not embeddings.
A semantic index could match paraphrases such as “an exhausting day” and “feeling
worn out” without shared keywords.

For this scale, a local embedding index is sufficient; a separate database server
is optional. A future implementation should combine keyword and vector results,
then rank by relevance, importance, evidence quality and recency. It needs an
explicit embedding model/provider, versioned vectors, invalidation after edits or
deletion, and the same companion ownership filters. Retrieval must keep the current
bounded prompt budget. No embedding model or vector database is installed by this change.
