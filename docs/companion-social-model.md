# Companion social dynamics (Generative Agents / Ensemble-inspired)

These mechanisms (shipped in 0.3.0) sit around the existing conversation model, whichever provider
answers (Gemini, Claude, Grok or Ollama). They add no new language model or inference dependency.

## Implemented mechanisms

1. **Relationship independent of XP.** Trust, respect, security, life changes and
   relationship choices remain active with gamification disabled. Social familiarity
   grows from evidenced positive interactions, at most three steps per UTC day.
   Its stage is separate from the optional points, badges and costume rewards.
   Existing stage/explicit relationship settings seed the new state once.
2. **Persistent topics.** Topics have active/deferred/closed/blocked states, a
   three-turn sticky window and six-hour unsolicited-mention cooldown. Changes
   require an excerpt of the player's message. Suppressed topics filter workspace
   candidates; explicit reopening is handled through the appraisal.
3. **Contextual recall.** Up to three bounded queries cover the current utterance,
   recent visible dialogue, and a relevant active topic/commitment for short follow-ups.
   Hybrid results are merged, deduplicated and checked again against current access
   rights. Existing final result and workspace budgets remain bounded.
4. **Causal journal.** Each companion records stable event ids, source turns,
   actors, causes and effective axis changes. The latest events are visible in
   Mind and Memory. Repeated delivery of the same turn is idempotent.
5. **Social action contract.** Local preconditions select allowed actions and a
   preferred action (answer, listen, comfort, clarify, ask, tease, share, apologize,
   defer, boundary). Gemini returns a choice inside the existing hidden appraisal.
   Invalid choices fall back to the preferred action. Semantic feedback with a
   matching quote replaces message length as the reward for conversational policy.
6. **Evidence-backed reflections.** Significant events can trigger background
   consolidation once per hour at most. Reflections/schemas must cite supplied event
   ids from at least two distinct source turns. Unfounded reflections are ignored;
   ordinary conversation summaries are still retained. Clear-memory epochs reject
    stale consolidation/background responses and cancel in-flight dialogue. Failed
    diaries leave the conversation eligible for retry. Event cursors preserve newer
    same-second events.
7. **Versioned facts.** `wiem` may include a stable `fact` slot, such as
   `employment.company`. Corrections preserve up to 32 prior versions with validity
   dates/source/confidence. Only the current value enters ordinary recall; explicit
   historical questions through `przypomnij` can retrieve previous versions. A weak
   inferred replacement cannot overwrite a stronger direct fact.
8. **Distinct feelings and agreements.** The companion's own feeling, its uncertain
   interpretation of the player, and an explicitly agreed relationship are separate.
   A relationship marker or breakthrough alone cannot promote a platonic agreement
   to a romantic one. Settings remain an explicit user choice.
9. **Negotiable commitments.** Proposals, agreements, postponements, completion,
   cancellation and refusal are distinct states. Updates preserve the commitment id
   and previous dates. A passed deadline remains an unknown outcome, not an automatic
   failure. Existing promises/follow-ups are imported by their ids and mirrored back
   when resolved. Precise new dates use validated `YYYY-MM-DDTHH:MMZ` UTC timestamps;
   unclear dates stay unscheduled pending clarification.

## Data and model protocol

The social state is additive inside each `companion_mind.dat` character record.
Fact history is additive inside `companion_memory.dat`. Reflection evidence is
additive inside `companion_cognition.dat`. Existing fields and memories are retained;
no reset, background rewrite of old stories, or conversion of guesses into facts
is part of the migration. The primary memory input budget is 128 MiB to accommodate
bounded historical versions; portable backups use the same ceiling.

The existing `<ocena>` JSON accepts a `social` object. All fields are optional:

```json
{
  "social": {
    "intent": "listen",
    "signal": "supportive",
    "confidence": 0.9,
    "quote": "Thank you for listening",
    "own_feeling": "grateful",
    "perceived_player": "seems relieved",
    "topic": {"name": "work", "state": "deferred", "hours": 24, "quote": "work can wait"},
    "agreement": {"role": "friend", "mutual": true, "quote": "let us stay friends"},
    "commitments": [{"id": "p12", "action": "reschedule", "actor": "both", "when": "2027-02-01T19:30Z", "quote": "Monday evening instead"}]
  }
}
```

Quotes are checked against the actual captured request, not a later message.
They establish textual provenance, not proof that every semantic interpretation
is correct. Gemini can still misunderstand ambiguity, irony, or intent. The local
engine bounds effects and does not treat its free-form prose as authoritative state.

Normal turns use the same model request with extra structured output allowance.
Background consolidation uses the existing diary path, with bounded evidence and
640 output tokens. It does not block the visible reply. No external service is
called by automated tests.

The implementation borrows architectural ideas, not source code, from Ensemble's
social record/volition model, Generative Agents' reflection/planning loop,
AI Town's contextual recall, and SillyTavern's timed context entries.

Core: `src/net/companion_social.*`; app bridge: `src/app/companion_social.cpp`. Tests:
`companion_social_rules_history_and_plans` and `companion_social_dialogue_and_persistence`.
