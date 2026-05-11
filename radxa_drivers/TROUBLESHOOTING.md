# Development & Troubleshooting Discourse Log

This document captures the chronological development process, diagnosing, and troubleshooting steps encountered while bringing up the Radxa A7z Media Middleware on an `x86` local development machine (running Ubuntu 22.04 with an active Anaconda environment). 

Recording these issues ensures future developers can quickly identify and resolve similar hardware and environment-related snags.

---

## 1. Dependency Resolution & Package Management

**Issue:** Initial `setup.bash` script failed to install `cppzmq-dev`.
* **Diagnosis:** The script was designed to pull `libzmq3-dev` and `cppzmq-dev`. However, on Ubuntu (Jammy), the C++ bindings (`zmq.hpp`) are bundled directly within the `libzmq3-dev` package. The standalone `cppzmq-dev` package does not exist, causing `apt-get` to abort.
* **Resolution:** Removed `cppzmq-dev` from the installation script. Because `apt-get` aborted midway, subsequent packages (`flatbuffers-compiler`, `nlohmann-json3-dev`, `libspdlog-dev`) were skipped. Running `./setup.bash` a second time cleanly resolved all dependencies.

**Issue:** `v4l2-ctl: command not found`
* **Diagnosis:** The initial test script (`phase1_hardware_tests.sh`) relied on `v4l2-ctl` to query video device capabilities, but the tool was missing from the system.
* **Resolution:** Appended the `v4l-utils` package to the core build tools array in `setup.bash`.

---

## 2. GStreamer & Conda Environment Conflicts

**Issue:** `WARNING: erroneous pipeline: no element "v4l2src"` and `no element "alsasrc"`
* **Diagnosis:** Despite `gstreamer1.0-plugins-good` and `gstreamer1.0-alsa` being successfully installed via `apt-get`, the `gst-launch-1.0` command failed to locate basic elements. The terminal prompt indicated an active Conda environment: `(base) sid@sid-IdeaPad-Gaming-3-15IHU6`. 
* **Explanation:** Anaconda/Miniconda prepends its own binaries to the system `$PATH`. Conda ships with its own isolated GStreamer installation, which is completely unaware of the plugins installed via the system's `apt` package manager.
* **Resolution:** Instead of forcing the developer to constantly run `conda deactivate`, we updated the bash test scripts to explicitly call the absolute system path: `/usr/bin/gst-launch-1.0`. This successfully bypassed the Conda environment and linked against the `apt` plugins.

---

## 3. ALSA Hardware Format Negotiation

**Issue:** `ERROR: from element /GstPipeline:pipeline0/GstAlsaSrc:alsasrc0: Internal data stream error.` / `streaming stopped, reason not-negotiated (-4)`
* **Diagnosis:** The initial audio pipeline string strictly demanded `audio/x-raw,rate=16000,channels=1,format=S16LE` directly from the `alsasrc` (USB PnP Audio Device). The hardware microphone rigidly supported a different sampling rate (e.g., 44100Hz or 48000Hz stereo) and refused to initialize the stream.
* **Resolution:** We introduced `audioconvert ! audioresample` directly after `alsasrc`. 
  * *Updated Pipeline:* `alsasrc ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE`
  * This allows GStreamer to dynamically negotiate a compatible raw format with the hardware, intercept the stream, and resample/convert it down to our target 16000Hz mono footprint on-the-fly.

---

## 4. Local x86 vs Target RK3588 Emulation

**Issue:** `no element "mppjpegdec"` (Anticipated)
* **Diagnosis:** The `video_pipeline.cpp` specifies `mppjpegdec`, a highly optimized hardware-accelerated JPEG decoder strictly available on Rockchip SOCs (like the target Radxa A7z). 
* **Resolution / Workaround:** When compiling and running `video_ingestor` locally on an `x86` Ubuntu laptop, this pipeline will fail to construct. **Fix:** For local development, edit `src/media/video_ingestor/video_pipeline.cpp` and temporarily replace `mppjpegdec` with `jpegdec`. Remember to change it back before deploying to the Radxa A7z. 
* *Note:* Future architecture improvements could dynamically swap the decoder based on an environment variable or the `configs/media_config.json` flag (e.g., `"hardware_decode": true`).

---

## 5. CMake & Make Linker Conflicts (spdlog / fmt)

**Issue:** `/usr/bin/ld: warning: libfmt.so.8, needed by /usr/lib/x86_64-linux-gnu/libspdlog.so.1.9.2, may conflict with libfmt.so.9` followed by `undefined reference` errors during `make`.
* **Diagnosis:** The system's `libspdlog` (installed via `apt`) was compiled against `libfmt.so.8`. However, the active Conda environment (`(base)`) intercepts the linker and provides its own newer `libfmt.so.9`. This creates an ABI mismatch where the linker tries to resolve `spdlog` symbols using the wrong `fmt` library version.
* **Resolution:** Conda should be deactivated during the build process to prevent its libraries from shadowing the system's `apt` packages.
  * Run `conda deactivate`
  * Clean the build folder: `rm -rf *` (inside the `build` directory)
  * Rerun `cmake ..` and `make`

---

## 6. Live Feed Synchronization & Buffering Issues

**Issue:** `Dropped samples` from `alsasrc` and `Buffers being dropped` from `autovideosink` during live feed tests.
* **Diagnosis 1 (CPU Bottleneck):** Running high-resolution MJPG (e.g., 4K or 1440p) with software decoding (`jpegdec`) on an x86 CPU is extremely resource-intensive. When the CPU cannot decode video fast enough, the entire pipeline clock stalls, causing the audio source to drop incoming samples.
* **Diagnosis 2 (Thread Blocking):** By default, GStreamer pipelines without `queue` elements may run on the same master thread. If the Video Sink blocks slightly (e.g., waiting for the window manager to paint), it can directly stall the Audio Source.
* **Resolution:**
  * **For x86 Testing**: Use lower resolution uncompressed formats (e.g., 640x480 `YUYV`) to eliminate the decoding bottleneck.
  * **Threading**: Inject `queue` elements before both the Video and Audio sinks. This decouples the capture, processing, and rendering into separate threads, preventing a slow UI from blocking the hardware capture.
