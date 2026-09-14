# CI/CD Redesign — Ship-Gate & De-Duplication (handoff spec)

Status: **implemented** on branch `feature/ci-ship-gate` — all file edits
below are applied, actionlint-clean (1.7.7, zero findings), and the gate's
bash is unit-tested against a mocked `gh` (ship / refuse-failure /
refuse-cancelled paths) via `tmp/gate_test/run_gate_test.py`. One deliberate
divergence in `ci.yml` (see its header): master-push/PR triggers were
removed — CI is on-demand (`workflow_dispatch`) + weekly schedule only, so
routine commit/sync cycles never burn runners; release correctness never
depended on ci.yml (the hub gate re-checks versions and the packagers cover
Windows/macOS on every tag). Motivated by the 1.5.20/1.5.21 releases
(2026-09-14), where the fleet behaved as follows, all observed live:

- **Partial ships.** v1.5.20: PyPI + GitHub Release shipped while Unity
  Package and C# NuGet died (macOS `-Werror` flag break). There is no gate
  between packagers — each publishes on its own success, so a red sibling
  still ships its half of the release.
- **Double waves.** Every release commit gets the full suite twice: once on
  the master push, once on the tag push (wheels/nuget/unity/CI ×2). The
  master-push copies can never publish (publish jobs are tag-only) — pure
  runner cost. The 1.5.21 evening burned ~8 concurrent macOS jobs.
- **Benchmarks on every PR** (~10 min ubuntu + build per PR), although a
  benchmark number only means something for a tree we intend to ship.
- **UPM churn.** `upm-branch.yml` fires on any `Unity Package` completion,
  including the never-publishing master-push runs.

## Target architecture (decided — do not re-litigate)

**Hub-and-spoke.** The three packagers (`build-and-publish.yml`,
`csharp-nuget.yml`, `unity-package.yml`) become build-test-archive ONLY —
they never publish. `release-publish.yml` (rename display name to
**"Release Ship"**) is the single publisher: one gate job first polls the
siblings' **tag runs** until every one of them concludes `success` (refuse
and fail the ship otherwise), then downloads their archived artifacts
across runs (`gh run download <run_id> --pattern …`) and publishes GitHub
Release assets, PyPI, NuGet (both feeds), and lets `upm-branch.yml` follow.

Why a hub instead of per-workflow gates: cross-workflow `needs:` does not
exist and poll-style gates placed inside each packager's publish job
deadlock (each run waits on the sibling *run* finishing, but the sibling's
run cannot finish while its own gate polls back). Hub = one gate, zero
cycles.

## Edits per file

### `benchmark.yml` — DONE
- `pull_request` trigger removed; tags narrowed to `'v*.[0-9]*.[0-9]*'`
  (excludes CI-created `*-upm` tags).
- Dead `benchmark-pr` job (`if: github.event_name == 'pull_request'`) deleted.

### `build-and-publish.yml` — DONE
- `on`: remove `push.branches` entirely (kills the master wave); tags →
  `'v*.[0-9]*.[0-9]*'`. Keep `pull_request` + `workflow_dispatch`.
- Add:
  ```yaml
  concurrency:
    group: pybcsv-wheels-${{ github.ref }}
    cancel-in-progress: ${{ github.event_name == 'pull_request' }}
  ```
- Remove job `publish-pypi` (moves to hub). Keep `publish-testpypi` +
  `smoke-test` untouched (TestPyPI trusted publisher stays keyed to this
  file name; adjust the `if:` — the `refs/heads/master` arm is dead once
  branch pushes go away, may stay harmlessly or be tidied).
- Everything else (version-consistency, sdist, wheels matrix, sdist-test,
  test-wheel) unchanged.

### `csharp-nuget.yml` — DONE
- `on`: remove `push.branches` + the tag `paths:` block (paths never filter
  tag pushes anyway — the filter only silently gated master pushes in the
  old shape and misleads readers). Tags → `'v*.[0-9]*.[0-9]*'` (the
  `*-upm` tags must not re-ignite the fleet at a synthetic commit). Keep
  `pull_request` (with its paths) + `workflow_dispatch`.
- Add `concurrency: csharp-nuget-${{ github.ref }}`, PR-only cancel.
- Remove jobs `publish-github` **and** `publish-nuget` → leaving a comment
  block explaining: publishing lives in the hub, artifact name is
  `nuget-package`.

### `unity-package.yml` — DONE
- Same trigger/concurrency surgery as csharp-nuget (group
  `unity-package-${{ github.ref }}`).
- Remove tag-gated publish-style job `publish-release` (softprops attach of
  the .tgz to the GitHub Release) — moves to the hub. Keep the pack job
  (artifact `unity-package`) + EditMode tests + native matrix.

### `release-publish.yml` → hub — DONE
- `on.push.tags` stays `'v*.*.*'`; keep the existing
  `!contains(github.ref_name, '-')` guard (blocks `-upm` tags).
- New `gate` job (ubuntu, `permissions: contents: read`): inline bash, no
  composite action (a `.github/actions` sketch existed and was deleted —
  its design self-deadlocked; inline sibling-only polling is the fix):
  ```bash
  # inputs: SIBLINGS="unity-package.yml csharp-nuget.yml build-and-publish.yml"
  # poll every 120 s; total cap 150 min; any completed run whose conclusion
  # != success -> exit 1 (refuse to ship); cap elapsed -> exit 1.
  # run selector (gh api, per workflow file):
  #   /repos/$GITHUB_REPOSITORY/actions/workflows/<file>/runs?head_sha=$GITHUB_SHA
  #   | .workflow_runs[] | select(.event=="push" and .head_branch=="$TAG")
  #   (master-push runs of the same sha share event+sha → head_branch==tag
  #    is what disambiguates; absent run = still queued = keep polling).
  # outputs each run id (unity_run_id, nuget_run_id, wheels_run_id).
  ```
