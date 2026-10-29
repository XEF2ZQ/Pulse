# Rights and provenance

Pulse is a project by **Peter Kosanyi**, developed with AI-assisted implementation, research, documentation and testing. The repository documents the delivered behavior and its verification limits; it does not represent a claim that every line was written without assistance.

The root [rights notice](LICENSE) reserves rights in the project materials. Publication does not itself establish the protectability or authorship of every element. Contributions and third-party materials retain their own rights.

## Dependencies and attribution

- The application uses Windows system APIs and Microsoft C/C++ runtime libraries. Microsoft retains rights in its SDK, toolchain and runtime. Those products are not relicensed by this repository. The `/MT` build links the runtime statically; distribution of resulting binaries is subject to applicable Microsoft redistribution terms.
- No third-party source library is vendored in this source snapshot. G Helper is a separate application and is not bundled. The reference implementations linked in [RESEARCH.md](RESEARCH.md) are research sources, not included dependencies.
- The Pulse icon is a project-specific geometric design. Its vector asset and raster-generation script are included. Regenerating raster icons requires Pillow, which is a development-only tool and is not shipped in the application.
- GitHub Actions used by the CI workflow are independently maintained projects under their own terms.
- PulseTailProbe's shared-memory parser is an original implementation of the public HWiNFO interface layout. HWiNFO, G Helper and their drivers/modules are not bundled. Python analysis uses the standard library; optional plot regeneration uses Matplotlib, which is not included in the runtime package. Published figures and sanitized measurements are project evidence, not third-party benchmark results.
- Windows, Microsoft, ASUS, AMD, Intel and other product names identify compatibility or test environments. No affiliation or endorsement is implied.

## Optional AMD ADL panel observer

`PulseIdleProbe` can be built against externally supplied official ADL SDK headers. The local panel-enabled build uses checkout `84c881cb2136f04287eb3af12de00a9e2867aa9e`. No AMD driver binary is distributed. The SDK header terms are reproduced here; Pulse's root rights notice does not replace them.

Copyright (c) 2016 - 2022 Advanced Micro Devices, Inc. All rights reserved.

MIT LICENSE:

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
