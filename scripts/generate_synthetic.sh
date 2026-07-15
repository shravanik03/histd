#!/bin/bash
# generate_synthetic.sh — sends N synthetic records to the daemon socket
N=$1
SOCKET="$HOME/.local/share/histd/histd.sock"

commands=("docker run -p 3000:3000 myapp" "git status" "git commit -m fix" \
          "npm run build" "npm test" "cd ~/projects/myapp" "ls -la" \
          "curl -I https://example.com" "kubectl get pods" "python3 script.py")

for i in $(seq 1 "$N"); do
    cmd="${commands[$((RANDOM % ${#commands[@]}))]} --run$i"
    ts=$(( $(date +%s) - RANDOM % 2592000 ))  # random time in last 30 days
    exit_code=$(( RANDOM % 10 == 0 ? 1 : 0 ))  # ~10% failures
    printf "%s\x1f/home/shravani/project%d\x1f%d\x1f%d\x1f%d\x1fbash-bench\n" \
        "$cmd" "$((i % 5))" "$exit_code" "$((RANDOM % 2000))" "$ts" \
        | nc -w 0 -U "$SOCKET"
done