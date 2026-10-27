# adachi

A minimal tiling window manager for X11, built around Ratpoison-style frames

## Key bindings

Press `Ctrl+t`, release it, then press:

- `c`: open `xterm`
- `!`: open `dmenu_run`
- `n` or `Space`: show the next hidden window in the current frame
- `p`: show the previous hidden window in the current frame
- `t`: show the last window
- `0`-`9`: show a window by index (focuses its frame if it is already visible)
- `k`: close the current window
- `q`: quit Adachi

Frames:

- `s`: split the current frame into top and bottom
- `S`: split the current frame into left and right
- `Tab`: focus the next frame
- `Left`, `Right`, `Up`, `Down`: focus the frame in that direction
- `r`: resize the current frame, then use the arrow keys and press any other key to finish
- `Q`: keep only the current frame
- `R`: remove the current frame

![Adachi screenshot](assets/adachi.png)
