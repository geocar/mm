
mickey
=======

![screenshot](m.jpg?raw=true)

linux console mouse/keyboard tool

* yes, that is a graphical mouse pointer on the text-mode console.
  the bottom four characters ^A,^B,^C,^D are remapped to the cursor
  [like this](https://en.wikipedia.org/wiki/Text-based_user_interface#:~:text=Mouse%20cursor%20in%20Impulse%20Tracker)
* works with raw `/dev/input/by-id/*` devices; does not require `/dev/input/mice`
  or [gpm](https://www.nico.schottelius.org/software/gpm/)
  or [consolation](https://salsa.debian.org/consolation-team/consolation/)
  or [jamd](https://jamd.sourceforge.net)
 

### quick start

```sh
make && ./m /dev/input/by-id/*mouse &
```

### hotkey usage

- you can pass a _regular file_ and a keyboard device as arguments to enable a hotkey
- tap left-alt (or left-option/meta; tap means press and release) to invoke hotkey

```sh
# assuming you've bound ^B\ to something interesting...
echo 00000000 02 5c | xxd -r - > tmux-menu-hotkey  
./m /dev/input/by-id/* tmux-menu-hotkey &
tmux
```

### masking

- listing other vc `/dev/{tty,vcsa}[123...]` will disable drawing on any other vc
- masking should be used if you are using e.g. wayland, kmscon, etc on another vc
- you probably won't need this

### troubleshooting

- serial devices e.g. `/dev/ttyS0` and (named) fifos are forwarded to the vc like keystrokes
  if anyone still has a mouse for these i could be tempted to implement some serial mouse protocols as well
- remember you can cat `/dev/vcs` to see what's on the "screen" even if you can't see what's on the screen
- try running as root. if that works you can try using strace to see what the difference is
- you probably have setfont installed. try it. if it can't change the font, this isn't going to be able to either
