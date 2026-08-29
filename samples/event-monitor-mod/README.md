# Event Monitor

A read-only gameplay example that counts tower and bloon actions, writes a
summary after each round, and persists a completed-match counter in mod-owned
storage.

Set `log_each_action` to `true` in the profile configuration to log each
observed object's kind and stable ID. The example also demonstrates retaining
subscription tokens and removing them during shutdown.

Counter summaries use this order:

- towers: placed / upgraded / sold;
- bloons: spawned / popped / leaked.
