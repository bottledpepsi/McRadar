package mcradar;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Field;

/**
 * Raw field access through sun.misc.Unsafe (reached reflectively so the agent compiles for
 * Java 8 and runs on newer JVMs).
 *
 * Why Unsafe instead of Field.get()?
 *  - Reading a STATIC field with reflection initialises the class if it is not initialised yet.
 *    In a game that can trigger static initialisers early (or poison a class permanently with
 *    ExceptionInInitializerError). Unsafe.staticFieldBase/Offset reads never initialise anything.
 *  - It ignores module encapsulation (Forge/Fabric put game classes in named modules).
 *  - It also gives us the real in-object field offsets, which we export in the layout JSON.
 */
final class Mem {
    static final boolean OK;
    private static final MethodHandle GET_OBJ, GET_DOUBLE, GET_FLOAT, GET_INT, OBJ_OFF, STATIC_OFF, STATIC_BASE;

    static {
        boolean ok = false;
        MethodHandle a = null, b = null, c = null, d = null, e = null, g = null, h = null;
        try {
            Class<?> uc = Class.forName("sun.misc.Unsafe");
            Field f = uc.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            Object u = f.get(null);
            MethodHandles.Lookup l = MethodHandles.publicLookup();
            a = l.findVirtual(uc, "getObject", MethodType.methodType(Object.class, Object.class, long.class)).bindTo(u);
            b = l.findVirtual(uc, "getDouble", MethodType.methodType(double.class, Object.class, long.class)).bindTo(u);
            g = l.findVirtual(uc, "getFloat", MethodType.methodType(float.class, Object.class, long.class)).bindTo(u);
            h = l.findVirtual(uc, "getInt", MethodType.methodType(int.class, Object.class, long.class)).bindTo(u);
            c = l.findVirtual(uc, "objectFieldOffset", MethodType.methodType(long.class, Field.class)).bindTo(u);
            d = l.findVirtual(uc, "staticFieldOffset", MethodType.methodType(long.class, Field.class)).bindTo(u);
            e = l.findVirtual(uc, "staticFieldBase", MethodType.methodType(Object.class, Field.class)).bindTo(u);
            ok = true;
        } catch (Throwable t) {
            Log.info("Unsafe unavailable (" + t + "); falling back to reflection for instance fields only.");
        }
        OK = ok; GET_OBJ = a; GET_DOUBLE = b; GET_FLOAT = g; GET_INT = h; OBJ_OFF = c; STATIC_OFF = d; STATIC_BASE = e;
    }

    static Object getObject(Object base, long off) {
        try { return (Object) GET_OBJ.invokeExact(base, off); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static double getDouble(Object base, long off) {
        try { return (double) GET_DOUBLE.invokeExact(base, off); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static float getFloat(Object base, long off) {
        try { return (float) GET_FLOAT.invokeExact(base, off); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static int getInt(Object base, long off) {
        try { return (int) GET_INT.invokeExact(base, off); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static long objectFieldOffset(Field f) {
        try { return (long) OBJ_OFF.invokeExact(f); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static long staticFieldOffset(Field f) {
        try { return (long) STATIC_OFF.invokeExact(f); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
    static Object staticFieldBase(Field f) {
        try { return (Object) STATIC_BASE.invokeExact(f); }
        catch (Throwable t) { throw new IllegalStateException(t); }
    }
}
