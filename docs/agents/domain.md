# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Before exploring, read these

- **`GLOSSARY.md`** at the repo root, or
- **`GLOSSARY-MAP.md`** at the repo root if it exists: it points at one `GLOSSARY.md` per context. Read each one relevant to the topic.
- **`docs/adr/`**: read ADRs that touch the area you're about to work in. In multi-context repos, also check `src/<context>/docs/adr/` for context-scoped decisions.

If any of these files don't exist, **proceed silently**. Don't flag their absence; don't suggest creating them upfront. The `/domain-modeling` skill (reached via `/grill-with-docs` and `/improve-codebase-architecture`) creates them lazily when terms or decisions actually get resolved.

## File structure

This repo is **single-context**: one `GLOSSARY.md` at the root and one `docs/adr/` for the whole monorepo (`sim/ detector/ gateway/ hmi/`).

```
/
├── GLOSSARY.md
├── docs/adr/
│   ├── ADR-001-영상-전송-경로-분리.md
│   └── ADR-002-바이트순서-big-endian.md
├── sim/ detector/ gateway/ hmi/
```

ADR naming in this repo: `ADR-NNN-<한글-제목>.md`, with the header table (상태, 날짜, 관련) and sections 배경 / 선택지 / 결정 / 결과 / 면접 대비 한 줄. New ADRs follow this format, not the `0001-...` default.

For reference, the multi-context layout (not used here):

Multi-context repo (presence of `GLOSSARY-MAP.md` at the root):

```
/
├── GLOSSARY-MAP.md
├── docs/adr/                          ← system-wide decisions
└── src/
    ├── ordering/
    │   ├── GLOSSARY.md
    │   └── docs/adr/                  ← context-specific decisions
    └── billing/
        ├── GLOSSARY.md
        └── docs/adr/
```

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `GLOSSARY.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders), but worth reopening because…_
