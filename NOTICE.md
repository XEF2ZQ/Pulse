# Rights and provenance

Pulse is a project by **Peter Kosanyi**, developed with AI-assisted implementation, research, documentation and testing. The repository documents the delivered behavior and its verification limits; it does not represent a claim that every line was written without assistance.

The root [rights notice](LICENSE) reserves rights in the project materials. Publication does not itself establish the protectability or authorship of every element. Contributions and third-party materials retain their own rights.

## Dependencies and attribution

- The application uses Windows system APIs and Microsoft C/C++ runtime libraries. Microsoft retains rights in its SDK, toolchain and runtime. Those products are not relicensed by this repository. The `/MT` build links the runtime statically; distribution of resulting binaries is subject to applicable Microsoft redistribution terms.
- No third-party source library is vendored in this source snapshot. G Helper is a separate application and is not bundled. The reference implementations linked in [RESEARCH.md](RESEARCH.md) are research sources, not included dependencies.
- The Pulse icon is a project-specific geometric design. Its vector asset and raster-generation script are included. Regenerating raster icons requires Pillow, which is a development-only tool and is not shipped in the application.
- GitHub Actions used by the CI workflow are independently maintained projects under their own terms.
- Windows, Microsoft, ASUS, AMD, Intel and other product names identify compatibility or test environments. No affiliation or endorsement is implied.
