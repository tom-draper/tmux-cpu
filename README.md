# tmux CPU

Visualise CPU usage in your tmux status bar.

```
gcc -03 -o /usr/local/bin/tmux-cpu tmux-cpu.c
```

Then in `~/.tmux.conf`:

```
set -g status-right "#(/usr/local/bin/tmux-cpu) #{pane_title} %H:%M %d-%b-%y"
set -g status-interval 1
```

Usage of each CPU core is visualised as a block in descending order. Example with 12 cores:

<img width="865" height="75" alt="image" src="https://github.com/user-attachments/assets/8f2e3af5-2b2a-4e0f-a85a-5b74edcf29da" />
