package mcradar;

import java.lang.instrument.Instrumentation;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

/**
 * Java agent entry point. Works both ways:
 *   - loaded into a RUNNING game via the Attach API (agentmain)   <- what mcradar.exe does
 *   - started with the JVM argument -javaagent:mcradar-agent.jar=port=47615  (premain)
 *
 * It starts one low-priority daemon thread that (1) discovers the player objects (Probe) and
 * (2) sends a text snapshot to 127.0.0.1:<port> ~20x per second. It never modifies the game.
 */
public final class Agent {
    private static boolean started;

    public static void premain(String args, Instrumentation inst) { start(args, inst); }
    public static void agentmain(String args, Instrumentation inst) { start(args, inst); }

    private static synchronized void start(String args, Instrumentation inst) {
        if (started) { System.err.println("[mcradar-agent] already running; ignoring second load."); return; }
        started = true;
        AgentConfig cfg = AgentConfig.parse(args);
        Log.init(cfg.exportDir);
        Log.info("Starting (Java " + System.getProperty("java.version") + ", port " + cfg.port + ", export " + cfg.exportDir + ")");
        Thread t = new Thread(new Worker(cfg, inst), "mcradar-agent");
        t.setDaemon(true);                       // never keeps the game from exiting
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
    }

    private static final class Worker implements Runnable {
        private final AgentConfig cfg;
        private final Instrumentation inst;
        Worker(AgentConfig cfg, Instrumentation inst) { this.cfg = cfg; this.inst = inst; }

        public void run() {
            DatagramSocket sock = null;
            try {
                sock = new DatagramSocket();
                InetAddress lo = InetAddress.getByAddress(new byte[] {127, 0, 0, 1});
                Probe probe = new Probe(inst, cfg);
                long periodMs = Math.max(1, 1000 / cfg.hz);
                long nextDiscover = 0, backoff = 1500;

                while (true) {
                    long t0 = System.currentTimeMillis();
                    Snapshot snap;
                    try {
                        if (!probe.isReady() && t0 >= nextDiscover) {
                            boolean ok = probe.discover();
                            if (ok) backoff = 1500;
                            else { nextDiscover = System.currentTimeMillis() + backoff; backoff = Math.min(backoff * 2, 15000); }
                        }
                        snap = probe.read();
                    } catch (Throwable t) {
                        snap = new Snapshot();
                        snap.status = "agent error: " + t;
                        Log.info("Error in tick: " + t);
                        probe.reset();
                    }
                    byte[] data = snap.encode();
                    try { sock.send(new DatagramPacket(data, data.length, lo, cfg.port)); } catch (Throwable ignored) { }

                    long sleep = periodMs - (System.currentTimeMillis() - t0);
                    if (sleep > 0) Thread.sleep(sleep);
                }
            } catch (InterruptedException ie) {
                Log.info("Worker interrupted; stopping.");
            } catch (Throwable t) {
                Log.info("Worker died: " + t);
            } finally {
                if (sock != null) sock.close();
            }
        }
    }
}
