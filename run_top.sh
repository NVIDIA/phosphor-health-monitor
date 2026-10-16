#!/bin/bash
# Usage: run_top.sh <resource>; prints the top processes by CPU or memory.
RESOURCE=$1

if [[ "$RESOURCE" == "CPU"* ]]; then
    COLS="%CPU"
else
    # busybox top shows %VSZ or %RSS depending on its config
    COLS="%VSZ %RSS %MEM"
fi

top -b -n 1 | awk -v want="$COLS" '
    !col && $1 == "PID" {
        n = split(want, w, " ")
        for (j = 1; j <= n && !col; j++)
            for (i = 1; i <= NF; i++)
                if ($i == w[j]) { col = i; break }
        for (i = 1; i <= NF; i++)
            if ($i == "COMMAND") cmd = i
        next
    }
    col && cmd && NF >= cmd {
        u = $col
        sub(/%$/, "", u)
        if (u + 0 > 0) printf "%s %s(%s%%)\n", u, $cmd, u
    }' | sort -rn | head -n 10 | cut -d" " -f2- | tr "\n" " "

if [[ "$RESOURCE" != "CPU"* ]]; then
    grep -E "^(MemAvailable|Shmem|Slab|SUnreclaim):" /proc/meminfo | tr -s " " | tr "\n" " "
fi
echo
