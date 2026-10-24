# Hello World

[All samples](../README.md) · [First mod tutorial](../../docs/mod-authoring.md)

The smallest complete Lua mod example. It demonstrates the three lifecycle
callbacks, structured logging, and a one-shot deterministic timer.

## Expected message text

```text
Hello World loaded
Hello World is ready
Hello World timer fired after 60 rendered frames
Hello World shut down
```

The timer follows rendered frames. The shutdown message requires orderly exit.

The mod requests no capabilities and does not change gameplay. Install the
packaged `hello-world.btd5mod`, enable it in a profile, launch the game, and view
the runtime log through the manager.

## Run it

1. Import `hello-world.btd5mod` and enable it in an offline test profile.
2. Launch from the manager. In Options' runtime log, find `Hello World loaded`
   and `Hello World is ready`.
3. Keep the game running for 60 rendered frames and find the timer message.
   The render loop can run in the menu; this is not a 60-second delay.
4. Exit normally and find `Hello World shut down`. A forced kill or crash may
   skip shutdown callbacks.

No settings or dependencies are required. Source: [manifest](mod.json),
[Lua](lua/main.lua). Copy this folder and change its identity to start a mod.
