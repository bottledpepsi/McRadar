package mcradar;

import java.lang.reflect.InvocationTargetException;

/**
 * Command-line loader:  java -jar mcradar-agent.jar <pid> <path-to-this-jar> [agent options]
 *
 * Uses the official JVM Attach API (what jcmd / VisualVM / IDE debuggers use) to ask the running
 * Minecraft JVM to load this jar as a Java agent. It is done reflectively so this class
 * compiles on any JDK. Requires a JDK (module jdk.attach) for THIS process; the game only needs
 * to have attach enabled (default).
 */
public final class Attach {
    public static void main(String[] a) {
        if (a.length < 2) {
            System.err.println("usage: java -jar mcradar-agent.jar <pid> <agent-jar> [options]");
            System.exit(2);
        }
        String pid = a[0], jar = a[1], opts = a.length > 2 ? a[2] : "";
        try {
            Class<?> vmc = Class.forName("com.sun.tools.attach.VirtualMachine");
            Object vm = vmc.getMethod("attach", String.class).invoke(null, pid);
            try {
                vmc.getMethod("loadAgent", String.class, String.class).invoke(vm, jar, opts);
            } finally {
                vmc.getMethod("detach").invoke(vm);
            }
            System.out.println("agent loaded into PID " + pid);
        } catch (ClassNotFoundException e) {
            System.err.println("This Java has no Attach API (jdk.attach). Use a full JDK (17+) as java_path.");
            System.exit(3);
        } catch (InvocationTargetException e) {
            Throwable c = e.getCause() == null ? e : e.getCause();
            System.err.println("attach failed: " + c);
            System.exit(1);
        } catch (Throwable t) {
            System.err.println("attach failed: " + t);
            System.exit(1);
        }
    }
}
