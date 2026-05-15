#!/usr/bin/env python3
import subprocess
import sys
import os
import signal
import time
import threading

MAX_RESTARTS = 20
# Progressive backoff: 0s, 1s, 2s, 4s, 8s, 16s, then caps at 30s
MAX_BACKOFF_S = 30

class ProcessManager:
    def __init__(self):
        # Maps name -> {'cmd': list, 'cwd': str, 'process': Popen,
        #               'restarts': int, 'last_start': float}
        self.nodes = {}
        self.shutting_down = False
        self.lock = threading.RLock()  # Use RLock so start_process can be called from within monitor

    def start_process(self, name, cmd, cwd=None):
        if self.shutting_down:
            return
        
        with self.lock:
            if name not in self.nodes:
                self.nodes[name] = {'cmd': cmd, 'cwd': cwd, 'restarts': 0, 'last_start': 0.0}
            
            info = self.nodes[name]
            restarts = info['restarts']

            # ── Backoff on respawn ────────────────────────────────────────
            if restarts > 0:
                # Exponential backoff: 1, 2, 4, 8, 16, 30, 30, ...
                backoff = min(MAX_BACKOFF_S, 2 ** (restarts - 1))
                elapsed = time.monotonic() - info['last_start']
                if elapsed < backoff:
                    wait = backoff - elapsed
                    print(f"[\033[93mBACKOFF\033[0m] Waiting {wait:.1f}s before restarting {name} (restart #{restarts})...")
                    # Sleep in small increments so we can respond to shutdown
                    deadline = time.monotonic() + wait
                    while time.monotonic() < deadline and not self.shutting_down:
                        time.sleep(0.5)
                    if self.shutting_down:
                        return

            restart_msg = f" (Restart #{restarts})" if restarts > 0 else ""
            print(f"[\033[92mINFO\033[0m] Starting {name}{restart_msg}...")
            
            try:
                p = subprocess.Popen(
                    cmd,
                    cwd=cwd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1
                )
                info['process'] = p
                info['last_start'] = time.monotonic()
                
                # Start a new reader thread for this process instance
                t = threading.Thread(target=self._reader_thread, args=(name, p), daemon=True)
                t.start()
            except Exception as e:
                print(f"[\033[91mERROR\033[0m] Failed to start {name}: {e}")

    def _reader_thread(self, name, p):
        colors = {
            'video_ingestor': '\033[94m', # Blue
            'audio_ingestor': '\033[93m', # Yellow
            'av_receiver': '\033[95m',    # Magenta
            'stream_player': '\033[96m'   # Cyan
        }
        color = colors.get(name, '\033[97m')
        reset = '\033[0m'
        
        for line in p.stdout:
            sys.stdout.write(f"{color}[{name}]{reset} {line}")
            sys.stdout.flush()

    def terminate_all(self):
        self.shutting_down = True
        print("\n[\033[92mINFO\033[0m] Shutting down all nodes...")
        
        with self.lock:
            # Send SIGTERM
            for name, info in self.nodes.items():
                p = info.get('process')
                if p and p.poll() is None:
                    print(f"[\033[92mINFO\033[0m] Terminating {name}...")
                    p.terminate()
            
            # Wait and kill if necessary
            for name, info in self.nodes.items():
                p = info.get('process')
                if p and p.poll() is None:
                    try:
                        p.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        print(f"[\033[91mWARNING\033[0m] {name} did not terminate, killing it...")
                        p.kill()
                        
        print("[\033[92mINFO\033[0m] All nodes shut down cleanly.")

    def monitor(self):
        try:
            while not self.shutting_down:
                time.sleep(2.0)
                
                if self.shutting_down:
                    break
                    
                with self.lock:
                    all_dead = True
                    for name, info in list(self.nodes.items()):
                        p = info.get('process')
                        if p:
                            if p.poll() is None:
                                all_dead = False
                            else:
                                if not self.shutting_down:
                                    rc = p.returncode
                                    restarts = info['restarts']

                                    if restarts >= MAX_RESTARTS:
                                        print(f"[\033[91mFATAL\033[0m] {name} has crashed {restarts} times. "
                                              f"Giving up — check hardware/config.")
                                        continue

                                    print(f"[\033[91mERROR\033[0m] {name} exited unexpectedly (code {rc}). Respawning...")
                                    info['restarts'] += 1
                                    self.start_process(name, info['cmd'], info['cwd'])
                                    all_dead = False
                    
                    if all_dead and self.nodes:
                        # Should theoretically never reach here if we respawn successfully,
                        # but just in case everything keeps instantly failing.
                        pass
        except KeyboardInterrupt:
            pass
        finally:
            self.terminate_all()

def main():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    project_root = os.path.dirname(script_dir)
    build_dir = os.path.join(project_root, "build")

    if not os.path.exists(build_dir):
        print(f"[\033[91mERROR\033[0m] Build directory not found at {build_dir}.")
        print("Please build the project first.")
        sys.exit(1)

    manager = ProcessManager()

    def signal_handler(sig, frame):
        # We set shutting_down to true; the monitor loop will catch it and terminate.
        manager.shutting_down = True

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    nodes = [
        ("video_ingestor", os.path.join(build_dir, "video_ingestor")),
        ("audio_ingestor", os.path.join(build_dir, "audio_ingestor")),
        ("av_receiver", os.path.join(build_dir, "av_receiver"))
    ]

    for name, cmd in nodes:
        if os.path.exists(cmd):
            manager.start_process(name, [cmd], cwd=build_dir)
        else:
            print(f"[\033[93mWARNING\033[0m] Executable {cmd} not found. Skipping {name}.")

    if not manager.nodes:
        print("[\033[91mERROR\033[0m] No executables found to run. Exiting.")
        sys.exit(1)

    manager.monitor()

if __name__ == "__main__":
    main()
