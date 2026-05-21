#!/usr/bin/env python3

import os
import random
import signal
import subprocess
import time

TARGET_NODES = [
    "video_ingestor",
    "audio_ingestor",
    "av_receiver"
]

KILL_MODES = [
    "sigterm",
    "sigkill",
    "stop_continue",
    "force_segfault"
]

MIN_DELAY = 3
MAX_DELAY = 5


def get_pids(process_name):
    """
    Returns all PIDs matching process_name.
    """
    try:
        output = subprocess.check_output(
            ["pgrep", "-f", process_name],
            text=True
        ).strip()

        if not output:
            return []

        return [int(pid) for pid in output.splitlines()]

    except subprocess.CalledProcessError:
        return []


def kill_process(pid, mode):
    """
    Apply different failure modes.
    """

    try:
        if mode == "sigterm":
            print(f"[CHAOS] SIGTERM -> PID {pid}")
            os.kill(pid, signal.SIGTERM)

        elif mode == "sigkill":
            print(f"[CHAOS] SIGKILL -> PID {pid}")
            os.kill(pid, signal.SIGKILL)

        elif mode == "stop_continue":
            print(f"[CHAOS] SIGSTOP -> PID {pid}")
            os.kill(pid, signal.SIGSTOP)

            time.sleep(5)

            print(f"[CHAOS] SIGCONT -> PID {pid}")
            os.kill(pid, signal.SIGCONT)

        elif mode == "force_segfault":
            print(f"[CHAOS] Force segfault -> PID {pid}")
            os.kill(pid, signal.SIGSEGV)

    except ProcessLookupError:
        print(f"[INFO] PID {pid} already dead")


def main():
    print("===================================")
    print("   AV STACK CHAOS TEST STARTED")
    print("===================================")

    while True:

        target = random.choice(TARGET_NODES)
        mode = random.choice(KILL_MODES)

        pids = get_pids(target)

        if not pids:
            print(f"[WARN] No PID found for {target}")

        else:
            pid = random.choice(pids)
            kill_process(pid, mode)

        delay = random.randint(MIN_DELAY, MAX_DELAY)

        print(f"[CHAOS] Sleeping for {delay} sec\n")

        time.sleep(delay)


if __name__ == "__main__":
    main()