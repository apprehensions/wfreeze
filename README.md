# wfreeze

Freeze the screen, and run a command. Works well with [slurp].

No new wayland events are handled after successfully freezing, since
having new outputs added or disconnected breaks the purpose of freezing.

## Building

To build wfreeze ensure that you have the following dependencies:

* pkg-config
* wayland
* wayland-protocols

Afterwards, run:
```
make
make install
```

## Usage

Note that some compositors may choose to handle layer surfaces differently.
On dwl and river, the default command will work fine:

```sh
wfreeze slurp
```

If you are on Sway, you will have to use the `-b` flag with wfreeze - which
may or may not work half of the time due to race conditions, or use
another piece of software that has freezing like capabilities such as [dulcepan],
which is a screenshotter, or [wayfreeze], which is similar to wfreeze but is
tested and written in Rust.

This is due to the fact the layer shell outlining that compositors have different
implementations as to how to order and display layer surfaces, such as Sway or Hyprland.

[slurp]: https://github.com/emersion/slurp
[dulcepan]: https://codeberg.org/vyivel/dulcepan
[wayfreeze]: https://github.com/Jappie3/wayfreeze
