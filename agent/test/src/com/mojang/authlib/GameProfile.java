package com.mojang.authlib;
import java.util.UUID;
/** Stand-in for the real authlib class (same package + name, which is all the agent relies on). */
public class GameProfile {
    private final UUID id; private final String name;
    public GameProfile(UUID id, String name) { this.id = id; this.name = name; }
    public String getName() { return name; }
    public UUID getId() { return id; }
}
