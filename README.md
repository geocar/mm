
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

    make && ./m /dev/input/by-id/*mouse &

