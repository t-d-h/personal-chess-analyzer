# AGENTS.md

## Project Overview
- This is a chess analyer service.
- More detail, read docs/ARCHITECTURE.md

## Start Commands
- Install dependencies: `make setup`
- Start dev server: `make dev`
- Run tests: `make test`
- Full verification: `make check`
 
## Project Structure
- src/ — Code.
- docs/  — Documents.
- deploy/ — Example k8s manifest to deploy this service.
- tests/ — Tests.

## Working modes
You will run as one of these modes, you decide, based on what user say to you. Each mode have a different rules so before start doing thing, tell user what mode you are going to do.

### Plan mode
- Use when user propose an idea or design a new feature/improvement for this service.
- Feature list file: docs/feature_list.json
- When user want to add new feature, generate new element in feature list file, propose and design to user and wait for approve. If user approve, save the design as markdown in docs/design_files/ and stop, dont implement it in the current session.
- Only one feature active at a time.
- Verification command must pass before marking as completed.
- When create a new feature, use this template:
```
{
    "id": "F01",
    "behavior": "POST /api/auth/register with user credentials returns 201, and POST /api/auth/login returns JWT token",
    "verification": "pytest src/tests/test_auth.py",
    "design_file": "F01.md",
    "state": "todo",
    "evidence": ""
}
```
- The state is one of: todo, working, blocked, completed. 
- The feature implementation only done when all tests passed, and "state" and "evidence" (output of check command) in feature list file have to be updated.
- Update related documents when the feature is completed.
- This mode can finish only when the new feature is added to docs/feature_list.json file, and the design file is created.

### Implement/Code mode
User will choose a feature in docs/future_list.json and you will start coding using designs in docs/design_files/

#### Work Rules
- Work on one feature at a time
- If you got any error when running any `make` command, stop implementing new rule, instead fix all the error first.
- Only start the next feature after the current one passes end-to-end verification
- Don't "also refactor" feature B while implementing feature A
- When 1 feature is finished, update relates documents in docs/
- Write a test for each new feature.
- When start implementing a feature, strongly follow the design of it in docs/design_files/. If the design file not exists, prospose a new design and wait for user to approve. Just read the current implementing feature design file, no need to read other feature design file.

#### Definition Of Done
A feature is done only when all of the following are true:
- The target behavior is implemented.
- The required verification actually ran.
- Evidence is recorded in feature_list.json or docs/PROGRESS.md.
- The repository remains restartable from the standard startup path.

#### At session start (clock in)
1. Confirm the working directory with `pwd`
2. Run `make setup` and `make dev` for standard startup and verification path. 
3. Read docs/PROGRESS.md for current state
4. Read docs/SESSION-HANDOFF.md to see what changed in the previous session.
5. Review recent commits with git log --oneline -5
6. Read docs/decisions/ for important decisions
7. Run `make check` to confirm repo is in consistent state. 
8. Continue from docs/PROGRESS.md "Next Steps" section

#### Before session end (clock out)
1. Update docs/PROGRESS.md
2. Run `make check` to confirm consistent state
3. Commit all completed work
4. Update features_list.json.
5. Record any unresolved risk or blocker.
6. Update docs/SESSION-HANDOFF.md
7. Leave the repo clean enough for the next session to run `make setup` and `make dev` immediately.
8. Run `make stop` to stop dev server.
9. Commit with a descriptive message once the work is in a safe state. 

#### Hard Constraints
- All PRs must pass pytest + mypy --strict + ruff check
- If you made some important decisions, save it in docs/decisions/ as markdown

### Debug mode
You can read docs/ and src/ to debug the service like what user say. If you find bugs on code, propose and wait for user to approve. You can create test code but remove them later when you are done. Don't change anything on code or docs without permission.
