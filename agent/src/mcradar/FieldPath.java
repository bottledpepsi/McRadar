package mcradar;

import java.util.List;

/**
 * A path of fields starting at a static root: root.field0.field1... Replayed every tick, so it
 * keeps working when the game replaces intermediate objects (e.g. a new ClientLevel on world join).
 */
final class FieldPath {
    final Acc[] steps;
    FieldPath(List<Acc> s) { steps = s.toArray(new Acc[0]); }

    Object follow() {
        Object cur = null;
        for (int i = 0; i < steps.length; i++) {
            cur = steps[i].getObj(i == 0 ? null : cur);
            if (cur == null) return null;
        }
        return cur;
    }

    String describe() {
        StringBuilder sb = new StringBuilder();
        for (Acc a : steps) { if (sb.length() > 0) sb.append(" -> "); sb.append(a.describe()); }
        return sb.toString();
    }
}
