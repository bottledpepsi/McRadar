# Agent test harness (no Minecraft needed)

`FakeGame` imitates Minecraft's *structure* with obfuscated names (a Vec3-like class, a position
field next to a velocity field, a tab-list class that has a GameProfile but no position, a mixed
"all entities" list, a server-side player list, and an old-style variant with plain double fields).

```
cd agent/test
javac -d out src/com/mojang/authlib/GameProfile.java src/FakeGame.java
java -javaagent:../esp-agent.jar="port=47615;export=./exp" -cp out FakeGame --username Alice      # new style
java -javaagent:../esp-agent.jar="port=47615;export=./exp" -cp out FakeGame --username Alice old  # old style
```
Then run `mcradar --agent` (or any UDP listener on 127.0.0.1:47615) and check `exp/mcradar_layout.json`.
Expected: Alice is the local player, Bob and Carol are the other players, yaw values follow the fake look vector.
