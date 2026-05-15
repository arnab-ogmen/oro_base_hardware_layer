#!/usr/bin/env python3

import csv
import os
import random
import signal
import subprocess
import time
from datetime import datetime

TARGET_NODES = [
    "video_ingestor",
    "audio_ingestor",
    "av_receiver"
]

CHAOS_MODES = [
    "sigterm",
    "sigkill",
    "sigsegv",
    "freeze",
    "cpu_stress",
    "memory_stress",
    "io_stress"
]

MIN_INTERVAL = 3
MAX_INTERVAL = 5

RECOVERY_TIMEOUT = 10

CSV_FILE = "chaos_results.csv"


def get_pids(name):
    try:
        out = subprocess.check_output(
            ["pgrep", "-f", name],
            text=True
        ).strip()

        if not out:
            return []

        return [int(x) for x in out.splitlines()]

    except subprocess.CalledProcessError:
        return []


def is_running(name):
    return len(get_pids(name)) > 0


def inject_failure(pid, mode):

    print(f"[CHAOS] Injecting {mode} into PID {pid}")

    if mode == "sigterm":
        os.kill(pid, signal.SIGTERM)

    elif mode == "sigkill":
        os.kill(pid, signal.SIGKILL)

    elif mode == "sigsegv":
        os.kill(pid, signal.SIGSEGV)

    elif mode == "freeze":
        os.kill(pid, signal.SIGSTOP)

        time.sleep(5)

        try:
            os.kill(pid, signal.SIGCONT)
        except:
            pass

    elif mode == "cpu_stress":
        subprocess.Popen([
            "stress-ng",
            "--cpu", "8",
            "--timeout", "10s"
        ])

    elif mode == "memory_stress":
        subprocess.Popen([
            "stress-ng",
            "--vm", "2",
            "--vm-bytes", "85%",
            "--timeout", "10s"
        ])

    elif mode == "io_stress":
        subprocess.Popen([
            "stress-ng",
            "--hdd", "4",
            "--timeout", "10s"
        ])


def wait_for_death(old_pid, timeout=5):

    start = time.time()

    while time.time() - start < timeout:

        try:
            os.kill(old_pid, 0)
        except OSError:
            return True

        time.sleep(0.2)

    return False


def wait_for_recovery(node, old_pids):

    start = time.time()

    while time.time() - start < RECOVERY_TIMEOUT:

        current_pids = set(get_pids(node))

        new_pids = current_pids - set(old_pids)

        if len(new_pids) > 0:
            return True, time.time() - start

        time.sleep(0.5)

    return False, RECOVERY_TIMEOUT


def log_result(row):

    file_exists = os.path.exists(CSV_FILE)

    with open(CSV_FILE, "a", newline="") as f:

        writer = csv.writer(f)

        if not file_exists:
            writer.writerow([
                "timestamp",
                "node",
                "mode",
                "recovery_time_sec",
                "success"
            ])

        writer.writerow(row)


def main():

    print("=" * 60)
    print("ADVANCED AV STACK CHAOS BENCHMARK")
    print("=" * 60)

    while True:

        node = random.choice(TARGET_NODES)

        mode = random.choice(CHAOS_MODES)

        old_pids = get_pids(node)

        if not old_pids:
            print(f"[WARN] {node} not running")

            time.sleep(5)
            continue

        pid = random.choice(old_pids)

        inject_failure(pid, mode)

        if mode in ["sigterm", "sigkill", "sigsegv"]:

            dead = wait_for_death(pid)

            if dead:
                print(f"[INFO] PID {pid} terminated")
            else:
                print(f"[WARN] PID {pid} still alive")

        print("[INFO] Waiting for recovery...")

        success, recovery_time = wait_for_recovery(
            node,
            old_pids
        )

        if success:
            print(
                f"[SUCCESS] {node} recovered in "
                f"{recovery_time:.2f}s"
            )
        else:
            print(
                f"[FAIL] {node} failed recovery within timeout"
            )

        log_result([
            datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            node,
            mode,
            round(recovery_time, 2),
            success
        ])

        sleep_time = random.randint(
            MIN_INTERVAL,
            MAX_INTERVAL
        )

        print(f"[INFO] Sleeping {sleep_time}s\n")

        time.sleep(sleep_time)


if __name__ == "__main__":
    main()