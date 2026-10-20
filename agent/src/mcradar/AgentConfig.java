package mcradar;

import java.util.HashMap;
import java.util.Map;

/** Agent options, passed as "key=value;key=value" (premain/agentmain argument string). */
final class AgentConfig {
    int port = 47615;          // UDP port of the C++ radar on 127.0.0.1
    int hz = 20;               // send rate (1..240); the overlay passes hz=120 by default
    String exportDir = System.getProperty("java.io.tmpdir");
    String username;           // override for the local player's name (normally auto-detected)

    static AgentConfig parse(String args) {
        AgentConfig c = new AgentConfig();
        if (args == null) return c;
        Map<String, String> kv = new HashMap<String, String>();
        for (String part : args.split(";")) {
            int i = part.indexOf('=');
            if (i > 0) kv.put(part.substring(0, i).trim(), part.substring(i + 1).trim());
        }
        try { if (kv.containsKey("port")) c.port = Integer.parseInt(kv.get("port")); } catch (NumberFormatException ignored) { }
        try { if (kv.containsKey("hz")) c.hz = Math.max(1, Math.min(240, Integer.parseInt(kv.get("hz")))); } catch (NumberFormatException ignored) { }
        if (kv.containsKey("export") && !kv.get("export").isEmpty()) c.exportDir = kv.get("export");
        if (kv.containsKey("user") && !kv.get("user").isEmpty()) c.username = kv.get("user");
        return c;
    }
}
