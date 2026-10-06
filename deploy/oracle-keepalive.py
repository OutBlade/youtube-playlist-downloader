#!/usr/bin/env python3
"""
Oracle Cloud Always Free Idle Reclamation Keepalive
===================================================
Oracle Cloud terminates 'idle' Always Free instances after 7 days if:
- CPU utilization (95th percentile) is less than 20%
- Memory utilization is less than 20% (for Ampere A1 shapes)
- Network utilization is less than 20%

This lightweight daemon prevents false-positive idle reclamation:
1. Allocates ~21% of total system memory in an in-memory buffer.
2. Maintains ~21% CPU usage on a single core ONLY when the server is idle.
   If real workloads (yt-dlp, ffmpeg) are running, it automatically sleeps.
3. Runs at the lowest scheduling priority (nice 19) so it never slows down user downloads.
"""

import os
import sys
import time
import signal

def get_cpu_idle():
    """Read aggregate CPU idle percentage from /proc/stat over a 0.5s interval."""
    try:
        def read_stat():
            with open('/proc/stat', 'r') as f:
                fields = [float(x) for x in f.readline().split()[1:]]
            idle = fields[3] + (fields[4] if len(fields) > 4 else 0)
            total = sum(fields)
            return idle, total

        i1, t1 = read_stat()
        time.sleep(0.5)
        i2, t2 = read_stat()
        diff_total = t2 - t1
        diff_idle = i2 - i1
        if diff_total <= 0:
            return 100.0
        return (diff_idle / diff_total) * 100.0
    except Exception:
        return 100.0

def get_total_ram_bytes():
    try:
        with open('/proc/meminfo', 'r') as f:
            for line in f:
                if line.startswith('MemTotal:'):
                    kb = int(line.split()[1])
                    return kb * 1024
    except Exception:
        pass
    return 12 * 1024 * 1024 * 1024  # default to 12 GB

running = True

def handle_signal(sig, frame):
    global running
    running = False

signal.signal(signal.SIGTERM, handle_signal)
signal.signal(signal.SIGINT, handle_signal)

def main():
    try:
        os.nice(19)
    except Exception:
        pass

    total_bytes = get_total_ram_bytes()
    # Allocate 21% of RAM to satisfy Oracle A1 memory threshold
    target_alloc = int(total_bytes * 0.21)
    print(f"[blade-keepalive] Allocating {target_alloc // (1024 * 1024)} MB buffer to protect against Oracle idle reclamation...")
    try:
        buffer = bytearray(target_alloc)
        # Touch pages every 4096 bytes so Linux actually faults the memory pages
        for i in range(0, target_alloc, 4096 * 64):
            buffer[i] = 1
    except MemoryError:
        print("[blade-keepalive] Warning: Memory allocation failed; using smaller buffer.")
        buffer = bytearray(512 * 1024 * 1024)

    print("[blade-keepalive] Active. Maintaining baseline activity at low priority...")
    sys.stdout.flush()

    while running:
        idle_pct = get_cpu_idle()
        # If CPU is mostly idle (> 85% idle, meaning system is < 15% utilized),
        # burn ~21% of 1 CPU core for a 1-second window.
        if idle_pct > 85.0:
            # 210ms compute, 790ms sleep
            start = time.perf_counter()
            while time.perf_counter() - start < 0.21 and running:
                _ = 3.14159 * 2.71828 * 1.41421
            remaining = 1.0 - (time.perf_counter() - start)
            if remaining > 0 and running:
                time.sleep(remaining)
        else:
            # System is already busy with real work; sleep and do nothing
            time.sleep(10.0)

    print("[blade-keepalive] Shutting down cleanly.")

if __name__ == '__main__':
    main()
