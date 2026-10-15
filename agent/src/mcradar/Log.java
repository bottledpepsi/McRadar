package mcradar;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;

/** Minimal logger: stderr (shows in the game's log) + mcradar_agent.log in the export dir. */
final class Log {
    private static volatile File file;

    static void init(String exportDir) {
        try {
            File dir = new File(exportDir);
            if (!dir.isDirectory()) dir.mkdirs();
            file = new File(dir, "mcradar_agent.log");
        } catch (Throwable ignored) { }
    }

    static synchronized void info(String msg) {
        String line = "[mcradar-agent] " + msg;
        System.err.println(line);
        File f = file;
        if (f == null) return;
        try (PrintWriter w = new PrintWriter(new FileWriter(f, true))) {
            w.println(new java.util.Date() + " " + msg);
        } catch (IOException ignored) { }
    }
}
