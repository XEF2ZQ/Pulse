# Release lines

| Branch | Starting point | Policy |
|---|---|---|
| `standard` | Existing project history plus the tested 1.4.3.2 cadence baseline | Conservative everyday line. Experimental scheduling is not automatically merged here. |
| `experimental` | Preserved 1.4.4 platform investigation, followed by 1.5.0 experimental scheduling | Opt-in investigation and acceptance-gated features. Continues independently of standard. |
| `main` | Existing published history | Left intact during the split; not silently repointed to experimental. |

The branch names are permanent development lines, not a promise to release every
intermediate version. A fix can be reviewed and cherry-picked into either line;
feature promotion requires its documented acceptance checks. Use branch-qualified
links when reviewing features. The experimental line does not redefine the
standard line's source-control or weak-charger behavior.

The initial split reconstructs the existing local sources in Git. It is not a
claim that rebuilding those commits reproduces previously installed binaries
bit for bit. Preserve the original installations and preferences when switching.

Both lines use the existing **all-rights-reserved** license. A public repository
does not grant a general reuse license.
