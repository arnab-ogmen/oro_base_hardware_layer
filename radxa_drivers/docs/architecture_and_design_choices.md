# Architecture & Design Choices: Video & Audio Pipelines

## Overview

This document explains the architecture of the video and audio capture pipeline on the Radxa Cubie A7z, the design decisions made, and why specific technologies, formats, and parameters were chosen.

---

## System Architecture

### High-Level Data Flow

```
┌──────────────┐    USB 2.0     ┌──────────────┐    pipe (stdout)    ┌─────────────────────────────────────────────┐
│  USB 4K      │───────────────▶│   v4l2-ctl   │───────────────────▶│              GStreamer Pipeline              │
│  Camera      │    480 Mbps    │  (V4L2 API)  │    raw MJPEG       │                                             │
│  (UVC)       │   shared bus   │              │    byte stream     │  fdsrc → jpegparse → jpegdec → videosink    │
└──────────────┘                └──────────────┘                    └─────────────────────────────────────────────┘

┌──────────────┐    USB 2.0     ┌─────────────────────────────────────────────────────────────────────────────────┐
│  USB PnP     │───────────────▶│                         GStreamer Pipeline                                     │
│  Audio       │    480 Mbps    │                                                                               │
│  (UAC)       │                │  alsasrc (plughw:2,0) → audioconvert → audioresample → alsasink (plughw:2,0)  │
└──────────────┘                └─────────────────────────────────────────────────────────────────────────────────┘
```

### Detailed Pipeline Architecture

```
                         VIDEO PATH
                         ══════════

  ┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌───────────┐     ┌───────┐     ┌──────────────┐
  │ Camera  │────▶│ v4l2-ctl │────▶│  fdsrc   │────▶│ jpegparse│────▶│  jpegdec  │────▶│ queue │────▶│ autovideosink│
  │ HW      │ USB │ --stream │ fd0 │ do-ts=T  │ raw │ (framing)│ jpg │ (SW dec)  │ raw │ leaky │ raw │  sync=false  │
  │         │MJPEG│  -mmap   │stdin│          │bytes│          │frame│           │video│ d/s   │video│              │
  └─────────┘     └──────────┘     └──────────┘     └──────────┘     └───────────┘     └───────┘     └──────────────┘
                   Kernel space      User space ─────────────────────────────────────────────────────────────────────▶


                         AUDIO PATH
                         ══════════

  ┌─────────┐     ┌──────────┐     ┌───────────┐     ┌───────────┐     ┌───────┐     ┌──────────┐
  │ USB PnP │────▶│ alsasrc  │────▶│audioconvert│───▶│audioresamp│────▶│ queue │────▶│ alsasink │
  │ Mic     │ USB │plughw:2,0│ raw │           │ raw │ le        │16kHz│       │     │plughw:2,0│
  │         │ UAC │          │audio│           │audio│           │mono │       │     │          │
  └─────────┘     └──────────┘     └───────────┘     └───────────┘     └───────┘     └──────────┘
                   ALSA driver       GStreamer pipeline ──────────────────────────────────────────▶
```

---

## Design Decisions

### Decision 1: v4l2-ctl Pipe Instead of v4l2src

**Choice:** Use `v4l2-ctl --stream-to=-` piped into GStreamer's `fdsrc` instead of GStreamer's native `v4l2src` element.

**Reason:** The GStreamer `v4l2src` plugin on this board is an Allwinner-customized version that integrates with the board's ISP hardware. This customization makes it incompatible with standard USB UVC cameras. The plugin attempts to initialize the Cedar Video Engine and MIPI-CSI interfaces, which fail for USB cameras.

**Alternatives considered:**

| Approach | Result |
|---|---|
| `v4l2src` (standard) | ❌ Fails — Allwinner patches break UVC support |
| `v4l2src en-awisp=false` | ❌ Fails — ISP init still runs |
| `GST_V4L2_USE_LIBV4L2=1` | ❌ Fails — still uses patched plugin |
| Recompile GStreamer from source | ⚠️ Possible but high maintenance burden |
| **`v4l2-ctl` pipe → `fdsrc`** | **✅ Works — bypasses GStreamer's V4L2 layer entirely** |

**Trade-offs:**
- ✅ Reliable, uses the standard kernel V4L2 API directly
- ✅ No custom patches or recompilation needed
- ⚠️ Slightly higher latency (~1 frame) due to pipe buffering
- ⚠️ No GStreamer-level control over V4L2 properties (brightness, contrast, etc.)

### Decision 2: MJPEG Format

**Choice:** Use MJPEG (`image/jpeg`) instead of YUYV (`video/x-raw`).

**Reason:** USB 2.0 bandwidth constraints.

| Format | 720p @ 30fps Bandwidth | 1080p @ 30fps Bandwidth | USB 2.0 Budget |
|---|---|---|---|
| **YUYV** (uncompressed) | ~663 Mbps | ~1493 Mbps | ❌ Exceeds 480 Mbps |
| **MJPEG** (compressed) | ~30-60 Mbps | ~60-120 Mbps | ✅ Fits in 480 Mbps |

MJPEG compresses each frame independently as a JPEG image, reducing bandwidth by ~10-20x compared to raw YUYV. This is essential when the camera is on a shared USB 2.0 bus.

**Trade-off:** MJPEG requires CPU decoding, which adds ~15-25% CPU load at 720p.

