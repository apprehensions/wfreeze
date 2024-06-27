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

```sh
wfreeze slurp
```

[slurp]: https://github.com/emersion/slurp
