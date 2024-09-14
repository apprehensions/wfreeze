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

If you are on Sway, you will have to use the `-b` flag:

```sh
wfreeze -b slurp
```

[slurp]: https://github.com/emersion/slurp