- Existing `release` job (header-zip + softprops): `needs: gate`; download
  the `unity-package` artifact from `${{ needs.gate.outputs.unity_run_id }}`
  via `gh run download` and add it to the release `files:`.
- New `publish-pypi`: `needs: [gate, release]`, `environment: pypi`,
  `id-token: write`, `gh run download "$WHEELS_RUN" -p "built-*"` →
  `pypa/gh-action-pypi-publish` with `skip-existing: true` (keeps
  tag-move/retag tolerance).
- New `publish-nuget-gh` (`packages: write`, `GITHUB_TOKEN`,
  `--skip-duplicate`) and `publish-nuget-org` (`environment: nuget`,
  `secrets.NUGET_API_KEY`) from the `nuget-package` artifact of
  `${{ needs.gate.outputs.nuget_run_id }}`.
- Top-level `permissions: contents: write` stays; per-job scope the rest.

### `upm-branch.yml`
- `workflow_run.workflows: ["Unity Package"]` → `["Release Ship"]` (the new
  hub display name). Success of the hub now *implies* the whole fleet was
  green, so the upm branch only ever advances on a fully-shipped release —
  and never from master pushes (those runs are gone).

### `upm-branch.yml` — DONE
- `workflow_run.workflows: ["Unity Package"]` → `["Release Ship"]` (the new
  hub display name). Success of the hub now *implies* the whole fleet was
  green, so the upm branch only ever advances on a fully-shipped release —
  and never from master pushes (those runs are gone).
  Implementation note: the hub archives no artifacts itself, so the
  workflow resolves the Unity Package *tag run* for the hub's head sha via
  `gh api …?head_sha=…` (event==push selects the tag run), falling back to
  the latest successful run on manual dispatch.

### CI matrix (`ci.yml`) — CHANGED (on-demand)
- Master-push and `pull_request` triggers removed; CI now runs only on
  `workflow_dispatch` and the weekly schedule. This is the deliberate
  deviation from the original "no changes" decision: the user wants CI on
  demand, not per commit/sync. Windows/macOS coverage for a release is
  guaranteed independently — every packager tag run builds and tests those
  platforms, and the hub gate blocks a ship unless they are green.

## One-time user task (required before the NEXT tag is fully shipped)

PyPI Trusted Publishing is keyed to the workflow **file name**: add
`release-publish.yml` (repo `webertob/bcsv`, and the environment name used
by the pypi job if one is set) as a trusted publisher on
pypi.org → account → pybcsv → publishing. NuGet moves cost nothing
(API-key secret + `environment: nuget` are repo-scoped, follow the job).

## Verification plan

1. `tmp/bin/actionlint` 1.7.7 (already downloaded; gitignored dir) → zero
   findings across `.github/workflows/`; also
   `python3 -c "import yaml,glob;[yaml.safe_load(open(f)) for f in
   glob.glob('.github/workflows/*.yml')]"`.
2. Push branch, open PR → packager PR jobs must build/test but publish
   nothing (publishers are hub-only; hub jobs skip on non-tag events);
   `ci.yml` runs as before; **Benchmark must NOT run** on the PR anymore.
3. Merge to master → expect CI only (no packager wave).
4. First real tag (1.5.22+) = the true end-to-end test: hub gate blocks
   until siblings green, then all four channels publish together; a red
   sibling must leave every channel untouched (that is the acceptance
   criterion). **Do not fire test tags** — production PyPI accepts them.

## Known traps (learned the hard way)

- `multi_replace_string_in_file` items each need a `filePath`; a batch
  without it rejects the whole call (the csharp-nuget edits above are the
  casualties — reapply them individually).
- Workspace path is `/home/tobias/ws/bcsv` — an earlier typo created a
  stray dir once; triple-check paths in file tools.
- Tag-push runs and master-push runs of the same sha both report
  `event: push`; never gate on sha alone — also match `head_branch == tag`.
- `*-upm` tags: excluded ONLY by the narrowed globs `'v*.[0-9]*.[0-9]*'` /
  the hub ref-name guard. Do not "simplify" back to `'v*'`.
- The hub's `gate` must poll siblings only — never its own workflow, and
  never its own run.
- Keep `skip-existing`/`--skip-duplicate` everywhere: they make an
  emergency retag survivable (lesson of 1.5.20 → 1.5.21).
- Fish is the interactive shell but tool terminals are bash; `set -l` etc.
  are not portable — plain bash syntax only in workflow runs too.
- `scripts/check_versions.py --tag v…-upm` will fail on the synthetic upm
  commit by design; if a future edit ever widens tag triggers, that check
  is the last line of defence.

## Branch state when this was written

`feature/ci-ship-gate` (based on master `c68fbb2`, the fully-green 1.5.21
line). All spec edits are now applied on the branch: benchmark.yml trigger
surgery + `benchmark-pr` deletion, `build-and-publish.yml` /
`csharp-nuget.yml` / `unity-package.yml` build-test-archive conversion,
`release-publish.yml` hub rewrite (gate + release + publish-pypi +
publish-nuget-gh + publish-nuget-org), `upm-branch.yml` retargeted to
"Release Ship", and `ci.yml` converted to on-demand. actionlint 1.7.7
reports zero findings. Push, open PR, and let the first real tag (1.5.22+)
be the end-to-end test per the verification plan.