### Decision 3: 720p Resolution

**Choice:** Use 1280x720 instead of 1920x1080 for live feed.

**Reason:** The Allwinner A527 CPU cannot decode 1080p MJPEG at 30fps in software without dropping frames. At 720p, the software `jpegdec` keeps up at ~25 fps with no dropped buffers.

| Resolution | Decode Time/Frame | 30fps Budget | Result |
|---|---|---|---|
| 640x480 | ~8ms | 33ms | ✅ Smooth |
| **1280x720** | **~22ms** | **33ms** | **✅ Smooth** |
| 1920x1080 | ~45ms | 33ms | ❌ Drops frames |

> **Note:** 1080p capture works for non-real-time use cases (e.g., `fakesink` tests, recording to file) where frame drops are acceptable.

### Decision 4: Software Decoder (jpegdec) Over Hardware (omxmjpegvideodec)

**Choice:** Use GStreamer's software `jpegdec` instead of the Allwinner OMX hardware decoder.

**Reason:** The `omxmjpegvideodec` hardware decoder crashes when receiving MJPEG data through `fdsrc` (pipe). It expects DMA-aligned buffers from the V4L2 subsystem, not raw byte streams from stdin.

| Decoder | Source Compatibility | CPU Usage | Status |
|---|---|---|---|
| `jpegdec` (software) | Works with any source | ~20-25% @ 720p | ✅ |
| `omxmjpegvideodec` (hardware) | Only works with V4L2/DMA sources | ~2% | ❌ Crashes with `fdsrc` |

### Decision 5: UVC Quirk 0x80 (FIX_BANDWIDTH)

**Choice:** Load `uvcvideo` with `quirks=0x80` to bypass USB bandwidth verification.

**Reason:** The camera's USB descriptor claims more isochronous bandwidth than is available on the shared USB 2.0 bus. The actual compressed MJPEG data fits within bandwidth, but the *claimed* allocation exceeds it, causing the kernel to reject the stream with `EPROTO`.

**Risk assessment:**
- The quirk disables the bandwidth *check*, not the bandwidth *limit*.
- If the actual data exceeds bandwidth, frames will be corrupted (not a crash).
- At 720p MJPEG, actual bandwidth (~30-60 Mbps) is well within the 480 Mbps USB 2.0 limit.

### Decision 6: Pipeline Timing Configuration

**Choice:** Use `do-timestamp=true` on `fdsrc`, `sync=false` on video sink, and `leaky=downstream` on queue.

**Reason:** When data arrives via a Unix pipe, GStreamer has no hardware clock reference. These settings ensure smooth playback:

| Setting | Purpose |
|---|---|
| `fdsrc do-timestamp=true` | Auto-generates monotonic timestamps as data arrives |
| `queue leaky=downstream` | Drops oldest frames when the queue fills up, preventing pipeline stalls |
| `autovideosink sync=false` | Renders frames immediately without waiting for clock alignment |
| `alsasink sync=true` | Audio still syncs to its own hardware clock for consistent sample rate |

### Decision 7: Audio Format (16kHz Mono S16LE)

**Choice:** Resample audio to 16kHz, mono, signed 16-bit little-endian.

**Reason:** This is the standard format for speech processing and voice recognition systems, which is the intended downstream use case. The USB PnP mic's native format varies, so explicit caps ensure consistent output regardless of hardware.

---

## USB Bus Layout

```
Bus 01 (USB 2.0, xhci-hcd, 480 Mbps shared)
├── Port 1: USB 2.0 Hub (4-port)
│   ├── Port 1: Dell Wireless Device (Keyboard/Mouse HID) — 12 Mbps
│   ├── Port 2: USB 4K Live Camera (UVC + UAC) — 480 Mbps
│   │   ├── Interface 0: Video (uvcvideo) → /dev/video0
│   │   ├── Interface 1: Video (uvcvideo) → /dev/video1
│   │   ├── Interface 2: Audio (snd-usb-audio) → card 1
│   │   └── Interface 3: Audio (snd-usb-audio) → card 1
│   └── Port 4: USB PnP Audio Device (UAC) — 12 Mbps
│       ├── Interface 0: Audio Capture (snd-usb-audio) → card 2
│       ├── Interface 1: Audio Playback (snd-usb-audio) → card 2
│       └── Interface 3: HID (volume control)

Bus 02 (USB 3.1, xhci-hcd, 10 Gbps) — USB-C OTG port
└── (Available but not used for peripherals in current setup)

Bus 03 (USB 2.0, sunxi-ehci, 480 Mbps)
└── Port 1: AIC8800 WiFi/Bluetooth module

Bus 04 (USB 1.1, sunxi-ohci, 12 Mbps)
└── (Empty)
```

---

## Performance Characteristics

| Metric | Value |
|---|---|
| Video resolution | 1280x720 (HD) |
| Video format | MJPEG (compressed) |
| Video frame rate | 25-30 fps |
| Video latency | ~50-80ms (pipe + decode + render) |
| Audio sample rate | 16000 Hz |
| Audio channels | 1 (mono) |
| Audio format | S16LE (signed 16-bit little-endian) |
| Audio latency | ~20-40ms |
| USB bandwidth used | ~40-80 Mbps (of 480 Mbps available) |
| CPU usage (video decode) | ~20-25% |
| CPU usage (audio) | ~1-2% |