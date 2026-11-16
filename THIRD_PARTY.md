# Third-party code

`sm_pc` itself is licensed under the GNU General Public License, version 3 (see [LICENSE](LICENSE)). It uses the
following third-party code, each under its own license, all compatible with the GPL.

| Component | Where | License |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) v1.91.5-docking | fetched by CMake at configure time | MIT |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) 1.1, a subset (allocator, index generator, simplifier), unmodified | `third_party/meshoptimizer/` | MIT, see `third_party/meshoptimizer/LICENSE.md` |

At run time `sm_pc` also uses components of Windows (Direct3D 11, XAudio2, XInput, Media Foundation) and, when it
is on the `PATH`, starts [FFmpeg](https://ffmpeg.org/) as a separate program to decode the game's movies. FFmpeg is
not part of `sm_pc` and is not distributed with it.
