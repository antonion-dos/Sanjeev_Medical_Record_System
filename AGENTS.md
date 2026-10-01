# Agent Operating Rules for Sanjeev

## Mandatory Domain Isolation
- Agents MUST only modify files in the domain designated by the user:
  - If working on **Blockchain**, ONLY edit inside `blockchain/`. DO NOT touch `application/`.
  - If working on **Application**, ONLY edit inside `application/`. DO NOT touch `blockchain/`.

## Mandatory Branch & PR Workflow
- NEVER push directly to `main`.
- ALWAYS create and work within a temporary feature branch (e.g. `feature/<name>`, `agent/<task>`).
- Submit changes via a Pull Request (PR).
